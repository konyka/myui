/**
 * @file my_rect_test.c
 * @brief Unit tests for my_rect helpers.
 */
#include "myr/my_rect.h"

#include "mytest.h"

static void test_is_empty_contains(void) {
  my_rect_t r = my_rect_init(10, 20, 30, 40);
  TEST_ASSERT(!my_rect_is_empty(&r));
  TEST_ASSERT(my_rect_is_empty(NULL));
  TEST_ASSERT(my_rect_is_empty(&(my_rect_t){0, 0, 0, 10}));
  TEST_ASSERT(my_rect_is_empty(&(my_rect_t){0, 0, 10, -1}));

  TEST_ASSERT(my_rect_contains(&r, 10, 20));
  TEST_ASSERT(my_rect_contains(&r, 39, 59));
  TEST_ASSERT(!my_rect_contains(&r, 40, 20)); /* half-open */
  TEST_ASSERT(!my_rect_contains(&r, 9, 20));
}

static void test_intersect(void) {
  my_rect_t a = my_rect_init(0, 0, 10, 10);
  my_rect_t b = my_rect_init(5, 5, 10, 10);
  my_rect_t out;

  TEST_ASSERT(my_rect_intersect(&a, &b, &out));
  TEST_ASSERT_EQ_INT(out.x, 5);
  TEST_ASSERT_EQ_INT(out.y, 5);
  TEST_ASSERT_EQ_INT(out.w, 5);
  TEST_ASSERT_EQ_INT(out.h, 5);

  /* touching edges = empty intersection */
  TEST_ASSERT(!my_rect_intersect(&a, &(my_rect_t){10, 0, 5, 5}, NULL));
  /* disjoint */
  TEST_ASSERT(!my_rect_intersect(&a, &(my_rect_t){20, 20, 5, 5}, &out));
  /* empty input */
  TEST_ASSERT(!my_rect_intersect(&a, &(my_rect_t){0, 0, 0, 0}, NULL));
  TEST_ASSERT(!my_rect_intersect(NULL, &a, NULL));
}

static void test_union(void) {
  my_rect_t a = my_rect_init(0, 0, 10, 10);
  my_rect_t b = my_rect_init(5, 8, 10, 10);
  my_rect_t out;

  my_rect_union(&a, &b, &out);
  TEST_ASSERT_EQ_INT(out.x, 0);
  TEST_ASSERT_EQ_INT(out.y, 0);
  TEST_ASSERT_EQ_INT(out.w, 15);
  TEST_ASSERT_EQ_INT(out.h, 18);

  my_rect_union(&a, &(my_rect_t){0, 0, 0, 0}, &out);
  TEST_ASSERT_EQ_INT(out.w, 10);
  TEST_ASSERT_EQ_INT(out.h, 10);
}

static void test_rectf_init(void) {
  my_rectf_t r = my_rectf_init(1.5f, 2.5f, 3.0f, 4.0f);
  TEST_ASSERT(r.x == 1.5f && r.y == 2.5f && r.w == 3.0f && r.h == 4.0f);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_is_empty_contains);
  MYTEST_RUN(test_intersect);
  MYTEST_RUN(test_union);
  MYTEST_RUN(test_rectf_init);
MYTEST_MAIN_END()
