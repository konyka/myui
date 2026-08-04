/**
 * @file my_color_test.c
 * @brief Unit tests for my_color helpers.
 */
#include "myr/my_color.h"

#include "mytest.h"

static void test_construct_and_pack(void) {
  my_color_t c = my_color_rgba(1, 2, 3, 4);
  TEST_ASSERT_EQ_INT(c.r, 1);
  TEST_ASSERT_EQ_INT(c.g, 2);
  TEST_ASSERT_EQ_INT(c.b, 3);
  TEST_ASSERT_EQ_INT(c.a, 4);

  c = my_color_rgb(10, 20, 30);
  TEST_ASSERT_EQ_INT(c.a, 255);

  TEST_ASSERT(my_color_to_rgba32(my_color_rgba(0x12, 0x34, 0x56, 0x78)) ==
              0x12345678u);
  c = my_color_from_rgba32(0x12345678u);
  TEST_ASSERT_EQ_INT(c.r, 0x12);
  TEST_ASSERT_EQ_INT(c.g, 0x34);
  TEST_ASSERT_EQ_INT(c.b, 0x56);
  TEST_ASSERT_EQ_INT(c.a, 0x78);
}

static void test_eq(void) {
  TEST_ASSERT(my_color_eq(my_color_rgb(1, 2, 3), my_color_rgb(1, 2, 3)));
  TEST_ASSERT(!my_color_eq(my_color_rgb(1, 2, 3), my_color_rgb(1, 2, 4)));
  TEST_ASSERT(!my_color_eq(my_color_rgb(1, 2, 3), my_color_rgba(1, 2, 3, 0)));
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_construct_and_pack);
  MYTEST_RUN(test_eq);
MYTEST_MAIN_END()
