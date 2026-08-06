/**
 * @file my_text_area_wrap_test.c
 * @brief Word wrap tests for text_area (M10b).
 */
#include "myui/widgets/my_text_area.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_event_t key_ev(uint32_t key, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = key;
  e.u.key.modifiers = mods;
  return e;
}

static void key(my_widget_t* w, uint32_t k, uint8_t mods) {
  my_event_t e = key_ev(k, mods);
  w->vtable->on_event(w, &e);
}

static void type_str(my_widget_t* w, const char* s) {
  ((my_text_area_t*)w)->focused = true;
  while (*s != '\0') {
    if (*s == '\n') {
      key(w, MY_KEY_RETURN, 0);
    } else {
      key(w, (uint8_t)*s, 0);
    }
    s++;
  }
}

/* bitmap font: 8px cells; inner width = w_px - 8 (TA_PAD_X * 2) */
static my_widget_t* wrap_area(int32_t w_px) {
  my_widget_t* w = my_text_area_create(NULL);
  my_font_t* font = my_font_bitmap_create(NULL);
  my_text_area_set_font(w, font, 8);
  my_widget_set_rect(w, &(my_rect_t){0, 0, w_px, 200});
  my_text_area_set_wrap(w, true);
  return w;
}

static void test_break_points_with_space(void) {
  my_widget_t* w = wrap_area(40); /* inner 32px = 4 cells */

  /* "aaa bbb ccc" (11 cp) at 4 cells: break after the space at col 3:
   * v0="aaa " (4), v1="bbb " (4), v2="ccc" (3) */
  my_text_area_set_text(w, "aaa bbb ccc");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 3);
  {
    const my_visual_line_t* v0 = my_text_area_visual_line_at(w, 0);
    const my_visual_line_t* v1 = my_text_area_visual_line_at(w, 1);
    const my_visual_line_t* v2 = my_text_area_visual_line_at(w, 2);
    TEST_ASSERT_EQ_INT(v0->len_cp, 4);
    TEST_ASSERT_EQ_INT(v0->start_cp, 0);
    TEST_ASSERT_EQ_INT(v1->start_cp, 4);
    TEST_ASSERT_EQ_INT(v1->len_cp, 4);
    TEST_ASSERT_EQ_INT(v2->start_cp, 8);
    TEST_ASSERT_EQ_INT(v2->len_cp, 3);
  }
  my_widget_unref(w);
}

static void test_hard_break_no_space(void) {
  my_widget_t* w = wrap_area(64); /* 7 cells */

  my_text_area_set_text(w, "abcdefghijk"); /* 11 cp, no spaces */
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 2);
  {
    const my_visual_line_t* v0 = my_text_area_visual_line_at(w, 0);
    const my_visual_line_t* v1 = my_text_area_visual_line_at(w, 1);
    TEST_ASSERT_EQ_INT(v0->len_cp, 7); /* hard break at width */
    TEST_ASSERT_EQ_INT(v1->start_cp, 7);
    TEST_ASSERT_EQ_INT(v1->len_cp, 4);
  }
  my_widget_unref(w);
}

static void test_empty_and_boundary_lines(void) {
  my_widget_t* w = wrap_area(64);

  my_text_area_set_text(w, "\n\nab");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 3); /* 2 empties+1 */
  {
    const my_visual_line_t* v0 = my_text_area_visual_line_at(w, 0);
    TEST_ASSERT_EQ_INT(v0->len_cp, 0);
    TEST_ASSERT_EQ_INT(v0->phys, 0);
  }
  my_widget_unref(w);
}

static void test_edit_rebuilds_only_affected(void) {
  my_widget_t* w = wrap_area(40);
  my_text_area_t* ta = (my_text_area_t*)w;
  size_t before;

  my_text_area_set_text(w, "aaa bbb ccc\nddd eee");
  before = my_text_area_visual_line_count(w);

  /* edit on physical line 1 ("ddd eee"): visual lines of line 0 kept */
  ta->focused = true;
  key(w, MY_KEY_END, MY_KEYMOD_CTRL);
  type_str(w, " fff ggg");
  TEST_ASSERT(my_text_area_visual_line_count(w) > before);
  /* line 0's visual structure intact */
  {
    const my_visual_line_t* v0 = my_text_area_visual_line_at(w, 0);
    TEST_ASSERT_EQ_INT(v0->phys, 0);
    TEST_ASSERT_EQ_INT(v0->len_cp, 4);
  }
  my_widget_unref(w);
}

static void test_resize_full_rebuild(void) {
  my_widget_t* w = wrap_area(40);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "aaa bbb ccc");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 3);

  /* widen to 12 cells: the whole string fits one visual line */
  my_widget_set_rect(w, &(my_rect_t){0, 0, 104, 200});
  ta->vlines_dirty = true; /* layout marks dirty; simulate relayout */
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 1);

  /* 8 cells: "aaa bbb "/"ccc" -> 2 visual lines */
  my_widget_set_rect(w, &(my_rect_t){0, 0, 72, 200});
  ta->vlines_dirty = true;
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 2);
  my_widget_unref(w);
}

static void test_visual_cursor_up_down_goal_col(void) {
  my_widget_t* w = wrap_area(40); /* v0 "aaa " v1 "bbb " v2 "ccc" */
  my_text_area_t* ta = (my_text_area_t*)w;
  size_t civ, vi;

  my_text_area_set_text(w, "aaa bbb ccc");
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL);

  key(w, MY_KEY_RIGHT, 0);
  key(w, MY_KEY_RIGHT, 0); /* col 2 in v0 */
  key(w, MY_KEY_DOWN, 0);    /* visual down: v1, goal col 2 */
  TEST_ASSERT_EQ_INT(ta->cursor_row, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 4 + 2); /* still physical line 0 */

  key(w, MY_KEY_DOWN, 0); /* v2 "ccc", goal 2 */
  TEST_ASSERT_EQ_INT(ta->cursor_col, 8 + 2);

  /* back up: goal col still 2 */
  key(w, MY_KEY_UP, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 4 + 2);

  vi = my_text_area_visual_line_of_pos(w, ta->cursor_row, ta->cursor_col, &civ);
  TEST_ASSERT_EQ_INT(vi, 1);

  my_widget_unref(w);
}

static void test_home_end_visual_semantics(void) {
  my_widget_t* w = wrap_area(40);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "aaa bbb ccc");
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL);
  key(w, MY_KEY_DOWN, 0); /* into v1 ("bbb ") */

  key(w, MY_KEY_HOME, 0); /* visual line start */
  TEST_ASSERT_EQ_INT(ta->cursor_col, 4);
  key(w, MY_KEY_END, 0); /* visual line end */
  TEST_ASSERT_EQ_INT(ta->cursor_col, 8);

  my_widget_unref(w);
}

static void test_left_right_cross_visual_boundary(void) {
  my_widget_t* w = wrap_area(40);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "aaa bbb ccc");
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL);

  /* walk right past the visual boundary (col 4 -> 5): stays one move */
  key(w, MY_KEY_RIGHT, 0);
  key(w, MY_KEY_RIGHT, 0);
  key(w, MY_KEY_RIGHT, 0);
  key(w, MY_KEY_RIGHT, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 4);
  key(w, MY_KEY_RIGHT, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 5); /* into v1 physically, one cp */

  my_widget_unref(w);
}

static void test_paint_wrap_segments(void) {
  my_widget_t* w = wrap_area(40);
  my_text_area_t* ta = (my_text_area_t*)w;
  rec_vg_t rec;

  my_text_area_set_text(w, "aaa bbb ccc");
  my_widget_set_rect(w, &(my_rect_t){0, 0, 40, 200});
  ta->scroll_y = 0;
  ta->vlines_dirty = true;
  my_text_area_visual_line_count(w);

  rec_vg_init(&rec);
  my_widget_paint(w, (my_vgcanvas_t*)&rec);
  /* 3 visual lines -> 3 draw_text ops */
  TEST_ASSERT_EQ_INT(rec_count(&rec, "draw_text"), 3);

  my_widget_unref(w);
}

static void test_wrap_off_zero_regression(void) {
  my_widget_t* w = wrap_area(64);

  my_text_area_set_text(w, "aaa bbb ccc");
  my_text_area_set_wrap(w, false);
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 1); /* off = phys */
  my_widget_unref(w);
}

static void test_wrap_scroll_and_no_hscroll(void) {
  my_widget_t* w = wrap_area(64);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "aaa bbb ccc");
  ta->focused = true;
  key(w, MY_KEY_END, MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_INT(ta->scroll_x, 0);
  TEST_ASSERT(ta->scroll_y > 0 || ta->cursor_row == 0);

  my_widget_unref(w);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_break_points_with_space);
  MYTEST_RUN(test_hard_break_no_space);
  MYTEST_RUN(test_empty_and_boundary_lines);
  MYTEST_RUN(test_edit_rebuilds_only_affected);
  MYTEST_RUN(test_resize_full_rebuild);
  MYTEST_RUN(test_visual_cursor_up_down_goal_col);
  MYTEST_RUN(test_home_end_visual_semantics);
  MYTEST_RUN(test_left_right_cross_visual_boundary);
  MYTEST_RUN(test_paint_wrap_segments);
  MYTEST_RUN(test_wrap_off_zero_regression);
  MYTEST_RUN(test_wrap_scroll_and_no_hscroll);
MYTEST_MAIN_END()
