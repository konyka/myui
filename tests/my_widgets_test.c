/**
 * @file my_widgets_test.c
 * @brief Unit tests for the built-in button and label widgets.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_window.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_label.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_event_t pointer_ev(my_event_type_t type, int32_t x, int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  e.u.pointer.button = 1;
  return e;
}

static void on_click(void* ctx, const char* event, void* data) {
  int* n = (int*)ctx;
  (void)event;
  (void)data;
  (*n)++;
}

static my_widget_t* make_button(int32_t x, int32_t y, int32_t w, int32_t h) {
  my_widget_t* b = my_button_create(NULL, "OK");
  my_widget_set_rect(b, &(my_rect_t){x, y, w, h});
  return b;
}

static void test_button_press_release_click(void) {
  my_widget_t* b = make_button(10, 10, 80, 30);
  my_button_t* btn = (my_button_t*)b;
  int clicks = 0;
  my_event_t e;

  my_widget_on(b, "click", on_click, &clicks);
  TEST_ASSERT(b->focusable);

  e = pointer_ev(MY_EVENT_POINTER_DOWN, 20, 20);
  TEST_ASSERT_EQ_INT(b->vtable->on_event(b, &e), MY_RET_OK);
  TEST_ASSERT(btn->pressed);

  e = pointer_ev(MY_EVENT_POINTER_UP, 20, 20);
  TEST_ASSERT_EQ_INT(b->vtable->on_event(b, &e), MY_RET_OK);
  TEST_ASSERT(!btn->pressed);
  TEST_ASSERT_EQ_INT(clicks, 1);

  my_widget_unref(b);
}

static void test_button_release_outside_no_click(void) {
  my_widget_t* b = make_button(10, 10, 80, 30);
  int clicks = 0;
  my_event_t e;

  my_widget_on(b, "click", on_click, &clicks);

  e = pointer_ev(MY_EVENT_POINTER_DOWN, 20, 20);
  b->vtable->on_event(b, &e);
  e = pointer_ev(MY_EVENT_POINTER_UP, 500, 500); /* outside */
  b->vtable->on_event(b, &e);
  TEST_ASSERT_EQ_INT(clicks, 0);
  TEST_ASSERT(!((my_button_t*)b)->pressed);

  my_widget_unref(b);
}

static void test_button_up_without_down_ignored(void) {
  my_widget_t* b = make_button(0, 0, 50, 20);
  my_event_t e = pointer_ev(MY_EVENT_POINTER_UP, 5, 5);
  TEST_ASSERT_EQ_INT(b->vtable->on_event(b, &e), MY_RET_FAIL);
  my_widget_unref(b);
}

static void test_button_paint_states(void) {
  my_widget_t* b = make_button(0, 0, 80, 30);
  my_button_t* btn = (my_button_t*)b;
  rec_vg_t rec;
  rec_vg_init(&rec);

  btn->color_normal = my_color_rgb(10, 20, 30);
  my_widget_paint(b, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "set_fill #0a141e")); /* normal */

  rec_vg_init(&rec);
  btn->pressed = true;
  my_widget_paint(b, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(!rec_has(&rec, "set_fill #0a141e"));

  rec_vg_init(&rec);
  b->enable = false;
  my_widget_paint(b, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "stroke_rect")); /* border always drawn */

  my_widget_unref(b);
}

static void test_button_set_text(void) {
  my_widget_t* b = make_button(0, 0, 10, 10);
  TEST_ASSERT_EQ_STR(((my_button_t*)b)->text, "OK");
  TEST_ASSERT_EQ_INT(my_button_set_text(b, "Cancel"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(((my_button_t*)b)->text, "Cancel");
  TEST_ASSERT_EQ_INT(my_button_set_text(NULL, "x"), MY_RET_INVALID_PARAMS);
  my_widget_unref(b);
}

static void test_label_paint(void) {
  my_widget_t* l = my_label_create(NULL, "hello");
  rec_vg_t rec;
  rec_vg_init(&rec);

  TEST_ASSERT(!l->enable); /* labels are non-interactive */
  my_widget_set_rect(l, &(my_rect_t){0, 0, 100, 20});
  my_widget_paint(l, (my_vgcanvas_t*)&rec);

  TEST_ASSERT_EQ_INT(rec_count(&rec, "fill_rect"), 2); /* bg + text bar */
  my_widget_unref(l);
}

static void test_label_no_text(void) {
  my_widget_t* l = my_label_create(NULL, NULL);
  rec_vg_t rec;
  rec_vg_init(&rec);
  my_widget_set_rect(l, &(my_rect_t){0, 0, 100, 20});
  my_widget_paint(l, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "fill_rect"), 1); /* bg only */
  my_widget_unref(l);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* b = my_button_create(dbg, "leak check");
  my_widget_t* l = my_label_create(dbg, "leak check");
  int clicks = 0;

  my_widget_set_rect(b, &(my_rect_t){0, 0, 50, 20});
  my_widget_on(b, "click", on_click, &clicks);
  my_button_set_text(b, "renamed");
  {
    my_event_t e = pointer_ev(MY_EVENT_POINTER_DOWN, 5, 5);
    b->vtable->on_event(b, &e);
    e = my_event_init(MY_EVENT_POINTER_UP);
    e.u.pointer.x = 5;
    e.u.pointer.y = 5;
    b->vtable->on_event(b, &e);
  }
  TEST_ASSERT_EQ_INT(clicks, 1);

  my_widget_unref(b);
  my_widget_unref(l);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

static void test_button_min_press_display(void) {
  /* M16: a quick click keeps the pressed visual until the 120ms minimum,
   * driven by the loop timer; framework-level regression for frame
   * coalescing eating the pressed frame */
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win = my_window_create(NULL, pal, 200, 100, "t");
  my_widget_t* b = my_button_create(NULL, "OK");
  my_button_t* btn = (my_button_t*)b;
  my_event_t e;
  my_widget_set_rect(b, &(my_rect_t){10, 10, 80, 30});
  my_widget_add_child(my_window_widget(win), b);
  my_widget_unref(b);
  my_window_manager_open(wm, win); /* sets win->loop (min-press timer) */

  e = pointer_ev(MY_EVENT_POINTER_DOWN, 20, 20);
  my_window_on_pal_event(win, &e);
  TEST_ASSERT(btn->pressed);
  e = pointer_ev(MY_EVENT_POINTER_UP, 20, 20);
  my_window_on_pal_event(win, &e);
  TEST_ASSERT(btn->pressed); /* still pressed: min display time */

  my_pal_dummy_set_now_ms(pal, 200);
  my_pal_main_loop_run(loop);
  TEST_ASSERT(!btn->pressed); /* released after the delay */

  my_widget_unref(my_window_widget(win));
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_button_press_release_click);
  MYTEST_RUN(test_button_release_outside_no_click);
  MYTEST_RUN(test_button_up_without_down_ignored);
  MYTEST_RUN(test_button_paint_states);
  MYTEST_RUN(test_button_set_text);
  MYTEST_RUN(test_button_min_press_display);
  MYTEST_RUN(test_label_paint);
  MYTEST_RUN(test_label_no_text);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
