/**
 * @file dxx_panels_test.c
 * @brief duanxianxia clone tests (M14c): pool snapshot integrity, ztpool
 * table structure/heights, stock item paint colors, tooltip, stock card
 * dialog, nested scroll wheel routing.
 */
#include "dxx_data.h"
#include "dxx_theme.h"
#include "myc/my_str.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_scroll_view.h"
#include "views/stock_item.h"
#include "views/views.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

/* ---------------- data integrity ---------------- */

static void test_pool_data_integrity(void) {
  static const int COUNTS[DXX_POOL_ROW_COUNT] = {1, 1, 2, 12, 42, 72};
  static const char* const PROG[DXX_POOL_ROW_COUNT] = {
      "6进7", "4进5", "3进4", "2进3", "1进2", "首板"};
  int r;
  for (r = 0; r < DXX_POOL_ROW_COUNT; r++) {
    TEST_ASSERT_EQ_INT(DXX_POOL_ROWS[r].stock_count, COUNTS[r]);
    TEST_ASSERT(my_str_eq(DXX_POOL_ROWS[r].progress, PROG[r]));
  }
  /* spot checks */
  TEST_ASSERT(my_str_eq(DXX_POOL_ROWS[0].stocks[0].name, "百花医药"));
  TEST_ASSERT_EQ_INT(DXX_POOL_ROWS[0].stocks[0].market, DXX_MKT_SH);
  TEST_ASSERT_EQ_INT(DXX_POOL_ROWS[0].stocks[0].state, DXX_ST_SUCCESS);
  TEST_ASSERT(my_str_eq(DXX_POOL_ROWS[0].rate, "1/1=100%"));
  TEST_ASSERT(my_str_eq(DXX_POOL_ROWS[5].rate, "62/72=86%"));
  /* 浩淼科技: 北交所 + 无题材 */
  TEST_ASSERT(my_str_eq(DXX_POOL_ROWS[5].stocks[35].name, "浩淼科技"));
  TEST_ASSERT_EQ_INT(DXX_POOL_ROWS[5].stocks[35].market, DXX_MKT_BJ);
  TEST_ASSERT(DXX_POOL_ROWS[5].stocks[35].theme == NULL);
  /* last entry: 引力传媒 炸 */
  TEST_ASSERT(my_str_eq(DXX_POOL_ROWS[5].stocks[71].name, "引力传媒"));
  TEST_ASSERT_EQ_INT(DXX_POOL_ROWS[5].stocks[71].state, DXX_ST_BROKEN);
  /* 1进2 has exactly 6 成 (rate numerator) */
  {
    int i, succ = 0;
    for (i = 0; i < DXX_POOL_ROWS[4].stock_count; i++) {
      if (DXX_POOL_ROWS[4].stocks[i].state == DXX_ST_SUCCESS) {
        succ++;
      }
    }
    TEST_ASSERT_EQ_INT(succ, 6);
  }
}

/* ---------------- window fixture ---------------- */

typedef struct fx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
} fx_t;

static void fx_init(fx_t* f, int32_t w, int32_t h) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  f->win = my_window_create(NULL, f->pal, w, h, "dxx");
  my_window_set_theme(f->win, dxx_theme_create(NULL), true);
  my_window_manager_open(f->wm, f->win);
  my_widget_unref(my_window_widget(f->win));
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static void inject(fx_t* f, my_event_type_t type, int32_t x, int32_t y,
                   int32_t delta) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  e.u.pointer.delta = delta;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
}

/* ---------------- ztpool ---------------- */

static void test_ztpool_structure_and_heights(void) {
  fx_t f;
  my_widget_t* table;
  my_widget_t* row67;
  my_widget_t* row_first;
  fx_init(&f, 1320, 900);
  table = dxx_build_ztpool(f.wm, my_window_widget(f.win), 1300);
  TEST_ASSERT(table != NULL);
  /* header + 6 rows */
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(table), 7);
  row67 = my_widget_get_child(table, 1);
  row_first = my_widget_get_child(table, 6);
  /* row height adapts: single-stock row = 40, 首板 (72 items) much taller */
  TEST_ASSERT_EQ_INT(row67->rect.h, 40);
  TEST_ASSERT(row_first->rect.h > 300);
  /* rows stack without overlap */
  TEST_ASSERT_EQ_INT(row_first->rect.y + row_first->rect.h, table->rect.h);
  /* cell3 of 6进7 holds exactly 1 stock item */
  TEST_ASSERT_EQ_INT(
      (int)my_widget_child_count(my_widget_get_child(row67, 3)), 1);
  TEST_ASSERT_EQ_INT(
      (int)my_widget_child_count(my_widget_get_child(row_first, 3)), 72);
  fx_destroy(&f);
}

static void test_stock_item_paint_and_tooltip_text(void) {
  fx_t f;
  my_widget_t* it;
  rec_vg_t rec;
  static my_font_t* bmp = NULL;
  fx_init(&f, 1320, 900);
  it = dxx_stock_item_create(NULL, &DXX_POOL_ROWS[0].stocks[0], f.wm);
  my_widget_set_rect(it, &(my_rect_t){0, 0, dxx_stock_item_width(
                                                &DXX_POOL_ROWS[0].stocks[0]),
                                      DXX_STOCK_ITEM_HEIGHT});
  if (bmp == NULL) {
    bmp = my_font_bitmap_create(NULL);
  }
  rec_vg_init(&rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)&rec, bmp, 12);
  my_widget_paint(it, (my_vgcanvas_t*)&rec);
  /* 沪 badge #E64C62, name in rise red (成), theme text present */
  TEST_ASSERT(rec_has(&rec, "set_fill #e64c62"));
  TEST_ASSERT(rec_has(&rec, "set_fill #ff0000"));
  TEST_ASSERT(rec_has(&rec, "百花医药"));
  TEST_ASSERT(rec_has(&rec, "[+10.04%]"));
  TEST_ASSERT(rec_has(&rec, "CRO"));
  TEST_ASSERT(my_str_eq(my_widget_get_tooltip(it), "百花医药 成 +10.04% CRO"));
  my_widget_unref(it);
  fx_destroy(&f);
}

static void test_tooltip_shows_on_hover(void) {
  fx_t f;
  my_widget_t* table;
  my_widget_t* c3;
  my_widget_t* item;
  int32_t cx, cy;
  fx_init(&f, 1320, 900);
  table = dxx_build_ztpool(f.wm, my_window_widget(f.win), 1300);
  c3 = my_widget_get_child(my_widget_get_child(table, 1), 3);
  my_widget_relayout(my_window_widget(f.win)); /* flow assigns item rects */
  item = my_widget_get_child(c3, 0);
  cx = item->rect.w / 2;
  cy = item->rect.h / 2;
  my_widget_local_to_global(item, &cx, &cy);
  inject(&f, MY_EVENT_POINTER_MOVE, cx, cy, 0);
  my_pal_dummy_set_now_ms(f.pal, 700);
  my_pal_main_loop_run(f.loop);
  {
    my_widget_t* root = my_window_widget(f.win);
    my_widget_t* tip =
        my_widget_get_child(root, my_widget_child_count(root) - 1);
    TEST_ASSERT(my_str_eq(tip->base.name, "tooltip"));
    TEST_ASSERT(my_str_eq(tip->tooltip, "百花医药 成 +10.04% CRO"));
  }
  fx_destroy(&f);
}

static void test_stock_click_opens_dialog(void) {
  fx_t f;
  my_widget_t* table;
  my_widget_t* item;
  int32_t cx, cy;
  fx_init(&f, 1320, 900);
  table = dxx_build_ztpool(f.wm, my_window_widget(f.win), 1300);
  my_widget_relayout(my_window_widget(f.win)); /* flow assigns item rects */
  item = my_widget_get_child(
      my_widget_get_child(my_widget_get_child(table, 1), 3), 0);
  cx = item->rect.w / 2;
  cy = item->rect.h / 2;
  my_widget_local_to_global(item, &cx, &cy);
  TEST_ASSERT(my_window_manager_top(f.wm) == f.win);
  inject(&f, MY_EVENT_POINTER_DOWN, cx, cy, 0);
  inject(&f, MY_EVENT_POINTER_UP, cx, cy, 0);
  /* the stock card dialog is a new top window */
  TEST_ASSERT(my_window_manager_top(f.wm) != f.win);
  TEST_ASSERT(f.win->scrim); /* modal veil on the main window */
  fx_destroy(&f);
}

static void test_nested_wheel_inner_first(void) {
  fx_t f;
  my_scroll_view_t* outer = my_scroll_view_create(NULL);
  my_scroll_view_t* inner = my_scroll_view_create(NULL);
  my_widget_t* outer_content = my_widget_create(NULL, "oc");
  my_widget_t* inner_content = my_widget_create(NULL, "ic");
  fx_init(&f, 400, 300);
  my_widget_set_rect((my_widget_t*)outer, &(my_rect_t){0, 0, 400, 300});
  my_scroll_view_set_content(outer, outer_content);
  my_widget_unref(outer_content);
  my_scroll_view_set_content_height(outer, 1000);
  my_widget_add_child(my_window_widget(f.win), (my_widget_t*)outer);
  my_widget_unref((my_widget_t*)outer);
  /* inner panel inside the outer content */
  my_widget_set_rect((my_widget_t*)inner, &(my_rect_t){50, 50, 200, 100});
  my_scroll_view_set_content(inner, inner_content);
  my_widget_unref(inner_content);
  my_scroll_view_set_content_height(inner, 500); /* max = 400 */
  my_widget_add_child(outer_content, (my_widget_t*)inner);
  my_widget_unref((my_widget_t*)inner);
  /* wheel over the inner panel: inner scrolls, outer untouched */
  inject(&f, MY_EVENT_POINTER_WHEEL, 100, 100, -1);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(inner), 72);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(outer), 0);
  /* inner at its end: wheel bubbles to the outer page (standard nested
   * scrolling; the old "inner always eats" semantics froze the page over
   * any maxed-out panel) */
  my_scroll_view_set_offset(inner, 400);
  inject(&f, MY_EVENT_POINTER_WHEEL, 100, 100, -1);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(inner), 400);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(outer), 72);
  /* wheel up over outer-only area scrolls the page back */
  inject(&f, MY_EVENT_POINTER_WHEEL, 300, 250, 1);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(outer), 0);
  /* and down again */
  inject(&f, MY_EVENT_POINTER_WHEEL, 300, 250, -1);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(outer), 72);
  fx_destroy(&f);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_pool_data_integrity);
  MYTEST_RUN(test_ztpool_structure_and_heights);
  MYTEST_RUN(test_stock_item_paint_and_tooltip_text);
  MYTEST_RUN(test_tooltip_shows_on_hover);
  MYTEST_RUN(test_stock_click_opens_dialog);
  MYTEST_RUN(test_nested_wheel_inner_first);
MYTEST_MAIN_END()
