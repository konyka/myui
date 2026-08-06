/**
 * @file rec_vgcanvas.h
 * @brief Recording vgcanvas for tests: logs every call as a text op.
 *
 * Op formats (space separated, floats via %g):
 *   begin_frame end_frame save restore
 *   translate X Y | clip X Y W H
 *   fill_rect X Y W H #RRGGBB | stroke_rect X Y W H #RRGGBB
 *   rounded_rect X Y W H R #RRGGBB | fill #RRGGBB | stroke #RRGGBB
 *   set_line_width W | move_to X Y | line_to X Y | close_path begin_path
 */
#ifndef REC_VGCANVAS_H
#define REC_VGCANVAS_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "myr/my_vgcanvas.h"

#define REC_VG_MAX_OPS 1024
#define REC_VG_OP_LEN 96

typedef struct rec_vg_t {
  my_vgcanvas_t base;
  int n_ops;
  char ops[REC_VG_MAX_OPS][REC_VG_OP_LEN];
  my_font_t* font;   /**< set via set_font (borrowed; for measure) */
  int32_t font_size;
} rec_vg_t;

static void rec_op(rec_vg_t* r, const char* fmt, ...) {
  va_list args;
  if (r->n_ops >= REC_VG_MAX_OPS) {
    return;
  }
  va_start(args, fmt);
  vsnprintf(r->ops[r->n_ops], REC_VG_OP_LEN, fmt, args);
  va_end(args);
  r->n_ops++;
}

/** @brief Count recorded ops whose text contains needle. */
static inline int rec_count(rec_vg_t* r, const char* needle) {
  int i, n = 0;
  for (i = 0; i < r->n_ops; i++) {
    if (strstr(r->ops[i], needle) != NULL) {
      n++;
    }
  }
  return n;
}

/** @brief Whether any recorded op contains needle. */
static inline bool rec_has(rec_vg_t* r, const char* needle) {
  return rec_count(r, needle) > 0;
}

#define REC_DEF_0(name, text)                          \
  static my_ret_t rec_##name(my_vgcanvas_t* vg) {      \
    rec_op((rec_vg_t*)vg, text);                       \
    return MY_RET_OK;                                  \
  }

REC_DEF_0(save, "save")
REC_DEF_0(restore, "restore")
REC_DEF_0(begin_path, "begin_path")
REC_DEF_0(close_path, "close_path")

static my_ret_t rec_begin_frame(my_vgcanvas_t* vg, const my_rect_t* dirty) {
  (void)dirty;
  rec_op((rec_vg_t*)vg, "begin_frame");
  return MY_RET_OK;
}

static my_ret_t rec_end_frame(my_vgcanvas_t* vg) {
  rec_op((rec_vg_t*)vg, "end_frame");
  return MY_RET_OK;
}

static my_ret_t rec_translate(my_vgcanvas_t* vg, float dx, float dy) {
  rec_op((rec_vg_t*)vg, "translate %g %g", (double)dx, (double)dy);
  return MY_RET_OK;
}

static my_ret_t rec_clip_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  rec_op((rec_vg_t*)vg, "clip %g %g %g %g", (double)rect->x, (double)rect->y,
         (double)rect->w, (double)rect->h);
  return MY_RET_OK;
}

static unsigned color_key(my_color_t c) {
  return ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | (unsigned)c.b;
}

static my_ret_t rec_set_fill_color(my_vgcanvas_t* vg, my_color_t color) {
  rec_op((rec_vg_t*)vg, "set_fill #%06x", color_key(color));
  return MY_RET_OK;
}

static my_ret_t rec_set_stroke_color(my_vgcanvas_t* vg, my_color_t color) {
  rec_op((rec_vg_t*)vg, "set_stroke #%06x", color_key(color));
  return MY_RET_OK;
}

static my_ret_t rec_set_line_width(my_vgcanvas_t* vg, float width) {
  rec_op((rec_vg_t*)vg, "set_line_width %g", (double)width);
  return MY_RET_OK;
}

static my_ret_t rec_fill_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  rec_op((rec_vg_t*)vg, "fill_rect %g %g %g %g", (double)rect->x, (double)rect->y,
         (double)rect->w, (double)rect->h);
  return MY_RET_OK;
}

static my_ret_t rec_stroke_rect(my_vgcanvas_t* vg, const my_rectf_t* rect) {
  rec_op((rec_vg_t*)vg, "stroke_rect %g %g %g %g", (double)rect->x,
         (double)rect->y, (double)rect->w, (double)rect->h);
  return MY_RET_OK;
}

static my_ret_t rec_fill_rounded_rect(my_vgcanvas_t* vg, const my_rectf_t* rect,
                                      float radius) {
  rec_op((rec_vg_t*)vg, "rounded_rect %g %g %g %g %g", (double)rect->x,
         (double)rect->y, (double)rect->w, (double)rect->h, (double)radius);
  return MY_RET_OK;
}

static my_ret_t rec_move_to(my_vgcanvas_t* vg, float x, float y) {
  rec_op((rec_vg_t*)vg, "move_to %g %g", (double)x, (double)y);
  return MY_RET_OK;
}

static my_ret_t rec_line_to(my_vgcanvas_t* vg, float x, float y) {
  rec_op((rec_vg_t*)vg, "line_to %g %g", (double)x, (double)y);
  return MY_RET_OK;
}

static my_ret_t rec_fill(my_vgcanvas_t* vg) {
  rec_op((rec_vg_t*)vg, "fill");
  return MY_RET_OK;
}

static my_ret_t rec_stroke(my_vgcanvas_t* vg) {
  rec_op((rec_vg_t*)vg, "stroke");
  return MY_RET_OK;
}

static my_ret_t rec_draw_text(my_vgcanvas_t* vg, const char* text, float x,
                              float y) {
  rec_op((rec_vg_t*)vg, "draw_text %g %g %s", (double)x, (double)y,
         text != NULL ? text : "");
  return MY_RET_OK;
}

static my_ret_t rec_draw_image(my_vgcanvas_t* vg, const uint8_t* rgba,
                               int32_t w, int32_t h, const my_rectf_t* dst,
                               const my_color_t* bg) {
  (void)rgba;
  (void)bg;
  rec_op((rec_vg_t*)vg, "draw_image %d %d %g %g %g %g", (int)w, (int)h,
         (double)dst->x, (double)dst->y, (double)dst->w, (double)dst->h);
  return MY_RET_OK;
}

static my_ret_t rec_set_line_cap(my_vgcanvas_t* vg, my_line_cap_t cap) {
  rec_op((rec_vg_t*)vg, "set_line_cap %d", (int)cap);
  return MY_RET_OK;
}

static my_ret_t rec_set_line_join(my_vgcanvas_t* vg, my_line_join_t join) {
  rec_op((rec_vg_t*)vg, "set_line_join %d", (int)join);
  return MY_RET_OK;
}

static void rec_destroy(my_vgcanvas_t* vg) {
  (void)vg; /* stack-allocated, nothing to free */
}

static my_ret_t rec_set_font(my_vgcanvas_t* vg, my_font_t* font, int32_t size) {
  rec_vg_t* r = (rec_vg_t*)vg;
  rec_op(r, "set_font %d", (int)size);
  if (font != NULL) {
    r->font = font;
  }
  if (size > 0) {
    r->font_size = size;
  }
  return MY_RET_OK;
}

static my_ret_t rec_measure_text(my_vgcanvas_t* vg, const char* text,
                                 int32_t* w, int32_t* h) {
  rec_vg_t* r = (rec_vg_t*)vg;
  if (r->font == NULL || r->font_size <= 0) {
    return MY_RET_NOT_SUPPORTED;
  }
  return my_font_measure(r->font, text, r->font_size, w, h);
}

static const my_vgcanvas_vtable_t REC_VG_VTABLE = {
    rec_begin_frame, rec_end_frame,  rec_save,       rec_restore,
    rec_translate,   rec_clip_rect,  rec_set_fill_color, rec_set_stroke_color,
    rec_set_line_width, rec_fill_rect, rec_stroke_rect, rec_fill_rounded_rect,
    rec_begin_path,  rec_move_to,    rec_line_to,    rec_close_path,
    rec_fill,        rec_stroke,     rec_draw_text,  rec_destroy,
    rec_set_font,    rec_measure_text, rec_draw_image, rec_set_line_cap,
    rec_set_line_join};

static void rec_vg_init(rec_vg_t* r) {
  memset(r, 0, sizeof(*r));
  r->base.vtable = &REC_VG_VTABLE;
}

#endif /* REC_VGCANVAS_H */
