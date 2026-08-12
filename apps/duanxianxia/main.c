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
#include "myui/widgets/my_scroll_bar.h"
#include "myui/widgets/my_scroll_view.h"
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
  my_scroll_view_t* page_sv;
  my_widget_t* page;
  my_widget_t* bar;
  dxx_topbar_t topbar;
  my_widget_t* strip;
  my_widget_t* ztpool;
  my_widget_t* footer;
  int32_t y;
  if (pal == NULL || loop == NULL || wm == NULL) {
    fprintf(stderr, "dxx: init failed\n");
    return 1;
  }
  win = my_window_create(NULL, pal, DXX_WIN_W, DXX_WIN_H, "短线侠");
  my_window_set_theme(win, dxx_theme_create(NULL), true);
  font = create_app_font();
  my_window_set_font(win, font, 16);

  /* whole page scrolls (content ~2600px > window): page scroll_view +
   * a right-edge scroll_bar; the live cards' inner scroll_views consume
   * the wheel first, so nested scrolling works */
  page_sv = my_scroll_view_create(NULL);
  my_widget_set_rect((my_widget_t*)page_sv,
                     &(my_rect_t){0, 0, DXX_WIN_W - 14, DXX_WIN_H});
  page = my_widget_create(NULL, "dxx_page");
  my_scroll_view_set_content(page_sv, page);
  my_widget_unref(page);
  my_widget_add_child(my_window_widget(win), (my_widget_t*)page_sv);
  my_widget_unref((my_widget_t*)page_sv);
  bar = my_scroll_bar_create(NULL);
  my_widget_set_rect(bar, &(my_rect_t){DXX_WIN_W - 14, 0, 14, DXX_WIN_H});
  my_widget_add_child(my_window_widget(win), bar);
  my_widget_unref(bar);
  my_scroll_view_set_scroll_bar(page_sv, bar);

  dxx_build_topbar(win, page, &topbar);
  strip = dxx_build_index_strip(page);
  my_widget_set_rect(strip, &(my_rect_t){10, DXX_TOPBAR_H + 8, 1300, 64});
  y = DXX_TOPBAR_H + 8 + 64 + 12;
  y += dxx_build_live_area(page, 10, y, 1300) + 12;
  ztpool = dxx_build_ztpool(wm, page, 1300);
  my_widget_set_rect(ztpool, &(my_rect_t){10, y, 1300, ztpool->rect.h});
  y += ztpool->rect.h + 12;
  footer = dxx_build_footer(page);
  my_widget_set_rect(footer, &(my_rect_t){10, y, 1300, 48});
  y += 48 + 8;
  my_scroll_view_set_content_height(page_sv, y);

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      /* find the first stock item of the 首板 row (deep in the table) */
      my_widget_t* first_stock = NULL;
      {
        my_widget_t* row = my_widget_get_child(ztpool, 1 + 5); /* 首板 */
        my_widget_t* c3 = my_widget_get_child(row, 3); /* c3bg,c1,c2,c3 */
        first_stock = my_widget_get_child(c3, 0);
      }
      my_widget_invalidate(my_window_widget(win), NULL);
      my_window_paint(win);
      dump_ppm(win->pal_window, dump);
      /* menu open state (复盘, 4 items) */
      my_menu_popup(win, topbar.menus[2], 290, 50, NULL, NULL);
      my_widget_invalidate(my_window_widget(win), NULL);
      my_window_paint(win);
      dump_ppm(win->pal_window, "/tmp/dxx_menu.ppm");
      my_menu_dismiss(topbar.menus[2]);
      /* scrolled to the pool table */
      my_scroll_view_set_offset(page_sv, ztpool->rect.y - 60);
      my_window_paint(win);
      dump_ppm(win->pal_window, "/tmp/dxx_pool.ppm");
      /* 首板 row top */
      my_scroll_view_set_offset(
          page_sv, ztpool->rect.y +
                       my_widget_get_child(ztpool, 1 + 5)->rect.y - 80);
      my_window_paint(win);
      dump_ppm(win->pal_window, "/tmp/dxx_first_board.ppm");
      /* tooltip on the first 首板 stock (500ms fake-clock hover) */
      {
        my_event_t e;
        int32_t cx = first_stock->rect.w / 2, cy = first_stock->rect.h / 2;
        my_widget_local_to_global(first_stock, &cx, &cy);
        e = my_event_init(MY_EVENT_POINTER_MOVE);
        e.u.pointer.x = cx;
        e.u.pointer.y = cy;
        my_window_on_pal_event(win, &e);
        my_pal_dummy_set_now_ms(pal, 700);
        my_pal_main_loop_run(loop);
        my_window_paint(win);
        dump_ppm(win->pal_window, "/tmp/dxx_tooltip.ppm");
        /* click -> stock card dialog (separate window) */
        e = my_event_init(MY_EVENT_POINTER_DOWN);
        e.u.pointer.x = cx;
        e.u.pointer.y = cy;
        my_window_on_pal_event(win, &e);
        e = my_event_init(MY_EVENT_POINTER_UP);
        e.u.pointer.x = cx;
        e.u.pointer.y = cy;
        my_window_on_pal_event(win, &e);
        if (my_window_manager_top(wm) != win) {
          my_window_t* top = my_window_manager_top(wm);
          my_widget_invalidate(my_window_widget(top), NULL);
          my_window_paint(top);
          dump_ppm(top->pal_window, "/tmp/dxx_dialog.ppm");
        }
      }
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
