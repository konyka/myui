/**
 * @file my_hidpi_test.c
 * @brief HiDPI tests (M12c): Xft.dpi parsing/rounding and the dummy-port
 * scale=2 full chain (physical lcd size, logical widget rect, 2x glyph
 * rendering position). x11's own event-coordinate conversion needs a
 * display and is covered implicitly by the smoke tests.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myr/my_lcd_mem.h"
#include "myui/my_layout.h"
#include "myui/my_window.h"
#include "myui/widgets/my_label.h"

#include "mytest.h"

#if defined(MYUI_PAL_X11)
#include "mypal/x11/my_pal_x11.h"

static void test_x11_dpi_parse(void) {
  TEST_ASSERT(my_pal_x11_parse_xft_dpi("Xft.dpi:\t192\nfoo: bar\n") == 192.0);
  TEST_ASSERT(my_pal_x11_parse_xft_dpi("foo: bar\nXft.dpi:\t144\n") == 144.0);
  TEST_ASSERT(my_pal_x11_parse_xft_dpi("foo: bar") == 0.0);
  TEST_ASSERT(my_pal_x11_parse_xft_dpi(NULL) == 0.0);
  /* rounding to nearest 0.25 step of dpi/96 */
  TEST_ASSERT(my_pal_x11_scale_from_xft_dpi(96.0) == 1.0f);
  TEST_ASSERT(my_pal_x11_scale_from_xft_dpi(108.0) == 1.25f);
  TEST_ASSERT(my_pal_x11_scale_from_xft_dpi(144.0) == 1.5f);
  TEST_ASSERT(my_pal_x11_scale_from_xft_dpi(192.0) == 2.0f);
  TEST_ASSERT(my_pal_x11_scale_from_xft_dpi(30.0) == 1.0f);  /* implausible */
  TEST_ASSERT(my_pal_x11_scale_from_xft_dpi(0.0) == 1.0f);
}
#endif /* MYUI_PAL_X11 */

static void test_dummy_scale_two_full_chain(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win;
  my_widget_t* label;
  my_lcd_t* lcd;
  uint8_t* buf;
  uint32_t stride, x, y;
  int32_t first_lit = -1;

  my_pal_dummy_set_scale_factor(pal, 2.0f);
  TEST_ASSERT(my_pal_get_scale_factor(pal) == 2.0f);

  win = my_window_create(NULL, pal, 400, 300, "hidpi");
  TEST_ASSERT(win->scale == 2.0f);
  {
    my_theme_t* theme = my_theme_create(NULL);
    my_theme_load_str(theme, "window.normal.bg_color=#000000\n");
    my_window_set_theme(win, theme, true);
  }

  /* physical lcd = logical * 2; widget rect stays logical */
  lcd = my_pal_window_get_lcd(win->pal_window);
  TEST_ASSERT_EQ_INT(my_lcd_get_width(lcd), 800);
  TEST_ASSERT_EQ_INT(my_lcd_get_height(lcd), 600);
  TEST_ASSERT_EQ_INT(((my_widget_t*)win)->rect.w, 400);

  /* label with "ab" centered (font 16 logical -> 32 device, bitmap) */
  my_window_set_font(win, my_font_bitmap_create(NULL), 16);
  my_widget_set_layouter(my_window_widget(win),
                         my_layouter_linear_create(NULL, false, 8));
  label = my_label_create(NULL, "ab");
  my_label_set_align(label, MY_TEXT_ALIGN_CENTER); /* M11d: LEFT default */
  my_widget_set_layout_params(label, "h:40");
  my_widget_add_child(my_window_widget(win), label);
  my_widget_unref(label);
  my_widget_invalidate(my_window_widget(win), NULL);
  my_window_paint(win);

  /* logical center x = (400 - 32)/2 = 184 -> device ~368: the first lit
   * glyph pixel on the label row must sit near 2x, not 1x */
  buf = my_lcd_mem_get_buffer(lcd);
  stride = my_lcd_mem_get_stride(lcd);
  for (y = 2; y < 38 && first_lit < 0; y++) {
    for (x = 0; x < 800; x++) {
      const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
      if (p[2] > 100) { /* bright glyph pixel */
        first_lit = (int32_t)x;
        break;
      }
    }
  }
  TEST_ASSERT(first_lit >= 360 && first_lit <= 384);

  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);
}

static void test_dummy_scale_one_passthrough(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win;
  my_lcd_t* lcd;
  TEST_ASSERT(my_pal_get_scale_factor(pal) == 1.0f);
  win = my_window_create(NULL, pal, 320, 240, "std");
  lcd = my_pal_window_get_lcd(win->pal_window);
  TEST_ASSERT_EQ_INT(my_lcd_get_width(lcd), 320); /* 1:1, no scaling */
  TEST_ASSERT_EQ_INT(my_lcd_get_height(lcd), 240);
  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);
}

MYTEST_MAIN_BEGIN()
#if defined(MYUI_PAL_X11)
  MYTEST_RUN(test_x11_dpi_parse);
#endif
  MYTEST_RUN(test_dummy_scale_two_full_chain);
  MYTEST_RUN(test_dummy_scale_one_passthrough);
MYTEST_MAIN_END()
