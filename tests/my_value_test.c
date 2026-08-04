/**
 * @file my_value_test.c
 * @brief Unit tests for my_value.
 */
#include "myc/my_value.h"

#include <string.h>

#include "mytest.h"

static void test_init_is_none(void) {
  my_value_t v;
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_NONE);
  TEST_ASSERT_NULL(my_value_get_str(&v));
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 0);
  my_value_reset(&v);
}

static void test_scalar_roundtrip(void) {
  my_value_t v;
  my_value_init(&v, NULL);

  my_value_set_bool(&v, true);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_BOOL);
  TEST_ASSERT(my_value_get_bool(&v));

  my_value_set_int32(&v, -123);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_INT32);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), -123);

  my_value_set_uint32(&v, 4000000000u);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_UINT32);
  TEST_ASSERT(my_value_get_uint32(&v) == 4000000000u);

  my_value_set_int64(&v, -9000000000LL);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_INT64);
  TEST_ASSERT(my_value_get_int64(&v) == -9000000000LL);

  my_value_set_float(&v, 1.5f);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_FLOAT);
  TEST_ASSERT(my_value_get_float(&v) == 1.5f);

  my_value_set_double(&v, 2.25);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_DOUBLE);
  TEST_ASSERT(my_value_get_double(&v) == 2.25);

  my_value_reset(&v);
}

static void test_getter_type_mismatch_returns_zero(void) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_int32(&v, 7);
  TEST_ASSERT(!my_value_get_bool(&v));
  TEST_ASSERT(my_value_get_double(&v) == 0.0);
  TEST_ASSERT_NULL(my_value_get_str(&v));
  TEST_ASSERT_NULL(my_value_get_pointer(&v));
  my_value_reset(&v);
}

static void test_str_is_deep_copied(void) {
  my_value_t v;
  char buf[16];
  const char* stored;
  strcpy(buf, "hello");

  my_value_init(&v, NULL);
  my_value_set_str(&v, buf);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_STR);

  buf[0] = 'X'; /* mutate the source; the value must not change */
  stored = my_value_get_str(&v);
  TEST_ASSERT_EQ_STR(stored, "hello");
  TEST_ASSERT(stored != buf);

  my_value_set_str(&v, NULL); /* resets to NONE */
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_NONE);
  my_value_reset(&v);
}

static void test_str_overwrite_frees_old(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_value_t v;
  my_value_init(&v, dbg);

  my_value_set_str(&v, "first");
  my_value_set_str(&v, "second");
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "second");
  my_value_reset(&v);

  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

static void test_pointer_is_borrowed(void) {
  my_value_t v;
  int x = 5;
  my_value_init(&v, NULL);
  my_value_set_pointer(&v, &x);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_POINTER);
  TEST_ASSERT(my_value_get_pointer(&v) == &x);
  my_value_reset(&v); /* must not free &x */
  TEST_ASSERT_EQ_INT(x, 5);
}

static void test_copy_deep_copies_str(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_value_t src, dst;

  my_value_init(&src, dbg);
  my_value_init(&dst, dbg);

  my_value_set_str(&src, "payload");
  TEST_ASSERT_EQ_INT(my_value_copy(&dst, &src), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_type(&dst), MY_VALUE_STR);
  TEST_ASSERT_EQ_STR(my_value_get_str(&dst), "payload");
  TEST_ASSERT(my_value_get_str(&dst) != my_value_get_str(&src));

  my_value_set_int32(&src, 9);
  TEST_ASSERT_EQ_INT(my_value_copy(&dst, &src), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_type(&dst), MY_VALUE_INT32);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&dst), 9);

  my_value_reset(&src);
  my_value_reset(&dst);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

static void test_null_params(void) {
  my_value_t v;
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_value_set_int32(NULL, 1), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_value_set_str(NULL, "x"), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_value_copy(NULL, &v), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_value_copy(&v, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_value_type(NULL), MY_VALUE_NONE);
  my_value_reset(NULL); /* must be safe */
  my_value_reset(&v);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_init_is_none);
  MYTEST_RUN(test_scalar_roundtrip);
  MYTEST_RUN(test_getter_type_mismatch_returns_zero);
  MYTEST_RUN(test_str_is_deep_copied);
  MYTEST_RUN(test_str_overwrite_frees_old);
  MYTEST_RUN(test_pointer_is_borrowed);
  MYTEST_RUN(test_copy_deep_copies_str);
  MYTEST_RUN(test_null_params);
MYTEST_MAIN_END()
