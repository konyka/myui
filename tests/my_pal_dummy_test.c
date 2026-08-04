/**
 * @file my_pal_dummy_test.c
 * @brief Unit tests for the dummy PAL port (window, loop, timers, clock).
 */
#include "mypal/dummy/my_pal_dummy.h"

#include "myr/my_lcd_mem.h"

#include <string.h>

#include "mytest.h"

/* ---------------- window ---------------- */

static void test_window_basics(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_window_t* win = my_pal_window_create(pal, 100, 50, "hello");
  int32_t w = 0, h = 0;

  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_EQ_INT(my_pal_window_get_size(win, &w, &h), MY_RET_OK);
  TEST_ASSERT_EQ_INT(w, 100);
  TEST_ASSERT_EQ_INT(h, 50);

  TEST_ASSERT_EQ_INT(my_pal_window_set_title(win, "world"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_pal_window_show(win), MY_RET_OK);

  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

static void test_window_lcd_draw_and_readback(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_window_t* win = my_pal_window_create(pal, 16, 16, NULL);
  my_lcd_t* lcd = my_pal_window_get_lcd(win);
  uint8_t* buf;
  const my_color_t RED = {255, 0, 0, 255};

  TEST_ASSERT_NOT_NULL(lcd);
  TEST_ASSERT_EQ_INT(my_lcd_get_format(lcd), MY_PIXEL_FORMAT_BGRA8888);

  my_lcd_fill_rect(lcd, &(my_rect_t){2, 3, 4, 4}, RED);
  buf = my_lcd_mem_get_buffer(lcd); /* dummy lcd IS a mem lcd */
  TEST_ASSERT_NOT_NULL(buf);
  TEST_ASSERT_EQ_INT(buf[(3 * 16 + 2) * 4 + 2], 255); /* R channel */

  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

static void test_window_resize_reallocates(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_window_t* win = my_pal_window_create(pal, 8, 8, NULL);
  int32_t w = 0, h = 0;

  TEST_ASSERT_EQ_INT(my_pal_window_resize(win, 32, 24), MY_RET_OK);
  my_pal_window_get_size(win, &w, &h);
  TEST_ASSERT_EQ_INT(w, 32);
  TEST_ASSERT_EQ_INT(h, 24);
  TEST_ASSERT_NOT_NULL(my_pal_window_get_lcd(win));
  TEST_ASSERT_EQ_INT(my_lcd_get_width(my_pal_window_get_lcd(win)), 32);

  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

/* ---------------- event loop ---------------- */

typedef struct log_t {
  int count;
  my_event_type_t last_type;
  my_pal_window_t* last_window;
  my_pal_main_loop_t* loop;
} log_t;

static my_ret_t on_event(void* ctx, my_pal_window_t* window,
                         const my_event_t* event) {
  log_t* l = (log_t*)ctx;
  l->count++;
  l->last_type = event->type;
  l->last_window = window;
  if (event->type == MY_EVENT_QUIT && l->loop != NULL) {
    my_pal_main_loop_quit(l->loop);
  }
  return MY_RET_OK;
}

static void test_loop_fifo_dispatch(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  log_t l = {0, MY_EVENT_NONE, NULL, NULL};
  my_event_t e;

  my_pal_set_event_handler(pal, on_event, &l);

  e = my_event_init(MY_EVENT_USER);
  my_pal_main_loop_post_event(loop, &e);
  e = my_event_init(MY_EVENT_PAINT);
  my_pal_main_loop_post_event(loop, &e);
  e = my_event_init(MY_EVENT_RESIZE);
  my_pal_main_loop_post_event(loop, &e);

  TEST_ASSERT_EQ_INT(my_pal_main_loop_pump_n(loop, 2), 2);
  TEST_ASSERT_EQ_INT(l.count, 2);
  TEST_ASSERT_EQ_INT(l.last_type, MY_EVENT_PAINT); /* FIFO: USER then PAINT */

  TEST_ASSERT_EQ_INT(my_pal_main_loop_pump_n(loop, 5), 1); /* only 1 left */
  TEST_ASSERT_EQ_INT(l.last_type, MY_EVENT_RESIZE);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_pump_n(loop, 5), 0);

  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_loop_run_until_quit(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  log_t l = {0, MY_EVENT_NONE, NULL, loop};
  my_event_t e;

  my_pal_set_event_handler(pal, on_event, &l);
  e = my_event_init(MY_EVENT_USER);
  my_pal_main_loop_post_event(loop, &e);
  e = my_event_init(MY_EVENT_QUIT);
  my_pal_main_loop_post_event(loop, &e);

  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);
  TEST_ASSERT_EQ_INT(l.count, 2); /* USER then QUIT, run returned */

  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_loop_run_starved_returns(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  /* empty queue, no timers: run must not hang */
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

/* ---------------- clock + timers ---------------- */

static my_ret_t on_timer(void* ctx) {
  int* hits = (int*)ctx;
  (*hits)++;
  return MY_RET_FAIL; /* one-shot */
}

static void test_clock_injection_and_timer(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  int hits = 0;
  uint32_t id;

  TEST_ASSERT_EQ_INT(my_pal_time_now_ms(pal), 0);
  my_pal_dummy_set_now_ms(pal, 1000);
  TEST_ASSERT_EQ_INT(my_pal_time_now_ms(pal), 1000);

  id = my_pal_main_loop_add_timer(loop, on_timer, &hits, 50);
  TEST_ASSERT(id > 0);

  /* not due yet */
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);
  TEST_ASSERT_EQ_INT(hits, 0);

  /* advance clock past the deadline: run fires it once then starves */
  my_pal_dummy_set_now_ms(pal, 1060);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);
  TEST_ASSERT_EQ_INT(hits, 1);

  /* one-shot removed: advancing again fires nothing */
  my_pal_dummy_set_now_ms(pal, 2000);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);
  TEST_ASSERT_EQ_INT(hits, 1);

  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_loop_remove_timer(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  int hits = 0;
  uint32_t id = my_pal_main_loop_add_timer(loop, on_timer, &hits, 10);

  TEST_ASSERT_EQ_INT(my_pal_main_loop_remove_timer(loop, id), MY_RET_OK);
  my_pal_dummy_set_now_ms(pal, 100);
  my_pal_main_loop_run(loop);
  TEST_ASSERT_EQ_INT(hits, 0);

  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

/* ---------------- hygiene ---------------- */

static void test_null_params(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);

  TEST_ASSERT_EQ_INT(my_pal_main_loop_post_event(loop, NULL),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_pump_n(NULL, 1), 0);
  my_pal_dummy_set_now_ms(NULL, 5); /* must be safe */
  my_pal_window_destroy(NULL);
  my_pal_main_loop_destroy(NULL);
  my_pal_destroy(NULL);

  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(dbg);
  my_pal_window_t* win = my_pal_window_create(pal, 32, 32, "t");
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  log_t l = {0, MY_EVENT_NONE, NULL, loop};
  my_event_t e;
  int hits = 0;

  my_pal_set_event_handler(pal, on_event, &l);
  my_pal_window_set_title(win, "longer title");
  my_pal_window_resize(win, 16, 16);

  my_pal_main_loop_add_timer(loop, on_timer, &hits, 10);
  e = my_event_init(MY_EVENT_USER);
  my_pal_main_loop_post_event(loop, &e);
  e = my_event_init(MY_EVENT_QUIT);
  my_pal_main_loop_post_event(loop, &e);
  my_pal_dummy_set_now_ms(pal, 100);
  my_pal_main_loop_run(loop);

  my_pal_main_loop_destroy(loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_window_basics);
  MYTEST_RUN(test_window_lcd_draw_and_readback);
  MYTEST_RUN(test_window_resize_reallocates);
  MYTEST_RUN(test_loop_fifo_dispatch);
  MYTEST_RUN(test_loop_run_until_quit);
  MYTEST_RUN(test_loop_run_starved_returns);
  MYTEST_RUN(test_clock_injection_and_timer);
  MYTEST_RUN(test_loop_remove_timer);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
