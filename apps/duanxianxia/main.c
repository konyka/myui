/**
 * @file main.c
 * @brief dxx: duanxianxia.com homepage clone (M14b skeleton: topbar +
 * index strip + footer).
 *
 * Under the dummy port (headless): set MYUI_DEMO_DUMP_PPM=<path> to
 * paint one frame, dump it as PPM and exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dxx_data.h"
#include "dxx_theme.h"
#include "myui/my_window_manager.h"
#include "views/views.h"

#ifdef MYUI_PAL_DUMMY
#include "myr/my_lcd_mem.h"
#include "mypal/dummy/my_pal_dummy.h"
#endif

#define DXX_WIN_W 1320
#define DXX_WIN_H 900

/** @brief Font chain: DroidSansFallback covers CJK, LiberationSans the
 * Latin side (each lacks the other's glyphs; the chain routes per
 * codepoint). Falls back to the built-in 8x8 bitmap font. */
static my_font_t* create_app_font(void) {
  static const char* chain[] = {
      "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation-sans-fonts/LiberationSans-Regular.ttf"};
  my_font_t* font = my_font_stb_create_chain(NULL, chain, 3, 0);
  if (font == NULL) {
    font = my_font_bitmap_create(NULL);
  }
  return font;
}

#ifdef MYUI_PAL_DUMMY
static void dump_ppm(my_pal_window_t* window, const char* path) {
  my_lcd_t* lcd = my_pal_window_get_lcd(window);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  uint32_t w = my_lcd_get_width(lcd);
  uint32_t h = my_lcd_get_height(lcd);
  uint32_t x, y;
  FILE* f = fopen(path, "wb");
  if (f == NULL || buf == NULL) {
    return;
  }
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
      fputc(p[2], f);
      fputc(p[1], f);
      fputc(p[0], f);
    }
  }
  fclose(f);
  printf("dxx: dumped %s\n", path);
}
#endif

int main(void) {
  my_pal_t* pal = my_pal_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win;
  my_font_t* font;
  dxx_topbar_t topbar;
  my_widget_t* strip;
  my_widget_t* footer;
  if (pal == NULL || loop == NULL || wm == NULL) {
    fprintf(stderr, "dxx: init failed\n");
    return 1;
  }
  win = my_window_create(NULL, pal, DXX_WIN_W, DXX_WIN_H, "短线侠");
  my_window_set_theme(win, dxx_theme_create(NULL), true);
  font = create_app_font();
  my_window_set_font(win, font, 16);

  dxx_build_topbar(win, my_window_widget(win), &topbar);
  strip = dxx_build_index_strip(my_window_widget(win));
  my_widget_set_rect(strip, &(my_rect_t){10, DXX_TOPBAR_H + 8, DXX_WIN_W - 20, 64});
  footer = dxx_build_footer(my_window_widget(win));
  my_widget_set_rect(footer,
                     &(my_rect_t){10, DXX_WIN_H - 56, DXX_WIN_W - 20, 48});

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      my_widget_invalidate(my_window_widget(win), NULL);
      my_window_paint(win);
      dump_ppm(win->pal_window, dump);
      /* menu open state (复盘, 4 items) for visual inspection */
      my_menu_popup(win, topbar.menus[2], 290, 50, NULL, NULL);
      my_widget_invalidate(my_window_widget(win), NULL);
      my_window_paint(win);
      dump_ppm(win->pal_window, "/tmp/dxx_menu.ppm");
      dxx_topbar_destroy(&topbar);
      my_font_destroy(font);
      my_window_manager_destroy(wm);
      my_pal_main_loop_destroy(loop);
      my_pal_destroy(pal);
      return 0;
    }
  }
#endif

  my_pal_main_loop_run(loop);

  dxx_topbar_destroy(&topbar);
  my_font_destroy(font);
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  return 0;
}
