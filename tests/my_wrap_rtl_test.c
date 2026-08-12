/**
 * @file my_wrap_rtl_test.c
 * @brief Wrap + RTL line-level semantics (M13b): default alignment
 * follows the PARAGRAPH base direction (RTL paragraph -> right edge),
 * and vertical cursor moves enter a wrapped RTL line at its visual
 * start (logical end). Bitmap font, 8px cells.
 */
#include "myui/widgets/my_text_area.h"

#include <stdlib.h>
#include <string.h>

#include "mytest.h"
#include "rec_vgcanvas.h"

/* 10 Hebrew cps (אבגדהוזחטיכ) */
static const char HEB10[] =
    "\xD7\x90\xD7\x91\xD7\x92\xD7\x93\xD7\x94\xD7\x95\xD7\x96\xD7\x97\xD7"
    "\x98\xD7\x99";

static my_widget_t* make_ta(const char* text, int32_t w_px, bool wrap) {
  my_widget_t* w = my_text_area_create(NULL);
  my_font_t* f = my_font_bitmap_create(NULL);
  my_text_area_set_font(w, f, 8);
  my_widget_set_rect(w, &(my_rect_t){0, 0, w_px, 200});
  my_text_area_set_wrap(w, wrap);
  my_text_area_set_text(w, text);
  return w;
}

static void key(my_widget_t* w, uint32_t k, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = k;
  e.u.key.modifiers = mods;
  w->vtable->on_event(w, &e);
}

/** @brief x of the n-th draw_text op (-1 when fewer). */
static float nth_text_x(rec_vg_t* rec, int nth) {
  int i;
  for (i = 0; i < rec->n_ops; i++) {
    if (strncmp(rec->ops[i], "draw_text ", 10) == 0 && nth-- == 0) {
      return (float)atof(rec->ops[i] + 10);
    }
  }
  return -1.0f;
}

static void paint_rec(my_widget_t* ta, rec_vg_t* rec) {
  static my_font_t* bmp = NULL;
  if (bmp == NULL) {
    bmp = my_font_bitmap_create(NULL);
  }
  rec_vg_init(rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)rec, bmp, 8);
  my_widget_paint(ta, (my_vgcanvas_t*)rec);
}

static void test_rtl_wrap_defaults_right_aligned(void) {
  rec_vg_t rec;
  my_widget_t* ta;
  /* w=64 -> inner 56 (7 cells); 10 Hebrew cps wrap 7+3. v0 (7 cps=56px)
   * fills the line; v1 (3 cps=24px): LEFT would draw at x=4, the RTL
   * default right-aligns -> 4+(56-24)=36 */
  ta = make_ta(HEB10, 64, true);
  ((my_text_area_t*)ta)->focused = true;
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(ta), 2);
  paint_rec(ta, &rec);
  TEST_ASSERT(nth_text_x(&rec, 0) == 4.0f);  /* v0 fills the width */
  TEST_ASSERT(nth_text_x(&rec, 1) == 36.0f); /* v1 right-aligned */
  my_widget_unref(ta);

  /* a mixed LTR paragraph (first strong is Latin) stays LEFT */
  ta = make_ta("abc \xD7\x90\xD7\x91\xD7\x92\xD7\x93\xD7\x94\xD7\x95"
               "\xD7\x96\xD7\x97\xD7\x98",
               64, true); /* "abc אבגדהוזחט" = 14 cps -> v1 7 cps */
  paint_rec(ta, &rec);
  TEST_ASSERT(nth_text_x(&rec, 1) == 4.0f); /* v1 stays left */
  my_widget_unref(ta);
}

static void test_rtl_wrap_down_enters_visual_start(void) {
  my_widget_t* ta = make_ta(HEB10, 64, true);
  my_text_area_t* t = (my_text_area_t*)ta;
  t->focused = true;
  key(ta, MY_KEY_HOME, MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_INT(t->cursor_row, 0);
  TEST_ASSERT_EQ_INT(t->cursor_col, 0);

  /* Down into v1 (cps 7..9): enters at the line's VISUAL start, which
   * for an RTL line is its logical end (boundary after cp 9 -> col 10) */
  key(ta, MY_KEY_DOWN, 0);
  TEST_ASSERT_EQ_INT(t->cursor_row, 0);
  TEST_ASSERT_EQ_INT(t->cursor_col, 10);

  /* Up returns to v0's visual start: logical end of the 7-cp segment */
  key(ta, MY_KEY_UP, 0);
  TEST_ASSERT_EQ_INT(t->cursor_col, 7);
  my_widget_unref(ta);
}

static void test_rtl_wrap_explicit_align_wins(void) {
  rec_vg_t rec;
  my_widget_t* ta = make_ta(HEB10, 64, true);
  my_text_area_set_align(ta, MY_TEXT_ALIGN_CENTER);
  paint_rec(ta, &rec);
  /* explicit CENTER on v1 (24px in 56): 4 + (56-24)/2 = 20 */
  TEST_ASSERT(nth_text_x(&rec, 1) == 20.0f);
  my_widget_unref(ta);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_rtl_wrap_defaults_right_aligned);
  MYTEST_RUN(test_rtl_wrap_down_enters_visual_start);
  MYTEST_RUN(test_rtl_wrap_explicit_align_wins);
MYTEST_MAIN_END()
