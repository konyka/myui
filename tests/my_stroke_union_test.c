/**
 * @file my_stroke_union_test.c
 * @brief Translucent stroke union-merge tests (M11d): join/cap coverage
 * is merged (max-capped) within one stroke() call -- a joint pixel sees
 * the stroke alpha once, not twice. Opaque strokes are unchanged.
 */
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include "mytest.h"

static uint8_t px_r(my_lcd_t* lcd, int32_t x, int32_t y) {
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  return buf[(size_t)y * stride + (size_t)x * 4 + 2]; /* BGRA -> R */
}

static void stroke_poly(my_vgcanvas_t* vg, uint8_t a) {
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgba(255, 255, 255, a));
  my_vgcanvas_set_line_width(vg, 8.0f);
  my_vgcanvas_set_line_cap(vg, MY_LINE_CAP_ROUND);
  my_vgcanvas_set_line_join(vg, MY_LINE_JOIN_ROUND);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 8, 32);
  my_vgcanvas_line_to(vg, 24, 32);
  my_vgcanvas_line_to(vg, 24, 16);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
}

static void test_join_alpha_not_doubled(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 48, 48, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t v;
  stroke_poly(vg, 128);
  /* joint vertex (24,32): covered by BOTH quads AND the join disk --
   * old per-piece path composed ~224 (three blends), union = ~128 */
  v = px_r(lcd, 24, 32);
  TEST_ASSERT(v > 118 && v < 138);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_cap_alpha_not_doubled(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 48, 48, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t v;
  stroke_poly(vg, 128);
  /* round-cap center at the first endpoint (8,32): disk + first quad */
  v = px_r(lcd, 8, 32);
  TEST_ASSERT(v > 118 && v < 138);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_opaque_stroke_unchanged(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 48, 48, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  stroke_poly(vg, 255);
  /* fully covered interior pixels stay 255; bg stays 0 */
  TEST_ASSERT(px_r(lcd, 24, 32) == 255);
  TEST_ASSERT(px_r(lcd, 8, 32) == 255);
  TEST_ASSERT(px_r(lcd, 0, 0) == 0);
  TEST_ASSERT(px_r(lcd, 47, 47) == 0);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_union_only_within_one_call(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 48, 48, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  uint8_t v;
  /* two stroke() calls over the same pixel: normal src-over (documented
   * boundary) -- alpha composes up (~192), no union across calls */
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgba(255, 255, 255, 128));
  my_vgcanvas_set_line_width(vg, 8.0f);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 8, 24);
  my_vgcanvas_line_to(vg, 40, 24);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 24, 8);
  my_vgcanvas_line_to(vg, 24, 40);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
  v = px_r(lcd, 24, 24); /* crossing point: two calls blended */
  TEST_ASSERT(v > 170);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_join_alpha_not_doubled);
  MYTEST_RUN(test_cap_alpha_not_doubled);
  MYTEST_RUN(test_opaque_stroke_unchanged);
  MYTEST_RUN(test_union_only_within_one_call);
MYTEST_MAIN_END()
