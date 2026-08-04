/**
 * @file my_mem_test.c
 * @brief Unit tests for my_mem (default + debug allocators).
 */
#include "myc/my_mem.h"

#include <string.h>

#include "mytest.h"

static void test_default_alloc_free(void) {
  const my_allocator_t* a = my_allocator_default();
  char* p = (char*)my_mem_alloc(a, 16);
  TEST_ASSERT_NOT_NULL(p);
  strcpy(p, "hello");
  TEST_ASSERT_EQ_STR(p, "hello");
  my_mem_free(a, p);
}

static void test_null_allocator_uses_default(void) {
  char* p = (char*)my_mem_alloc(NULL, 8);
  TEST_ASSERT_NOT_NULL(p);
  my_mem_free(NULL, p);
}

static void test_calloc_zeroes(void) {
  const my_allocator_t* a = my_allocator_default();
  int* p = (int*)my_mem_calloc(a, 4, sizeof(int));
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQ_INT(p[0], 0);
  TEST_ASSERT_EQ_INT(p[3], 0);
  my_mem_free(a, p);
}

static void test_realloc_preserves_content(void) {
  const my_allocator_t* a = my_allocator_default();
  char* p = (char*)my_mem_alloc(a, 8);
  TEST_ASSERT_NOT_NULL(p);
  strcpy(p, "abc");
  p = (char*)my_mem_realloc(a, p, 64);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQ_STR(p, "abc");
  my_mem_free(a, p);
}

static void test_free_null_is_noop(void) {
  my_mem_free(NULL, NULL);
  TEST_ASSERT(1);
}

static void test_debug_allocator_counts_leaks(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  TEST_ASSERT_NOT_NULL(dbg);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);

  {
    void* p1 = my_mem_alloc(dbg, 8);
    void* p2 = my_mem_calloc(dbg, 2, 8);
    TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 2);

    my_mem_free(dbg, p1);
    TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 1);

    p2 = my_mem_realloc(dbg, p2, 32); /* realloc keeps the count */
    TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 1);

    my_mem_free(dbg, NULL); /* NULL free must not corrupt the count */
    TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 1);

    my_mem_free(dbg, p2);
    TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  }

  my_allocator_debug_destroy(dbg);
}

static void test_debug_allocator_realloc_null_allocates(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  void* p = my_mem_realloc(dbg, NULL, 16);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 1);
  my_mem_free(dbg, p);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_default_alloc_free);
  MYTEST_RUN(test_null_allocator_uses_default);
  MYTEST_RUN(test_calloc_zeroes);
  MYTEST_RUN(test_realloc_preserves_content);
  MYTEST_RUN(test_free_null_is_noop);
  MYTEST_RUN(test_debug_allocator_counts_leaks);
  MYTEST_RUN(test_debug_allocator_realloc_null_allocates);
MYTEST_MAIN_END()
