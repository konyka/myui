/**
 * @file my_antialias_test.c
 * @brief Scanline coverage AA tests for the soft backend (M7c).
 */
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include "mytest.h"

static uint8_t px_r(my_lcd_t* lcd, int32_t x, int32_t y) {
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  return buf[(size_t)y * stride + (size_t)x * 4 + 2]; /* BGRA: R at +2 */
}

static void draw_triangle(my_vgcanvas_t* vg, float x0, float y0, float x1,
                          float y1, float x2, float y2) {
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, x0, y0);
  my_vgcanvas_line_to(vg, x1, y1);
  my_vgcanvas_line_to(vg, x2, y2);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
}

static void test_diagonal_edge_has_intermediate_pixels(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  bool found_partial = false;
  bool found_full = false;
  int32_t x, y;

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  draw_triangle(vg, 16, 4, 28, 28, 4, 28);
  my_vgcanvas_end_frame(vg);

  for (y = 4; y < 28; y++) {
    for (x = 4; x < 28; x++) {
      uint8_t r = px_r(lcd, x, y);
      if (r > 0 && r < 255) {
        found_partial = true;
      }
      if (r == 255) {
        found_full = true;
      }
    }
  }
  TEST_ASSERT(found_partial); /* AA edge pixels exist */
  TEST_ASSERT(found_full);    /* interior is fully covered */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_aa_off_has_no_intermediate_pixels(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  int32_t x, y;

  my_vgcanvas_soft_set_antialias(vg, false);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  draw_triangle(vg, 16, 4, 28, 28, 4, 28);
  my_vgcanvas_end_frame(vg);

  for (y = 4; y < 28; y++) {
    for (x = 4; x < 28; x++) {
      uint8_t r = px_r(lcd, x, y);
      TEST_ASSERT(r == 0 || r == 255); /* hard edges only */
    }
  }

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_straight_edges_full_coverage(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  /* axis-aligned rectangle: every covered pixel fully covered */
  draw_triangle(vg, 4, 4, 28, 4, 4, 28);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT_EQ_INT(px_r(lcd, 4, 10), 255); /* vertical edge: full */
  TEST_ASSERT_EQ_INT(px_r(lcd, 10, 4), 255); /* horizontal edge: full */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_coverage_blend_formula(void) {
  /* translucent fill + AA edge: pixel alpha = color.a * cov / 4,
   * blend formula out = src*a' + dst*(255-a') over white.
   * Just assert monotonicity: with a=128 fill, interior ends up at the
   * same value as a plain translucent rect. */
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 32, 32});
  my_vgcanvas_set_fill_color(vg, my_color_rgba(255, 0, 0, 128));
  draw_triangle(vg, 16, 4, 28, 28, 4, 28);
  my_vgcanvas_end_frame(vg);

  /* interior pixel: red at 128 alpha over white = (255,127,127) */
  TEST_ASSERT_EQ_INT(px_r(lcd, 16, 20), 255);
  {
    uint8_t* buf = my_lcd_mem_get_buffer(lcd);
    TEST_ASSERT_EQ_INT(buf[(20 * 32 + 16) * 4 + 1], 127); /* G */
  }

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_rounded_corner_aa(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 40, 40, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  bool found_partial = false;
  int32_t x, y;

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){4, 4, 32, 32}, 10);
  my_vgcanvas_end_frame(vg);

  for (y = 4; y < 14 && !found_partial; y++) {
    for (x = 4; x < 14; x++) {
      uint8_t r = px_r(lcd, x, y);
      if (r > 0 && r < 255) {
        found_partial = true;
      }
    }
  }
  TEST_ASSERT(found_partial); /* corner arc has AA coverage pixels */
  TEST_ASSERT_EQ_INT(px_r(lcd, 20, 20), 255); /* center opaque */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_near_horizontal_edge_y_aa(void) {
  /* shallow slope (~10 deg): level 1 leaves top/bottom edges mostly hard,
   * level 2 (y2) produces intermediate values along the shallow edge */
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 40, 20, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  int partial_l2 = 0;
  int32_t x, y;

  my_vgcanvas_soft_set_antialias_level(vg, 2);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 2, 16);
  my_vgcanvas_line_to(vg, 38, 8);
  my_vgcanvas_line_to(vg, 38, 18);
  my_vgcanvas_line_to(vg, 2, 18);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_end_frame(vg);

  for (y = 6; y < 18; y++) {
    for (x = 2; x < 38; x++) {
      uint8_t r = px_r(lcd, x, y);
      if (r > 0 && r < 255) {
        partial_l2++;
      }
    }
  }
  TEST_ASSERT(partial_l2 > 10); /* shallow top edge shows y-coverage */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_stroke_diagonal_has_aa(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  bool found_partial = false;
  int32_t x, y;

  my_vgcanvas_soft_set_antialias_level(vg, 2);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_set_line_width(vg, 2);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4, 28);
  my_vgcanvas_line_to(vg, 28, 6);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);

  for (y = 4; y < 30 && !found_partial; y++) {
    for (x = 4; x < 30; x++) {
      uint8_t r = px_r(lcd, x, y);
      if (r > 0 && r < 255) {
        found_partial = true;
      }
    }
  }
  TEST_ASSERT(found_partial);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_stroke_width_precise(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  my_vgcanvas_soft_set_antialias_level(vg, 0); /* hard edges for exact math */

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_set_line_width(vg, 3);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4, 16);
  my_vgcanvas_line_to(vg, 28, 16);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);

  /* 3px horizontal line at y=16 covers rows 15..17, not 14/18 */
  TEST_ASSERT_EQ_INT(px_r(lcd, 16, 15), 255);
  TEST_ASSERT_EQ_INT(px_r(lcd, 16, 16), 255);
  TEST_ASSERT_EQ_INT(px_r(lcd, 16, 17), 255);
  TEST_ASSERT_EQ_INT(px_r(lcd, 16, 14), 0);
  TEST_ASSERT_EQ_INT(px_r(lcd, 16, 18), 0);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_diagonal_edge_has_intermediate_pixels);
  MYTEST_RUN(test_aa_off_has_no_intermediate_pixels);
  MYTEST_RUN(test_straight_edges_full_coverage);
  MYTEST_RUN(test_coverage_blend_formula);
  MYTEST_RUN(test_rounded_corner_aa);
  MYTEST_RUN(test_near_horizontal_edge_y_aa);
  MYTEST_RUN(test_stroke_diagonal_has_aa);
  MYTEST_RUN(test_stroke_width_precise);
MYTEST_MAIN_END()
