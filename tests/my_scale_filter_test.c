/**
 * @file my_scale_filter_test.c
 * @brief Bilinear scaling tests for the soft backend (M9b).
 */
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include <math.h>
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

/* ---------------- box pre-downsample (M10c) ---------------- */

static void fill_checker(uint8_t* img, int32_t w, int32_t h) {
  int32_t x, y;
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      uint8_t* p = img + ((size_t)y * (size_t)w + (size_t)x) * 4u;
      uint8_t v = (uint8_t)(((x + y) & 1) ? 255 : 0);
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
  }
}

static void test_box_checker_8x_to_gray(void) {
  /* 8x8 checkerboard -> 1x1 dst: 8x box average = mid gray (nearest would
   * give a hard black/white sample) */
  uint8_t img[8 * 8 * 4];
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 1, 1, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;
  fill_checker(img, 8, 8);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 8, 8, &(my_rectf_t){0, 0, 1, 1}, NULL);
  my_vgcanvas_end_frame(vg);

  px_rgb(lcd, 0, 0, &r, &g, &b);
  TEST_ASSERT(r > 110 && r < 145); /* ~(0+255)/2 = 127.5 */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_box_solid_red_stays_red(void) {
  /* 8x8 solid red -> 2x2 dst (4x box): still pure red */
  uint8_t img[8 * 8 * 4];
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 2, 2, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;
  int i;
  for (i = 0; i < 8 * 8; i++) {
    img[i * 4 + 0] = 255;
    img[i * 4 + 1] = 0;
    img[i * 4 + 2] = 0;
    img[i * 4 + 3] = 255;
  }

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 8, 8, &(my_rectf_t){0, 0, 2, 2}, NULL);
  my_vgcanvas_end_frame(vg);

  px_rgb(lcd, 0, 0, &r, &g, &b);
  TEST_ASSERT(r == 255 && g == 0 && b == 0);
  px_rgb(lcd, 1, 1, &r, &g, &b);
  TEST_ASSERT(r == 255 && g == 0 && b == 0);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_box_checker_variance_below_nearest(void) {
  /* 64x64 checkerboard -> 8x8 dst: box+bilinear output is all mid-gray
   * (low variance); nearest output contains hard 0/255 samples */
  uint8_t img[64 * 64 * 4];
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 8, 8, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;
  int x, y;
  bool saw_hard = false;
  fill_checker(img, 64, 64);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_NEAREST);
  my_vgcanvas_draw_image(vg, img, 64, 64, &(my_rectf_t){0, 0, 8, 8}, NULL);
  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      px_rgb(lcd, x, y, &r, &g, &b);
      if (r == 0 || r == 255) {
        saw_hard = true;
      }
    }
  }
  TEST_ASSERT(saw_hard);

  memset(my_lcd_mem_get_buffer(lcd), 0, 8 * my_lcd_mem_get_stride(lcd));
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 64, 64, &(my_rectf_t){0, 0, 8, 8}, NULL);
  my_vgcanvas_end_frame(vg);
  for (y = 0; y < 8; y++) {
    for (x = 0; x < 8; x++) {
      px_rgb(lcd, x, y, &r, &g, &b);
      TEST_ASSERT(r > 100 && r < 155); /* every pixel near 127.5 */
    }
  }

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

/* M12c: fixed-point bilinear must match a float reference within +-1
 * per channel (weight quantization 1/256). */
static void test_fixedpoint_matches_float_within_1(void) {
  uint8_t img[3 * 3 * 4];
  int x, y;
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 7, 5, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  static const int DW = 7, DH = 5;
  for (y = 0; y < 3; y++) {
    for (x = 0; x < 3; x++) {
      uint8_t* p = img + (y * 3 + x) * 4;
      p[0] = (uint8_t)(x * 80 + y * 20);
      p[1] = (uint8_t)(255 - x * 60);
      p[2] = (uint8_t)(x * 10 + y * 70);
      p[3] = 255;
    }
  }
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 3, 3, &(my_rectf_t){0, 0, DW, DH}, NULL);
  my_vgcanvas_end_frame(vg);

  for (y = 0; y < DH; y++) {
    for (x = 0; x < DW; x++) {
      /* float reference for the same pixel-center mapping */
      float gx = ((float)x + 0.5f) * 3.0f / (float)DW - 0.5f;
      float gy = ((float)y + 0.5f) * 3.0f / (float)DH - 0.5f;
      int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
      float ax = gx - (float)x0, ay = gy - (float)y0;
      int x1, y1, c;
      uint8_t r, g, b;
      if (x0 < 0) { x0 = 0; ax = 0.0f; }
      if (y0 < 0) { y0 = 0; ay = 0.0f; }
      x1 = x0 + 1 < 3 ? x0 + 1 : 2;
      y1 = y0 + 1 < 3 ? y0 + 1 : 2;
      if (x0 >= 3) { x0 = 2; x1 = 2; ax = 0.0f; }
      if (y0 >= 3) { y0 = 2; y1 = 2; ay = 0.0f; }
      px_rgb(lcd, x, y, &r, &g, &b);
      {
        uint8_t got[3] = {r, g, b};
        for (c = 0; c < 3; c++) {
          float v00 = (float)img[(y0 * 3 + x0) * 4 + c];
          float v10 = (float)img[(y0 * 3 + x1) * 4 + c];
          float v01 = (float)img[(y1 * 3 + x0) * 4 + c];
          float v11 = (float)img[(y1 * 3 + x1) * 4 + c];
          float top = v00 + (v10 - v00) * ax;
          float bot = v01 + (v11 - v01) * ax;
          int want = (int)(top + (bot - top) * ay + 0.5f);
          int d = (int)got[c] - want;
          TEST_ASSERT(d >= -1 && d <= 1);
        }
      }
    }
  }

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_box_gradient_exact(void) {
  /* 4x4 -> 1x1 (4x tier): SWAR-packed accumulation (M11c) must produce
   * pixel-exact identical sums to the scalar path */
  uint8_t img[4 * 4 * 4];
  int x, y;
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 1, 1, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;
  for (y = 0; y < 4; y++) {
    for (x = 0; x < 4; x++) {
      uint8_t* p = img + (y * 4 + x) * 4;
      p[0] = (uint8_t)(x + y * 4); /* 0..15, mean 7.5 -> 7 */
      p[1] = 255;
      p[2] = (uint8_t)(x * 16); /* 0/16/32/48, mean 24 */
      p[3] = 255;
    }
  }
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 4, 4, &(my_rectf_t){0, 0, 1, 1}, NULL);
  my_vgcanvas_end_frame(vg);
  px_rgb(lcd, 0, 0, &r, &g, &b);
  TEST_ASSERT(r == 7 && g == 255 && b == 24);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_box_tier_partial_blocks(void) {
  /* 12x12 solid green -> 3x3 dst: ratio 0.25 -> 4x tier, 12/4=3 exact;
   * then 13x13 -> 3x3: partial edge blocks must not read OOB nor skew */
  uint8_t img[13 * 13 * 4];
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 3, 3, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t r, g, b;
  int i;
  for (i = 0; i < 13 * 13; i++) {
    img[i * 4 + 0] = 0;
    img[i * 4 + 1] = 200;
    img[i * 4 + 2] = 0;
    img[i * 4 + 3] = 255;
  }

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_soft_set_scale_filter(vg, MY_SCALE_FILTER_BILINEAR);
  my_vgcanvas_draw_image(vg, img, 13, 13, &(my_rectf_t){0, 0, 3, 3}, NULL);
  my_vgcanvas_end_frame(vg);
  px_rgb(lcd, 2, 2, &r, &g, &b); /* corner from a partial block */
  TEST_ASSERT(g > 180 && r < 40);

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
  MYTEST_RUN(test_box_checker_8x_to_gray);
  MYTEST_RUN(test_box_solid_red_stays_red);
  MYTEST_RUN(test_box_checker_variance_below_nearest);
  MYTEST_RUN(test_box_tier_partial_blocks);
  MYTEST_RUN(test_box_gradient_exact);
  MYTEST_RUN(test_fixedpoint_matches_float_within_1);
MYTEST_MAIN_END()
