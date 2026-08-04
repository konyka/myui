/**
 * @file my_timer_test.c
 * @brief Unit tests for my_timer_manager (injected fake clock).
 */
#include "mypal/my_timer.h"

#include "mytest.h"

static uint64_t g_now = 0;
static uint64_t fake_now(void* ctx) {
  (void)ctx;
  return g_now;
}

typedef struct hits_t {
  int hits;
  int remove_after; /**< when >0, callback returns FAIL on this hit count */
} hits_t;

static my_ret_t on_timer(void* ctx) {
  hits_t* h = (hits_t*)ctx;
  h->hits++;
  if (h->remove_after > 0 && h->hits >= h->remove_after) {
    return MY_RET_FAIL; /* one-shot */
  }
  return MY_RET_OK; /* repeat */
}

static void test_add_and_fire_when_due(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  hits_t h = {0, 0};
  g_now = 0;

  TEST_ASSERT(my_timer_add(mgr, on_timer, &h, 100) > 0);
  TEST_ASSERT_EQ_INT(my_timer_manager_due_in_ms(mgr), 100);

  g_now = 99;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 0);
  TEST_ASSERT_EQ_INT(h.hits, 0);

  g_now = 100;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 1);
  TEST_ASSERT_EQ_INT(h.hits, 1);

  my_timer_manager_destroy(mgr);
}

static void test_periodic_reschedules(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  hits_t h = {0, 0};
  g_now = 0;
  my_timer_add(mgr, on_timer, &h, 50);

  g_now = 50;
  my_timer_manager_fire(mgr);
  TEST_ASSERT_EQ_INT(my_timer_manager_due_in_ms(mgr), 50);

  g_now = 100;
  my_timer_manager_fire(mgr);
  g_now = 150;
  my_timer_manager_fire(mgr);
  TEST_ASSERT_EQ_INT(h.hits, 3);

  my_timer_manager_destroy(mgr);
}

static void test_one_shot_removed_after_fire(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  hits_t h = {0, 1}; /* one-shot */
  g_now = 0;
  my_timer_add(mgr, on_timer, &h, 10);

  g_now = 10;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 1);
  TEST_ASSERT_EQ_INT(my_timer_manager_due_in_ms(mgr), UINT32_MAX);

  g_now = 20;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 0);
  TEST_ASSERT_EQ_INT(h.hits, 1);

  my_timer_manager_destroy(mgr);
}

static void test_remove_before_due(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  hits_t h = {0, 0};
  uint32_t id;
  g_now = 0;
  id = my_timer_add(mgr, on_timer, &h, 10);
  TEST_ASSERT_EQ_INT(my_timer_remove(mgr, id), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_timer_remove(mgr, id), MY_RET_NOT_FOUND);

  g_now = 100;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 0);
  TEST_ASSERT_EQ_INT(h.hits, 0);

  my_timer_manager_destroy(mgr);
}

static void test_multiple_timers_order(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  hits_t a = {0, 0}, b = {0, 0}, c = {0, 0};
  g_now = 0;
  my_timer_add(mgr, on_timer, &a, 10);
  my_timer_add(mgr, on_timer, &b, 10); /* same due: insertion order */
  my_timer_add(mgr, on_timer, &c, 20);

  TEST_ASSERT_EQ_INT(my_timer_manager_due_in_ms(mgr), 10);
  g_now = 15;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 2);
  TEST_ASSERT_EQ_INT(a.hits, 1);
  TEST_ASSERT_EQ_INT(b.hits, 1);
  TEST_ASSERT_EQ_INT(c.hits, 0);
  TEST_ASSERT_EQ_INT(my_timer_manager_due_in_ms(mgr), 5);

  my_timer_manager_destroy(mgr);
}

static hits_t g_self_remove = {0, 0};
static my_timer_manager_t* g_mgr = NULL;
static uint32_t g_self_id = 0;
static my_ret_t on_self_remove(void* ctx) {
  hits_t* h = (hits_t*)ctx;
  h->hits++;
  my_timer_remove(g_mgr, g_self_id); /* remove self inside callback */
  return MY_RET_OK;
}

static void test_remove_inside_callback_is_safe(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  g_mgr = mgr;
  g_self_remove.hits = 0;
  g_now = 0;
  g_self_id = my_timer_add(mgr, on_self_remove, &g_self_remove, 10);

  g_now = 10;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 1);
  TEST_ASSERT_EQ_INT(g_self_remove.hits, 1);

  g_now = 20;
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(mgr), 0);
  TEST_ASSERT_EQ_INT(g_self_remove.hits, 1);

  my_timer_manager_destroy(mgr);
  g_mgr = NULL;
}

static void test_null_params(void) {
  my_timer_manager_t* mgr = my_timer_manager_create(NULL, fake_now, NULL);
  TEST_ASSERT_EQ_INT(my_timer_add(NULL, on_timer, NULL, 10), 0);
  TEST_ASSERT_EQ_INT(my_timer_add(mgr, NULL, NULL, 10), 0);
  TEST_ASSERT_EQ_INT(my_timer_remove(NULL, 1), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_timer_manager_due_in_ms(NULL), UINT32_MAX);
  TEST_ASSERT_EQ_INT(my_timer_manager_fire(NULL), 0);
  my_timer_manager_destroy(NULL); /* must be safe */
  my_timer_manager_destroy(mgr);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_timer_manager_t* mgr = my_timer_manager_create(dbg, fake_now, NULL);
  hits_t h = {0, 0};
  g_now = 0;
  my_timer_add(mgr, on_timer, &h, 10);
  my_timer_add(mgr, on_timer, &h, 20);
  my_timer_add(mgr, on_timer, &h, 30);
  g_now = 25;
  my_timer_manager_fire(mgr);
  my_timer_manager_destroy(mgr); /* pending timers freed here */
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_add_and_fire_when_due);
  MYTEST_RUN(test_periodic_reschedules);
  MYTEST_RUN(test_one_shot_removed_after_fire);
  MYTEST_RUN(test_remove_before_due);
  MYTEST_RUN(test_multiple_timers_order);
  MYTEST_RUN(test_remove_inside_callback_is_safe);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
