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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct soft_state_t {
  my_color_t fill_color;
  my_color_t stroke_color;
  float line_width;
  float tx;
  float ty;
  my_rect_t clip; /* device coordinates */
  my_font_t* font;     /**< borrowed; NULL = no text */
  int32_t font_size;
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
  int antialias_level; /**< 0=off 1=x4 2=x4*y2 (M8c, default 2) */
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

/* ---------------- coverage anti-aliasing (M7c x, M8c +y) ----------------
 * AA levels: 0 = off (pixel-center hard edges, M1 behavior),
 * 1 = x-direction 4x subsampling (subsample centers (2k+1)/8),
 * 2 = x4 x y2 (scanline evaluated at +0.25 and +0.75). Edge pixels blend
 * src-over with alpha = color.a * cov / maxcov. Axis-aligned straight
 * edges always have full coverage (no visual/perf regression).
 */

/** @brief Coverage 0..4 of a left-edge pixel whose fraction is f. */
static int cov_left(float f) {
  int k, n = 0;
  for (k = 0; k < 4; k++) {
    if ((float)(2 * k + 1) / 8.0f >= f) {
      n++;
    }
  }
  return n;
}

/** @brief Coverage 0..4 of a right-edge pixel whose fraction is f. */
static int cov_right(float f) {
  int k, n = 0;
  for (k = 0; k < 4; k++) {
    if ((float)(2 * k + 1) / 8.0f <= f) {
      n++;
    }
  }
  return n;
}

static int float_cmp(const void* a, const void* b);

/** @brief Per-row coverage/alpha buffers for one fill call. */
typedef struct aa_rowbuf_t {
  uint8_t* cov;
  uint8_t* alpha;
} aa_rowbuf_t;

static void aa_add(uint8_t* cov, int32_t idx, int n) {
  int v = cov[idx] + n;
  cov[idx] = (uint8_t)(v > 8 ? 8 : v);
}

/** @brief Accumulate x-coverage (0..4 per pixel) of span [xl,xr] into cov. */
static void span_accum(uint8_t* cov, int32_t base_x,
                       int32_t width, float xl, float xr) {
  int32_t x0 = base_x, x1 = base_x + width;
  float fxl, fxr;
  int32_t lpix, rpix, p;
  if (xr <= xl) {
    return;
  }
  fxl = xl - floorf(xl);
  fxr = xr - floorf(xr);
  lpix = (int32_t)floorf(xl);
  rpix = (int32_t)floorf(xr);
  if (lpix == rpix) {
    int k, n = 0;
    for (k = 0; k < 4; k++) {
      float c = (float)(2 * k + 1) / 8.0f;
      if (c >= fxl && c <= fxr) {
        n++;
      }
    }
    if (lpix >= x0 && lpix < x1) {
      aa_add(cov, lpix - x0, n);
    }
    return;
  }
  if (lpix >= x0 && lpix < x1) {
    aa_add(cov, lpix - x0, cov_left(fxl));
  }
  for (p = lpix + 1; p <= rpix - 1; p++) {
    if (p >= x0 && p < x1) {
      aa_add(cov, p - x0, 4);
    }
  }
  if (fxr > 0.0f && rpix >= x0 && rpix < x1) {
    aa_add(cov, rpix - x0, cov_right(fxr));
  }
}

/** @brief Emit one device row from the coverage buffer. */
static void emit_row(my_vgcanvas_soft_t* s, aa_rowbuf_t* rb, int32_t y,
                     int32_t base_x, int32_t width, int maxcov,
                     my_color_t color) {
  int32_t i = 0;
  int32_t first = -1, last = -1;
  while (i < width) {
    if (rb->cov[i] == 0) {
      i++;
      continue;
    }
    if ((int)rb->cov[i] == maxcov) {
      int32_t start = i;
      while (i < width && (int)rb->cov[i] == maxcov) {
        i++;
      }
      soft_fill_device_rect(s, my_rect_init(base_x + start, y, i - start, 1),
                            color);
      if (first < 0) {
        first = start;
      }
      last = i;
    } else {
      int32_t start = i, n = 0;
      while (i < width && rb->cov[i] > 0 && (int)rb->cov[i] < maxcov) {
        rb->alpha[n++] = (uint8_t)((color.a * rb->cov[i]) / maxcov);
        i++;
      }
      if (n > 0) {
        my_lcd_blend_span(s->lcd, base_x + start, y, rb->alpha, n, color);
      }
      if (first < 0) {
        first = start;
      }
      last = i;
    }
  }
  if (first >= 0) {
    my_dirty_rects_add(&s->dirty, &(my_rect_t){base_x + first, y,
                                               last - first, 1});
  }
}

/** @brief Collect even-odd scanline intersections at yc (device space). */
static size_t collect_intersections(my_vgcanvas_soft_t* s,
                                    const path_point_t* pts,
                                    const contour_t* contours,
                                    size_t ncontours, float yc, float* xs,
                                    size_t cap) {
  size_t nxs = 0, ci, i;
  for (ci = 0; ci < ncontours; ci++) {
    const contour_t* c = &contours[ci];
    for (i = 0; i < c->count; i++) {
      size_t j = i + 1;
      float x0, y0, x1, y1;
      if (j == c->count) {
        if (!c->closed) {
          break;
        }
        j = 0;
      }
      x0 = pts[c->start + i].x + s->state.tx;
      y0 = pts[c->start + i].y + s->state.ty;
      x1 = pts[c->start + j].x + s->state.tx;
      y1 = pts[c->start + j].y + s->state.ty;
      if ((y0 <= yc) != (y1 <= yc) && nxs < cap) {
        xs[nxs++] = x0 + (yc - y0) * (x1 - x0) / (y1 - y0);
      }
    }
  }
  return nxs;
}

/**
 * @brief Fill a set of polygon contours (even-odd) with the current AA
 * level. Core of soft_fill/soft_stroke (M8c: shared).
 */
static my_ret_t fill_polys(my_vgcanvas_soft_t* s, const path_point_t* pts,
                           size_t npts, const contour_t* contours,
                           size_t ncontours, my_color_t color) {
  const my_rect_t* clip = &s->state.clip;
  float* xs;
  size_t xs_cap;
  int32_t y;
  if (npts < 2 || ncontours == 0 || clip->w <= 0 || clip->h <= 0) {
    return MY_RET_OK;
  }
  xs_cap = npts;
  xs = (float*)my_mem_alloc(s->allocator, xs_cap * sizeof(float));
  if (xs == NULL) {
    return MY_RET_OOM;
  }
  if (s->antialias_level <= 0) {
    /* hard edges: pixel-center rule */
    for (y = clip->y; y < clip->y + clip->h; y++) {
      size_t nxs = collect_intersections(s, pts, contours, ncontours,
                                         (float)y + 0.5f, xs, xs_cap);
      size_t k;
      qsort(xs, nxs, sizeof(float), float_cmp);
      for (k = 0; k + 1 < nxs; k += 2) {
        int32_t xa = (int32_t)ceilf(xs[k] - 0.5f);
        int32_t xb = (int32_t)ceilf(xs[k + 1] - 0.5f);
        if (xb > xa) {
          soft_fill_device_rect(s, my_rect_init(xa, y, xb - xa, 1), color);
        }
      }
    }
  } else {
    aa_rowbuf_t rb;
    int halves = s->antialias_level >= 2 ? 2 : 1;
    static const float OFF1[1] = {0.5f};
    static const float OFF2[2] = {0.25f, 0.75f};
    const float* offs = halves == 2 ? OFF2 : OFF1;
    rb.cov = (uint8_t*)my_mem_alloc(s->allocator, (size_t)clip->w * 2);
    if (rb.cov == NULL) {
      my_mem_free(s->allocator, xs);
      return MY_RET_OOM;
    }
    rb.alpha = rb.cov + clip->w;
    {
      /* limit the scan to the polygon's y range (clipped) */
      float ymin = pts[0].y + s->state.ty, ymax = ymin;
      size_t pi;
      int32_t row0, row1;
      for (pi = 1; pi < npts; pi++) {
        float py = pts[pi].y + s->state.ty;
        if (py < ymin) {
          ymin = py;
        }
        if (py > ymax) {
          ymax = py;
        }
      }
      row0 = (int32_t)floorf(ymin) > clip->y ? (int32_t)floorf(ymin) : clip->y;
      row1 = (int32_t)ceilf(ymax) < clip->y + clip->h ? (int32_t)ceilf(ymax)
                                                      : clip->y + clip->h;
      for (y = row0; y < row1; y++) {
      float row_min = 0.0f, row_max = 0.0f;
      int32_t bx0, bw;
      int hh;
      row_min = (float)(clip->x + clip->w);
      row_max = (float)clip->x;
      for (hh = 0; hh < halves; hh++) {
        size_t nxs = collect_intersections(s, pts, contours, ncontours,
                                           (float)y + offs[hh], xs, xs_cap);
        if (nxs > 0) {
          qsort(xs, nxs, sizeof(float), float_cmp);
          if (xs[0] < row_min) {
            row_min = xs[0];
          }
          if (xs[nxs - 1] > row_max) {
            row_max = xs[nxs - 1];
          }
        }
      }
      if (row_max <= row_min) {
        continue;
      }
      bx0 = (int32_t)floorf(row_min);
      if (bx0 < clip->x) {
        bx0 = clip->x;
      }
      bw = (int32_t)ceilf(row_max) - bx0;
      if (bx0 + bw > clip->x + clip->w) {
        bw = clip->x + clip->w - bx0;
      }
      if (bw <= 0) {
        continue;
      }
      memset(rb.cov, 0, (size_t)bw);
      for (hh = 0; hh < halves; hh++) {
        size_t nxs = collect_intersections(s, pts, contours, ncontours,
                                           (float)y + offs[hh], xs, xs_cap);
        size_t k;
        qsort(xs, nxs, sizeof(float), float_cmp);
        for (k = 0; k + 1 < nxs; k += 2) {
          span_accum(rb.cov, bx0, bw, xs[k], xs[k + 1]);
        }
      }
      emit_row(s, &rb, y, bx0, bw, 4 * halves, color);
      }
    }
    my_mem_free(s->allocator, rb.cov);
  }
  my_mem_free(s->allocator, xs);
  return MY_RET_OK;
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
  if (s->antialias_level <= 0) {
    for (dy = -r; dy <= r; dy++) {
      int32_t dx = (int32_t)floorf(sqrtf((float)(r * r - dy * dy)));
      soft_fill_device_rect(s, my_rect_init(cx - dx, cy + dy, 2 * dx + 1, 1),
                            color);
    }
    return;
  }
  {
    int halves = s->antialias_level >= 2 ? 2 : 1;
    static const float OFF1[1] = {0.5f};
    static const float OFF2[2] = {0.25f, 0.75f};
    const float* offs = halves == 2 ? OFF2 : OFF1;
    const my_rect_t* clip = &s->state.clip;
    int32_t y0 = cy - r > clip->y ? cy - r : clip->y;
    int32_t y1 = cy + r < clip->y + clip->h - 1 ? cy + r : clip->y + clip->h - 1;
    aa_rowbuf_t rb;
    int32_t y;
    if (clip->w <= 0) {
      return;
    }
    rb.cov = (uint8_t*)my_mem_alloc(s->allocator, (size_t)clip->w * 2);
    if (rb.cov == NULL) {
      return;
    }
    rb.alpha = rb.cov + clip->w;
    for (y = y0; y <= y1; y++) {
      int hh;
      float fx = (float)cx + 0.5f;
      int32_t bx0 = (int32_t)floorf(fx - (float)r);
      int32_t bw = (int32_t)ceilf(fx + (float)r) - bx0 + 1;
      if (bx0 < clip->x) {
        bw -= clip->x - bx0;
        bx0 = clip->x;
      }
      if (bx0 + bw > clip->x + clip->w) {
        bw = clip->x + clip->w - bx0;
      }
      if (bw <= 0) {
        continue;
      }
      memset(rb.cov, 0, (size_t)bw);
      for (hh = 0; hh < halves; hh++) {
        float fdy = (float)y + offs[hh] - ((float)cy + 0.5f);
        float fdx;
        if (fdy * fdy > (float)(r * r)) {
          continue;
        }
        fdx = sqrtf((float)(r * r) - fdy * fdy);
        span_accum(rb.cov, bx0, bw, fx - fdx, fx + fdx);
      }
      emit_row(s, &rb, y, bx0, bw, 4 * halves, color);
    }
    my_mem_free(s->allocator, rb.cov);
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
static my_ret_t soft_fill(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  return fill_polys(s, s->points, s->point_count, s->contours,
                    s->contour_count, s->state.fill_color);
}

/** @brief Stroke: each segment becomes a quad contour filled with the
 * shared coverage path (blending + AA for free). Square caps/joins
 * (round caps are a TODO); translucent strokes may over-blend at joints
 * (segments are filled independently). */
static my_ret_t soft_stroke(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  float half = s->state.line_width / 2.0f;
  float odd_off = 0.0f;
  size_t ci, i;
  if (half < 0.5f) {
    half = 0.5f;
  }
  /* odd integer widths: shift 0.5px so thin lines land on pixel centers */
  if (((int32_t)s->state.line_width) % 2 == 1) {
    odd_off = 0.5f;
  }
  for (ci = 0; ci < s->contour_count; ci++) {
    const contour_t* c = &s->contours[ci];
    size_t edges = c->count > 1 ? (c->closed ? c->count : c->count - 1) : 0;
    for (i = 0; i < edges; i++) {
      size_t j = (i + 1) % c->count;
      float x0 = s->points[c->start + i].x + odd_off;
      float y0 = s->points[c->start + i].y + odd_off;
      float x1 = s->points[c->start + j].x + odd_off;
      float y1 = s->points[c->start + j].y + odd_off;
      float dx = x1 - x0, dy = y1 - y0;
      float len = sqrtf(dx * dx + dy * dy);
      float nx, ny;
      path_point_t quad[4];
      contour_t qcontour;
      if (len < 0.001f) {
        /* zero-length segment: small square stamp */
        quad[0].x = x0 - half;
        quad[0].y = y0 - half;
        quad[1].x = x0 + half;
        quad[1].y = y0 - half;
        quad[2].x = x0 + half;
        quad[2].y = y0 + half;
        quad[3].x = x0 - half;
        quad[3].y = y0 + half;
      } else {
        nx = -dy / len * half;
        ny = dx / len * half;
        quad[0].x = x0 + nx;
        quad[0].y = y0 + ny;
        quad[1].x = x1 + nx;
        quad[1].y = y1 + ny;
        quad[2].x = x1 - nx;
        quad[2].y = y1 - ny;
        quad[3].x = x0 - nx;
        quad[3].y = y0 - ny;
      }
      qcontour.start = 0;
      qcontour.count = 4;
      qcontour.closed = true;
      fill_polys(s, quad, 4, &qcontour, 1, s->state.stroke_color);
    }
  }
  return MY_RET_OK;
}

static my_ret_t soft_draw_text(my_vgcanvas_t* vg, const char* text, float x,
                               float y) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  const my_rect_t* clip = &s->state.clip;
  int32_t ascent;
  float pen_x;
  int32_t top;
  const char* p = text;

  if (text == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  if (s->state.font == NULL || s->state.font_size <= 0) {
    return MY_RET_NOT_SUPPORTED;
  }
  ascent = my_font_ascent(s->state.font, s->state.font_size);
  pen_x = x + s->state.tx;
  top = soft_round(y + s->state.ty);

  while (*p != '\0') {
    uint32_t cp = my_utf8_next(&p);
    my_glyph_t g;
    int32_t gx, gy, row;
    if (my_font_get_glyph(s->state.font, cp, s->state.font_size, &g) !=
        MY_RET_OK) {
      continue;
    }
    gx = soft_round(pen_x) + g.bearing_x;
    gy = top + ascent - g.bearing_y;
    if (g.bitmap != NULL) {
      for (row = 0; row < g.h; row++) {
        int32_t dy = gy + row;
        int32_t dx0 = gx, dx1 = gx + g.w;
        const uint8_t* alpha_row = g.bitmap + (size_t)row * (size_t)g.w;
        if (dy < clip->y || dy >= clip->y + clip->h) {
          continue;
        }
        if (dx0 < clip->x) {
          dx0 = clip->x;
        }
        if (dx1 > clip->x + clip->w) {
          dx1 = clip->x + clip->w;
        }
        if (dx1 > dx0) {
          my_lcd_blend_span(s->lcd, dx0, dy, alpha_row + (dx0 - gx), dx1 - dx0,
                            s->state.fill_color);
        }
      }
    }
    pen_x += (float)g.advance;
  }
  return MY_RET_OK;
}

static my_ret_t soft_set_font(my_vgcanvas_t* vg, my_font_t* font,
                              int32_t size) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  if (font != NULL) {
    s->state.font = font;
  }
  if (size > 0) {
    s->state.font_size = size;
  }
  return MY_RET_OK;
}

static my_ret_t soft_measure_text(my_vgcanvas_t* vg, const char* text,
                                  int32_t* w, int32_t* h) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  if (s->state.font == NULL || s->state.font_size <= 0) {
    return MY_RET_NOT_SUPPORTED;
  }
  return my_font_measure(s->state.font, text, s->state.font_size, w, h);
}

/** @brief Pack one RGBA pixel into the lcd's native format (over bg). */
static void pack_native(my_pixel_format_t fmt, const uint8_t* rgba,
                        const my_color_t* bg, uint8_t* out) {
  uint8_t r = rgba[0], g = rgba[1], b = rgba[2], a = rgba[3];
  if (bg != NULL && a < 255) {
    r = (uint8_t)(((uint32_t)r * a + (uint32_t)bg->r * (255u - a)) / 255u);
    g = (uint8_t)(((uint32_t)g * a + (uint32_t)bg->g * (255u - a)) / 255u);
    b = (uint8_t)(((uint32_t)b * a + (uint32_t)bg->b * (255u - a)) / 255u);
    a = 255;
  }
  switch (fmt) {
    case MY_PIXEL_FORMAT_RGB565: {
      uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      memcpy(out, &v, 2);
      break;
    }
    case MY_PIXEL_FORMAT_RGB888:
      out[0] = r;
      out[1] = g;
      out[2] = b;
      break;
    case MY_PIXEL_FORMAT_ARGB8888:
      out[0] = a;
      out[1] = r;
      out[2] = g;
      out[3] = b;
      break;
    case MY_PIXEL_FORMAT_BGRA8888:
      out[0] = b;
      out[1] = g;
      out[2] = r;
      out[3] = a;
      break;
    case MY_PIXEL_FORMAT_MONO:
    default:
      out[0] = (uint8_t)(((uint32_t)r * 299 + (uint32_t)g * 587 +
                          (uint32_t)b * 114) / 1000u >= 128u
                             ? 1
                             : 0);
      break;
  }
}

static my_ret_t soft_draw_image(my_vgcanvas_t* vg, const uint8_t* rgba,
                                int32_t w, int32_t h, const my_rectf_t* dst,
                                const my_color_t* bg) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  my_pixel_format_t fmt;
  my_rect_t dev, clipped;
  uint32_t bpp;
  uint8_t* row = NULL;
  int32_t dy;
  my_ret_t ret = MY_RET_OK;
  if (rgba == NULL || dst == NULL || w <= 0 || h <= 0) {
    return MY_RET_INVALID_PARAMS;
  }
  fmt = my_lcd_get_format(s->lcd);
  if (fmt == MY_PIXEL_FORMAT_MONO) {
    return MY_RET_NOT_SUPPORTED; /* 1bpp dithering: TODO */
  }
  bpp = my_pixel_format_bpp(fmt) / 8u;
  dev = my_rect_init((int32_t)floorf(dst->x + s->state.tx),
                     (int32_t)floorf(dst->y + s->state.ty),
                     (int32_t)floorf(dst->w), (int32_t)floorf(dst->h));
  if (!my_rect_intersect(&dev, &s->state.clip, &clipped)) {
    return MY_RET_OK;
  }
  row = (uint8_t*)my_mem_alloc(s->allocator, (size_t)clipped.w * bpp);
  if (row == NULL) {
    return MY_RET_OOM;
  }
  for (dy = clipped.y; dy < clipped.y + clipped.h; dy++) {
    int32_t sy = (int32_t)((int64_t)(dy - dev.y) * h / (dev.h > 0 ? dev.h : 1));
    int32_t dx;
    uint8_t* out = row;
    if (sy < 0) {
      sy = 0;
    }
    if (sy >= h) {
      sy = h - 1;
    }
    for (dx = clipped.x; dx < clipped.x + clipped.w; dx++) {
      int32_t sx = (int32_t)((int64_t)(dx - dev.x) * w / (dev.w > 0 ? dev.w : 1));
      if (sx < 0) {
        sx = 0;
      }
      if (sx >= w) {
        sx = w - 1;
      }
      pack_native(fmt, rgba + ((size_t)sy * (size_t)w + (size_t)sx) * 4u, bg,
                  out);
      out += bpp;
    }
    ret = my_lcd_draw_pixels(s->lcd, row, clipped.x, dy,
                             (uint32_t)clipped.w, 1);
    if (ret != MY_RET_OK) {
      break;
    }
  }
  my_mem_free(s->allocator, row);
  if (ret == MY_RET_OK) {
    my_dirty_rects_add(&s->dirty, &clipped);
  }
  return ret;
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
    soft_destroy,          soft_set_font,    soft_measure_text,
    soft_draw_image};

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
  s->state.font = NULL;
  s->state.font_size = 16;
  s->state.clip =
      my_rect_init(0, 0, (int32_t)my_lcd_get_width(lcd), (int32_t)my_lcd_get_height(lcd));
  s->antialias_level = 2;
  my_dirty_rects_init(&s->dirty);
  return (my_vgcanvas_t*)s;
}

void my_vgcanvas_soft_set_antialias(my_vgcanvas_t* vg, bool enabled) {
  my_vgcanvas_soft_set_antialias_level(vg, enabled ? 2 : 0);
}

void my_vgcanvas_soft_set_antialias_level(my_vgcanvas_t* vg, int level) {
  my_vgcanvas_soft_t* s = (my_vgcanvas_soft_t*)vg;
  if (s != NULL && s->base.vtable == &s_soft_vtable) {
    if (level < 0) {
      level = 0;
    }
    if (level > 2) {
      level = 2;
    }
    s->antialias_level = level;
  }
}

const my_dirty_rects_t* my_vgcanvas_soft_get_dirty_rects(my_vgcanvas_t* vg) {
  if (vg == NULL || vg->vtable != &s_soft_vtable) {
    return NULL;
  }
  return &((my_vgcanvas_soft_t*)vg)->dirty;
}
