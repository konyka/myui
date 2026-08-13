/**
 * @file my_dialog_test.c
 * @brief Modal dialog tests (M13c): modal input blocking, button result
 * reporting, ESC cancel, scrim flag, leaks.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_dialog.h"

#include "mytest.h"

static int g_clicked;
static void on_main_click(void* ctx, const char* event, void* data) {
  (void)ctx;
  (void)event;
  (void)data;
  g_clicked++;
}

static int g_result = -999;
static void on_result(void* ctx, int32_t result) {
  (void)ctx;
  g_result = result;
}

typedef struct fx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* main_win;
  my_widget_t* main_btn;
} fx_t;

static void fx_init(fx_t* f) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  f->main_win = my_window_create(NULL, f->pal, 400, 300, "main");
  f->main_btn = my_button_create(NULL, "ok");
  my_widget_set_rect(f->main_btn, &(my_rect_t){10, 10, 80, 32});
  my_widget_on(f->main_btn, "click", on_main_click, NULL);
  my_widget_add_child(my_window_widget(f->main_win), f->main_btn);
  my_widget_unref(f->main_btn);
  my_window_manager_open(f->wm, f->main_win);
  my_widget_unref(my_window_widget(f->main_win));
  g_clicked = 0;
  g_result = -999;
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

/** @brief Send a click through the pal handler (full wm routing). */
static void click_via_pal(fx_t* f, my_window_t* win, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, win->pal_window, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, win->pal_window, &e);
}

/** @brief Run pending one-shot timers (dialog close is deferred one tick
 * so the window is never destroyed mid-dispatch). */
static void pump(fx_t* f) {
  my_pal_dummy_set_now_ms(f->pal, 10000);
  my_pal_main_loop_run(f->loop);
}

static void test_modal_blocks_lower_window(void) {
  fx_t f;
  my_dialog_t* dlg;
  fx_init(&f);
  dlg = my_dialog_create(NULL, f.pal, "confirm", 200, 120);
  my_dialog_add_button(dlg, "Yes", 1);
  my_dialog_open(dlg, f.wm, on_result, NULL);
  TEST_ASSERT(my_window_manager_top(f.wm) == dlg->win);
  TEST_ASSERT(f.main_win->scrim);

  /* click the main window's button through the pal/wm: blocked by the
   * modal dialog on top */
  click_via_pal(&f, f.main_win, 20, 20);
  TEST_ASSERT_EQ_INT(g_clicked, 0);

  my_dialog_close(dlg, 1);
  pump(&f); /* deferred close runs on the next loop tick */
  TEST_ASSERT_EQ_INT(g_result, 1);
  TEST_ASSERT(!f.main_win->scrim);
  TEST_ASSERT_EQ_INT(my_window_manager_count(f.wm), 1);

  /* after closing, the same click reaches the button again */
  click_via_pal(&f, f.main_win, 20, 20);
  TEST_ASSERT_EQ_INT(g_clicked, 1);
  my_dialog_destroy(dlg);
  fx_destroy(&f);
}

static void test_esc_cancels(void) {
  fx_t f;
  my_dialog_t* dlg;
  fx_init(&f);
  dlg = my_dialog_create(NULL, f.pal, "confirm", 200, 120);
  my_dialog_add_button(dlg, "Yes", 1);
  my_dialog_open(dlg, f.wm, on_result, NULL);
  {
    my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
    e.u.key.key = MY_KEY_ESCAPE;
    my_pal_dummy_inject_event(f.pal, dlg->win->pal_window, &e);
  }
  pump(&f);
  TEST_ASSERT_EQ_INT(g_result, MY_DIALOG_CANCEL);
  my_dialog_destroy(dlg);
  fx_destroy(&f);
}

static void test_button_click_reports(void) {
  fx_t f;
  my_dialog_t* dlg;
  fx_init(&f);
  dlg = my_dialog_create(NULL, f.pal, "confirm", 200, 120);
  my_dialog_add_button(dlg, "No", 2);
  my_dialog_open(dlg, f.wm, on_result, NULL);
  /* force layout (paint does relayout; the dummy loop is not running) */
  my_widget_invalidate(my_window_widget(dlg->win), NULL);
  my_window_paint(dlg->win);
  /* the button sits in the bottom row: h=40 row at y = 120-40 = 80,
   * button w:96 h:32 at x 0..96 */
  click_via_pal(&f, dlg->win, 10, 90);
  pump(&f);
  TEST_ASSERT_EQ_INT(g_result, 2);
  my_dialog_destroy(dlg);
  fx_destroy(&f);
}

static void test_unused_dialog_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_dialog_t* dlg = my_dialog_create(NULL, pal, "x", 100, 80);
  my_dialog_add_button(dlg, "ok", 0);
  my_dialog_destroy(dlg);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_modal_blocks_lower_window);
  MYTEST_RUN(test_esc_cancels);
  MYTEST_RUN(test_button_click_reports);
  MYTEST_RUN(test_unused_dialog_no_leak);
MYTEST_MAIN_END()
