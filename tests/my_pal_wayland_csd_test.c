/**
 * @file my_pal_wayland_csd_test.c
 * @brief Rounded-corner mask tests (M16): corner pixels punched,
 * interior untouched, degenerate inputs safe.
 */
#include "mypal/wayland/my_pal_wayland_csd.h"

#include <string.h>

#include "mytest.h"

#define TW 12
#define TH 12

static void fill_opaque(uint8_t* buf) {
  memset(buf, 0xFF, TW * TH * 4);
}

static uint8_t alpha_at(const uint8_t* buf, int x, int y) {
  return buf[(size_t)y * TW * 4 + (size_t)x * 4 + 3];
}

static void test_corners_punched(void) {
  uint8_t buf[TW * TH * 4];
  fill_opaque(buf);
  myui_wl_corner_mask(buf, TW, TH, TW * 4, 4);
  /* the extreme corner pixels are outside the arc in all four corners */
  TEST_ASSERT_EQ_INT(alpha_at(buf, 0, 0), 0);            /* TL */
  TEST_ASSERT_EQ_INT(alpha_at(buf, TW - 1, 0), 0);       /* TR */
  TEST_ASSERT_EQ_INT(alpha_at(buf, 0, TH - 1), 0);       /* BL */
  TEST_ASSERT_EQ_INT(alpha_at(buf, TW - 1, TH - 1), 0);  /* BR */
  /* the arc keeps pixels near the diagonal (3,3 area inside r=4) */
  TEST_ASSERT(alpha_at(buf, 3, 3) == 0xFF);
  /* center and edge midpoints untouched */
  TEST_ASSERT(alpha_at(buf, TW / 2, TH / 2) == 0xFF);
  TEST_ASSERT(alpha_at(buf, TW / 2, 0) == 0xFF);
  TEST_ASSERT(alpha_at(buf, 0, TH / 2) == 0xFF);
  /* beyond the corner box the row is fully opaque */
  TEST_ASSERT(alpha_at(buf, 5, 0) == 0xFF);
}

static void test_degenerate_inputs(void) {
  uint8_t buf[TW * TH * 4];
  fill_opaque(buf);
  myui_wl_corner_mask(NULL, TW, TH, TW * 4, 4); /* no crash */
  myui_wl_corner_mask(buf, TW, TH, TW * 4, 0);  /* radius 0 = noop */
  TEST_ASSERT(alpha_at(buf, 0, 0) == 0xFF);
  myui_wl_corner_mask(buf, 4, 4, TW * 4, 10);   /* radius > w/2: noop */
  TEST_ASSERT(alpha_at(buf, 0, 0) == 0xFF);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_corners_punched);
  MYTEST_RUN(test_degenerate_inputs);
MYTEST_MAIN_END()
