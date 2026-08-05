/**
 * @file my_text_area_test.c
 * @brief Unit tests for the multi-line text_area (M9a).
 */
#include "myui/widgets/my_text_area.h"

#include "myui/my_event_dispatch.h"

#include "mypal/dummy/my_pal_dummy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mytest.h"
#include "rec_vgcanvas.h"

static size_t ta_off_(my_text_area_t* ta, size_t row) {
  return (size_t)my_darray_get(ta->line_offsets, row);
}

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
  my_text_area_t* ta = (my_text_area_t*)w;
  ta->focused = true;
  while (*s != '\0') {
    if (*s == '\n') {
      key(w, MY_KEY_RETURN, 0);
    } else {
      key(w, (uint8_t)*s, 0);
    }
    s++;
  }
}

/* ---------------- line offset cache ---------------- */

static void test_line_offsets_basic(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "ab\ncd\nef");
  TEST_ASSERT_EQ_INT(my_text_area_line_count(w), 3);
  TEST_ASSERT_EQ_INT(ta_off_(ta, 0), 0);
  TEST_ASSERT_EQ_INT(ta_off_(ta, 1), 3);
  TEST_ASSERT_EQ_INT(ta_off_(ta, 2), 6);

  /* insert into line 0: offsets of following lines shift */
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL); /* doc start */
  type_str(w, "X");
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "Xab\ncd\nef");
  TEST_ASSERT_EQ_INT(ta_off_(ta, 1), 4);
  TEST_ASSERT_EQ_INT(ta_off_(ta, 2), 7);

  /* split line 1 */
  key(w, MY_KEY_END, 0);
  key(w, MY_KEY_RETURN, 0);
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "Xab\n\ncd\nef");
  TEST_ASSERT_EQ_INT(my_text_area_line_count(w), 4);

  /* merge lines back */
  key(w, MY_KEY_BACKSPACE, 0);
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "Xab\ncd\nef");
  TEST_ASSERT_EQ_INT(my_text_area_line_count(w), 3);

  my_widget_unref(w);
}

/* ---------------- cursor movement ---------------- */

static void test_goal_column_vertical_moves(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "abcdef\nab\nabcdefgh");
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL); /* to doc start (0,0) */

  /* cursor at row 0 col 5; move down: clamps to row1 len 2 */
  {
    int i;
    for (i = 0; i < 5; i++) {
      key(w, MY_KEY_RIGHT, 0);
    }
  }
  TEST_ASSERT_EQ_INT(ta->cursor_row, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 5);

  key(w, MY_KEY_DOWN, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_row, 1);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 2); /* clamped to short line */

  /* goal col remembered: down again goes back to col 5 on row 2 */
  key(w, MY_KEY_DOWN, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_row, 2);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 5);

  /* horizontal move resets the goal column */
  key(w, MY_KEY_LEFT, 0);
  key(w, MY_KEY_UP, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 2); /* new goal = 4, clamped by row1 */

  my_widget_unref(w);
}

static void test_split_and_merge_lines(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "hello");
  ta->focused = true;
  key(w, MY_KEY_HOME, 0);
  key(w, MY_KEY_RIGHT, 0);
  key(w, MY_KEY_RIGHT, 0);
  key(w, MY_KEY_RETURN, 0); /* "he\nllo", cursor row 1 col 0 */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "he\nllo");
  TEST_ASSERT_EQ_INT(ta->cursor_row, 1);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 0);

  key(w, MY_KEY_BACKSPACE, 0); /* merge at line start */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "hello");
  TEST_ASSERT_EQ_INT(ta->cursor_row, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 2);

  key(w, MY_KEY_DELETE, 0); /* delete forward */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "helo");

  my_widget_unref(w);
}

/* ---------------- selection + clipboard ---------------- */

static void test_selection_and_clipboard_multiline(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* root = my_widget_create(NULL, "window"); /* fake root for pal? no */
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;
  char buf[64];

  (void)root;
  my_widget_unref(root);

  /* no window root: clipboard calls are silent no-ops (pal NULL) */
  my_text_area_set_text(w, "ab\ncd");
  ta->focused = true;
  key(w, 'a', MY_KEYMOD_CTRL);
  key(w, 'c', MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_INT(my_pal_clipboard_get_text(pal, buf, sizeof(buf)),
                     MY_RET_NOT_FOUND);
  my_widget_unref(w);
  my_pal_destroy(pal);

  /* selection semantics without pal */
  w = my_text_area_create(NULL);
  ta = (my_text_area_t*)w;
  my_text_area_set_text(w, "ab\ncd");
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL);
  key(w, MY_KEY_DOWN, MY_KEYMOD_SHIFT);
  key(w, MY_KEY_RIGHT, MY_KEYMOD_SHIFT);
  TEST_ASSERT(ta->cursor_row == 1 && ta->cursor_col == 1);
  TEST_ASSERT(ta->anchor_row == 0 && ta->anchor_col == 0);

  key(w, MY_KEY_BACKSPACE, 0); /* delete selection */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "d");

  my_widget_unref(w);
}

/* ---------------- utf8 ---------------- */

static void test_utf8_multiline(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "\xE4\xBD\xA0\xE5\xA5\xBD\n\xE4\xB8\xAD");
  ta->focused = true;
  TEST_ASSERT_EQ_INT(my_text_area_line_count(w), 2);
  TEST_ASSERT_EQ_INT(ta->cursor_row, 1);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 1); /* 中 = 1 codepoint */

  key(w, MY_KEY_LEFT, 0);
  TEST_ASSERT_EQ_INT(ta->cursor_col, 0);
  key(w, MY_KEY_BACKSPACE, 0); /* merge: 你好中 */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\xAD");
  TEST_ASSERT_EQ_INT(ta->cursor_col, 2);

  my_widget_unref(w);
}

/* ---------------- paint / viewport ---------------- */

static void test_paint_only_visible_rows(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;
  rec_vg_t rec;
  char big[64 * 20];
  int i, n = 0;

  for (i = 0; i < 20; i++) {
    n += snprintf(big + n, sizeof(big) - n, "row%02d\n", i);
  }
  my_text_area_set_text(w, big);
  my_widget_set_rect(w, &(my_rect_t){0, 0, 100, 40}); /* ~2 rows of 16px */

  rec_vg_init(&rec);
  my_widget_paint(w, (my_vgcanvas_t*)&rec);
  /* only visible rows painted (2-3), not all 20 */
  TEST_ASSERT(rec_count(&rec, "draw_text") <= 4);

  /* scroll down: cursor at bottom -> scroll_y follows */
  ta->focused = true;
  key(w, MY_KEY_END, MY_KEYMOD_CTRL);
  TEST_ASSERT(ta->scroll_y > 0);

  my_widget_unref(w);
}

static void test_readonly_and_max_len(void) {
  my_widget_t* w = my_text_area_create(NULL);

  my_text_area_set_max_len(w, 4);
  type_str(w, "abcdef");
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "abcd");

  my_text_area_set_readonly(w, true);
  type_str(w, "X");
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "abcd");
  my_text_area_set_readonly(w, false);

  my_widget_unref(w);
}

static void test_big_doc_cursor_perf(void) {
  /* 10k lines: cursor moves must not rescan the buffer per move */
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;
  size_t cap = 10000 * 4 + 1;
  char* big = (char*)malloc(cap);
  int i;
  size_t n = 0;
  clock_t t0, t1;

  for (i = 0; i < 10000; i++) {
    n += snprintf(big + n, cap - n, "r%d\n", i % 10);
  }
  my_text_area_set_text(w, big);
  ta->focused = true;

  t0 = clock();
  for (i = 0; i < 200; i++) {
    key(w, MY_KEY_UP, 0);
  }
  for (i = 0; i < 200; i++) {
    key(w, MY_KEY_DOWN, 0);
  }
  t1 = clock();
  free(big);
  /* 400 moves on 10k lines: generous 1s bound (should be ~ms) */
  TEST_ASSERT((double)(t1 - t0) / CLOCKS_PER_SEC < 1.0);

  my_widget_unref(w);
}

static void test_hint_and_focus(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;
  rec_vg_t rec;

  my_text_area_set_hint(w, "type here...");
  my_widget_set_rect(w, &(my_rect_t){0, 0, 200, 100});
  rec_vg_init(&rec);
  my_widget_paint(w, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "draw_text"), 1); /* hint drawn */

  my_emitter_emit(w->emitter, "focus", NULL);
  TEST_ASSERT(ta->focused);
  my_emitter_emit(w->emitter, "blur", NULL);
  TEST_ASSERT(!ta->focused);

  my_widget_unref(w);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* w = my_text_area_create(dbg);

  my_text_area_set_hint(w, "h");
  my_text_area_set_text(w, "line1\nline2\nline3");
  ((my_text_area_t*)w)->focused = true;
  type_str(w, "X\nY");
  key(w, 'a', MY_KEYMOD_CTRL);
  key(w, MY_KEY_BACKSPACE, 0);
  my_text_area_set_text(w, "reset");
  my_widget_invalidate(w, NULL);

  my_widget_unref(w);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_line_offsets_basic);
  MYTEST_RUN(test_goal_column_vertical_moves);
  MYTEST_RUN(test_split_and_merge_lines);
  MYTEST_RUN(test_selection_and_clipboard_multiline);
  MYTEST_RUN(test_utf8_multiline);
  MYTEST_RUN(test_paint_only_visible_rows);
  MYTEST_RUN(test_readonly_and_max_len);
  MYTEST_RUN(test_big_doc_cursor_perf);
  MYTEST_RUN(test_hint_and_focus);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
