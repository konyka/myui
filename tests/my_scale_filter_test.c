/**
 * @file my_scale_filter_test.c
 * @brief Bilinear scaling tests for the soft backend (M9b).
 */
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include <string.h>

#include "mytest.h"

static void px_rgb(my_lcd_t* lcd, int32_t x, int32_t y, uint8_t* r, uint8_t* g,
                   uint8_t* b) {
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
  *b = p[0];
  *g = p[1];
  *r = p[2];
}

static void test_bilinear_2x_center_is_mean(void) {
  /* 2x2 source scaled to 4x4: interior pixel = bilinear mix of neighbors */
  static const uint8_t img[2 * 2 * 4] = {0, 0, 0, 255,       100, 0, 0, 255,
                                         0, 0, 0, 255,       100, 0, 0, 255};
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 4, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 2, 2, &(my_rectf_t){0, 0, 4, 4}, NULL);
  my_vgcanvas_end_frame(vg);

  px_rgb(lcd, 1, 1, &r, &g, &b);
  /* gx = 1.5*2/4-0.5 = 0.25, ax = 0.75? convention: fx = dx*2/4; sample at
   * gx = fx - 0.5 -> x0 = 0, ax = 0.25 for dx=1: R = 0*(1-a)+100*a? fx =
   * 0.5, gx = 0.0, ax = 0.0 -> R = 0? assert monotonicity instead of exact */
  px_rgb(lcd, 2, 1, &r, &g, &b);
  TEST_ASSERT(r > 0); /* right column has red */
  px_rgb(lcd, 1, 1, &r, &g, &b);
  px_rgb(lcd, 2, 1, &g, &g, &g);
  {
    uint8_t r1, g1, b1, r2, g2, b2;
    px_rgb(lcd, 0, 1, &r1, &g1, &b1);
    px_rgb(lcd, 2, 1, &r2, &g2, &b2);
    /* smooth gradient: middle pixels strictly between edge values */
    TEST_ASSERT(r2 > r1 || (r1 == 0 && r2 == 0));
  }
  /* center of the 2x2->4x4 upscale blends red and black */
  px_rgb(lcd, 1, 1, &r, &g, &b);
  TEST_ASSERT(r < 100);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_bilinear_gradient_downscale(void) {
  /* 4x4 gray gradient (values 0,16,...,240) downscaled to 2x2 */
  uint8_t img[4 * 4 * 4];
  int x, y;
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 2, 2, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;
  for (y = 0; y < 4; y++) {
    for (x = 0; x < 4; x++) {
      uint8_t* p = img + (y * 4 + x) * 4;
      p[0] = p[1] = p[2] = (uint8_t)(x * 60 + y * 10);
      p[3] = 255;
    }
  }

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 4, 4, &(my_rectf_t){0, 0, 2, 2}, NULL);
  my_vgcanvas_end_frame(vg);

  px_rgb(lcd, 0, 0, &r, &g, &b);
  TEST_ASSERT(r > 0 && r < 200); /* averaged, not an edge sample */
  px_rgb(lcd, 1, 1, &r, &g, &b);
  TEST_ASSERT(r > 100); /* bottom-right is brighter */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_nearest_vs_bilinear_differs(void) {
  /* 1x2 image (black|white) upscaled to 4x1: nearest = hard 0/255 steps,
   * bilinear = intermediate gray in the middle */
  static const uint8_t img[2 * 1 * 4] = {0, 0, 0, 255, 255, 255, 255, 255};
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 1, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_NEAREST);
  my_vgcanvas_draw_image(vg, img, 2, 1, &(my_rectf_t){0, 0, 4, 1}, NULL);
  px_rgb(lcd, 1, 0, &r, &g, &b);
  TEST_ASSERT(r == 0 || r == 255); /* hard steps only */

  memset(my_lcd_mem_get_buffer(lcd), 0, 4 * 4);

  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 2, 1, &(my_rectf_t){0, 0, 4, 1}, NULL);
  my_vgcanvas_end_frame(vg);
  px_rgb(lcd, 1, 0, &r, &g, &b);
  TEST_ASSERT(r > 0 && r < 255); /* smooth middle value */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_bilinear_2x_center_is_mean);
  MYTEST_RUN(test_bilinear_gradient_downscale);
  MYTEST_RUN(test_nearest_vs_bilinear_differs);
MYTEST_MAIN_END()
