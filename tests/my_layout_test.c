/**
 * @file my_layout_test.c
 * @brief Unit tests for layout params parser and linear layouter.
 */
#include "myui/my_layout.h"

#include "mytest.h"

/* ---------------- parser ---------------- */

static void test_parse_px_percent_flex(void) {
  my_layout_params_t p;
  TEST_ASSERT_EQ_INT(my_layout_params_parse("w:100 h:30", &p), MY_RET_OK);
  TEST_ASSERT_EQ_INT(p.w_mode, MY_LAYOUT_PX);
  TEST_ASSERT(p.w_value == 100.0f);
  TEST_ASSERT_EQ_INT(p.h_mode, MY_LAYOUT_PX);
  TEST_ASSERT(p.h_value == 30.0f);

  my_layout_params_parse("w:50%", &p);
  TEST_ASSERT_EQ_INT(p.w_mode, MY_LAYOUT_PERCENT);
  TEST_ASSERT(p.w_value == 50.0f);
  TEST_ASSERT_EQ_INT(p.h_mode, MY_LAYOUT_AUTO); /* missing axis */

  my_layout_params_parse("h:2f", &p);
  TEST_ASSERT_EQ_INT(p.h_mode, MY_LAYOUT_FLEX);
  TEST_ASSERT(p.h_value == 2.0f);

  my_layout_params_parse(NULL, &p);
  TEST_ASSERT_EQ_INT(p.w_mode, MY_LAYOUT_AUTO);
  my_layout_params_parse("", &p);
  TEST_ASSERT_EQ_INT(p.w_mode, MY_LAYOUT_AUTO);
}

static void test_parse_invalid(void) {
  my_layout_params_t p;
  TEST_ASSERT_EQ_INT(my_layout_params_parse("w:", &p), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_layout_params_parse("w:abc", &p), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_layout_params_parse("x:100", &p), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_layout_params_parse("w:10%f", &p), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_layout_params_parse(NULL, NULL), MY_RET_INVALID_PARAMS);
}

/* ---------------- default layouter ---------------- */

static void test_default_layouter_noop(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* c = my_widget_create(NULL, "c");
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  my_widget_set_rect(c, &(my_rect_t){7, 8, 9, 10});
  my_widget_add_child(root, c);
  my_widget_unref(c);

  my_widget_set_layouter(root, my_layouter_default());
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(c->rect.x, 7); /* untouched */
  TEST_ASSERT_EQ_INT(c->rect.w, 9);

  my_widget_unref(root);
}

/* ---------------- linear layouter ---------------- */

static my_widget_t* child_with(my_widget_t* parent, const char* name,
                               const char* params) {
  my_widget_t* c = my_widget_create(NULL, name);
  my_widget_set_layout_params(c, params);
  my_widget_add_child(parent, c);
  my_widget_unref(c);
  return c;
}

static void test_linear_horizontal_px_and_spacing(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t *a, *b;
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 40});
  my_widget_set_layouter(root, my_layouter_linear_create(NULL, true, 5));

  a = child_with(root, "a", "w:20 h:10");
  b = child_with(root, "b", "w:30 h:10");
  my_widget_relayout(root);

  TEST_ASSERT_EQ_INT(a->rect.x, 0);
  TEST_ASSERT_EQ_INT(a->rect.w, 20);
  TEST_ASSERT_EQ_INT(a->rect.h, 10);
  TEST_ASSERT_EQ_INT(b->rect.x, 25); /* 20 + spacing 5 */
  TEST_ASSERT_EQ_INT(b->rect.w, 30);

  my_widget_unref(root);
}

static void test_linear_vertical_percent(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t *a, *b;
  my_widget_set_rect(root, &(my_rect_t){0, 0, 50, 200});
  my_widget_set_layouter(root, my_layouter_linear_create(NULL, false, 0));

  a = child_with(root, "a", "h:25%");
  b = child_with(root, "b", "h:50%");
  my_widget_relayout(root);

  TEST_ASSERT_EQ_INT(a->rect.y, 0);
  TEST_ASSERT_EQ_INT(a->rect.h, 50);  /* 25% of 200 */
  TEST_ASSERT_EQ_INT(a->rect.w, 50);  /* cross axis AUTO fills the parent */
  TEST_ASSERT_EQ_INT(b->rect.y, 50);
  TEST_ASSERT_EQ_INT(b->rect.h, 100); /* 50% of 200 */

  my_widget_unref(root);
}

static void test_linear_flex_shares_remaining(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t *a, *b, *c;
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 20});
  my_widget_set_layouter(root, my_layouter_linear_create(NULL, true, 0));

  a = child_with(root, "a", "w:20");   /* fixed 20 */
  b = child_with(root, "b", "w:1f");   /* flex 1 */
  c = child_with(root, "c", "w:3f");   /* flex 3 */
  my_widget_relayout(root);

  /* remaining = 100 - 20 = 80; b gets 20, c gets 60 */
  TEST_ASSERT_EQ_INT(a->rect.w, 20);
  TEST_ASSERT_EQ_INT(b->rect.x, 20);
  TEST_ASSERT_EQ_INT(b->rect.w, 20);
  TEST_ASSERT_EQ_INT(c->rect.x, 40);
  TEST_ASSERT_EQ_INT(c->rect.w, 60);

  my_widget_unref(root);
}

static void test_linear_skips_invisible_and_cross_fill(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t *a, *b;
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 40});
  my_widget_set_layouter(root, my_layouter_linear_create(NULL, true, 0));

  a = child_with(root, "a", "w:50");
  b = child_with(root, "b", "w:50");
  my_widget_set_visible(a, false);
  my_widget_relayout(root);

  TEST_ASSERT_EQ_INT(b->rect.x, 0); /* a invisible: b starts at 0 */

  my_widget_unref(root);
}

static void test_auto_layout_flag(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* c = my_widget_create(NULL, "c");
  TEST_ASSERT(root->need_layout == false);
  my_widget_add_child(root, c); /* marks need_layout */
  TEST_ASSERT(root->need_layout);
  my_widget_unref(c);
  my_widget_relayout(root);
  TEST_ASSERT(!root->need_layout);
  my_widget_unref(root);
}

static void test_null_params(void) {
  my_widget_t* w = my_widget_create(NULL, "w");
  TEST_ASSERT_EQ_INT(my_widget_set_layout_params(NULL, "w:1"), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_widget_set_layouter(NULL, NULL), MY_RET_INVALID_PARAMS);
  my_widget_set_layouter(w, NULL); /* allowed: resets to absolute */
  my_widget_relayout(NULL);        /* must be safe */
  my_widget_unref(w);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* root = my_widget_create(dbg, "root");
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  my_widget_set_layouter(root, my_layouter_linear_create(dbg, true, 4));
  {
    my_widget_t* a = my_widget_create(dbg, "a");
    my_widget_set_layout_params(a, "w:1f h:50%");
    my_widget_add_child(root, a);
    my_widget_unref(a);
  }
  my_widget_relayout(root);
  my_widget_unref(root);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_parse_px_percent_flex);
  MYTEST_RUN(test_parse_invalid);
  MYTEST_RUN(test_default_layouter_noop);
  MYTEST_RUN(test_linear_horizontal_px_and_spacing);
  MYTEST_RUN(test_linear_vertical_percent);
  MYTEST_RUN(test_linear_flex_shares_remaining);
  MYTEST_RUN(test_linear_skips_invisible_and_cross_fill);
  MYTEST_RUN(test_auto_layout_flag);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
