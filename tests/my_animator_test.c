/**
 * @file my_animator_test.c
 * @brief Unit tests for my_animator (dummy PAL + fake clock, deterministic).
 */
#include "myui/my_animator.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"

#include "mypal/dummy/my_pal_dummy.h"

#include "mytest.h"

static void test_easing_functions(void) {
  TEST_ASSERT(my_easing_linear(0.5f) == 0.5f);
  TEST_ASSERT(my_easing_ease_in(0.0f) == 0.0f);
  TEST_ASSERT(my_easing_ease_in(1.0f) == 1.0f);
  TEST_ASSERT(my_easing_ease_out(0.0f) == 0.0f);
  TEST_ASSERT(my_easing_ease_out(1.0f) == 1.0f);
  TEST_ASSERT(my_easing_ease_in_out(0.0f) == 0.0f);
  TEST_ASSERT(my_easing_ease_in_out(1.0f) == 1.0f);
  TEST_ASSERT(my_easing_ease_in_out(0.5f) == 0.5f);
  /* ease_in is slower than linear mid-way, ease_out faster */
  TEST_ASSERT(my_easing_ease_in(0.5f) < 0.5f);
  TEST_ASSERT(my_easing_ease_out(0.5f) > 0.5f);
}

typedef struct anim_ctx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_widget_t* btn;
} anim_ctx_t;

static void anim_ctx_init(anim_ctx_t* c) {
  c->pal = my_pal_dummy_create(NULL);
  c->loop = my_pal_main_loop_create(c->pal);
  c->wm = my_window_manager_create(NULL, c->pal, c->loop);
  c->win = my_window_create(NULL, c->pal, 200, 100, "anim");
  c->btn = my_button_create(NULL, "b");
  my_widget_set_rect(c->btn, &(my_rect_t){0, 0, 40, 20});
  my_widget_add_child(my_window_widget(c->win), c->btn);
  my_widget_unref(c->btn);
  my_window_manager_open(c->wm, c->win);
  my_widget_unref(my_window_widget(c->win));
}

static void anim_ctx_destroy(anim_ctx_t* c) {
  my_window_manager_destroy(c->wm);
  my_pal_main_loop_destroy(c->loop);
  my_pal_destroy(c->pal);
}

static int g_done = 0;
static int g_updates = 0;
static void on_done(my_widget_t* w, void* ctx) {
  (void)w;
  (void)ctx;
  g_done++;
}
static void on_update(my_widget_t* w, void* ctx) {
  (void)w;
  (void)ctx;
  g_updates++;
}

static void test_move_to_completes(void) {
  anim_ctx_t c;
  uint32_t id;
  anim_ctx_init(&c);
  g_done = 0;
  g_updates = 0;

  id = my_animator_move_to(c.btn, 100, 50, 100, my_easing_linear);
  TEST_ASSERT(id > 0);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(c.wm->anim_mgr), 1);

  /* mid-animation: halfway at t=50ms */
  my_pal_dummy_set_now_ms(c.pal, 50);
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT(c.btn->rect.x > 0 && c.btn->rect.x < 100);

  /* finish */
  my_pal_dummy_set_now_ms(c.pal, 200);
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT_EQ_INT(c.btn->rect.x, 100);
  TEST_ASSERT_EQ_INT(c.btn->rect.y, 50);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(c.wm->anim_mgr), 0);

  anim_ctx_destroy(&c);
}

static void test_on_done_and_on_update(void) {
  anim_ctx_t c;
  anim_ctx_init(&c);
  g_done = 0;
  g_updates = 0;

  my_animator_animate(c.wm->anim_mgr, c.btn, "x", 50, 0, 40, 0,
                      my_easing_linear, 0, false, on_update, on_done, NULL);
  my_pal_dummy_set_now_ms(c.pal, 40); /* past the first 33ms anim tick */
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT(g_updates > 0);
  my_pal_dummy_set_now_ms(c.pal, 100);
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT_EQ_INT(g_done, 1);

  anim_ctx_destroy(&c);
}

static void test_delay_before_start(void) {
  anim_ctx_t c;
  anim_ctx_init(&c);

  my_animator_animate(c.wm->anim_mgr, c.btn, "x", 50, 0, 40, 100,
                      my_easing_linear, 0, false, NULL, NULL, NULL);
  my_pal_dummy_set_now_ms(c.pal, 50); /* still inside delay */
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT_EQ_INT(c.btn->rect.x, 0);

  my_pal_dummy_set_now_ms(c.pal, 200);
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT_EQ_INT(c.btn->rect.x, 50);

  anim_ctx_destroy(&c);
}

static void test_yoyo_repeat(void) {
  anim_ctx_t c;
  anim_ctx_init(&c);
  g_done = 0;

  /* repeat_count 1 + yoyo: plays forward then backward, ends at "from" */
  my_animator_animate(c.wm->anim_mgr, c.btn, "x", 100, 0, 50, 0,
                      my_easing_linear, 1, true, NULL, on_done, NULL);

  my_pal_dummy_set_now_ms(c.pal, 40); /* forward (first 33ms tick fired) */
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT(c.btn->rect.x > 0);

  my_pal_dummy_set_now_ms(c.pal, 75); /* second cycle, yoyo reversed */
  my_pal_main_loop_run(c.loop);

  my_pal_dummy_set_now_ms(c.pal, 200); /* beyond end */
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT_EQ_INT(c.btn->rect.x, 0); /* yoyo odd repeats: back at from */
  TEST_ASSERT_EQ_INT(g_done, 1);

  anim_ctx_destroy(&c);
}

static void test_stop_cancels(void) {
  anim_ctx_t c;
  uint32_t id;
  anim_ctx_init(&c);

  id = my_animator_move_to(c.btn, 100, 100, 100, my_easing_linear);
  my_animator_stop(c.wm->anim_mgr, id);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(c.wm->anim_mgr), 0);

  my_pal_dummy_set_now_ms(c.pal, 500);
  my_pal_main_loop_run(c.loop);
  TEST_ASSERT_EQ_INT(c.btn->rect.x, 0); /* never advanced */

  anim_ctx_destroy(&c);
}

static void test_removing_widget_cancels_anim_and_grab(void) {
  anim_ctx_t c;
  my_event_t e;
  anim_ctx_init(&c);

  /* grab the button via pointer down */
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 10;
  e.u.pointer.y = 10;
  my_window_on_pal_event(c.win, &e);
  TEST_ASSERT(c.win->dispatcher.grabbed == c.btn);
  TEST_ASSERT(c.win->dispatcher.focused == c.btn);

  my_animator_move_to(c.btn, 100, 100, 100, my_easing_linear);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(c.wm->anim_mgr), 1);

  /* remove from the tree: grab/focus refs and animations must be dropped */
  my_widget_remove_child(my_window_widget(c.win), c.btn);
  TEST_ASSERT_NULL(c.win->dispatcher.grabbed);
  TEST_ASSERT_NULL(c.win->dispatcher.focused);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(c.wm->anim_mgr), 0);

  /* further events on the old position must not touch freed memory */
  e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.x = 10;
  e.u.pointer.y = 10;
  my_window_on_pal_event(c.win, &e);
  my_pal_dummy_set_now_ms(c.pal, 500);
  my_pal_main_loop_run(c.loop);

  anim_ctx_destroy(&c);
}

static void test_window_destroy_cancels_anims(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win = my_window_create(NULL, pal, 100, 100, "x");
  my_widget_t* btn = my_button_create(NULL, "b");

  my_widget_set_rect(btn, &(my_rect_t){0, 0, 40, 20});
  my_widget_add_child(my_window_widget(win), btn);
  my_widget_unref(btn);
  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win)); /* manager holds the only ref */

  my_animator_move_to(btn, 90, 90, 1000, my_easing_linear);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(wm->anim_mgr), 1);

  /* close (and destroy) the window while an animation runs */
  my_window_manager_close(wm, win);
  TEST_ASSERT_EQ_INT(my_animator_manager_active_count(wm->anim_mgr), 0);

  my_pal_dummy_set_now_ms(pal, 2000);
  my_pal_main_loop_run(loop);

  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(dbg);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(dbg, pal, loop);
  my_window_t* win = my_window_create(dbg, pal, 100, 100, "x");
  my_widget_t* btn = my_button_create(dbg, "b");

  my_widget_set_rect(btn, &(my_rect_t){0, 0, 40, 20});
  my_widget_add_child(my_window_widget(win), btn);
  my_widget_unref(btn);
  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

  my_animator_move_to(btn, 80, 80, 50, my_easing_linear);
  my_pal_dummy_set_now_ms(pal, 25);
  my_pal_main_loop_run(loop);
  my_pal_dummy_set_now_ms(pal, 100);
  my_pal_main_loop_run(loop);

  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_easing_functions);
  MYTEST_RUN(test_move_to_completes);
  MYTEST_RUN(test_on_done_and_on_update);
  MYTEST_RUN(test_delay_before_start);
  MYTEST_RUN(test_yoyo_repeat);
  MYTEST_RUN(test_stop_cancels);
  MYTEST_RUN(test_removing_widget_cancels_anim_and_grab);
  MYTEST_RUN(test_window_destroy_cancels_anims);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
