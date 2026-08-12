/**
 * @file my_tooltip_test.c
 * @brief Tooltip tests (M13c): hover delay shows the tip, moving away or
 * pressing a button hides/cancels it, edge clamp, leaks.
 */
#include "myc/my_str.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"

#include "mytest.h"

typedef struct fx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_widget_t* btn;
} fx_t;

static void fx_init(fx_t* f, const my_allocator_t* alloc) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  f->win = my_window_create(alloc, f->pal, 400, 300, "main");
  f->btn = my_button_create(alloc, "ok");
  my_widget_set_rect(f->btn, &(my_rect_t){10, 10, 80, 32});
  my_widget_add_child(my_window_widget(f->win), f->btn);
  my_widget_unref(f->btn);
  my_window_manager_open(f->wm, f->win);
  my_widget_unref(my_window_widget(f->win));
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static void inject(fx_t* f, my_event_type_t type, int32_t x, int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
}

/** @brief The tip widget = last root child named "tooltip" (NULL none). */
static my_widget_t* tip_of(fx_t* f) {
  my_widget_t* root = my_window_widget(f->win);
  size_t n = my_widget_child_count(root);
  my_widget_t* last;
  if (n == 0) {
    return NULL;
  }
  last = my_widget_get_child(root, n - 1);
  return my_str_eq(last->base.name, "tooltip") ? last : NULL;
}

static void test_hover_shows_tip(void) {
  fx_t f;
  fx_init(&f, NULL);
  my_widget_set_tooltip(f.btn, "Save file");
  inject(&f, MY_EVENT_POINTER_MOVE, 20, 20);
  TEST_ASSERT(tip_of(&f) == NULL); /* not before the delay */
  my_pal_dummy_set_now_ms(f.pal, 600);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(f.loop), MY_RET_OK);
  {
    my_widget_t* tip = tip_of(&f);
    TEST_ASSERT(tip != NULL);
    TEST_ASSERT(tip->floating);
    TEST_ASSERT_EQ_INT(tip->rect.x, 20 + 12);
    TEST_ASSERT_EQ_INT(tip->rect.y, 20 + 16);
    TEST_ASSERT_EQ_INT(tip->rect.w, 9 * 8 + 12); /* strlen("Save file") */
  }
  fx_destroy(&f);
}

static void test_move_away_cancels(void) {
  fx_t f;
  fx_init(&f, NULL);
  my_widget_set_tooltip(f.btn, "hint");
  inject(&f, MY_EVENT_POINTER_MOVE, 20, 20);
  inject(&f, MY_EVENT_POINTER_MOVE, 200, 200); /* leave before the delay */
  my_pal_dummy_set_now_ms(f.pal, 1000);
  my_pal_main_loop_run(f.loop);
  TEST_ASSERT(tip_of(&f) == NULL);
  fx_destroy(&f);
}

static void test_down_hides_tip(void) {
  fx_t f;
  fx_init(&f, NULL);
  my_widget_set_tooltip(f.btn, "hint");
  inject(&f, MY_EVENT_POINTER_MOVE, 20, 20);
  my_pal_dummy_set_now_ms(f.pal, 600);
  my_pal_main_loop_run(f.loop);
  TEST_ASSERT(tip_of(&f) != NULL);
  inject(&f, MY_EVENT_POINTER_DOWN, 20, 20);
  TEST_ASSERT(tip_of(&f) == NULL);
  fx_destroy(&f);
}

static void test_no_tooltip_no_tip(void) {
  fx_t f;
  fx_init(&f, NULL);
  inject(&f, MY_EVENT_POINTER_MOVE, 20, 20);
  my_pal_dummy_set_now_ms(f.pal, 1000);
  my_pal_main_loop_run(f.loop);
  TEST_ASSERT(tip_of(&f) == NULL);
  fx_destroy(&f);
}

static void test_tip_clamped_to_window(void) {
  fx_t f;
  fx_init(&f, NULL);
  my_widget_set_rect(f.btn, &(my_rect_t){330, 260, 60, 30});
  my_widget_set_tooltip(f.btn, "a rather long tooltip text");
  inject(&f, MY_EVENT_POINTER_MOVE, 340, 270);
  my_pal_dummy_set_now_ms(f.pal, 600);
  my_pal_main_loop_run(f.loop);
  {
    my_widget_t* tip = tip_of(&f);
    TEST_ASSERT(tip != NULL);
    TEST_ASSERT(tip->rect.x >= 0);
    TEST_ASSERT(tip->rect.x + tip->rect.w <= 400);
    TEST_ASSERT(tip->rect.y + tip->rect.h <= 300);
  }
  fx_destroy(&f);
}

static void test_tooltip_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  fx_t f;
  fx_init(&f, dbg);
  my_widget_set_tooltip(f.btn, "hint");
  inject(&f, MY_EVENT_POINTER_MOVE, 20, 20);
  my_pal_dummy_set_now_ms(f.pal, 600);
  my_pal_main_loop_run(f.loop);
  TEST_ASSERT(tip_of(&f) != NULL); /* tip shown: freed via window destroy */
  fx_destroy(&f);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_hover_shows_tip);
  MYTEST_RUN(test_move_away_cancels);
  MYTEST_RUN(test_down_hides_tip);
  MYTEST_RUN(test_no_tooltip_no_tip);
  MYTEST_RUN(test_tip_clamped_to_window);
  MYTEST_RUN(test_tooltip_no_leak);
MYTEST_MAIN_END()
