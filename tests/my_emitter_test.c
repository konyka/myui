/**
 * @file my_emitter_test.c
 * @brief Unit tests for my_emitter.
 */
#include "myc/my_emitter.h"

#include "mytest.h"

typedef struct counter_t {
  int hits;
  const char* last_event;
  void* last_data;
} counter_t;

static void on_count(void* ctx, const char* event, void* event_data) {
  counter_t* c = (counter_t*)ctx;
  c->hits++;
  c->last_event = event;
  c->last_data = event_data;
}

static void test_basic_on_emit(void) {
  my_emitter_t* e = my_emitter_create(NULL);
  counter_t c = {0, NULL, NULL};
  int data = 42;
  uint32_t id;

  TEST_ASSERT_NOT_NULL(e);
  id = my_emitter_on(e, "click", on_count, &c);
  TEST_ASSERT(id > 0);

  TEST_ASSERT_EQ_INT(my_emitter_emit(e, "click", &data), MY_RET_OK);
  TEST_ASSERT_EQ_INT(c.hits, 1);
  TEST_ASSERT_EQ_STR(c.last_event, "click");
  TEST_ASSERT(c.last_data == &data);

  my_emitter_destroy(e);
}

static void test_multiple_listeners_and_isolation(void) {
  my_emitter_t* e = my_emitter_create(NULL);
  counter_t a = {0, NULL, NULL}, b = {0, NULL, NULL};

  my_emitter_on(e, "click", on_count, &a);
  my_emitter_on(e, "click", on_count, &b);
  my_emitter_on(e, "hover", on_count, &a);

  my_emitter_emit(e, "click", NULL);
  TEST_ASSERT_EQ_INT(a.hits, 1);
  TEST_ASSERT_EQ_INT(b.hits, 1);

  my_emitter_emit(e, "hover", NULL);
  TEST_ASSERT_EQ_INT(a.hits, 2);
  TEST_ASSERT_EQ_INT(b.hits, 1);

  my_emitter_destroy(e);
}

static void test_off(void) {
  my_emitter_t* e = my_emitter_create(NULL);
  counter_t a = {0, NULL, NULL};
  uint32_t id = my_emitter_on(e, "click", on_count, &a);

  TEST_ASSERT_EQ_INT(my_emitter_off(e, id), MY_RET_OK);
  my_emitter_emit(e, "click", NULL);
  TEST_ASSERT_EQ_INT(a.hits, 0);

  TEST_ASSERT_EQ_INT(my_emitter_off(e, id), MY_RET_NOT_FOUND);
  my_emitter_destroy(e);
}

typedef struct self_off_ctx_t {
  my_emitter_t* emitter;
  uint32_t id_to_remove;
  int hits_a;
  int hits_b;
} self_off_ctx_t;

static void on_off_other(void* ctx, const char* event, void* event_data) {
  self_off_ctx_t* c = (self_off_ctx_t*)ctx;
  (void)event;
  (void)event_data;
  c->hits_a++;
  my_emitter_off(c->emitter, c->id_to_remove);
}

static void on_count_b(void* ctx, const char* event, void* event_data) {
  self_off_ctx_t* c = (self_off_ctx_t*)ctx;
  (void)event;
  (void)event_data;
  c->hits_b++;
}

static void test_off_during_emit_is_safe(void) {
  my_emitter_t* e = my_emitter_create(NULL);
  self_off_ctx_t c = {e, 0, 0, 0};

  /* listener A runs first and offs listener B: B must NOT fire this round */
  my_emitter_on(e, "ev", on_off_other, &c);
  c.id_to_remove = my_emitter_on(e, "ev", on_count_b, &c);

  my_emitter_emit(e, "ev", NULL);
  TEST_ASSERT_EQ_INT(c.hits_a, 1);
  TEST_ASSERT_EQ_INT(c.hits_b, 0);

  /* B stays removed on later emits */
  my_emitter_emit(e, "ev", NULL);
  TEST_ASSERT_EQ_INT(c.hits_a, 2);
  TEST_ASSERT_EQ_INT(c.hits_b, 0);

  my_emitter_destroy(e);
}

typedef struct self_reg_ctx_t {
  my_emitter_t* emitter;
  int hits;
} self_reg_ctx_t;

static void on_register_again(void* ctx, const char* event, void* event_data) {
  self_reg_ctx_t* c = (self_reg_ctx_t*)ctx;
  (void)event;
  (void)event_data;
  c->hits++;
  if (c->hits == 1) {
    my_emitter_on(c->emitter, "ev", on_register_again, c);
  }
}

static void test_on_during_emit_not_called_this_round(void) {
  my_emitter_t* e = my_emitter_create(NULL);
  self_reg_ctx_t c = {e, 0};

  my_emitter_on(e, "ev", on_register_again, &c);
  my_emitter_emit(e, "ev", NULL);
  TEST_ASSERT_EQ_INT(c.hits, 1); /* new listener not fired in the same emit */

  my_emitter_emit(e, "ev", NULL);
  TEST_ASSERT_EQ_INT(c.hits, 3); /* now both listeners fire */

  my_emitter_destroy(e);
}

static void test_null_params(void) {
  my_emitter_t* e = my_emitter_create(NULL);
  TEST_ASSERT_EQ_INT(my_emitter_on(NULL, "x", on_count, NULL), 0);
  TEST_ASSERT_EQ_INT(my_emitter_on(e, NULL, on_count, NULL), 0);
  TEST_ASSERT_EQ_INT(my_emitter_on(e, "x", NULL, NULL), 0);
  TEST_ASSERT_EQ_INT(my_emitter_emit(NULL, "x", NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_emitter_emit(e, NULL, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_emitter_off(NULL, 1), MY_RET_INVALID_PARAMS);
  my_emitter_destroy(NULL); /* must be safe */
  my_emitter_destroy(e);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_emitter_t* e = my_emitter_create(dbg);
  counter_t a = {0, NULL, NULL};

  my_emitter_on(e, "a", on_count, &a);
  my_emitter_on(e, "a", on_count, &a);
  my_emitter_on(e, "b", on_count, &a);
  my_emitter_emit(e, "a", NULL);
  /* one listener removed inside emit is freed only after the sweep */
  my_emitter_emit(e, "b", NULL);
  my_emitter_destroy(e);

  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_basic_on_emit);
  MYTEST_RUN(test_multiple_listeners_and_isolation);
  MYTEST_RUN(test_off);
  MYTEST_RUN(test_off_during_emit_is_safe);
  MYTEST_RUN(test_on_during_emit_not_called_this_round);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
