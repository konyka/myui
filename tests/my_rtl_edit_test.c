/**
 * @file my_rtl_edit_test.c
 * @brief RTL editing tests (M12a): visual arrow navigation, clicks,
 * Home/End, selection segments for edit and text_area. Deterministic
 * cases use the no-font 8px-cell fallback; a Noto Hebrew case (skip when
 * absent) checks the same logic with real glyph advances.
 */
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_text_area.h"

#include <string.h>

#include "myr/my_text_layout.h"
#include "mytest.h"
#if defined(MYUI_BIDI)
#include "rec_vgcanvas.h"
#endif

#if defined(MYUI_BIDI)

/* "abc אבג def": 11 cps, 14 bytes (Hebrew = 2 bytes/cp) */
static const char MIXED[] = "abc \xD7\x90\xD7\x91\xD7\x92 def";
static const char HEBREW[] = "\xD7\x90\xD7\x91\xD7\x92"; /* אבג */

static void key(my_widget_t* w, uint32_t k, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = k;
  e.u.key.modifiers = mods;
  w->vtable->on_event(w, &e);
}

static void click_at(my_widget_t* w, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  w->vtable->on_event(w, &e);
}

/** @brief fill_rect ops directly after the highlight set_fill (selection
 * segments); stops at the next set_fill. */
static int highlight_rects(rec_vg_t* rec) {
  int i, n = 0;
  bool on = false;
  for (i = 0; i < rec->n_ops; i++) {
    if (strncmp(rec->ops[i], "set_fill ", 9) == 0) {
      on = strstr(rec->ops[i], "#82aae6") != NULL;
    } else if (on && strncmp(rec->ops[i], "fill_rect ", 10) == 0) {
      n++;
    }
  }
  return n;
}

static my_widget_t* make_edit(const char* text) {
  my_widget_t* e = my_edit_create(NULL);
  my_widget_set_rect(e, &(my_rect_t){0, 0, 400, 30});
  my_edit_set_text(e, text);
  ((my_edit_t*)e)->focused = true;
  return e;
}

static void test_edit_rtl_left_walk(void) {
  my_widget_t* e = make_edit(MIXED);
  /* cursor starts at the logical end (byte 14 = cp 11). Visual Left
   * walk: cps 10 9 8 5 6 4 3 2 1 0 -> bytes: */
  static const size_t expect[] = {13, 12, 11, 6, 8, 4, 3, 2, 1, 0};
  size_t i;
  for (i = 0; i < 10; i++) {
    key(e, MY_KEY_LEFT, 0);
    TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, expect[i]);
  }
  my_widget_unref(e);
}

static void test_edit_rtl_right_walk(void) {
  my_widget_t* e = make_edit(MIXED);
  static const size_t expect[] = {1, 2, 3, 4, 8, 6, 11, 12, 13, 14};
  size_t i;
  key(e, MY_KEY_HOME, 0); /* visual start = cp 0 (LTR paragraph) */
  TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, 0);
  for (i = 0; i < 10; i++) {
    key(e, MY_KEY_RIGHT, 0);
    TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, expect[i]);
  }
  my_widget_unref(e);
}

static void test_edit_rtl_home_end_pure_rtl(void) {
  my_widget_t* e = make_edit(HEBREW);
  key(e, MY_KEY_HOME, 0);
  TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, 6); /* visual start = cp3 */
  key(e, MY_KEY_END, 0);
  TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, 0); /* visual end = cp0 */
  my_widget_unref(e);
}

static void test_edit_rtl_click(void) {
  my_widget_t* e = make_edit(MIXED);
  /* cells are 8px (no font); click x=36 (right half of ג [32,40)) */
  click_at(e, 4 + 36, 15); /* + EDIT_PAD_X */
  TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, 8);  /* boundary cp6 */
  click_at(e, 4 + 34, 15); /* left half of ג */
  TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, 4);  /* boundary cp4 */
  click_at(e, 4 + 0, 15);
  TEST_ASSERT_EQ_INT(((my_edit_t*)e)->cursor, 0);
  my_widget_unref(e);
}

static void test_edit_rtl_selection_segments(void) {
  rec_vg_t rec;
  my_widget_t* e = make_edit(MIXED);
  my_edit_t* ed = (my_edit_t*)e;
  rec_vg_init(&rec);
  /* select across the run boundary: cps [3,5) -> 2 visual segments */
  ed->anchor = 3;
  ed->cursor = 6; /* byte of cp5 */
  my_widget_paint(e, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(highlight_rects(&rec), 2);
  my_widget_unref(e);
}

/* ---------------- text_area ---------------- */

static my_widget_t* make_ta(const char* text, int32_t w_px) {
  my_widget_t* w = my_text_area_create(NULL);
  my_widget_set_rect(w, &(my_rect_t){0, 0, w_px, 200});
  my_text_area_set_text(w, text);
  ((my_text_area_t*)w)->focused = true;
  return w;
}

static void test_ta_rtl_arrows(void) {
  my_widget_t* ta = make_ta(MIXED, 400);
  my_text_area_t* t = (my_text_area_t*)ta;
  /* Ctrl+Home -> (0,0); visual Right walk: cols 1 2 3 4 6 5 8 9 10 11 */
  static const size_t expect[] = {1, 2, 3, 4, 6, 5, 8, 9, 10, 11};
  size_t i;
  key(ta, MY_KEY_HOME, MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_INT(t->cursor_col, 0);
  for (i = 0; i < 10; i++) {
    key(ta, MY_KEY_RIGHT, 0);
    TEST_ASSERT_EQ_INT(t->cursor_col, expect[i]);
    TEST_ASSERT_EQ_INT(t->cursor_row, 0);
  }
  /* and back */
  {
    static const size_t back[] = {10, 9, 8, 5, 6, 4, 3, 2, 1, 0};
    for (i = 0; i < 10; i++) {
      key(ta, MY_KEY_LEFT, 0);
      TEST_ASSERT_EQ_INT(t->cursor_col, back[i]);
    }
  }
  my_widget_unref(ta);
}

static void test_ta_rtl_home_end(void) {
  my_widget_t* ta = make_ta(HEBREW, 400);
  key(ta, MY_KEY_HOME, MY_KEYMOD_CTRL);
  key(ta, MY_KEY_HOME, 0);
  TEST_ASSERT_EQ_INT(((my_text_area_t*)ta)->cursor_col, 3); /* visual start */
  key(ta, MY_KEY_END, 0);
  TEST_ASSERT_EQ_INT(((my_text_area_t*)ta)->cursor_col, 0); /* visual end */
  my_widget_unref(ta);
}

static void test_ta_rtl_click(void) {
  my_widget_t* ta = make_ta(MIXED, 400);
  my_text_area_t* t = (my_text_area_t*)ta;
  click_at(ta, 4 + 46, 6); /* right half of ב [40,48) -> boundary cp5 */
  TEST_ASSERT_EQ_INT(t->cursor_col, 5);
  TEST_ASSERT_EQ_INT(t->cursor_row, 0);
  click_at(ta, 4 + 34, 6); /* left half of ג -> cp4 */
  TEST_ASSERT_EQ_INT(t->cursor_col, 4);
  my_widget_unref(ta);
}

static void test_ta_rtl_selection_segments(void) {
  rec_vg_t rec;
  my_widget_t* ta = make_ta(MIXED, 400);
  my_text_area_t* t = (my_text_area_t*)ta;
  rec_vg_init(&rec);
  /* select the whole text: one visual segment; then boundary-crossing */
  t->anchor_row = 0;
  t->anchor_col = 0;
  t->cursor_row = 0;
  t->cursor_col = 11;
  my_widget_paint(ta, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(highlight_rects(&rec), 1);

  rec_vg_init(&rec);
  t->anchor_col = 3; /* cp3..cp5 across the boundary: 2 segments */
  t->cursor_col = 5;
  my_widget_paint(ta, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(highlight_rects(&rec), 2);
  my_widget_unref(ta);
}

static void test_rtl_with_noto_font(void) {
  my_font_t* font = my_font_stb_create(
      NULL, "/usr/share/fonts/google-noto-vf/NotoSansHebrew[wght].ttf", 0);
  my_text_layout_t* l;
  int32_t x4, x6, x5;
  if (font == NULL) {
    fprintf(stdout, "SKIP: no Noto Hebrew font\n");
    return;
  }
  l = my_text_layout_process(NULL, MIXED);
  TEST_ASSERT_NOT_NULL(l);
  /* same logical walk as the cell fallback (font-independent) */
  {
    size_t b = 11;
    b = my_text_layout_boundary_left(l, b);
    TEST_ASSERT_EQ_INT(b, 10);
    b = my_text_layout_boundary_left(l, b);
    TEST_ASSERT_EQ_INT(b, 9);
  }
  /* visual x order within the Hebrew run with real advances:
   * b4 (run's left end) < b6 (inside) < b5 (run's right end) */
  x4 = my_text_layout_visual_x(l, font, 16, 4);
  x6 = my_text_layout_visual_x(l, font, 16, 6);
  x5 = my_text_layout_visual_x(l, font, 16, 5);
  TEST_ASSERT(x4 < x6 && x6 < x5);
  my_text_layout_destroy(l);
  my_font_destroy(font);
}

#endif /* MYUI_BIDI */

MYTEST_MAIN_BEGIN()
#if defined(MYUI_BIDI)
  MYTEST_RUN(test_edit_rtl_left_walk);
  MYTEST_RUN(test_edit_rtl_right_walk);
  MYTEST_RUN(test_edit_rtl_home_end_pure_rtl);
  MYTEST_RUN(test_edit_rtl_click);
  MYTEST_RUN(test_edit_rtl_selection_segments);
  MYTEST_RUN(test_ta_rtl_arrows);
  MYTEST_RUN(test_ta_rtl_home_end);
  MYTEST_RUN(test_ta_rtl_click);
  MYTEST_RUN(test_ta_rtl_selection_segments);
  MYTEST_RUN(test_rtl_with_noto_font);
#else
  fprintf(stdout, "SKIP: built with MYUI_BIDI=OFF\n");
#endif
MYTEST_MAIN_END()
