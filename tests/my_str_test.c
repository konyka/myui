/**
 * @file my_str_test.c
 * @brief Unit tests for my_str (UTF-8 safe string utilities).
 */
#include "myc/my_str.h"

#include "mytest.h"

static void test_strdup(void) {
  char* p = my_strdup(NULL, "hello");
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQ_STR(p, "hello");
  my_mem_free(NULL, p);

  TEST_ASSERT_NULL(my_strdup(NULL, NULL));
}

static void test_strndup(void) {
  char* p = my_strndup(NULL, "hello", 3);
  TEST_ASSERT_EQ_STR(p, "hel");
  my_mem_free(NULL, p);

  p = my_strndup(NULL, "hi", 10); /* n beyond length */
  TEST_ASSERT_EQ_STR(p, "hi");
  my_mem_free(NULL, p);

  TEST_ASSERT_NULL(my_strndup(NULL, NULL, 3));
}

static void test_str_len(void) {
  TEST_ASSERT_EQ_INT(my_str_len("abc"), 3);
  TEST_ASSERT_EQ_INT(my_str_len(""), 0);
  TEST_ASSERT_EQ_INT(my_str_len(NULL), 0);
}

static void test_str_eq(void) {
  TEST_ASSERT(my_str_eq("a", "a"));
  TEST_ASSERT(!my_str_eq("a", "b"));
  TEST_ASSERT(!my_str_eq("ab", "a"));
  TEST_ASSERT(my_str_eq(NULL, NULL));
  TEST_ASSERT(!my_str_eq(NULL, "a"));
  TEST_ASSERT(!my_str_eq("a", NULL));
}

static void test_start_end_with(void) {
  TEST_ASSERT(my_str_start_with("hello world", "hello"));
  TEST_ASSERT(!my_str_start_with("hello world", "world"));
  TEST_ASSERT(my_str_start_with("hello", ""));
  TEST_ASSERT(!my_str_start_with("hi", "hello"));
  TEST_ASSERT(!my_str_start_with(NULL, "a"));
  TEST_ASSERT(!my_str_start_with("a", NULL));

  TEST_ASSERT(my_str_end_with("hello world", "world"));
  TEST_ASSERT(!my_str_end_with("hello world", "hello"));
  TEST_ASSERT(my_str_end_with("hello", ""));
  TEST_ASSERT(!my_str_end_with("hi", "hello"));
  TEST_ASSERT(!my_str_end_with(NULL, "a"));
  TEST_ASSERT(!my_str_end_with("a", NULL));
}

static void test_utf8_char_len(void) {
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len("a"), 1);
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len("\xC3\xA9"), 2);          /* é */
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len("\xE4\xB8\xAD"), 3);      /* 中 */
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len("\xF0\x9F\x98\x80"), 4);  /* emoji */
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len("\xFF"), 1);              /* invalid: skip 1 */
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len(""), 0);
  TEST_ASSERT_EQ_INT(my_str_utf8_char_len(NULL), 0);
}

static void test_utf8_strlen(void) {
  TEST_ASSERT_EQ_INT(my_str_utf8_strlen("abc"), 3);
  TEST_ASSERT_EQ_INT(my_str_utf8_strlen("a\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80"), 4);
  TEST_ASSERT_EQ_INT(my_str_utf8_strlen(""), 0);
  TEST_ASSERT_EQ_INT(my_str_utf8_strlen(NULL), 0);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  char* a = my_strdup(dbg, "x");
  char* b = my_strndup(dbg, "xyz", 2);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 2);
  my_mem_free(dbg, a);
  my_mem_free(dbg, b);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_strdup);
  MYTEST_RUN(test_strndup);
  MYTEST_RUN(test_str_len);
  MYTEST_RUN(test_str_eq);
  MYTEST_RUN(test_start_end_with);
  MYTEST_RUN(test_utf8_char_len);
  MYTEST_RUN(test_utf8_strlen);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
