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

static const char* VS_TEXT_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec2 u_resolution;\n"
    "varying vec2 v_uv;\n"
    "void main(void) {\n"
    "  vec2 ndc = (a_pos / u_resolution) * 2.0 - 1.0;\n"
    "  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char* FS_IMG_SRC =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main(void) { gl_FragColor = texture2D(u_tex, v_uv); }\n";

static const char* FS_TEXT_SRC =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main(void) {\n"
    "  gl_FragColor = vec4(u_color.rgb, u_color.a * texture2D(u_tex, v_uv).r);\n"
    "}\n";

#define GLES_TEX_CACHE_SIZE 64
#define GLES_IMG_TEX_CACHE_SIZE 16

/* ---------------- state ---------------- */

typedef struct gles_state_t {
  my_color_t fill_color;
  my_color_t stroke_color;
  float line_width;
  float tx;
  float ty;
  my_rect_t clip;
  my_font_t* font;   /**< borrowed; NULL = no text */
  int32_t font_size;
} gles_state_t;

typedef struct gles_tex_entry_t {
  uint32_t codepoint;
  int32_t size;
  uint32_t texture; /**< 0 = empty */
} gles_tex_entry_t;

/** @brief Image texture cache entry: keyed by (ptr, w, h); the caller must
 * keep the bitmap alive while it may be used (my_image's decode cache
 * holds images for many frames, so this is safe in practice). */
typedef struct gles_img_tex_entry_t {
  const uint8_t* ptr;
  int32_t w;
  int32_t h;
  uint32_t texture; /**< 0 = empty */
  uint64_t last_used;
} gles_img_tex_entry_t;

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
  uint32_t text_program; /**< lazy: created on first draw_text */
  uint32_t img_program;  /**< lazy: created on first draw_image */
  gles_tex_entry_t tex_cache[GLES_TEX_CACHE_SIZE];
  gles_img_tex_entry_t img_tex_cache[GLES_IMG_TEX_CACHE_SIZE];
  uint64_t img_tex_tick;
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
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  int32_t ascent;
  float pen_x, top;
  const char* p = text;

  if (text == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  if (s->state.font == NULL || s->state.font_size <= 0 ||
      s->gl.create_texture == NULL) {
    return MY_RET_NOT_SUPPORTED;
  }
  if (s->text_program == 0) {
    s->text_program =
        s->gl.create_program(s->gl.ctx, VS_TEXT_SRC, FS_TEXT_SRC);
    if (s->text_program == 0) {
      return MY_RET_FAIL;
    }
  }
  s->gl.use_program(s->gl.ctx, s->text_program);
  s->gl.uniform2f(s->gl.ctx, s->text_program, "u_resolution", (float)s->fb_w,
                  (float)s->fb_h);

  ascent = my_font_ascent(s->state.font, s->state.font_size);
  pen_x = x + s->state.tx;
  top = y + s->state.ty;

  while (*p != '\0') {
    uint32_t cp = my_utf8_next(&p);
    my_glyph_t g;
    uint32_t slot;
    float gx, gy, quad[24];
    if (my_font_get_glyph(s->state.font, cp, s->state.font_size, &g) !=
            MY_RET_OK ||
        g.bitmap == NULL || g.w <= 0 || g.h <= 0) {
      pen_x += g.advance > 0 ? (float)g.advance : 0.0f;
      continue;
    }
    /* direct-mapped texture cache: evict on slot collision */
    slot = (cp ^ (uint32_t)s->state.font_size) % GLES_TEX_CACHE_SIZE;
    if (s->tex_cache[slot].texture == 0 ||
        s->tex_cache[slot].codepoint != cp ||
        s->tex_cache[slot].size != s->state.font_size) {
      if (s->tex_cache[slot].texture != 0) {
        s->gl.delete_texture(s->gl.ctx, s->tex_cache[slot].texture);
      }
      s->tex_cache[slot].texture =
          s->gl.create_texture(s->gl.ctx, g.bitmap, g.w, g.h);
      s->tex_cache[slot].codepoint = cp;
      s->tex_cache[slot].size = s->state.font_size;
    }
    gx = pen_x + (float)g.bearing_x;
    gy = top + (float)(ascent - g.bearing_y);
    /* quad: 2 triangles, interleaved xy+uv */
    {
      float x0 = gx, y0 = gy, x1 = gx + (float)g.w, y1 = gy + (float)g.h;
      const float verts[6][4] = {{x0, y0, 0, 0}, {x1, y0, 1, 0}, {x1, y1, 1, 1},
                                 {x0, y0, 0, 0}, {x1, y1, 1, 1}, {x0, y1, 0, 1}};
      memcpy(quad, verts, sizeof(quad));
    }
    s->gl.uniform4f(s->gl.ctx, s->text_program, "u_color",
                    (float)s->state.fill_color.r / 255.0f,
                    (float)s->state.fill_color.g / 255.0f,
                    (float)s->state.fill_color.b / 255.0f,
                    (float)s->state.fill_color.a / 255.0f);
    s->gl.draw_textured_quads(s->gl.ctx, s->text_program,
                              s->tex_cache[slot].texture, quad, 6);
    pen_x += (float)g.advance;
  }
  /* restore the flat-color program for subsequent geometry */
  s->gl.use_program(s->gl.ctx, s->program);
  return MY_RET_OK;
}

static my_ret_t gles_set_font(my_vgcanvas_t* vg, my_font_t* font,
                              int32_t size) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (font != NULL) {
    s->state.font = font;
  }
  if (size > 0) {
    s->state.font_size = size;
  }
  return MY_RET_OK;
}

static uint32_t gles_image_texture(my_vgcanvas_gles2_t* s, const uint8_t* rgba,
                                   int32_t w, int32_t h) {
  size_t i;
  gles_img_tex_entry_t* lru = &s->img_tex_cache[0];
  for (i = 0; i < GLES_IMG_TEX_CACHE_SIZE; i++) {
    gles_img_tex_entry_t* e = &s->img_tex_cache[i];
    if (e->texture == 0) {
      lru = e;
      continue;
    }
    if (e->last_used < lru->last_used) {
      lru = e;
    }
    if (e->ptr == rgba && e->w == w && e->h == h) {
      e->last_used = ++s->img_tex_tick;
      return e->texture;
    }
  }
  if (lru->texture != 0) {
    s->gl.delete_texture(s->gl.ctx, lru->texture);
  }
  lru->texture = s->gl.create_texture_rgba(s->gl.ctx, rgba, w, h);
  lru->ptr = rgba;
  lru->w = w;
  lru->h = h;
  lru->last_used = ++s->img_tex_tick;
  return lru->texture;
}

static my_ret_t gles_draw_image(my_vgcanvas_t* vg, const uint8_t* rgba,
                                int32_t w, int32_t h, const my_rectf_t* dst,
                                const my_color_t* bg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  uint32_t tex;
  float quad[24];
  if (rgba == NULL || dst == NULL || w <= 0 || h <= 0) {
    return MY_RET_INVALID_PARAMS;
  }
  if (s->gl.create_texture_rgba == NULL) {
    return MY_RET_NOT_SUPPORTED;
  }
  /* bg compositing: paint bg rect first, then blend the textured quad */
  if (bg != NULL && bg->a > 0) {
    vbuf_t b;
    vbuf_reset(&b, s);
    vbuf_rect(&b, dst->x, dst->y, dst->x + dst->w, dst->y + dst->h);
    if (b.count > 0) {
      gles_draw(s, s->verts, (int32_t)(b.count / 2), *bg);
    }
  }
  if (s->img_program == 0) {
    s->img_program = s->gl.create_program(s->gl.ctx, VS_TEXT_SRC, FS_IMG_SRC);
    if (s->img_program == 0) {
      return MY_RET_FAIL;
    }
  }
  tex = gles_image_texture(s, rgba, w, h);
  if (tex == 0) {
    return MY_RET_OOM;
  }
  {
    float x0 = dst->x + s->state.tx, y0 = dst->y + s->state.ty;
    float x1 = x0 + dst->w, y1 = y0 + dst->h;
    const float verts[6][4] = {{x0, y0, 0, 0}, {x1, y0, 1, 0}, {x1, y1, 1, 1},
                               {x0, y0, 0, 0}, {x1, y1, 1, 1}, {x0, y1, 0, 1}};
    memcpy(quad, verts, sizeof(quad));
  }
  s->gl.use_program(s->gl.ctx, s->img_program);
  s->gl.uniform2f(s->gl.ctx, s->img_program, "u_resolution", (float)s->fb_w,
                  (float)s->fb_h);
  s->gl.draw_textured_quads(s->gl.ctx, s->img_program, tex, quad, 6);
  s->gl.use_program(s->gl.ctx, s->program);
  return MY_RET_OK;
}

static my_ret_t gles_measure_text(my_vgcanvas_t* vg, const char* text,
                                  int32_t* w, int32_t* h) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s->state.font == NULL || s->state.font_size <= 0) {
    return MY_RET_NOT_SUPPORTED;
  }
  return my_font_measure(s->state.font, text, s->state.font_size, w, h);
}

/* ---------------- lifecycle ---------------- */

static void gles_destroy(my_vgcanvas_t* vg) {
  my_vgcanvas_gles2_t* s = (my_vgcanvas_gles2_t*)vg;
  if (s != NULL) {
    size_t i;
    if (s->gl.delete_texture != NULL) {
      for (i = 0; i < GLES_TEX_CACHE_SIZE; i++) {
        if (s->tex_cache[i].texture != 0) {
          s->gl.delete_texture(s->gl.ctx, s->tex_cache[i].texture);
        }
      }
      for (i = 0; i < GLES_IMG_TEX_CACHE_SIZE; i++) {
        if (s->img_tex_cache[i].texture != 0) {
          s->gl.delete_texture(s->gl.ctx, s->img_tex_cache[i].texture);
        }
      }
    }
    if (s->text_program != 0 && s->gl.delete_program != NULL) {
      s->gl.delete_program(s->gl.ctx, s->text_program);
    }
    if (s->img_program != 0 && s->gl.delete_program != NULL) {
      s->gl.delete_program(s->gl.ctx, s->img_program);
    }
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
    gles_begin_frame,      gles_end_frame,   gles_save,          gles_restore,
    gles_translate,        gles_clip_rect,   gles_set_fill_color,
    gles_set_stroke_color, gles_set_line_width, gles_fill_rect,  gles_stroke_rect,
    gles_fill_rounded_rect, gles_begin_path, gles_move_to,       gles_line_to,
    gles_close_path,       gles_fill,        gles_stroke,        gles_draw_text,
    gles_destroy,          gles_set_font,    gles_measure_text,
    gles_draw_image};

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
  s->state.font = NULL;
  s->state.font_size = 16;
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
