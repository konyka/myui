/**
 * @file my_view_model_test.c
 * @brief Unit tests for my_view_model base + dummy implementation.
 */
#include "mymvvm/my_view_model.h"

#include "mytest.h"

static void test_dummy_props_roundtrip(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_value_t v, out;

  my_value_init(&v, NULL);
  my_value_init(&out, NULL);

  my_value_set_str(&v, "hello");
  TEST_ASSERT_EQ_INT(my_view_model_set_prop(vm, "name", &v), MY_RET_OK);

  TEST_ASSERT_EQ_INT(my_view_model_get_prop(vm, "name", &out), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_value_get_str(&out), "hello");

  my_value_set_int32(&v, 42);
  my_view_model_set_prop(vm, "age", &v);
  my_view_model_get_prop(vm, "age", &out);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&out), 42);

  TEST_ASSERT_EQ_INT(my_view_model_get_prop(vm, "missing", &out),
                     MY_RET_NOT_FOUND);

  my_value_reset(&v);
  my_value_reset(&out);
  my_view_model_unref(vm);
}

static void on_hit(void* ctx, const char* e, void* d) {
  int* n = (int*)ctx;
  (void)e;
  (void)d;
  (*n)++;
}

static void test_notify_on_set_and_manual(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_value_t v;
  int hits = 0;
  uint32_t id;

  id = my_emitter_on(vm->emitter, "prop:name", on_hit, &hits);
  TEST_ASSERT(id > 0);

  my_value_init(&v, NULL);
  my_value_set_str(&v, "x");
  my_view_model_set_prop(vm, "name", &v); /* auto-notifies */
  TEST_ASSERT_EQ_INT(hits, 1);

  my_view_model_notify_change(vm, "name");
  TEST_ASSERT_EQ_INT(hits, 2);

  TEST_ASSERT_EQ_INT(my_view_model_notify_change(vm, NULL), MY_RET_OK);
  my_value_reset(&v);
  my_view_model_unref(vm);
}

static int g_cmd_calls = 0;
static char g_cmd_args[32];
static my_ret_t on_save(void* ctx, const char* args) {
  (void)ctx;
  g_cmd_calls++;
  if (args != NULL) {
    size_t n = strlen(args);
    if (n >= sizeof(g_cmd_args)) {
      n = sizeof(g_cmd_args) - 1;
    }
    memcpy(g_cmd_args, args, n);
    g_cmd_args[n] = '\0';
  }
  return MY_RET_OK;
}

static void test_dummy_commands(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  g_cmd_calls = 0;
  g_cmd_args[0] = '\0';

  TEST_ASSERT(!my_view_model_can_exec(vm, "save", NULL));
  my_view_model_dummy_add_command(vm, "save", on_save, NULL);
  TEST_ASSERT(my_view_model_can_exec(vm, "save", NULL));

  TEST_ASSERT_EQ_INT(my_view_model_exec(vm, "save", "btn1"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(g_cmd_calls, 1);
  TEST_ASSERT_EQ_STR(g_cmd_args, "btn1");

  TEST_ASSERT_EQ_INT(my_view_model_exec(vm, "nope", NULL), MY_RET_NOT_FOUND);
  my_view_model_unref(vm);
}

static void test_null_params(void) {
  TEST_ASSERT_EQ_INT(my_view_model_get_prop(NULL, "x", NULL),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_view_model_set_prop(NULL, "x", NULL),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT(!my_view_model_can_exec(NULL, "x", NULL));
  TEST_ASSERT_EQ_INT(my_view_model_exec(NULL, "x", NULL), MY_RET_INVALID_PARAMS);
  my_view_model_unref(NULL);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_view_model_t* vm = my_view_model_dummy_create(dbg);
  my_value_t v;

  my_value_init(&v, NULL);
  my_value_set_str(&v, "a string with heap");
  my_view_model_set_prop(vm, "s1", &v);
  my_view_model_set_prop(vm, "s2", &v);
  my_value_set_int32(&v, 1);
  my_view_model_set_prop(vm, "n", &v);
  my_value_reset(&v);
  my_view_model_dummy_add_command(vm, "save", on_save, NULL);

  my_view_model_unref(vm);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_dummy_props_roundtrip);
  MYTEST_RUN(test_notify_on_set_and_manual);
  MYTEST_RUN(test_dummy_commands);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
