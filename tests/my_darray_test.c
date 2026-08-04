/**
 * @file my_darray_test.c
 * @brief Unit tests for my_darray.
 */
#include "myc/my_darray.h"

#include "mytest.h"

static void test_push_get_size(void) {
  my_darray_t* arr = my_darray_create(NULL, 0);
  int a = 1, b = 2;
  TEST_ASSERT_NOT_NULL(arr);
  TEST_ASSERT_EQ_INT(my_darray_size(arr), 0);

  TEST_ASSERT_EQ_INT(my_darray_push(arr, &a), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_darray_push(arr, &b), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_darray_size(arr), 2);
  TEST_ASSERT(my_darray_get(arr, 0) == &a);
  TEST_ASSERT(my_darray_get(arr, 1) == &b);

  my_darray_destroy(arr);
}

static void test_growth(void) {
  my_darray_t* arr = my_darray_create(NULL, 2);
  int i;
  TEST_ASSERT_NOT_NULL(arr);
  for (i = 0; i < 100; i++) {
    TEST_ASSERT_EQ_INT(my_darray_push(arr, (void*)(size_t)i), MY_RET_OK);
  }
  TEST_ASSERT_EQ_INT(my_darray_size(arr), 100);
  for (i = 0; i < 100; i++) {
    TEST_ASSERT_EQ_INT((size_t)my_darray_get(arr, (size_t)i), (size_t)i);
  }
  my_darray_destroy(arr);
}

static void test_remove_at_shifts(void) {
  my_darray_t* arr = my_darray_create(NULL, 0);
  int a = 1, b = 2, c = 3;
  my_darray_push(arr, &a);
  my_darray_push(arr, &b);
  my_darray_push(arr, &c);

  TEST_ASSERT_EQ_INT(my_darray_remove_at(arr, 1), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_darray_size(arr), 2);
  TEST_ASSERT(my_darray_get(arr, 0) == &a);
  TEST_ASSERT(my_darray_get(arr, 1) == &c);

  TEST_ASSERT_EQ_INT(my_darray_remove_at(arr, 5), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_darray_size(arr), 2);

  my_darray_destroy(arr);
}

static void test_get_out_of_range(void) {
  my_darray_t* arr = my_darray_create(NULL, 0);
  TEST_ASSERT_NULL(my_darray_get(arr, 0));
  TEST_ASSERT_NULL(my_darray_get(NULL, 0));
  my_darray_destroy(arr);
}

static void test_clear(void) {
  my_darray_t* arr = my_darray_create(NULL, 0);
  int a = 1;
  my_darray_push(arr, &a);
  TEST_ASSERT_EQ_INT(my_darray_clear(arr), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_darray_size(arr), 0);
  TEST_ASSERT_NULL(my_darray_get(arr, 0));
  my_darray_destroy(arr);
}

static void test_null_params(void) {
  TEST_ASSERT_EQ_INT(my_darray_push(NULL, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_darray_remove_at(NULL, 0), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_darray_clear(NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_darray_size(NULL), 0);
  my_darray_destroy(NULL); /* must be safe */
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_darray_t* arr = my_darray_create(dbg, 1);
  int i;
  for (i = 0; i < 50; i++) {
    my_darray_push(arr, (void*)(size_t)i);
  }
  my_darray_clear(arr);
  my_darray_push(arr, NULL);
  my_darray_remove_at(arr, 0);
  my_darray_destroy(arr);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_push_get_size);
  MYTEST_RUN(test_growth);
  MYTEST_RUN(test_remove_at_shifts);
  MYTEST_RUN(test_get_out_of_range);
  MYTEST_RUN(test_clear);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
