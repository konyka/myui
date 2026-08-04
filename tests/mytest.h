/**
 * @file mytest.h
 * @brief Minimal zero-dependency unit test framework for myui.
 *
 * One translation unit = one test executable. Usage:
 *
 *   #include "mytest.h"
 *
 *   static void test_add(void) {
 *     TEST_ASSERT_EQ_INT(1 + 1, 2);
 *   }
 *
 *   MYTEST_MAIN_BEGIN()
 *     MYTEST_RUN(test_add);
 *   MYTEST_MAIN_END()
 *
 * Failures print file/line/expression to stderr; the process exit code is
 * the number of failed assertions (0 = success, usable directly by ctest).
 */
#ifndef MYTEST_H
#define MYTEST_H

#include <stdio.h>
#include <string.h>

static int mytest_assert_total = 0;
static int mytest_assert_failed = 0;
static int mytest_case_failed = 0;

static inline void mytest_report(const char* file, int line, const char* expr) {
  mytest_assert_failed++;
  fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
}

static inline int mytest_str_eq(const char* a, const char* b) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  return strcmp(a, b) == 0;
}

/** @brief Assert that cond is true. */
#define TEST_ASSERT(cond)                          \
  do {                                             \
    mytest_assert_total++;                         \
    if (!(cond)) {                                 \
      mytest_report(__FILE__, __LINE__, #cond);    \
    }                                              \
  } while (0)

/** @brief Assert that two integers are equal (compared as long long). */
#define TEST_ASSERT_EQ_INT(actual, expected)                                \
  do {                                                                      \
    long long mytest_a_ = (long long)(actual);                              \
    long long mytest_e_ = (long long)(expected);                            \
    mytest_assert_total++;                                                  \
    if (mytest_a_ != mytest_e_) {                                           \
      mytest_assert_failed++;                                               \
      fprintf(stderr, "FAIL %s:%d: %s == %s (actual %lld, expected %lld)\n", \
              __FILE__, __LINE__, #actual, #expected, mytest_a_, mytest_e_); \
    }                                                                       \
  } while (0)

/** @brief Assert that two C strings are equal (NULL-safe). */
#define TEST_ASSERT_EQ_STR(actual, expected)                               \
  do {                                                                     \
    const char* mytest_a_ = (actual);                                      \
    const char* mytest_e_ = (expected);                                    \
    mytest_assert_total++;                                                 \
    if (!mytest_str_eq(mytest_a_, mytest_e_)) {                            \
      mytest_assert_failed++;                                              \
      fprintf(stderr, "FAIL %s:%d: %s == %s (actual \"%s\", expected \"%s\")\n", \
              __FILE__, __LINE__, #actual, #expected,                      \
              mytest_a_ != NULL ? mytest_a_ : "(null)",                    \
              mytest_e_ != NULL ? mytest_e_ : "(null)");                   \
    }                                                                      \
  } while (0)

/** @brief Assert that a pointer is NULL. */
#define TEST_ASSERT_NULL(p)                            \
  do {                                                 \
    mytest_assert_total++;                             \
    if ((p) != NULL) {                                 \
      mytest_report(__FILE__, __LINE__, #p " == NULL"); \
    }                                                  \
  } while (0)

/** @brief Assert that a pointer is not NULL. */
#define TEST_ASSERT_NOT_NULL(p)                        \
  do {                                                 \
    mytest_assert_total++;                             \
    if ((p) == NULL) {                                 \
      mytest_report(__FILE__, __LINE__, #p " != NULL"); \
    }                                                  \
  } while (0)

/** @brief Run one test case function, reporting per-case status. */
#define MYTEST_RUN(fn)                                   \
  do {                                                   \
    int mytest_before_ = mytest_assert_failed;           \
    fprintf(stdout, "[ RUN  ] %s\n", #fn);               \
    fn();                                                \
    if (mytest_assert_failed == mytest_before_) {        \
      fprintf(stdout, "[  OK  ] %s\n", #fn);             \
    } else {                                             \
      mytest_case_failed++;                              \
      fprintf(stdout, "[ FAIL ] %s\n", #fn);             \
    }                                                    \
  } while (0)

/** @brief Open the test main body; register cases with MYTEST_RUN inside. */
#define MYTEST_MAIN_BEGIN() int main(void) {

/** @brief Close the test main body; exit code = failed assertion count. */
#define MYTEST_MAIN_END()                                          \
  fprintf(stdout, "[ DONE ] %d assertions, %d failed (%d cases)\n", \
          mytest_assert_total, mytest_assert_failed,               \
          mytest_case_failed);                                     \
  return mytest_assert_failed;                                     \
  }

#endif /* MYTEST_H */
