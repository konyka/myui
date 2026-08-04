/**
 * @file my_vgcanvas_soft_test.c
 * @brief Unit tests for the software vgcanvas backend.
 *
 * All tests render onto a 32-bit BGRA my_lcd_mem so pixels can be read
 * back directly as my_color_t values.
 */
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include <string.h>

#include "mytest.h"

static my_color_t px(my_lcd_t* lcd, int x, int y) {
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
  return my_color_rgba(p[2], p[1], p[0], p[3]); /* B,G,R,A */
}

static const my_color_t RED = {255, 0, 0, 255};
static const my_color_t GREEN = {0, 255, 0, 255};
static const my_color_t BLUE = {0, 0, 255, 255};
static const my_color_t BLACK = {0, 0, 0, 0};

static void test_fill_rect(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){4, 6, 8, 4});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 4, 6), RED));
  TEST_ASSERT(my_color_eq(px(lcd, 11, 9), RED));
  TEST_ASSERT(my_color_eq(px(lcd, 3, 6), BLACK));
  TEST_ASSERT(my_color_eq(px(lcd, 4, 10), BLACK));

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_stroke_rect_line_width(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, GREEN);
  my_vgcanvas_set_line_width(vg, 2);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){4, 4, 16, 16});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 4, 4), GREEN));    /* top edge */
  TEST_ASSERT(my_color_eq(px(lcd, 4, 5), GREEN));    /* 2px wide */
  TEST_ASSERT(my_color_eq(px(lcd, 19, 18), GREEN));  /* right/bottom edge */
  TEST_ASSERT(my_color_eq(px(lcd, 12, 12), BLACK));  /* hollow center */
  TEST_ASSERT(my_color_eq(px(lcd, 3, 3), BLACK));    /* outside */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_path_triangle_fill(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, BLUE);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 16, 4);
  my_vgcanvas_line_to(vg, 28, 28);
  my_vgcanvas_line_to(vg, 4, 28);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 16, 20), BLUE)); /* interior */
  TEST_ASSERT(my_color_eq(px(lcd, 16, 26), BLUE)); /* near base */
  TEST_ASSERT(my_color_eq(px(lcd, 2, 8), BLACK));  /* exterior */
  TEST_ASSERT(my_color_eq(px(lcd, 30, 8), BLACK));

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_fill_even_odd_hole(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 40, 40, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  /* outer rect then inner rect as one path: even-odd punches a hole */
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4, 4);
  my_vgcanvas_line_to(vg, 36, 4);
  my_vgcanvas_line_to(vg, 36, 36);
  my_vgcanvas_line_to(vg, 4, 36);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_move_to(vg, 16, 16);
  my_vgcanvas_line_to(vg, 24, 16);
  my_vgcanvas_line_to(vg, 24, 24);
  my_vgcanvas_line_to(vg, 16, 24);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 10, 10), RED));  /* ring */
  TEST_ASSERT(my_color_eq(px(lcd, 20, 20), BLACK)); /* hole */
  TEST_ASSERT(my_color_eq(px(lcd, 2, 2), BLACK));   /* outside */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_fill_concave_polygon(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 40, 40, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  /* L-shape: notch at top-right */
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, GREEN);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4, 4);
  my_vgcanvas_line_to(vg, 20, 4);
  my_vgcanvas_line_to(vg, 20, 20);
  my_vgcanvas_line_to(vg, 36, 20);
  my_vgcanvas_line_to(vg, 36, 36);
  my_vgcanvas_line_to(vg, 4, 36);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 10, 10), GREEN)); /* vertical bar */
  TEST_ASSERT(my_color_eq(px(lcd, 28, 28), GREEN)); /* horizontal bar */
  TEST_ASSERT(my_color_eq(px(lcd, 28, 10), BLACK)); /* the notch */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_clip_rect_limits_drawing(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){8, 8, 16, 16});
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 32, 32});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 8, 8), RED));
  TEST_ASSERT(my_color_eq(px(lcd, 23, 23), RED));
  TEST_ASSERT(my_color_eq(px(lcd, 7, 7), BLACK));   /* outside clip */
  TEST_ASSERT(my_color_eq(px(lcd, 24, 24), BLACK)); /* half-open clip */
  TEST_ASSERT(my_color_eq(px(lcd, 0, 31), BLACK));

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_nested_clip_and_restore(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){0, 0, 16, 32}); /* left half */
  my_vgcanvas_save(vg);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){8, 0, 24, 32}); /* intersect: 8..15 */
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 32, 32});
  my_vgcanvas_restore(vg); /* back to left-half clip */
  my_vgcanvas_set_fill_color(vg, GREEN);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 8, 32}); /* left quarter only */
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 12, 16), RED));  /* inner clip area */
  TEST_ASSERT(my_color_eq(px(lcd, 4, 16), GREEN)); /* outer clip only */
  TEST_ASSERT(my_color_eq(px(lcd, 20, 16), BLACK)); /* outside all clips */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_translate_offsets_drawing(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_translate(vg, 10, 6);
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 4, 4});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 10, 6), RED));
  TEST_ASSERT(my_color_eq(px(lcd, 13, 9), RED));
  TEST_ASSERT(my_color_eq(px(lcd, 9, 6), BLACK));
  TEST_ASSERT(my_color_eq(px(lcd, 10, 10), BLACK));

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_save_restore_restores_color(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 16, 8, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_save(vg);
  my_vgcanvas_set_fill_color(vg, GREEN);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 4, 4});
  my_vgcanvas_restore(vg);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){8, 0, 4, 4});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 2, 2), GREEN));
  TEST_ASSERT(my_color_eq(px(lcd, 10, 2), RED));

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_rounded_rect(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 40, 40, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){4, 4, 32, 32}, 8);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 20, 20), RED)); /* center */
  TEST_ASSERT(my_color_eq(px(lcd, 20, 4), RED));  /* top edge middle */
  TEST_ASSERT(my_color_eq(px(lcd, 4, 4), BLACK)); /* corner is rounded away */
  TEST_ASSERT(my_color_eq(px(lcd, 12, 12), RED)); /* inside the corner arc */

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_stroke_polyline(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, RED);
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4, 4);
  my_vgcanvas_line_to(vg, 28, 4);
  my_vgcanvas_line_to(vg, 28, 28);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(my_color_eq(px(lcd, 16, 4), RED));  /* horizontal segment */
  TEST_ASSERT(my_color_eq(px(lcd, 28, 16), RED)); /* vertical segment */
  TEST_ASSERT(my_color_eq(px(lcd, 16, 16), BLACK));

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_dirty_rects_tracked_per_frame(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 64, 64, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  const my_dirty_rects_t* dr = my_vgcanvas_soft_get_dirty_rects(vg);

  TEST_ASSERT_NOT_NULL(dr);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){8, 8, 4, 4});
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){40, 40, 4, 4});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT_EQ_INT(my_dirty_rects_count(dr), 2);

  /* next frame resets the dirty set */
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 2, 2});
  my_vgcanvas_end_frame(vg);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(dr), 1);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_draw_text_not_supported(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 16, 16, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "hi", 0, 0),
                     MY_RET_NOT_SUPPORTED);
  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_null_params(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 16, 16, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  TEST_ASSERT_NULL(my_vgcanvas_soft_create(NULL, NULL));
  TEST_ASSERT_EQ_INT(my_vgcanvas_fill_rect(vg, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_vgcanvas_clip_rect(vg, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_vgcanvas_fill_rounded_rect(vg, NULL, 4),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_vgcanvas_restore(vg), MY_RET_FAIL); /* empty stack */
  my_vgcanvas_destroy(vg);
  my_vgcanvas_destroy(NULL); /* must be safe */
  my_lcd_destroy(lcd);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_lcd_t* lcd = my_lcd_mem_create(dbg, 64, 64, MY_PIXEL_FORMAT_RGB565);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(dbg, lcd);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_save(vg);
  my_vgcanvas_translate(vg, 3, 3);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){0, 0, 50, 50});
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 10, 10);
  my_vgcanvas_line_to(vg, 40, 10);
  my_vgcanvas_line_to(vg, 25, 40);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_restore(vg);
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){0, 0, 20, 20}, 5);
  my_vgcanvas_end_frame(vg);

  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_fill_rect);
  MYTEST_RUN(test_stroke_rect_line_width);
  MYTEST_RUN(test_path_triangle_fill);
  MYTEST_RUN(test_fill_even_odd_hole);
  MYTEST_RUN(test_fill_concave_polygon);
  MYTEST_RUN(test_clip_rect_limits_drawing);
  MYTEST_RUN(test_nested_clip_and_restore);
  MYTEST_RUN(test_translate_offsets_drawing);
  MYTEST_RUN(test_save_restore_restores_color);
  MYTEST_RUN(test_rounded_rect);
  MYTEST_RUN(test_stroke_polyline);
  MYTEST_RUN(test_dirty_rects_tracked_per_frame);
  MYTEST_RUN(test_draw_text_not_supported);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
