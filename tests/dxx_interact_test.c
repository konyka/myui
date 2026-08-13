/**
 * @file dxx_interact_test.c
 * @brief duanxianxia clone interaction tests (M14d): navigation callback
 * + active highlight, placeholder roundtrip, login dialog MVVM (TwoWay
 * write-back + validator + command error path), share PNG export.
 */
#include <stdio.h>
#include <string.h>

#include "dxx_data.h"
#include "dxx_theme.h"
#include "myc/my_str.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_label.h"
#include "views/auth.h"
#include "views/placeholder.h"
#include "views/views.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

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

static void inject_at(my_window_t* w, my_event_type_t type, int32_t x,
                      int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_window_on_pal_event(w, &e);
}

static void click_widget(my_window_t* w, my_widget_t* widget) {
  int32_t cx = widget->rect.w / 2, cy = widget->rect.h / 2;
  my_widget_local_to_global(widget, &cx, &cy);
  inject_at(w, MY_EVENT_POINTER_DOWN, cx, cy);
  inject_at(w, MY_EVENT_POINTER_UP, cx, cy);
}

/* Dialog close is deferred to the next main-loop tick (destroying the
 * window synchronously would free the widget tree mid-dispatch). */
static void pump(fx_t* f) {
  my_pal_dummy_set_now_ms(f->pal, 10000);
  my_pal_main_loop_run(f->loop);
}

static void type_text(my_window_t* w, const char* text) {
  const char* p;
  for (p = text; *p != '\0'; p++) {
    my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
    e.u.key.key = (uint8_t)*p;
    my_window_on_pal_event(w, &e);
  }
}

/* ---------------- navigation ---------------- */

static char g_nav[64];
static void nav_capture(void* ctx, const char* name) {
  (void)ctx;
  snprintf(g_nav, sizeof(g_nav), "%s", name);
}

static void test_nav_flat_item_and_active(void) {
  fx_t f;
  dxx_topbar_t tb;
  rec_vg_t rec;
  int i;
  fx_init(&f, 1320, 900);
  dxx_build_topbar(f.win, my_window_widget(f.win), &tb);
  dxx_topbar_set_nav_handler(&tb, nav_capture, NULL);
  /* find the 龙虎榜 flat trigger */
  for (i = 0; i < 16 && tb.triggers[i].anchor != NULL; i++) {
    if (strcmp(tb.triggers[i].log_name, "龙虎榜") == 0) {
      break;
    }
  }
  TEST_ASSERT(i < 16);
  g_nav[0] = '\0';
  click_widget(f.win, tb.triggers[i].anchor);
  TEST_ASSERT_EQ_STR(g_nav, "龙虎榜");
  /* active highlight: the anchor paints its text in primary #E64C62 */
  rec_vg_init(&rec);
  my_widget_paint(tb.triggers[i].anchor, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "set_fill #e64c62"));
  /* navigating elsewhere reverts it */
  dxx_topbar_set_active(&tb, "涨停表现");
  rec_vg_init(&rec);
  my_widget_paint(tb.triggers[i].anchor, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(!rec_has(&rec, "set_fill #e64c62"));
  dxx_topbar_destroy(&tb);
  fx_destroy(&f);
}

static void test_nav_menu_item(void) {
  fx_t f;
  dxx_topbar_t tb;
  my_widget_t* root;
  fx_init(&f, 1320, 900);
  dxx_build_topbar(f.win, my_window_widget(f.win), &tb);
  dxx_topbar_set_nav_handler(&tb, nav_capture, NULL);
  root = my_window_widget(f.win);
  /* open 竞价 (trigger slot 1: slot 0 is the logo) */
  TEST_ASSERT_EQ_INT(tb.triggers[1].menu_index, 0);
  g_nav[0] = '\0';
  click_widget(f.win, tb.triggers[1].anchor);
  /* overlay = last root child; click the first item (竞价异动) */
  {
    my_widget_t* ov = my_widget_get_child(root, my_widget_child_count(root) - 1);
    my_widget_t* box = my_widget_get_child(ov, 0);
    my_widget_t* item0 = my_widget_get_child(box, 0);
    click_widget(f.win, item0);
  }
  TEST_ASSERT_EQ_STR(g_nav, "竞价异动");
  /* the 竞价▼ group title is highlighted after selecting its item */
  {
    rec_vg_t rec;
    rec_vg_init(&rec);
    my_widget_paint(tb.triggers[1].anchor, (my_vgcanvas_t*)&rec);
    TEST_ASSERT(rec_has(&rec, "set_fill #e64c62"));
  }
  /* logo navigates 首页 */
  g_nav[0] = '\0';
  click_widget(f.win, tb.triggers[0].anchor);
  TEST_ASSERT_EQ_STR(g_nav, "首页");
  dxx_topbar_destroy(&tb);
  fx_destroy(&f);
}

/* ---------------- placeholder ---------------- */

static int g_back;
static void on_back(void* ctx) {
  (void)ctx;
  g_back++;
}

static void test_placeholder_roundtrip(void) {
  my_widget_t* ph = dxx_placeholder_create(NULL, on_back, NULL);
  my_label_t* title;
  my_widget_t* back;
  g_back = 0;
  my_widget_set_rect(ph, &(my_rect_t){0, 0, 1320, 850});
  dxx_placeholder_set_title(ph, "竞价异动");
  title = (my_label_t*)my_widget_get_child(ph, 0);
  TEST_ASSERT(my_str_eq(title->text, "竞价异动"));
  /* back button = child 2 */
  back = my_widget_get_child(ph, 2);
  {
    my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
    my_event_dispatcher_t d;
    my_event_dispatcher_init(&d, ph);
    e.u.pointer.x = back->rect.x + 5;
    e.u.pointer.y = back->rect.y + 5;
    my_event_dispatch(&d, &e);
    e = my_event_init(MY_EVENT_POINTER_UP);
    e.u.pointer.x = back->rect.x + 5;
    e.u.pointer.y = back->rect.y + 5;
    my_event_dispatch(&d, &e);
  }
  TEST_ASSERT_EQ_INT(g_back, 1);
  my_widget_unref(ph);
}

/* ---------------- auth dialog (MVVM) ---------------- */

/** @brief Dig out dialog parts: content = root child 0. */
static my_widget_t* dlg_content(my_window_t* dlg_win) {
  return my_widget_get_child(my_window_widget(dlg_win), 0);
}

/** @brief Force layout (dialog children start 0x0 until a paint). */
static void dlg_layout(my_window_t* dlg_win) {
  my_widget_invalidate(my_window_widget(dlg_win), NULL);
  my_window_paint(dlg_win);
}

static void test_login_dialog_validation(void) {
  fx_t f;
  my_window_t* dw;
  my_widget_t* content;
  my_label_t* err;
  fx_init(&f, 1320, 900);
  dxx_show_auth_dialog(f.wm, false);
  dw = my_window_manager_top(f.wm);
  TEST_ASSERT(dw != f.win);
  dlg_layout(dw);
  content = dlg_content(dw);
  /* children: user edit, password edit, error label, submit button */
  err = (my_label_t*)my_widget_get_child(content, 2);
  TEST_ASSERT(my_str_eq(err->text, ""));
  /* empty submit -> 用户名不能为空 (command ran, dialog stays open) */
  click_widget(dw, my_widget_get_child(content, 3));
  TEST_ASSERT(my_str_eq(err->text, "用户名不能为空"));
  TEST_ASSERT(my_window_manager_top(f.wm) == dw);
  /* type into the user edit (click focuses; keys write back via TwoWay) */
  click_widget(dw, my_widget_get_child(content, 0));
  type_text(dw, "alice");
  click_widget(dw, my_widget_get_child(content, 3));
  TEST_ASSERT(my_str_eq(err->text, "密码不能为空"));
  /* cancel closes */
  {
    my_widget_t* btn_row = my_widget_get_child(my_window_widget(dw), 1);
    click_widget(dw, my_widget_get_child(btn_row, 0));
  }
  pump(&f); /* dialog close is deferred to the next loop tick */
  TEST_ASSERT(my_window_manager_top(f.wm) == f.win);
  fx_destroy(&f);
}

static void test_register_dialog_has_confirm(void) {
  fx_t f;
  my_window_t* dw;
  fx_init(&f, 1320, 900);
  dxx_show_auth_dialog(f.wm, true);
  dw = my_window_manager_top(f.wm);
  dlg_layout(dw);
  /* user, password, confirm, error, submit = 5 content children */
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(dlg_content(dw)), 5);
  TEST_ASSERT_EQ_INT(dw->base.rect.h, 320);
  /* cancel */
  {
    my_widget_t* btn_row = my_widget_get_child(my_window_widget(dw), 1);
    click_widget(dw, my_widget_get_child(btn_row, 0));
  }
  pump(&f); /* dialog close is deferred to the next loop tick */
  TEST_ASSERT(my_window_manager_top(f.wm) == f.win);
  fx_destroy(&f);
}

/* ---------------- share export ---------------- */

static void test_share_writes_png(void) {
  fx_t f;
  my_widget_t* table;
  const char* path = "/tmp/dxx_test_share.png";
  FILE* fp;
  long size;
  fx_init(&f, 1320, 900);
  table = dxx_build_ztpool(f.wm, my_window_widget(f.win), 1300);
  dxx_ztpool_set_share_path(table, path);
  remove(path);
  {
    my_widget_t* hdr = my_widget_get_child(table, 0);
    my_widget_t* share = my_widget_get_child(hdr, 3);
    click_widget(f.win, share);
  }
  fp = fopen(path, "rb");
  TEST_ASSERT(fp != NULL);
  size = 0;
  if (fp != NULL) {
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
  }
  TEST_ASSERT(size > 1000); /* a real PNG, not an empty stub */
  /* dialog opened with the file name; close it */
  TEST_ASSERT(my_window_manager_top(f.wm) != f.win);
  {
    my_window_t* dw = my_window_manager_top(f.wm);
    my_widget_t* btn_row;
    dlg_layout(dw);
    btn_row = my_widget_get_child(my_window_widget(dw), 1);
    click_widget(dw, my_widget_get_child(btn_row, 0));
  }
  pump(&f); /* dialog close is deferred to the next loop tick */
  TEST_ASSERT(my_window_manager_top(f.wm) == f.win);
  remove(path);
  fx_destroy(&f);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_nav_flat_item_and_active);
  MYTEST_RUN(test_nav_menu_item);
  MYTEST_RUN(test_placeholder_roundtrip);
  MYTEST_RUN(test_login_dialog_validation);
  MYTEST_RUN(test_register_dialog_has_confirm);
  MYTEST_RUN(test_share_writes_png);
MYTEST_MAIN_END()
