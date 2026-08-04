/**
 * @file my_dirty_rects_test.c
 * @brief Unit tests for my_dirty_rects merge policy.
 */
#include "myr/my_dirty_rects.h"

#include "mytest.h"

static void test_add_disjoint(void) {
  my_dirty_rects_t dr;
  my_dirty_rects_init(&dr);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 0);

  my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 10, 10});
  my_dirty_rects_add(&dr, &(my_rect_t){100, 100, 5, 5});
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 2);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->x, 0);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 1)->x, 100);
}

static void test_merge_overlapping(void) {
  my_dirty_rects_t dr;
  const my_rect_t* r;
  my_dirty_rects_init(&dr);

  my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 10, 10});
  my_dirty_rects_add(&dr, &(my_rect_t){5, 5, 10, 10});
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 1);
  r = my_dirty_rects_get(&dr, 0);
  TEST_ASSERT_EQ_INT(r->x, 0);
  TEST_ASSERT_EQ_INT(r->y, 0);
  TEST_ASSERT_EQ_INT(r->w, 15);
  TEST_ASSERT_EQ_INT(r->h, 15);
}

static void test_merge_touching_edges(void) {
  my_dirty_rects_t dr;
  my_dirty_rects_init(&dr);
  /* [0,10) and [10,20) touch -> merged */
  my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 10, 10});
  my_dirty_rects_add(&dr, &(my_rect_t){10, 0, 10, 10});
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 1);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->w, 20);
}

static void test_merge_cascades(void) {
  my_dirty_rects_t dr;
  my_dirty_rects_init(&dr);
  my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 10, 10});
  my_dirty_rects_add(&dr, &(my_rect_t){30, 0, 10, 10});
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 2);
  /* bridges both -> cascades into one */
  my_dirty_rects_add(&dr, &(my_rect_t){5, 0, 30, 10});
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 1);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->w, 40);
}

static void test_full_collapses_to_bbox(void) {
  my_dirty_rects_t dr;
  int i;
  my_dirty_rects_init(&dr);
  /* fill all slots with disjoint rects */
  for (i = 0; i < MY_DIRTY_RECTS_MAX; i++) {
    my_dirty_rects_add(&dr, &(my_rect_t){i * 100, 0, 10, 10});
  }
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), MY_DIRTY_RECTS_MAX);

  /* one more disjoint rect -> collapse to a single bounding box */
  my_dirty_rects_add(&dr, &(my_rect_t){0, 500, 10, 10});
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 1);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->x, 0);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->y, 0);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->w,
                     (MY_DIRTY_RECTS_MAX - 1) * 100 + 10);
  TEST_ASSERT_EQ_INT(my_dirty_rects_get(&dr, 0)->h, 510);
}

static void test_empty_rect_ignored(void) {
  my_dirty_rects_t dr;
  my_dirty_rects_init(&dr);
  TEST_ASSERT_EQ_INT(my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 0, 5}),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 5, -1}),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 0);
}

static void test_clear_and_null(void) {
  my_dirty_rects_t dr;
  my_dirty_rects_init(&dr);
  my_dirty_rects_add(&dr, &(my_rect_t){0, 0, 10, 10});
  my_dirty_rects_clear(&dr);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&dr), 0);
  TEST_ASSERT_NULL(my_dirty_rects_get(&dr, 0));

  TEST_ASSERT_EQ_INT(my_dirty_rects_add(NULL, &(my_rect_t){0, 0, 1, 1}),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_dirty_rects_add(&dr, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(NULL), 0);
  TEST_ASSERT_NULL(my_dirty_rects_get(NULL, 0));
  my_dirty_rects_clear(NULL); /* must be safe */
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_add_disjoint);
  MYTEST_RUN(test_merge_overlapping);
  MYTEST_RUN(test_merge_touching_edges);
  MYTEST_RUN(test_merge_cascades);
  MYTEST_RUN(test_full_collapses_to_bbox);
  MYTEST_RUN(test_empty_rect_ignored);
  MYTEST_RUN(test_clear_and_null);
MYTEST_MAIN_END()
