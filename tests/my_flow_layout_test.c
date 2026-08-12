/**
 * @file my_flow_layout_test.c
 * @brief Flow layouter tests (M14a): wrap boundaries, row height = max,
 * CENTER alignment, empty container, measure.
 */
#include "myui/my_layout.h"

#include "mytest.h"

static my_widget_t* add_child(my_widget_t* parent, const char* params) {
  my_widget_t* c = my_widget_create(NULL, "item");
  my_widget_set_layout_params(c, params);
  my_widget_add_child(parent, c);
  my_widget_unref(c);
  return c;
}

static my_widget_t* make_flow(int32_t w, int32_t h, int32_t hs, int32_t vs,
                              my_flow_align_t align) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_set_rect(root, &(my_rect_t){0, 0, w, h});
  my_widget_set_layouter(root, my_layouter_flow_create(NULL, hs, vs, align));
  return root;
}

static void test_single_row_no_wrap(void) {
  my_widget_t* root = make_flow(300, 200, 10, 5, MY_FLOW_ALIGN_LEFT);
  my_widget_t* a = add_child(root, "w:100 h:20");
  my_widget_t* b = add_child(root, "w:50 h:30");
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(a->rect.x, 0);
  TEST_ASSERT_EQ_INT(a->rect.y, 0);
  TEST_ASSERT_EQ_INT(b->rect.x, 110); /* 100 + h_spacing */
  TEST_ASSERT_EQ_INT(b->rect.y, 0);
  /* row height = max(20,30) = 30 */
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(root), 30);
  my_widget_unref(root);
}

static void test_wrap_boundary_exact_fit(void) {
  /* 100 + 10 + 100 = 210 <= 210: both fit on row 0 (exact fit, no wrap);
   * a third of width 1 wraps */
  my_widget_t* root = make_flow(210, 200, 10, 5, MY_FLOW_ALIGN_LEFT);
  my_widget_t* b;
  my_widget_t* c;
  add_child(root, "w:100 h:20");
  b = add_child(root, "w:100 h:20");
  c = add_child(root, "w:1 h:8");
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(b->rect.y, 0);  /* exact fit stays */
  TEST_ASSERT_EQ_INT(c->rect.y, 25); /* wrapped: 20 + v_spacing */
  TEST_ASSERT_EQ_INT(c->rect.x, 0);
  my_widget_unref(root);
}

static void test_wrap_overflow_by_one_px(void) {
  /* 100 + 10 + 101 = 211 > 210: b wraps */
  my_widget_t* root = make_flow(210, 200, 10, 5, MY_FLOW_ALIGN_LEFT);
  my_widget_t* a = add_child(root, "w:100 h:20");
  my_widget_t* b = add_child(root, "w:101 h:30");
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(a->rect.y, 0);
  TEST_ASSERT_EQ_INT(b->rect.x, 0);
  TEST_ASSERT_EQ_INT(b->rect.y, 25);
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(root), 55); /* 20+5+30 */
  my_widget_unref(root);
}

static void test_row_height_takes_max(void) {
  my_widget_t* root = make_flow(100, 200, 0, 7, MY_FLOW_ALIGN_LEFT);
  my_widget_t* c;
  add_child(root, "w:40 h:10");
  add_child(root, "w:40 h:33");
  c = add_child(root, "w:40 h:12"); /* wraps (40+40+40>100) */
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(c->rect.y, 40); /* 33 + 7 */
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(root), 52); /* 33+7+12 */
  my_widget_unref(root);
}

static void test_align_center(void) {
  /* row: 50 + 10 + 50 = 110 in 210 -> offset (210-110)/2 = 50 */
  my_widget_t* root = make_flow(210, 200, 10, 5, MY_FLOW_ALIGN_CENTER);
  my_widget_t* a = add_child(root, "w:50 h:20");
  my_widget_t* b = add_child(root, "w:50 h:20");
  my_widget_t* c = add_child(root, "w:100 h:20"); /* wraps; (210-100)/2=55 */
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(a->rect.x, 50);
  TEST_ASSERT_EQ_INT(b->rect.x, 110); /* 50 + 50 + 10 */
  TEST_ASSERT_EQ_INT(c->rect.x, 55);
  TEST_ASSERT_EQ_INT(c->rect.y, 25);
  my_widget_unref(root);
}

static void test_percent_width(void) {
  my_widget_t* root = make_flow(200, 200, 0, 5, MY_FLOW_ALIGN_LEFT);
  my_widget_t* a = add_child(root, "w:50% h:20"); /* 100 */
  my_widget_t* b = add_child(root, "w:50% h:20"); /* exact fit */
  my_widget_t* c = add_child(root, "w:60% h:20"); /* 120 -> wraps */
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(a->rect.w, 100);
  TEST_ASSERT_EQ_INT(b->rect.x, 100);
  TEST_ASSERT_EQ_INT(b->rect.y, 0);
  TEST_ASSERT_EQ_INT(c->rect.y, 25);
  my_widget_unref(root);
}

static void test_empty_and_invisible(void) {
  my_widget_t* root = make_flow(200, 200, 10, 5, MY_FLOW_ALIGN_LEFT);
  my_widget_t* a;
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(root), 0); /* empty */
  a = add_child(root, "w:100 h:20");
  my_widget_set_visible(a, false);
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(root), 0); /* invisible skip */
  my_widget_unref(root);
}

static void test_measure_without_flow_layouter(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  add_child(root, "w:50 h:20");
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(root), 0); /* no flow */
  TEST_ASSERT_EQ_INT(my_layouter_flow_measure(NULL), 0);
  my_widget_unref(root);
}

static void test_wider_than_parent_stays(void) {
  /* a child wider than the parent opens its own row and overflows */
  my_widget_t* root = make_flow(100, 200, 10, 5, MY_FLOW_ALIGN_LEFT);
  my_widget_t* a = add_child(root, "w:150 h:20");
  my_widget_t* b = add_child(root, "w:50 h:20");
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(a->rect.x, 0);
  TEST_ASSERT_EQ_INT(a->rect.w, 150);
  TEST_ASSERT_EQ_INT(b->rect.y, 25); /* wraps after the wide one */
  my_widget_unref(root);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_single_row_no_wrap);
  MYTEST_RUN(test_wrap_boundary_exact_fit);
  MYTEST_RUN(test_wrap_overflow_by_one_px);
  MYTEST_RUN(test_row_height_takes_max);
  MYTEST_RUN(test_align_center);
  MYTEST_RUN(test_percent_width);
  MYTEST_RUN(test_empty_and_invisible);
  MYTEST_RUN(test_measure_without_flow_layouter);
  MYTEST_RUN(test_wider_than_parent_stays);
MYTEST_MAIN_END()
