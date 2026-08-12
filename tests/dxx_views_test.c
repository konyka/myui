/**
 * @file dxx_views_test.c
 * @brief duanxianxia clone view tests (M14b): topbar bg/menu interaction,
 * index strip columns and rise/fall coloring, footer text, font chain.
 */
#include "dxx_data.h"
#include "dxx_theme.h"
#include "myc/my_str.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_label.h"
#include "views/views.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

typedef struct fx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
} fx_t;

static void fx_init(fx_t* f) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  f->win = my_window_create(NULL, f->pal, 1320, 900, "dxx");
  my_window_set_theme(f->win, dxx_theme_create(NULL), true);
  my_window_manager_open(f->wm, f->win);
  my_widget_unref(my_window_widget(f->win));
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static void click(fx_t* f, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
}

static size_t root_children(fx_t* f) {
  return my_widget_child_count(my_window_widget(f->win));
}

static void test_topbar_bg_and_children(void) {
  fx_t f;
  dxx_topbar_t tb;
  rec_vg_t rec;
  fx_init(&f);
  dxx_build_topbar(f.win, my_window_widget(f.win), &tb);
  TEST_ASSERT(tb.bar != NULL);
  TEST_ASSERT_EQ_INT(tb.bar->rect.h, 50);
  TEST_ASSERT(tb.bar->rect.w == 1320);
  /* 12 interactive items + 9 dividers + logo = 22 children */
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(tb.bar), 22);
  rec_vg_init(&rec);
  my_widget_paint(tb.bar, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "set_fill #444444"));
  dxx_topbar_destroy(&tb);
  fx_destroy(&f);
}

static void test_dropdown_opens_menu(void) {
  fx_t f;
  dxx_topbar_t tb;
  my_widget_t* anchor;
  int32_t cx, cy;
  size_t before;
  fx_init(&f);
  dxx_build_topbar(f.win, my_window_widget(f.win), &tb);
  before = root_children(&f);
  /* triggers[0] = logo (首页); trigger 1 = 竞价 dropdown (2 items) */
  anchor = tb.triggers[1].anchor;
  TEST_ASSERT(anchor != NULL);
  TEST_ASSERT_EQ_INT(tb.triggers[1].menu_index, 0);
  cx = anchor->rect.w / 2;
  cy = anchor->rect.h / 2;
  my_widget_local_to_global(anchor, &cx, &cy);
  click(&f, cx, cy);
  TEST_ASSERT_EQ_INT((int)root_children(&f), (int)before + 1); /* overlay */
  {
    my_widget_t* root = my_window_widget(f.win);
    my_widget_t* ov = my_widget_get_child(root, root_children(&f) - 1);
    my_widget_t* box = my_widget_get_child(ov, 0);
    TEST_ASSERT_EQ_INT((int)my_widget_child_count(box), 2); /* 竞价异动/强度 */
  }
  click(&f, 600, 500); /* outside: dismiss */
  TEST_ASSERT_EQ_INT((int)root_children(&f), (int)before);
  dxx_topbar_destroy(&tb);
  fx_destroy(&f);
}

static void test_index_strip_columns(void) {
  fx_t f;
  my_widget_t* strip;
  rec_vg_t rec;
  static my_font_t* bmp = NULL;
  fx_init(&f);
  strip = dxx_build_index_strip(my_window_widget(f.win));
  my_widget_set_rect(strip, &(my_rect_t){10, 58, 1300, 64});
  my_widget_relayout(strip);
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(strip), 12);
  /* equal columns: 1300/12 = 108, col1 x = 108 */
  TEST_ASSERT_EQ_INT(my_widget_get_child(strip, 1)->rect.x, 108);
  if (bmp == NULL) {
    bmp = my_font_bitmap_create(NULL);
  }
  rec_vg_init(&rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)&rec, bmp, 12);
  my_widget_paint(my_widget_get_child(strip, 0), (my_vgcanvas_t*)&rec);
  /* 上证指数 +0.21% -> value+pct in rise red (#ff0000), fake bold double */
  TEST_ASSERT(rec_has(&rec, "上证指数"));
  TEST_ASSERT(rec_has(&rec, "3942.37"));
  TEST_ASSERT(rec_has(&rec, "+0.21%"));
  TEST_ASSERT(rec_has(&rec, "set_fill #ff0000"));
  TEST_ASSERT(!rec_has(&rec, "set_fill #008000"));
  rec_vg_init(&rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)&rec, bmp, 12);
  my_widget_paint(my_widget_get_child(strip, 3), (my_vgcanvas_t*)&rec);
  /* 恒生指数 -0.42% -> fall green (#008000) */
  TEST_ASSERT(rec_has(&rec, "恒生指数"));
  TEST_ASSERT(rec_has(&rec, "set_fill #008000"));
  fx_destroy(&f);
}

static void test_footer_text(void) {
  fx_t f;
  my_widget_t* footer;
  fx_init(&f);
  footer = dxx_build_footer(my_window_widget(f.win));
  TEST_ASSERT(footer != NULL);
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(footer), 2);
  TEST_ASSERT(my_str_eq(((my_label_t*)my_widget_get_child(footer, 0))->text,
                        DXX_FOOTER_DISCLAIMER));
  TEST_ASSERT(my_str_eq(((my_label_t*)my_widget_get_child(footer, 1))->text,
                        DXX_FOOTER_ICP));
  fx_destroy(&f);
}

static void test_font_chain_routes_latin_and_cjk(void) {
#ifdef MYUI_FONT_STB
  static const char* paths[] = {
      "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf"};
  my_font_t* f;
  my_glyph_t g;
  int32_t w = 0, h = 0;
  FILE* probe = fopen(paths[0], "rb");
  if (probe == NULL) {
    fprintf(stdout, "SKIP: system fonts not installed\n");
    return;
  }
  fclose(probe);
  f = my_font_stb_create_chain(NULL, paths, 2, 0);
  TEST_ASSERT(f != NULL);
  /* Latin routed to Liberation (nonzero advance), CJK to Droid */
  TEST_ASSERT_EQ_INT(my_font_get_glyph(f, '1', 16, &g), MY_RET_OK);
  TEST_ASSERT(g.advance > 0);
  TEST_ASSERT(g.bitmap != NULL); /* real glyph, not the blank fallback */
  TEST_ASSERT_EQ_INT(my_font_get_glyph(f, 0x4E2D, 16, &g), MY_RET_OK);
  TEST_ASSERT(g.advance > 0);
  TEST_ASSERT(g.bitmap != NULL);
  TEST_ASSERT_EQ_INT(my_font_measure(f, "A中1", 16, &w, &h), MY_RET_OK);
  TEST_ASSERT(w > 16); /* more than one CJK width: all three contribute */
  TEST_ASSERT(h > 0);
  my_font_destroy(f);
#else
  fprintf(stdout, "SKIP: MYUI_FONT_STB off\n");
#endif
}

static void test_font_chain_skips_unloadable(void) {
#ifdef MYUI_FONT_STB
  static const char* paths[] = {"/nonexistent/none.ttf",
                                "/usr/share/fonts/liberation-sans-fonts/"
                                "LiberationSans-Regular.ttf"};
  my_font_t* f;
  FILE* probe = fopen(paths[1], "rb");
  if (probe == NULL) {
    fprintf(stdout, "SKIP: LiberationSans not installed\n");
    return;
  }
  fclose(probe);
  f = my_font_stb_create_chain(NULL, paths, 2, 0);
  TEST_ASSERT(f != NULL); /* first face skipped, second loads */
  my_font_destroy(f);
  f = my_font_stb_create_chain(NULL, paths, 1, 0);
  TEST_ASSERT(f == NULL); /* all unloadable -> NULL */
#else
  fprintf(stdout, "SKIP: MYUI_FONT_STB off\n");
#endif
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_topbar_bg_and_children);
  MYTEST_RUN(test_dropdown_opens_menu);
  MYTEST_RUN(test_index_strip_columns);
  MYTEST_RUN(test_footer_text);
  MYTEST_RUN(test_font_chain_routes_latin_and_cjk);
  MYTEST_RUN(test_font_chain_skips_unloadable);
MYTEST_MAIN_END()
