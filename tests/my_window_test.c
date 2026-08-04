/**
 * @file my_window_test.c
 * @brief Unit tests for my_window + my_window_manager (dummy PAL driven).
 */
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"

#include "mypal/dummy/my_pal_dummy.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_window_t* make_window(my_pal_t* pal, int32_t w, int32_t h) {
  return my_window_create(NULL, pal, w, h, "test");
}

static void test_window_paint_dirty_only(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = make_window(pal, 100, 80);
  my_widget_t* child = my_widget_create(NULL, "c");
  rec_vg_t rec;
  rec_vg_init(&rec);

  my_widget_set_rect(child, &(my_rect_t){10, 10, 20, 20});
  my_widget_add_child(my_window_widget(win), child);
  my_widget_unref(child);

  my_window_set_vgcanvas(win, (my_vgcanvas_t*)&rec);

  /* clean window: paint is a no-op */
  my_window_paint(win);
  TEST_ASSERT_EQ_INT(rec.n_ops, 0);

  /* invalidate a child: one clipped repaint pass over that dirty rect */
  my_widget_invalidate(child, NULL);
  my_window_paint(win);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "begin_frame"), 1);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "end_frame"), 1);
  TEST_ASSERT(rec_has(&rec, "clip 10 10 20 20"));

  /* dirty cleared: next paint is a no-op again */
  rec_vg_init(&rec);
  my_window_paint(win);
  TEST_ASSERT_EQ_INT(rec.n_ops, 0);

  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);
}

static void test_window_resize_updates_root(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = make_window(pal, 100, 80);
  my_event_t e = my_event_init(MY_EVENT_RESIZE);

  e.u.resize.w = 200;
  e.u.resize.h = 120;
  my_window_on_pal_event(win, &e);
  TEST_ASSERT_EQ_INT(my_window_widget(win)->rect.w, 200);
  TEST_ASSERT_EQ_INT(my_window_widget(win)->rect.h, 120);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&win->dirty), 0); /* painted already */

  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);
}

static void test_manager_open_close_stack(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* w1 = make_window(pal, 64, 64);
  my_window_t* w2 = make_window(pal, 64, 64);
  my_window_t* w3 = make_window(pal, 64, 64);

  my_window_manager_open(wm, w1);
  my_window_manager_open(wm, w2);
  my_window_manager_open(wm, w3);
  my_widget_unref(my_window_widget(w1));
  my_widget_unref(my_window_widget(w2));
  my_widget_unref(my_window_widget(w3));

  TEST_ASSERT_EQ_INT(my_window_manager_count(wm), 3);
  TEST_ASSERT(my_window_manager_top(wm) == w3);

  my_window_manager_back_to_home(wm);
  TEST_ASSERT_EQ_INT(my_window_manager_count(wm), 1);
  TEST_ASSERT(my_window_manager_top(wm) == w1);

  /* closing the last window requests quit */
  TEST_ASSERT(!wm->quit_requested);
  my_window_manager_close(wm, w1);
  TEST_ASSERT_EQ_INT(my_window_manager_count(wm), 0);
  TEST_ASSERT(wm->quit_requested);
  TEST_ASSERT_NULL(my_window_manager_top(wm));

  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void on_click_count(void* ctx, const char* event, void* data) {
  int* n = (int*)ctx;
  (void)event;
  (void)data;
  (*n)++;
}

static void test_manager_routes_quit_event(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win = make_window(pal, 64, 64);

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

  /* closing via the QUIT path: last window closed -> quit requested */
  TEST_ASSERT_EQ_INT(my_window_manager_close(wm, win), MY_RET_OK);
  TEST_ASSERT(wm->quit_requested);
  TEST_ASSERT_EQ_INT(my_window_manager_close(wm, win), MY_RET_NOT_FOUND);

  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_pointer_event_reaches_button(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win = make_window(pal, 100, 100);
  my_widget_t* btn = my_button_create(NULL, "ok");
  int clicks = 0;
  my_event_t e;

  my_widget_set_rect(btn, &(my_rect_t){10, 10, 60, 30});
  my_widget_add_child(my_window_widget(win), btn);
  my_widget_unref(btn);
  my_widget_on(btn, "click", on_click_count, &clicks);

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

  /* press inside the button then release inside: click fires */
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 20;
  e.u.pointer.y = 20;
  e.u.pointer.button = 1;
  my_window_on_pal_event(win, &e);

  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = 20;
  e.u.pointer.y = 20;
  e.u.pointer.button = 1;
  my_window_on_pal_event(win, &e);

  TEST_ASSERT_EQ_INT(clicks, 1);

  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_app_run_starved_loop_returns(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  /* dummy loop starves immediately: my_app_run must return cleanly */
  TEST_ASSERT_EQ_INT(my_app_run(pal, NULL, NULL), MY_RET_INVALID_PARAMS);
  my_pal_destroy(pal);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(dbg);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(dbg, pal, loop);
  my_window_t* win = my_window_create(dbg, pal, 80, 60, "leak");
  my_widget_t* btn = my_button_create(dbg, "x");

  my_widget_set_rect(btn, &(my_rect_t){5, 5, 30, 20});
  my_widget_add_child(my_window_widget(win), btn);
  my_widget_unref(btn);

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

  /* paint through the real soft backend on the dummy lcd */
  my_window_paint(win);
  {
    my_event_t e = my_event_init(MY_EVENT_RESIZE);
    e.u.resize.w = 100;
    e.u.resize.h = 90;
    my_window_on_pal_event(win, &e);
  }

  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_window_paint_dirty_only);
  MYTEST_RUN(test_window_resize_updates_root);
  MYTEST_RUN(test_manager_open_close_stack);
  MYTEST_RUN(test_manager_routes_quit_event);
  MYTEST_RUN(test_pointer_event_reaches_button);
  MYTEST_RUN(test_app_run_starved_loop_returns);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
