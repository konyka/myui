/**
 * @file my_line_break_test.c
 * @brief UAX#14 subset line breaking tests (M12d): class lookup, CJK
 * no-start/no-end rules, hyphen breaks, word runs, ASCII regression.
 */
#include "myr/my_line_break.h"
#include "myui/widgets/my_text_area.h"

#include "mytest.h"

/* width helper: w_px -> inner cells = (w_px - 8) / 8 */
static my_widget_t* wrap_area(int32_t w_px) {
  my_widget_t* w = my_text_area_create(NULL);
  my_font_t* f = my_font_bitmap_create(NULL);
  my_text_area_set_font(w, f, 8);
  my_widget_set_rect(w, &(my_rect_t){0, 0, w_px, 200});
  my_text_area_set_wrap(w, true);
  return w;
}

static void expect_vline(my_widget_t* w, size_t i, size_t start,
                         size_t len) {
  const my_visual_line_t* v = my_text_area_visual_line_at(w, i);
  TEST_ASSERT_EQ_INT(v->start_cp, start);
  TEST_ASSERT_EQ_INT(v->len_cp, len);
}

static void test_class_lookup(void) {
  TEST_ASSERT_EQ_INT(my_line_break_class('a'), MY_LB_AL);
  TEST_ASSERT_EQ_INT(my_line_break_class('0'), MY_LB_AL);
  TEST_ASSERT_EQ_INT(my_line_break_class(' '), MY_LB_SP);
  TEST_ASSERT_EQ_INT(my_line_break_class('-'), MY_LB_HY);
  TEST_ASSERT_EQ_INT(my_line_break_class(0x00ADu), MY_LB_HY); /* soft hyph */
  TEST_ASSERT_EQ_INT(my_line_break_class(0x4F60u), MY_LB_ID); /* 你 */
  TEST_ASSERT_EQ_INT(my_line_break_class(0xFF0Cu), MY_LB_NS); /* ， */
  TEST_ASSERT_EQ_INT(my_line_break_class(0x3002u), MY_LB_NS); /* 。 */
  TEST_ASSERT_EQ_INT(my_line_break_class(0x300Cu), MY_LB_OP); /* 「 */
  TEST_ASSERT_EQ_INT(my_line_break_class(0x0028u), MY_LB_OP); /* ( */
  TEST_ASSERT_EQ_INT(my_line_break_class(0x0029u), MY_LB_NS); /* ) */
}

static void test_cjk_no_start_punct(void) {
  /* "你好，世界啊" (6 cps) at 3 cells: overflow at 世 -> break AFTER the
   * comma (NS travels with the previous line, never starts one) */
  my_widget_t* w = wrap_area(32); /* inner 24px = 3 cells */
  my_text_area_set_text(w, "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C"
                           "\xE4\xB8\x96\xE7\x95\x8C\xE5\x95\x8A");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 2);
  expect_vline(w, 0, 0, 3); /* 你好， */
  expect_vline(w, 1, 3, 3); /* 世界啊 (starts with 世, not with ，) */
  my_widget_unref(w);
}

static void test_cjk_open_bracket_no_end(void) {
  /* "他说「你好」" (6 cps) at 3 cells: the 「 (OP) must not end a line;
   * the break falls back to before it */
  my_widget_t* w = wrap_area(32);
  my_text_area_set_text(w, "\xE4\xBB\x96\xE8\xAF\xB4\xE3\x80\x8C"
                           "\xE4\xBD\xA0\xE5\xA5\xBD\xE3\x80\x8D");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 3);
  expect_vline(w, 0, 0, 2); /* 他说 (does NOT end with 「) */
  expect_vline(w, 1, 2, 2); /* 「你 (「 may start a line) */
  expect_vline(w, 2, 4, 2); /* 好」 (」 NS stays line-final) */
  my_widget_unref(w);
}

static void test_hyphen_break(void) {
  my_widget_t* w = wrap_area(40); /* inner 32 = 4 cells */
  my_text_area_set_text(w, "abc-def");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 2);
  expect_vline(w, 0, 0, 4); /* abc- (break AFTER the hyphen) */
  expect_vline(w, 1, 4, 3); /* def */
  my_widget_unref(w);
}

static void test_word_run_no_break(void) {
  my_widget_t* w = wrap_area(40); /* 4 cells */
  my_text_area_set_text(w, "abcdef"); /* AL run: no legal break -> hard */
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 2);
  expect_vline(w, 0, 0, 4);
  expect_vline(w, 1, 4, 2);
  my_widget_unref(w);
}

static void test_mixed_cjk_latin(void) {
  /* CJK-Latin boundary is a legal break (ID adjacent to AL) */
  my_widget_t* w = wrap_area(32); /* 3 cells */
  my_text_area_set_text(w, "\xE4\xBD\xA0\xE5\xA5\xBD"
                           "abc"); /* 你好abc */
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 2);
  expect_vline(w, 0, 0, 2); /* 你好 (CJK|Latin boundary breaks) */
  expect_vline(w, 1, 2, 3); /* abc */
  my_widget_unref(w);
}

static void test_ascii_regression(void) {
  /* M10b behavior unchanged: "aaa bbb ccc" at 4 cells */
  my_widget_t* w = wrap_area(40);
  my_text_area_set_text(w, "aaa bbb ccc");
  TEST_ASSERT_EQ_INT(my_text_area_visual_line_count(w), 3);
  expect_vline(w, 0, 0, 4); /* "aaa " */
  expect_vline(w, 1, 4, 4); /* "bbb " */
  expect_vline(w, 2, 8, 3); /* "ccc" */
  my_widget_unref(w);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_class_lookup);
  MYTEST_RUN(test_cjk_no_start_punct);
  MYTEST_RUN(test_cjk_open_bracket_no_end);
  MYTEST_RUN(test_hyphen_break);
  MYTEST_RUN(test_word_run_no_break);
  MYTEST_RUN(test_mixed_cjk_latin);
  MYTEST_RUN(test_ascii_regression);
MYTEST_MAIN_END()
