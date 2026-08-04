/**
 * @file my_vgcanvas_soft.c
 * @brief Software rasterizer vgcanvas backend.
 *
 * Rasterization notes:
 *  - Path fill: scanline even-odd rule over ALL subpaths (a half-open
 *    [y0, y1) edge test avoids double-counted vertices); correct for
 *    concave and self-intersecting polygons; nested contours punch holes.
 *  - Stroke: Bresenham line per segment with a square line_width brush —
 *    a deliberate 1px/integer-width approximation, no joins/caps yet.
 *  - Rounded rect: 3 body rects + 4 scanline-filled quarter circles.
 *  - No anti-aliasing, no alpha blending (documented in my_vgcanvas.h).
 */
#include "myr/my_vgcanvas_soft.h"

#include <math.h>
#include <stdlib.h>

typedef struct soft_state_t {
  my_color_t fill_color;
  my_color_t stroke_color;
  float line_width;
  float tx;
  float ty;
  my_rect_t clip; /* device coordinates */
} soft_state_t;

typedef struct path_point_t {
  float x;
  float y;
} path_point_t;

typedef struct contour_t {
  size_t start;  /**< index into points[] */
  size_t count;  /**< number of points */
  bool closed;   /**< close_path() was called */
} contour_t;

typedef struct my_vgcanvas_soft_t {
  my_vgcanvas_t base;
  const my_allocator_t* allocator;
  my_lcd_t* lcd; /* borrowed */
  soft_state_t state;

  soft_state_t* stack;
  size_t stack_count;
  size_t stack_cap;

  path_point_t* points;
  size_t point_count;
  size_t point_cap;

  contour_t* contours;
  size_t contour_count;
  size_t contour_cap;

  my_dirty_rects_t dirty;
} my_vgcanvas_soft_t;

/* ---------------- growable arrays ---------------- */

static my_ret_t soft_grow(const my_allocator_t* alloc, void** arr, size_t* cap,
                          size_t need, size_t elem_size) {
  void* p;
  size_t new_cap = *cap > 0 ? *cap : 16;
  if (need <= *cap) {
    return MY_RET_OK;
  }
  while (new_cap < need) {
    new_cap *= 2;
  }
  p = my_mem_realloc(alloc, *arr, new_cap * elem_size);
  if (p == NULL) {
    return MY_RET_OOM;
  }
  *arr = p;
  *cap = new_cap;
  return MY_RET_OK;
}

/* ---------------- drawing helpers ---------------- */

static int32_t soft_round(float v) {
  return (int32_t)floorf(v + 0.5f);
}

/** @brief Fill a device-space rect, clipped to the current clip; tracked dirty. */
static void soft_fill_device_rect(my_vgcanvas_soft_t* s, my_rect_t r,
                                  my_color_t color) {
  my_rect_t clipped;
  if (my_rect_intersect(&r, &s->state.clip, &clipped)) {
    my_lcd_fill_rect(s->lcd, &clipped, color);
    my_dirty_rects_add(&s->dirty, &clipped);
  }
}

/** @brief User-space rect -> device-space rect (origin floor, size exact). */
static my_rect_t soft_user_rect_to_device(const my_vgcanvas_soft_t* s,
                                          const my_rectf_t* r) {
  int32_t x0 = (int32_t)floorf(r->x + s->state.tx);
  int32_t y0 = (int32_t)floorf(r->y + s->state.ty);
  int32_t x1 = (int32_t)floorf(r->x + s->state.tx + r->w);
  int32_t y1 = (int32_t)floorf(r->y + s->state.ty + r->h);
  return my_rect_init(x0, y0, x1 - x0, y1 - y0);
}

/* ---------------- vtable: frame ---------------- */

static my_ret_t soft_begin_frame(my_vgcanvas_t* vg, const my_rect_t* dirty) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_dirty_rects_clear(&s->dirty);
  return my_lcd_begin_frame(s->lcd, dirty);
}

static my_ret_t soft_end_frame(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  return my_lcd_end_frame(s->lcd);
}

/* ---------------- vtable: state stack ---------------- */

static my_ret_t soft_save(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_ret_t ret = soft_grow(s->allocator, (void**)&s->stack, &s->stack_cap,
                           s->stack_count + 1, sizeof(soft_state_t));
  if (ret != MY_RET_OK) {
    return ret;
  }
  s->stack[s->stack_count++] = s->state;
  return MY_RET_OK;
}

static my_ret_t soft_restore(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  if (s->stack_count == 0) {
    return MY_RET_FAIL;
  }
  s->state = s->stack[--s->stack_count];
  return MY_RET_OK;
}

static my_ret_t soft_translate(my_vgcanvas_t* vg, float dx, float dy) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  s->state.tx += dx;
  s->state.ty += dy;
  return MY_RET_OK;
}

static my_ret_t soft_clip_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_rect_t dev, clipped;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  /* clip is inclusive: origin floors, far edge ceils */
  dev = my_rect_init((int32_t)floorf(rect->x + s->state.tx),
                     (int32_t)floorf(rect->y + s->state.ty),
                     (int32_t)ceilf(rect->x + s->state.tx + rect->w) -
                         (int32_t)floorf(rect->x + s->state.tx),
                     (int32_t)ceilf(rect->y + s->state.ty + rect->h) -
                         (int32_t)floorf(rect->y + s->state.ty));
  if (my_rect_intersect(&s->state.clip, &dev, &clipped)) {
    s->state.clip = clipped;
  } else {
    s->state.clip = my_rect_init(0, 0, 0, 0); /* empty clip */
  }
  return MY_RET_OK;
}

static my_ret_t soft_set_fill_color(my_vgcanvas_t* vg, my_color_t color) {
  ((my_vgcanvas_soft_t*)vg)->state.fill_color = color;
  return MY_RET_OK;
}

static my_ret_t soft_set_stroke_color(my_vgcanvas_t* vg, my_color_t color) {
  ((my_vgcanvas_soft_t*)vg)->state.stroke_color = color;
  return MY_RET_OK;
}

static my_ret_t soft_set_line_width(my_vgcanvas_t* vg, float width) {
  ((my_vgcanvas_soft_t*)vg)->state.line_width = width;
  return MY_RET_OK;
}

/* ---------------- vtable: rect primitives ---------------- */

static my_ret_t soft_fill_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  soft_fill_device_rect(s, soft_user_rect_to_device(s, rect), s->state.fill_color);
  return MY_RET_OK;
}

static my_ret_t soft_stroke_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_rect_t r;
  int32_t lw;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  r = soft_user_rect_to_device(s, rect);
  lw = soft_round(s->state.line_width);
  if (lw < 1) {
    lw = 1;
  }
  if (lw * 2 >= r.w || lw * 2 >= r.h) {
    /* degenerate: the stroke covers everything */
    soft_fill_device_rect(s, r, s->state.stroke_color);
    return MY_RET_OK;
  }
  soft_fill_device_rect(s, my_rect_init(r.x, r.y, r.w, lw), s->state.stroke_color);
  soft_fill_device_rect(s, my_rect_init(r.x, r.y + r.h - lw, r.w, lw),
                        s->state.stroke_color);
  soft_fill_device_rect(s, my_rect_init(r.x, r.y + lw, lw, r.h - 2 * lw),
                        s->state.stroke_color);
  soft_fill_device_rect(s, my_rect_init(r.x + r.w - lw, r.y + lw, lw, r.h - 2 * lw),
                        s->state.stroke_color);
  return MY_RET_OK;
}

/** @brief Scanline-filled circle of radius r centered at (cx, cy). */
static void soft_fill_circle(my_vgcanvas_soft_t* s, int32_t cx, int32_t cy,
                             int32_t r, my_color_t color) {
  int32_t dy;
  for (dy = -r; dy <= r; dy++) {
    int32_t dx = (int32_t)floorf(sqrtf((float)(r * r - dy * dy)));
    soft_fill_device_rect(s, my_rect_init(cx - dx, cy + dy, 2 * dx + 1, 1), color);
  }
}

static my_ret_t soft_fill_rounded_rect(my_vgcanvas_t* vg, const my_rectf_t* rect,
                                       float radius) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_rect_t r;
  int32_t ri;
  if (rect == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  r = soft_user_rect_to_device(s, rect);
  ri = soft_round(radius);
  if (ri > r.w / 2) {
    ri = r.w / 2;
  }
  if (ri > r.h / 2) {
    ri = r.h / 2;
  }
  if (ri <= 0) {
    soft_fill_device_rect(s, r, s->state.fill_color);
    return MY_RET_OK;
  }
  /* body: full-height middle band + two side bands between the corners */
  soft_fill_device_rect(s, my_rect_init(r.x + ri, r.y, r.w - 2 * ri, r.h),
                        s->state.fill_color);
  soft_fill_device_rect(s, my_rect_init(r.x, r.y + ri, ri, r.h - 2 * ri),
                        s->state.fill_color);
  soft_fill_device_rect(s, my_rect_init(r.x + r.w - ri, r.y + ri, ri, r.h - 2 * ri),
                        s->state.fill_color);
  /* corners: quarter circles (full circles, overdrawing the body is fine) */
  soft_fill_circle(s, r.x + ri, r.y + ri, ri, s->state.fill_color);
  soft_fill_circle(s, r.x + r.w - ri - 1, r.y + ri, ri, s->state.fill_color);
  soft_fill_circle(s, r.x + ri, r.y + r.h - ri - 1, ri, s->state.fill_color);
  soft_fill_circle(s, r.x + r.w - ri - 1, r.y + r.h - ri - 1, ri,
                   s->state.fill_color);
  return MY_RET_OK;
}

/* ---------------- vtable: path ---------------- */

static my_ret_t soft_begin_path(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  s->point_count = 0;
  s->contour_count = 0;
  return MY_RET_OK;
}

static my_ret_t soft_move_to(my_vgcanvas_t* vg, float x, float y) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_ret_t ret = soft_grow(s->allocator, (void**)&s->contours, &s->contour_cap,
                           s->contour_count + 1, sizeof(contour_t));
  if (ret != MY_RET_OK) {
    return ret;
  }
  s->contours[s->contour_count].start = s->point_count;
  s->contours[s->contour_count].count = 0;
  s->contours[s->contour_count].closed = false;
  s->contour_count++;
  return my_vgcanvas_line_to(vg, x, y);
}

static my_ret_t soft_line_to(my_vgcanvas_t* vg, float x, float y) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_ret_t ret;
  if (s->contour_count == 0) {
    return soft_move_to(vg, x, y); /* implicit move_to */
  }
  ret = soft_grow(s->allocator, (void**)&s->points, &s->point_cap,
                  s->point_count + 1, sizeof(path_point_t));
  if (ret != MY_RET_OK) {
    return ret;
  }
  s->points[s->point_count].x = x;
  s->points[s->point_count].y = y;
  s->point_count++;
  s->contours[s->contour_count - 1].count++;
  return MY_RET_OK;
}

static my_ret_t soft_close_path(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
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

/** @brief Fill one scanline (device y) with the even-odd rule. */
static void soft_fill_scanline(my_vgcanvas_soft_t* s, int32_t y, float* xs,
                               size_t xs_cap) {
  float yc = (float)y + 0.5f;
  size_t nxs = 0;
  size_t ci, i, k;

  for (ci = 0; ci < s->contour_count; ci++) {
    const contour_t* c = &s->contours[ci];
    size_t edges = c->count;
    for (i = 0; i < edges; i++) {
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

  qsort(xs, nxs, sizeof(float), float_cmp);
  for (k = 0; k + 1 < nxs; k += 2) {
    int32_t xa = (int32_t)ceilf(xs[k] - 0.5f);
    int32_t xb = (int32_t)ceilf(xs[k + 1] - 0.5f);
    if (xb > xa) {
      soft_fill_device_rect(s, my_rect_init(xa, y, xb - xa, 1),
                            s->state.fill_color);
    }
  }
}

static my_ret_t soft_fill(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  float* xs;
  size_t xs_cap = s->point_count > 0 ? s->point_count : 1;
  int32_t y;

  if (s->point_count < 2) {
    return MY_RET_OK;
  }
  xs = (float*)my_mem_alloc(s->allocator, xs_cap * sizeof(float));
  if (xs == NULL) {
    return MY_RET_OOM;
  }
  for (y = s->state.clip.y; y < s->state.clip.y + s->state.clip.h; y++) {
    soft_fill_scanline(s, y, xs, xs_cap);
  }
  my_mem_free(s->allocator, xs);
  return MY_RET_OK;
}

/** @brief Bresenham line with a square line_width brush (approximation). */
static void soft_draw_segment(my_vgcanvas_soft_t* s, float fx0, float fy0,
                              float fx1, float fy1) {
  int32_t x0 = soft_round(fx0 + s->state.tx);
  int32_t y0 = soft_round(fy0 + s->state.ty);
  int32_t x1 = soft_round(fx1 + s->state.tx);
  int32_t y1 = soft_round(fy1 + s->state.ty);
  int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int32_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int32_t sx = x0 < x1 ? 1 : -1;
  int32_t sy = y0 < y1 ? 1 : -1;
  int32_t err = dx - dy;
  int32_t lw = soft_round(s->state.line_width);
  int32_t half;

  if (lw < 1) {
    lw = 1;
  }
  half = lw / 2;

  for (;;) {
    soft_fill_device_rect(s, my_rect_init(x0 - half, y0 - half, lw, lw),
                          s->state.stroke_color);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    {
      int32_t e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
}

static my_ret_t soft_stroke(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  size_t ci, i;
  for (ci = 0; ci < s->contour_count; ci++) {
    const contour_t* c = &s->contours[ci];
    for (i = 0; i + 1 < c->count; i++) {
      soft_draw_segment(s, s->points[c->start + i].x, s->points[c->start + i].y,
                        s->points[c->start + i + 1].x,
                        s->points[c->start + i + 1].y);
    }
    if (c->closed && c->count > 1) {
      soft_draw_segment(s, s->points[c->start + c->count - 1].x,
                        s->points[c->start + c->count - 1].y,
                        s->points[c->start].x, s->points[c->start].y);
    }
  }
  return MY_RET_OK;
}

static my_ret_t soft_draw_text(my_vgcanvas_t* vg, const char* text, float x,
                               float y) {
  (void)vg;
  (void)text;
  (void)x;
  (void)y;
  return MY_RET_NOT_SUPPORTED; /* fonts land in a later milestone */
}

/* ---------------- lifecycle ---------------- */

static void soft_destroy(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  if (s != NULL) {
    my_mem_free(s->allocator, s->stack);
    my_mem_free(s->allocator, s->points);
    my_mem_free(s->allocator, s->contours);
    my_mem_free(s->allocator, s);
  }
}

static const my_vgcanvas_vtable_t s_soft_vtable = {
    soft_begin_frame,      soft_end_frame,   soft_save,          soft_restore,
    soft_translate,        soft_clip_rect,   soft_set_fill_color,
    soft_set_stroke_color, soft_set_line_width, soft_fill_rect,  soft_stroke_rect,
    soft_fill_rounded_rect, soft_begin_path, soft_move_to,       soft_line_to,
    soft_close_path,       soft_fill,        soft_stroke,        soft_draw_text,
    soft_destroy};

my_vgcanvas_t* my_vgcanvas_soft_create(const my_allocator_t* allocator,
                                       my_lcd_t* lcd) {
  my_vgcanvas_soft_t* s;
  if (lcd == NULL) {
    return NULL;
  }
  s = (my_vgcanvas_soft_t*)my_mem_calloc(allocator, 1, sizeof(my_vgcanvas_soft_t));
  if (s == NULL) {
    return NULL;
  }
  s->base.vtable = &s_soft_vtable;
  s->allocator = allocator;
  s->lcd = lcd;
  s->state.fill_color = my_color_rgba(0, 0, 0, 255);
  s->state.stroke_color = my_color_rgba(0, 0, 0, 255);
  s->state.line_width = 1.0f;
  s->state.tx = 0.0f;
  s->state.ty = 0.0f;
  s->state.clip =
      my_rect_init(0, 0, (int32_t)my_lcd_get_width(lcd), (int32_t)my_lcd_get_height(lcd));
  my_dirty_rects_init(&s->dirty);
  return (my_vgcanvas_t*)s;
}

const my_dirty_rects_t* my_vgcanvas_soft_get_dirty_rects(my_vgcanvas_t* vg) {
  if (vg == NULL || vg->vtable != &s_soft_vtable) {
    return NULL;
  }
  return &((my_vgcanvas_soft_t*)vg)->dirty;
}
