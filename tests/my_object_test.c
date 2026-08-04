/**
 * @file my_object_test.c
 * @brief Unit tests for my_object (reference counting base class).
 */
#include "myc/my_object.h"

#include "mytest.h"

static void test_create_ref_unref(void) {
  my_object_t* obj = my_object_create(NULL, "button");
  TEST_ASSERT_NOT_NULL(obj);
  TEST_ASSERT_EQ_INT(obj->ref_count, 1);
  TEST_ASSERT_EQ_STR(obj->name, "button");

  TEST_ASSERT(my_object_ref(obj) == obj);
  TEST_ASSERT_EQ_INT(obj->ref_count, 2);

  my_object_unref(obj);
  TEST_ASSERT_EQ_INT(obj->ref_count, 1);

  my_object_unref(obj); /* reaches 0 -> default destroy */
}

static void test_null_name_allowed(void) {
  my_object_t* obj = my_object_create(NULL, NULL);
  TEST_ASSERT_NOT_NULL(obj);
  TEST_ASSERT_NULL(obj->name);
  my_object_unref(obj);
}

static void test_unref_null_is_safe(void) {
  my_object_unref(NULL);
  TEST_ASSERT(my_object_ref(NULL) == NULL);
}

/* --- subclass pattern: custom destroy chaining to my_object_destroy --- */

static int g_destroy_calls = 0;

static void fake_widget_destroy(my_object_t* obj) {
  g_destroy_calls++;
  my_object_destroy(obj); /* frees base name copy + struct */
}

static void test_subclass_destroy_called_once(void) {
  my_object_t* obj = my_object_create(NULL, "widget");
  g_destroy_calls = 0;
  obj->destroy = fake_widget_destroy; /* subclass installs its destructor */

  my_object_ref(obj);
  my_object_unref(obj);
  TEST_ASSERT_EQ_INT(g_destroy_calls, 0);

  my_object_unref(obj);
  TEST_ASSERT_EQ_INT(g_destroy_calls, 1);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_object_t* obj = my_object_create(dbg, "tracked");

  g_destroy_calls = 0;
  obj->destroy = fake_widget_destroy;

  my_object_ref(obj);
  my_object_unref(obj);
  my_object_unref(obj);

  TEST_ASSERT_EQ_INT(g_destroy_calls, 1);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_create_ref_unref);
  MYTEST_RUN(test_null_name_allowed);
  MYTEST_RUN(test_unref_null_is_safe);
  MYTEST_RUN(test_subclass_destroy_called_once);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
