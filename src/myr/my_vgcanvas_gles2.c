/**
 * @file my_vgcanvas_gles2.c
 * @brief GLES2 vgcanvas backend: CPU triangulation + batched submission.
 */
#include "myr/my_vgcanvas_gles2.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "myc/my_mem.h"

/* ---------------- shaders ---------------- */

static const char* VS_SRC =
    "attribute vec2 a_pos;\n"
    "uniform vec2 u_resolution;\n"
    "void main(void) {\n"
    "  vec2 ndc = (a_pos / u_resolution) * 2.0 - 1.0;\n"
    "  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\n";

static const char* FS_SRC =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "void main(void) { gl_FragColor = u_color; }\n";

/* ---------------- state ---------------- */

typedef struct gles_state_t {
  my_color_t fill_color;
  my_color_t stroke_color;
  float line_width;
  float tx;
  float ty;
  my_rect_t clip;
} gles_state_t;

typedef struct path_point_t {
  float x;
  float y;
} path_point_t;

typedef struct contour_t {
  size_t start;
  size_t count;
  bool closed;
} contour_t;

typedef struct my_vgcanvas_gles2_t {
  my_vgcanvas_t base;
  const my_allocator_t* allocator;
  my_gl_t gl;
  int32_t fb_w;
  int32_t fb_h;
  uint32_t program;
  gles_state_t state;

  gles_state_t* stack;
  size_t stack_count, stack_cap;

  path_point_t* points;
  size_t point_count, point_cap;
  contour_t* contours;
  size_t contour_count, contour_cap;

  float* verts; /* growable interleaved xy buffer */
  size_t vert_cap; /* in floats */
} my_vgcanvas_gles2_t;

/* ---------------- helpers ---------------- */

static my_ret_t gles_grow(const my_allocator_t* alloc, void** arr, size_t* cap,
                          size_t need, size_t elem) {
  void* p;
  size_t new_cap = *cap > 0 ? *cap : 64;
  if (need <= *cap) {
    return MY_RET_OK;
  }
  while (new_cap < need) {
    new_cap *= 2;
  }
  p = my_mem_realloc(alloc, *arr, new_cap * elem);
  if (p == NULL) {
    return MY_RET_OOM;
  }
  *arr = p;
  *cap = new_cap;
  return MY_RET_OK;
}

static void gles_apply_clip(my_vgcanvas_gles2_t* s) {
  /* GL scissor origin is bottom-left: flip y */
  s->gl.scissor(s->gl.ctx, s->state.clip.x,
                s->fb_h - s->state.clip.y - s->state.clip.h, s->state.clip.w,
                s->state.clip.h);
}

static void gles_draw(my_vgcanvas_gles2_t* s, const float* xy, int32_t count,
                      my_color_t color) {
  s->gl.uniform4f(s->gl.ctx, s->program, "u_color", (float)color.r / 255.0f,
                  (float)color.g / 255.0f, (float)color.b / 255.0f,
                  (float)color.a / 255.0f);
  s->gl.draw_arrays_triangles(s->gl.ctx, s->program, xy, count);
}

/** @brief Growable vertex writer. */
typedef struct vbuf_t {
  my_vgcanvas_gles2_t* s;
  size_t count; /* floats written */
} vbuf_t;

static void vbuf_reset(vbuf_t* b, my_vgcanvas_gles2_t* s) {
  b->s = s;
  b->count = 0;
}

static void vbuf_push(vbuf_t* b, float x, float y) {
  my_vgcanvas_gles2_t* s = b->s;
  if (gles_grow(s->allocator, (void**)&s->verts, &s->vert_cap, b->count + 2,
                sizeof(float)) == MY_RET_OK) {
    s->verts[b->count++] = x + s->state.tx;
    s->verts[b->count++] = y + s->state.ty;
  }
}

static void vbuf_rect(vbuf_t* b, float x0, float y0, float x1, float y1) {
  if (x1 <= x0 || y1 <= y0) {
    return;
  }
  vbuf_push(b, x0, y0);
  vbuf_push(b, x1, y0);
  vbuf_push(b, x1, y1);
  vbuf_push(b, x0, y0);
  vbuf_push(b, x1, y1);
  vbuf_push(b, x0, y1);
}

static void vbuf_circle_fan(vbuf_t* b, float cx, float cy, float r, int segments) {
  int i;
  for (i = 0; i < segments; i++) {
    float a0 = (float)i * 6.2831853f / (float)segments;
    float a1 = (float)(i + 1) * 6.2831853f / (float)segments;
    vbuf_push(b, cx, cy);
    vbuf_push(b, cx + r * cosf(a0), cy + r * sinf(a0));
    vbuf_push(b, cx + r * cosf(a1), cy + r * sinf(a1));
  }
}

/* ---------------- frame/state vtable ---------------- */

static my_ret_t gles_begin_frame(my_vgcanvas_t* vg, const my_rect_t* dirty) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  (void)dirty;
  s->gl.viewport(s->gl.ctx, s->fb_w, s->fb_h);
  s->gl.uniform2f(s->gl.ctx, s->program, "u_resolution", (float)s->fb_w,
                  (float)s->fb_h);
  s->gl.enable_scissor(s->gl.ctx, true);
  gles_apply_clip(s);
  return MY_RET_OK;
}

static my_ret_t gles_end_frame(my_vgcanvas_t* vg) {
  (void)vg; /* submission is immediate per draw call; nothing to flush */
  return MY_RET_OK;
}

static my_ret_t gles_save(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (gles_grow(s->allocator, (void**)&s->stack, &s->stack_cap,
                s->stack_count + 1, sizeof(gles_state_t)) != MY_RET_OK) {
    return MY_RET_OOM;
  }
  s->stack[s->stack_count++] = s->state;
  return MY_RET_OK;
}

static my_ret_t gles_restore(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s->stack_count == 0) {
    return MY_RET_FAIL;
  }
  s->state = s->stack[--s->stack_count];
  gles_apply_clip(s);
  return MY_RET_OK;
}

static my_ret_t gles_translate(my_vgcanvas_t* vg, float dx, float dy) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  s->state.tx += dx;
  s->state.ty += dy;
  return MY_RET_OK;
}

static my_ret_t gles_clip_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  my_rect_t dev, clipped;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  dev = my_rect_init((int32_t)floorf(rect->x + s->state.tx),
                     (int32_t)floorf(rect->y + s->state.ty),
                     (int32_t)ceilf(rect->x + s->state.tx + rect->w) -
                         (int32_t)floorf(rect->x + s->state.tx),
                     (int32_t)ceilf(rect->y + s->state.ty + rect->h) -
                         (int32_t)floorf(rect->y + s->state.ty));
  if (my_rect_intersect(&s->state.clip, &dev, &clipped)) {
    s->state.clip = clipped;
  } else {
    s->state.clip = my_rect_init(0, 0, 0, 0);
  }
  gles_apply_clip(s);
  return MY_RET_OK;
}

static my_ret_t gles_set_fill_color(my_vgcanvas_t* vg, my_color_t color) {
  ((my_vgcanvas_gles2_t*)vg)->state.fill_color = color;
  return MY_RET_OK;
}

static my_ret_t gles_set_stroke_color(my_vgcanvas_t* vg, my_color_t color) {
  ((my_vgcanvas_gles2_t*)vg)->state.stroke_color = color;
  return MY_RET_OK;
}

static my_ret_t gles_set_line_width(my_vgcanvas_t* vg, float width) {
  ((my_vgcanvas_gles2_t*)vg)->state.line_width = width;
  return MY_RET_OK;
}

/* ---------------- primitives ---------------- */

static my_ret_t gles_fill_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  vbuf_t b;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  vbuf_reset(&b, s);
  vbuf_rect(&b, rect->x, rect->y, rect->x + rect->w, rect->y + rect->h);
  if (b.count > 0) {
    gles_draw(s, s->verts, (int32_t)(b.count / 2), s->state.fill_color);
  }
  return MY_RET_OK;
}

static my_ret_t gles_stroke_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  float lw, x0, y0, x1, y1;
  vbuf_t b;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  lw = s->state.line_width < 1.0f ? 1.0f : s->state.line_width;
  x0 = rect->x;
  y0 = rect->y;
  x1 = rect->x + rect->w;
  y1 = rect->y + rect->h;
  vbuf_reset(&b, s);
  vbuf_rect(&b, x0, y0, x1, y0 + lw);
  vbuf_rect(&b, x0, y1 - lw, x1, y1);
  vbuf_rect(&b, x0, y0 + lw, x0 + lw, y1 - lw);
  vbuf_rect(&b, x1 - lw, y0 + lw, x1, y1 - lw);
  if (b.count > 0) {
    gles_draw(s, s->verts, (int32_t)(b.count / 2), s->state.stroke_color);
  }
  return MY_RET_OK;
}

static my_ret_t gles_fill_rounded_rect(my_vgcanvas_t* vg, const my_rectf_t* rect,
                                       float radius) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  float r, x0, y0, x1, y1;
  vbuf_t b;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  x0 = rect->x;
  y0 = rect->y;
  x1 = rect->x + rect->w;
  y1 = rect->y + rect->h;
  r = radius;
  if (r > rect->w / 2.0f) {
    r = rect->w / 2.0f;
  }
  if (r > rect->h / 2.0f) {
    r = rect->h / 2.0f;
  }
  vbuf_reset(&b, s);
  if (r <= 0.5f) {
    vbuf_rect(&b, x0, y0, x1, y1);
  } else {
    vbuf_rect(&b, x0 + r, y0, x1 - r, y1);
    vbuf_rect(&b, x0, y0 + r, x0 + r, y1 - r);
    vbuf_rect(&b, x1 - r, y0 + r, x1, y1 - r);
    vbuf_circle_fan(&b, x0 + r, y0 + r, r, 8);
    vbuf_circle_fan(&b, x1 - r, y0 + r, r, 8);
    vbuf_circle_fan(&b, x0 + r, y1 - r, r, 8);
    vbuf_circle_fan(&b, x1 - r, y1 - r, r, 8);
  }
  if (b.count > 0) {
    gles_draw(s, s->verts, (int32_t)(b.count / 2), s->state.fill_color);
  }
  return MY_RET_OK;
}

/* ---------------- path ---------------- */

static my_ret_t gles_begin_path(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  s->point_count = 0;
  s->contour_count = 0;
  return MY_RET_OK;
}

static my_ret_t gles_move_to(my_vgcanvas_t* vg, float x, float y) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (gles_grow(s->allocator, (void**)&s->contours, &s->contour_cap,
                s->contour_count + 1, sizeof(contour_t)) != MY_RET_OK) {
    return MY_RET_OOM;
  }
  s->contours[s->contour_count].start = s->point_count;
  s->contours[s->contour_count].count = 0;
  s->contours[s->contour_count].closed = false;
  s->contour_count++;
  return my_vgcanvas_line_to(vg, x, y);
}

static my_ret_t gles_line_to(my_vgcanvas_t* vg, float x, float y) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s->contour_count == 0) {
    return gles_move_to(vg, x, y);
  }
  if (gles_grow(s->allocator, (void**)&s->points, &s->point_cap,
                s->point_count + 1, sizeof(path_point_t)) != MY_RET_OK) {
    return MY_RET_OOM;
  }
  s->points[s->point_count].x = x;
  s->points[s->point_count].y = y;
  s->point_count++;
  s->contours[s->contour_count - 1].count++;
  return MY_RET_OK;
}

static my_ret_t gles_close_path(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s->contour_count > 0) {
    s->contours[s->contour_count - 1].closed = true;
  }
  return MY_RET_OK;
}

static int float_cmp(const void* a, const void* b) {
  float fa = *(const float*)a;
  float fb = *(const float*)b;
  return fa < fb ? -1 : fa > fb ? 1 : 0;
}

/**
 * @brief Even-odd scanline fill, identical rasterization rule to the soft
 * backend: every filled span becomes one rect (2 triangles).
 */
static my_ret_t gles_fill(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  const my_rect_t* clip = &s->state.clip;
  float* xs;
  size_t xs_cap;
  int32_t y;
  vbuf_t b;

  if (s->point_count < 2) {
    return MY_RET_OK;
  }
  xs_cap = s->point_count;
  xs = (float*)my_mem_alloc(s->allocator, xs_cap * sizeof(float));
  if (xs == NULL) {
    return MY_RET_OOM;
  }
  vbuf_reset(&b, s);

  for (y = clip->y; y < clip->y + clip->h; y++) {
    float yc = (float)y + 0.5f;
    size_t nxs = 0, ci, i, k;
    for (ci = 0; ci < s->contour_count; ci++) {
      const contour_t* c = &s->contours[ci];
      for (i = 0; i < c->count; i++) {
        size_t j = i + 1;
        float x0, y0, x1, y1;
        if (j == c->count) {
          if (!c->closed) {
            break;
          }
          j = 0;
        }
        x0 = s->points[c->start + i].x + s->state.tx;
        y0 = s->points[c->start + i].y + s->state.ty;
        x1 = s->points[c->start + j].x + s->state.tx;
        y1 = s->points[c->start + j].y + s->state.ty;
        if ((y0 <= yc) != (y1 <= yc) && nxs < xs_cap) {
          xs[nxs++] = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
        }
      }
    }
    if (nxs > 1) {
      qsort(xs, nxs, sizeof(float), float_cmp);
      for (k = 0; k + 1 < nxs; k += 2) {
        /* spans are emitted in DEVICE space: undo translate for vbuf */
        float xa = ceilf(xs[k] - 0.5f) - s->state.tx;
        float xb = ceilf(xs[k + 1] - 0.5f) - s->state.tx;
        vbuf_rect(&b, xa, (float)y - s->state.ty, xb,
                  (float)y + 1.0f - s->state.ty);
      }
    }
  }

  my_mem_free(s->allocator, xs);
  if (b.count > 0) {
    gles_draw(s, s->verts, (int32_t)(b.count / 2), s->state.fill_color);
  }
  return MY_RET_OK;
}

/** @brief One segment as a quad expanded along its normal. */
static void vbuf_segment(vbuf_t* b, float x0, float y0, float x1, float y1,
                         float half_w) {
  float dx = x1 - x0;
  float dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  float nx, ny;
  if (len < 0.001f) {
    vbuf_rect(b, x0 - half_w, y0 - half_w, x0 + half_w, y0 + half_w);
    return;
  }
  nx = -dy / len * half_w;
  ny = dx / len * half_w;
  vbuf_push(b, x0 + nx, y0 + ny);
  vbuf_push(b, x1 + nx, y1 + ny);
  vbuf_push(b, x1 - nx, y1 - ny);
  vbuf_push(b, x0 + nx, y0 + ny);
  vbuf_push(b, x1 - nx, y1 - ny);
  vbuf_push(b, x0 - nx, y0 - ny);
}

static my_ret_t gles_stroke(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  float half_w = s->state.line_width / 2.0f;
  size_t ci, i;
  vbuf_t b;
  if (half_w < 0.5f) {
    half_w = 0.5f;
  }
  vbuf_reset(&b, s);
  for (ci = 0; ci < s->contour_count; ci++) {
    const contour_t* c = &s->contours[ci];
    for (i = 0; i + 1 < c->count; i++) {
      vbuf_segment(&b, s->points[c->start + i].x, s->points[c->start + i].y,
                   s->points[c->start + i + 1].x, s->points[c->start + i + 1].y,
                   half_w);
    }
    if (c->closed && c->count > 1) {
      vbuf_segment(&b, s->points[c->start + c->count - 1].x,
                   s->points[c->start + c->count - 1].y, s->points[c->start].x,
                   s->points[c->start].y, half_w);
    }
  }
  if (b.count > 0) {
    gles_draw(s, s->verts, (int32_t)(b.count / 2), s->state.stroke_color);
  }
  return MY_RET_OK;
}

static my_ret_t gles_draw_text(my_vgcanvas_t* vg, const char* text, float x,
                               float y) {
  (void)vg;
  (void)text;
  (void)x;
  (void)y;
  return MY_RET_NOT_SUPPORTED;
}

/* ---------------- lifecycle ---------------- */

static void gles_destroy(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s != NULL) {
    if (s->program != 0 && s->gl.delete_program != NULL) {
      s->gl.delete_program(s->gl.ctx, s->program);
    }
    my_mem_free(s->allocator, s->stack);
    my_mem_free(s->allocator, s->points);
    my_mem_free(s->allocator, s->contours);
    my_mem_free(s->allocator, s->verts);
    my_mem_free(s->allocator, s);
  }
}

static const my_vgcanvas_vtable_t s_gles_vtable = {
    gles_begin_frame,      gles_end_frame,   gles_save,           gles_restore,
    gles_translate,        gles_clip_rect,   gles_set_fill_color,
    gles_set_stroke_color, gles_set_line_width, gles_fill_rect,  gles_stroke_rect,
    gles_fill_rounded_rect, gles_begin_path, gles_move_to,       gles_line_to,
    gles_close_path,       gles_fill,        gles_stroke,        gles_draw_text,
    gles_destroy};

my_vgcanvas_t* my_vgcanvas_gles2_create_with_gl(const my_allocator_t* allocator,
                                                int32_t width, int32_t height,
                                                const my_gl_t* gl) {
  my_vgcanvas_gles2_t* s;
  if (gl == NULL || gl->create_program == NULL || width <= 0 || height <= 0) {
    return NULL;
  }
  s = (my_vgcanvas_gles2_t*)my_mem_calloc(allocator, 1,
                                          sizeof(my_vgcanvas_gles2_t));
  if (s == NULL) {
    return NULL;
  }
  s->base.vtable = &s_gles_vtable;
  s->allocator = allocator;
  s->gl = *gl;
  s->fb_w = width;
  s->fb_h = height;
  s->program = gl->create_program(gl->ctx, VS_SRC, FS_SRC);
  if (s->program == 0) {
    my_mem_free(allocator, s);
    return NULL;
  }
  gl->use_program(gl->ctx, s->program);
  s->state.fill_color = my_color_rgba(0, 0, 0, 255);
  s->state.stroke_color = my_color_rgba(0, 0, 0, 255);
  s->state.line_width = 1.0f;
  s->state.clip = my_rect_init(0, 0, width, height);
  return (my_vgcanvas_t*)s;
}

my_vgcanvas_t* my_vgcanvas_gles2_create(const my_allocator_t* allocator,
                                        int32_t width, int32_t height) {
  const my_gl_t* gl = my_gl_real_default();
  if (gl == NULL) {
    return NULL;
  }
  return my_vgcanvas_gles2_create_with_gl(allocator, width, height, gl);
}

my_ret_t my_vgcanvas_gles2_resize(my_vgcanvas_t* vg, int32_t width,
                                  int32_t height) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s == NULL || width <= 0 || height <= 0) {
    return MY_RET_INVALID_PARAMS;
  }
  s->fb_w = width;
  s->fb_h = height;
  s->gl.viewport(s->gl.ctx, width, height);
  s->gl.uniform2f(s->gl.ctx, s->program, "u_resolution", (float)width,
                  (float)height);
  return MY_RET_OK;
}
