/**
 * @file my_draw_text_test.c
 * @brief draw_text pixel tests for the soft backend (bitmap + TTF fonts).
 */
#include <stdio.h>

#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include "mytest.h"

static my_color_t px(my_lcd_t* lcd, int x, int y) {
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
  return my_color_rgba(p[2], p[1], p[0], p[3]);
}

static int count_lit(my_lcd_t* lcd, int32_t x0, int32_t y0, int32_t x1,
                     int32_t y1) {
  int n = 0, x, y;
  for (y = y0; y < y1; y++) {
    for (x = x0; x < x1; x++) {
      if (px(lcd, x, y).r > 0) {
        n++;
      }
    }
  }
  return n;
}

static void test_bitmap_font_text_pixels(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 64, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  my_font_t* font = my_font_bitmap_create(NULL);
  int lit;

  my_vgcanvas_set_font(vg, font, 8);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 0, 0));
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "A", 4, 4), MY_RET_OK);
  my_vgcanvas_end_frame(vg);

  lit = count_lit(lcd, 4, 4, 12, 12);
  TEST_ASSERT(lit > 5); /* 'A' glyph area has red pixels */
  TEST_ASSERT_EQ_INT(count_lit(lcd, 20, 4, 32, 12), 0); /* outside untouched */

  my_vgcanvas_destroy(vg);
  my_font_destroy(font);
  my_lcd_destroy(lcd);
}

static void test_draw_text_clipped(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 64, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  my_font_t* font = my_font_bitmap_create(NULL);

  my_vgcanvas_set_font(vg, font, 8);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){0, 0, 8, 32}); /* only left 8px */
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_draw_text(vg, "AAA", 4, 4);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(count_lit(lcd, 8, 4, 32, 12) == 0); /* beyond clip: nothing */

  my_vgcanvas_destroy(vg);
  my_font_destroy(font);
  my_lcd_destroy(lcd);
}

static void test_draw_text_translates(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 64, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  my_font_t* font = my_font_bitmap_create(NULL);

  my_vgcanvas_set_font(vg, font, 8);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_translate(vg, 16, 0);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_draw_text(vg, "A", 4, 4);
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT(count_lit(lcd, 20, 4, 28, 12) > 5); /* shifted by 16 */
  TEST_ASSERT_EQ_INT(count_lit(lcd, 4, 4, 12, 12), 0);

  my_vgcanvas_destroy(vg);
  my_font_destroy(font);
  my_lcd_destroy(lcd);
}

static void test_ttf_text_pixels(void) {
  const char* candidates[] = {
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf", NULL};
  my_font_t* font = NULL;
  my_lcd_t* lcd;
  my_vgcanvas_t* vg;
  int i, lit;
  int32_t w = 0, h = 0;

  for (i = 0; candidates[i] != NULL && font == NULL; i++) {
    font = my_font_stb_create(NULL, candidates[i], 0);
  }
  if (font == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  lcd = my_lcd_mem_create(NULL, 128, 40, MY_PIXEL_FORMAT_BGRA8888);
  vg = my_vgcanvas_soft_create(NULL, lcd);

  my_vgcanvas_set_font(vg, font, 24);
  my_vgcanvas_measure_text(vg, "Hi", &w, &h);
  TEST_ASSERT(w > 10 && h >= 24);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "Hi", 4, 4), MY_RET_OK);
  my_vgcanvas_end_frame(vg);

  lit = count_lit(lcd, 4, 4, 4 + w, 4 + h);
  TEST_ASSERT(lit > 20);

  my_vgcanvas_destroy(vg);
  my_font_destroy(font);
  my_lcd_destroy(lcd);
}

/* RTL scripts via text_layout (M11a). Font paths contain [] (shell
 * escaping only; plain strings in C). Skip when the fonts are absent. */
static void render_rtl(const char* font_path, const char* text,
                       const char* ltr4, const char* tag) {
  my_font_t* font = my_font_stb_create(NULL, font_path, 0);
  my_lcd_t* lcd;
  my_vgcanvas_t* vg;
  int32_t w = 0, h = 0, wl = 0, hl = 0;
  int lit;
  if (font == NULL) {
    fprintf(stdout, "SKIP: %s font not found (%s)\n", tag, font_path);
    return;
  }
  lcd = my_lcd_mem_create(NULL, 256, 48, MY_PIXEL_FORMAT_BGRA8888);
  vg = my_vgcanvas_soft_create(NULL, lcd);
  my_vgcanvas_set_font(vg, font, 24);

  TEST_ASSERT_EQ_INT(my_vgcanvas_measure_text(vg, text, &w, &h), MY_RET_OK);
  TEST_ASSERT(w > 0 && h >= 24);
  TEST_ASSERT_EQ_INT(my_vgcanvas_measure_text(vg, ltr4, &wl, &hl), MY_RET_OK);
  TEST_ASSERT(w != wl); /* shaped RTL word != any 4 LTR chars */

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, text, 4, 4), MY_RET_OK);
  my_vgcanvas_end_frame(vg);
  lit = count_lit(lcd, 4, 4, 4 + w, 4 + h);
  TEST_ASSERT(lit > 40); /* real glyphs rendered (not tofu boxes) */

  my_vgcanvas_destroy(vg);
  my_font_destroy(font);
  my_lcd_destroy(lcd);
}

static void test_draw_text_arabic_shaped(void) {
  /* محمد: shaped (presentation forms) + visually reversed */
  render_rtl("/usr/share/fonts/google-noto-vf/NotoNaskhArabic[wght].ttf",
             "\xD9\x85\xD8\xAD\xD9\x85\xD8\xAF", "abcd", "arabic");
}

static void test_draw_text_hebrew_reordered(void) {
  /* אבג */
  render_rtl("/usr/share/fonts/google-noto-vf/NotoSansHebrew[wght].ttf",
             "\xD7\x90\xD7\x91\xD7\x92", "abcd", "hebrew");
}

static void test_no_font_returns_not_supported(void) {
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 32, 32, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  int32_t w = 0, h = 0;
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "A", 0, 0),
                     MY_RET_NOT_SUPPORTED);
  TEST_ASSERT_EQ_INT(my_vgcanvas_measure_text(vg, "A", &w, &h),
                     MY_RET_NOT_SUPPORTED);
  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_bitmap_font_text_pixels);
  MYTEST_RUN(test_draw_text_clipped);
  MYTEST_RUN(test_draw_text_translates);
  MYTEST_RUN(test_ttf_text_pixels);
  MYTEST_RUN(test_draw_text_arabic_shaped);
  MYTEST_RUN(test_draw_text_hebrew_reordered);
  MYTEST_RUN(test_no_font_returns_not_supported);
MYTEST_MAIN_END()
