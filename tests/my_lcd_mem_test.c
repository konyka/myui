/**
 * @file my_lcd_mem_test.c
 * @brief Unit tests for my_lcd_mem (pixel format specialization).
 */
#include "myr/my_lcd_mem.h"

#include <string.h>

#include "mytest.h"

static uint16_t read_u16(const uint8_t* buf, uint32_t stride, int x, int y) {
  uint16_t v;
  memcpy(&v, buf + (size_t)y * stride + (size_t)x * 2, 2);
  return v;
}

static void test_create_and_props(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 8, 4, MY_PIXEL_FORMAT_RGB565);
  TEST_ASSERT_NOT_NULL(lcd);
  TEST_ASSERT_EQ_INT(my_lcd_get_width(lcd), 8);
  TEST_ASSERT_EQ_INT(my_lcd_get_height(lcd), 4);
  TEST_ASSERT_EQ_INT(my_lcd_get_format(lcd), MY_PIXEL_FORMAT_RGB565);
  TEST_ASSERT_NOT_NULL(my_lcd_mem_get_buffer(lcd));
  TEST_ASSERT_EQ_INT(my_lcd_mem_get_stride(lcd), 16);
  my_lcd_destroy(lcd);
}

static void test_create_invalid(void) {
  TEST_ASSERT_NULL(my_lcd_mem_create(NULL, 0, 4, MY_PIXEL_FORMAT_RGB565));
  TEST_ASSERT_NULL(my_lcd_mem_create(NULL, 8, 0, MY_PIXEL_FORMAT_RGB565));
  TEST_ASSERT_NULL(my_lcd_mem_create(NULL, 8, 4, (my_pixel_format_t)99));
}

static void test_fill_rect_rgb565_packing(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 8, 8, MY_PIXEL_FORMAT_RGB565);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);

  my_lcd_fill_rect(lcd, &(my_rect_t){2, 3, 4, 2}, my_color_rgb(255, 0, 0));
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 2, 3), 0xF800); /* pure red */
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 5, 4), 0xF800);
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 1, 3), 0x0000); /* outside */
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 2, 2), 0x0000);

  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 1, 1}, my_color_rgb(0, 255, 0));
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 0, 0), 0x07E0);
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 1, 1, 1}, my_color_rgb(0, 0, 255));
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 0, 1), 0x001F);
  my_lcd_destroy(lcd);
}

static void test_fill_rect_bgra(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 4, MY_PIXEL_FORMAT_BGRA8888);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  const uint8_t* p;

  my_lcd_fill_rect(lcd, &(my_rect_t){1, 1, 2, 2}, my_color_rgba(10, 20, 30, 255));
  p = buf + 1 * stride + 1 * 4;
  TEST_ASSERT_EQ_INT(p[0], 30); /* B */
  TEST_ASSERT_EQ_INT(p[1], 20); /* G */
  TEST_ASSERT_EQ_INT(p[2], 10); /* R */
  TEST_ASSERT_EQ_INT(p[3], 255); /* A */
  p = buf; /* (0,0) untouched */
  TEST_ASSERT_EQ_INT(p[0], 0);
  my_lcd_destroy(lcd);
}

static void test_fill_rect_argb_and_rgb888(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 2, 1, MY_PIXEL_FORMAT_ARGB8888);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 1, 1}, my_color_rgba(1, 2, 3, 4));
  /* a=4 blends over the black bg: out = src*4/255 + dst = src*4/255 (0) */
  TEST_ASSERT_EQ_INT(buf[0], 0); /* A channel unblended by dst */
  TEST_ASSERT(buf[1] <= 1 && buf[2] <= 1 && buf[3] <= 1);
  /* opaque variant keeps replace semantics for the channels */
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 1, 1}, my_color_rgba(1, 2, 3, 255));
  TEST_ASSERT_EQ_INT(buf[0], 255);
  TEST_ASSERT_EQ_INT(buf[1], 1);
  TEST_ASSERT_EQ_INT(buf[2], 2);
  TEST_ASSERT_EQ_INT(buf[3], 3);
  my_lcd_destroy(lcd);

  lcd = my_lcd_mem_create(NULL, 2, 1, MY_PIXEL_FORMAT_RGB888);
  buf = my_lcd_mem_get_buffer(lcd);
  my_lcd_fill_rect(lcd, &(my_rect_t){1, 0, 1, 1}, my_color_rgb(7, 8, 9));
  TEST_ASSERT_EQ_INT(buf[3], 7);
  TEST_ASSERT_EQ_INT(buf[4], 8);
  TEST_ASSERT_EQ_INT(buf[5], 9);
  TEST_ASSERT_EQ_INT(buf[0], 0);
  my_lcd_destroy(lcd);
}

static void test_fill_rect_mono_bits(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 16, 2, MY_PIXEL_FORMAT_MONO);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  TEST_ASSERT_EQ_INT(my_lcd_mem_get_stride(lcd), 2);

  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 1, 1}, my_color_rgb(255, 255, 255));
  TEST_ASSERT_EQ_INT(buf[0] & 0x80, 0x80); /* MSB first */
  my_lcd_fill_rect(lcd, &(my_rect_t){9, 0, 1, 1}, my_color_rgb(255, 255, 255));
  TEST_ASSERT_EQ_INT(buf[1] & 0x40, 0x40); /* x=9: byte 1, bit 1 (MSB first) */

  /* dark color clears the bit */
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 1, 1}, my_color_rgb(0, 0, 0));
  TEST_ASSERT_EQ_INT(buf[0] & 0x80, 0x00);
  TEST_ASSERT_EQ_INT(buf[1] & 0x40, 0x40);
  my_lcd_destroy(lcd);
}

static void test_fill_rect_clips_to_surface(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 8, 8, MY_PIXEL_FORMAT_RGB565);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  /* partially outside, negative origin: must clip, not crash */
  TEST_ASSERT_EQ_INT(my_lcd_fill_rect(lcd, &(my_rect_t){-4, -4, 8, 8},
                                      my_color_rgb(255, 255, 255)),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 0, 0), 0xFFFF);
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 3, 3), 0xFFFF);
  TEST_ASSERT_EQ_INT(read_u16(buf, stride, 4, 4), 0x0000);
  /* fully outside */
  TEST_ASSERT_EQ_INT(my_lcd_fill_rect(lcd, &(my_rect_t){100, 0, 4, 4},
                                      my_color_rgb(255, 0, 0)),
                     MY_RET_OK);
  my_lcd_destroy(lcd);
}

static void test_draw_pixels_rgb888(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 4, MY_PIXEL_FORMAT_RGB888);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  const uint8_t src[2 * 2 * 3] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const uint8_t* p;

  TEST_ASSERT_EQ_INT(my_lcd_draw_pixels(lcd, src, 1, 1, 2, 2), MY_RET_OK);
  p = buf + 1 * stride + 1 * 3;
  TEST_ASSERT_EQ_INT(p[0], 1);
  TEST_ASSERT_EQ_INT(p[1], 2);
  TEST_ASSERT_EQ_INT(p[2], 3);
  p = buf + 2 * stride + 2 * 3;
  TEST_ASSERT_EQ_INT(p[0], 10);
  TEST_ASSERT_EQ_INT(p[2], 12);
  my_lcd_destroy(lcd);
}

static void test_draw_pixels_clips(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 4, MY_PIXEL_FORMAT_RGB565);
  uint16_t src[4 * 4];
  memset(src, 0xFF, sizeof(src));
  /* partially off-surface: clip, no crash */
  TEST_ASSERT_EQ_INT(my_lcd_draw_pixels(lcd, src, -2, -2, 4, 4), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_lcd_draw_pixels(lcd, src, 3, 3, 4, 4), MY_RET_OK);
  /* fully off-surface */
  TEST_ASSERT_EQ_INT(my_lcd_draw_pixels(lcd, src, 50, 50, 4, 4), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_lcd_draw_pixels(lcd, NULL, 0, 0, 1, 1),
                     MY_RET_INVALID_PARAMS);
  my_lcd_destroy(lcd);
}

static void test_fill_rect_alpha_blending(void) {
  /* formula: out = (src*a + dst*(255-a)) / 255 (truncating).
   * white bg + red a=128: g,b = 255*127/255 = 127 */
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 4, MY_PIXEL_FORMAT_BGRA8888);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  uint8_t* p;

  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 4, 4}, my_color_rgb(255, 255, 255));
  my_lcd_fill_rect(lcd, &(my_rect_t){1, 1, 2, 2}, my_color_rgba(255, 0, 0, 128));

  p = buf + 1 * stride + 1 * 4;
  TEST_ASSERT_EQ_INT(p[2], 255); /* R stays */
  TEST_ASSERT_EQ_INT(p[1], 127); /* G = 127 */
  TEST_ASSERT_EQ_INT(p[0], 127); /* B = 127 */

  p = buf; /* outside: untouched white */
  TEST_ASSERT_EQ_INT(p[2], 255);
  TEST_ASSERT_EQ_INT(p[1], 255);

  my_lcd_destroy(lcd);

  /* RGB565: same math, tolerance from 5/6-bit repacking */
  lcd = my_lcd_mem_create(NULL, 2, 2, MY_PIXEL_FORMAT_RGB565);
  buf = my_lcd_mem_get_buffer(lcd);
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 2, 2}, my_color_rgb(255, 255, 255));
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 1, 1}, my_color_rgba(255, 0, 0, 128));
  {
    uint16_t v;
    uint8_t g;
    memcpy(&v, buf, 2);
    TEST_ASSERT_EQ_INT((v >> 11) & 0x1F, 31); /* R full */
    g = (uint8_t)((v >> 5) & 0x3F);
    TEST_ASSERT(g >= 30 && g <= 33); /* ~half green */
  }
  my_lcd_destroy(lcd);
}

static void test_frame_and_null_params(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 4, 4, MY_PIXEL_FORMAT_RGB565);
  TEST_ASSERT_EQ_INT(my_lcd_begin_frame(lcd, NULL), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_lcd_end_frame(lcd), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_lcd_fill_rect(lcd, NULL, my_color_rgb(1, 2, 3)),
                     MY_RET_INVALID_PARAMS);
  my_lcd_destroy(lcd);
  my_lcd_destroy(NULL); /* must be safe */
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_lcd_t* lcd = my_lcd_mem_create(dbg, 32, 32, MY_PIXEL_FORMAT_ARGB8888);
  my_lcd_fill_rect(lcd, &(my_rect_t){0, 0, 32, 32}, my_color_rgb(1, 2, 3));
  my_lcd_destroy(lcd);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_create_and_props);
  MYTEST_RUN(test_create_invalid);
  MYTEST_RUN(test_fill_rect_rgb565_packing);
  MYTEST_RUN(test_fill_rect_bgra);
  MYTEST_RUN(test_fill_rect_argb_and_rgb888);
  MYTEST_RUN(test_fill_rect_mono_bits);
  MYTEST_RUN(test_fill_rect_clips_to_surface);
  MYTEST_RUN(test_draw_pixels_rgb888);
  MYTEST_RUN(test_draw_pixels_clips);
  MYTEST_RUN(test_fill_rect_alpha_blending);
  MYTEST_RUN(test_frame_and_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
