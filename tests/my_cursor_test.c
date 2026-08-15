/**
 * @file my_cursor_test.c
 * @brief Hover-driven mouse cursor tests (M21a): the dispatcher applies
 * the cursor class of the hover target through the PAL window
 * `set_cursor` slot (dummy records it) — edit/text_area -> TEXT, other
 * focusable widgets -> HAND, everything else -> ARROW; leaving a widget
 * restores ARROW; window-less trees and NULL vtable slots skip silently.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_event_dispatch.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_text_area.h"

#include <string.h>

#include "mytest.h"

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
  f->win = my_window_create(NULL, f->pal, 800, 600, "t");
  my_window_manager_open(f->wm, f->win);
  my_widget_unref(my_window_widget(f->win));
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static my_cursor_t cur(fx_t* f) {
  return my_pal_dummy_get_cursor(f->win->pal_window);
}

static void move(fx_t* f, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_window_on_pal_event(f->win, &e);
}

/* layout: edit (10,10,200,30) / text_area (10,60,200,80) /
 * button (10,160,120,32); the rest is empty window space */
static void populate(fx_t* f) {
  my_widget_t* edit = my_edit_create(NULL);
  my_widget_t* area = my_text_area_create(NULL);
  my_widget_t* btn = my_button_create(NULL, "确定");
  my_widget_set_rect(edit, &(my_rect_t){10, 10, 200, 30});
  my_widget_set_rect(area, &(my_rect_t){10, 60, 200, 80});
  my_widget_set_rect(btn, &(my_rect_t){10, 160, 120, 32});
  my_widget_add_child(my_window_widget(f->win), edit);
  my_widget_add_child(my_window_widget(f->win), area);
  my_widget_add_child(my_window_widget(f->win), btn);
  my_widget_unref(edit);
  my_widget_unref(area);
  my_widget_unref(btn);
}

static void test_hover_classes(void) {
  fx_t f;
  fx_init(&f);
  populate(&f);
  TEST_ASSERT(cur(&f) == MY_CURSOR_ARROW); /* dummy starts at ARROW */
  move(&f, 20, 20);                        /* edit */
  TEST_ASSERT(cur(&f) == MY_CURSOR_TEXT);
  move(&f, 20, 100); /* text_area */
  TEST_ASSERT(cur(&f) == MY_CURSOR_TEXT);
  move(&f, 20, 170); /* button (focusable, not a text input) */
  TEST_ASSERT(cur(&f) == MY_CURSOR_HAND);
  move(&f, 400, 400); /* empty space: back to the baseline */
  TEST_ASSERT(cur(&f) == MY_CURSOR_ARROW);
  move(&f, 20, 170); /* and back onto the button */
  TEST_ASSERT(cur(&f) == MY_CURSOR_HAND);
  move(&f, 20, 20); /* button -> edit directly */
  TEST_ASSERT(cur(&f) == MY_CURSOR_TEXT);
  fx_destroy(&f);
}

/** @brief Focusable custom widget (node_view style) hovers as HAND per
 * the documented rule; a plain non-focusable widget hovers as ARROW. */
static void test_focusable_non_input_is_hand(void) {
  fx_t f;
  my_widget_t* canvas = my_widget_create(NULL, "canvas");
  fx_init(&f);
  canvas->focusable = true;
  my_widget_set_rect(canvas, &(my_rect_t){300, 300, 100, 100});
  my_widget_add_child(my_window_widget(f.win), canvas);
  my_widget_unref(canvas);
  move(&f, 350, 350);
  TEST_ASSERT(cur(&f) == MY_CURSOR_HAND);
  canvas->focusable = false;
  move(&f, 500, 500); /* leave */
  move(&f, 350, 350); /* re-enter as non-focusable */
  TEST_ASSERT(cur(&f) == MY_CURSOR_ARROW);
  fx_destroy(&f);
}

/** @brief A dispatcher over a window-less tree (unit-test style) must
 * not crash and must not touch any pal. */
static void test_windowless_tree_is_silent(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* btn = my_button_create(NULL, "b");
  my_event_dispatcher_t d;
  my_event_t e;
  my_widget_set_rect(root, &(my_rect_t){0, 0, 400, 300});
  my_widget_set_rect(btn, &(my_rect_t){10, 10, 100, 32});
  my_widget_add_child(root, btn);
  my_widget_unref(btn);
  my_event_dispatcher_init(&d, root);
  e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.x = 20;
  e.u.pointer.y = 20;
  my_event_dispatch(&d, &e); /* hover switches to the button: no window */
  TEST_ASSERT(btn->hovered);
  e.u.pointer.x = 300;
  e.u.pointer.y = 200;
  my_event_dispatch(&d, &e);
  TEST_ASSERT(!btn->hovered);
  my_widget_unref(root);
}

/** @brief PAL boundary: NULL slot -> NOT_SUPPORTED; the dummy port
 * rejects out-of-range shapes. */
static void test_pal_slot_contract(void) {
  my_pal_window_t fake;
  my_pal_window_vtable_t vt;
  fx_t f;
  memset(&vt, 0, sizeof(vt));
  fake.vtable = &vt;
  TEST_ASSERT(my_pal_window_set_cursor(&fake, MY_CURSOR_HAND) ==
              MY_RET_NOT_SUPPORTED);
  fx_init(&f);
  TEST_ASSERT(my_pal_window_set_cursor(f.win->pal_window,
                                       (my_cursor_t)99) ==
              MY_RET_INVALID_PARAMS);
  TEST_ASSERT(cur(&f) == MY_CURSOR_ARROW); /* unchanged by the reject */
  fx_destroy(&f);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_hover_classes);
  MYTEST_RUN(test_focusable_non_input_is_hand);
  MYTEST_RUN(test_windowless_tree_is_silent);
  MYTEST_RUN(test_pal_slot_contract);
MYTEST_MAIN_END()
