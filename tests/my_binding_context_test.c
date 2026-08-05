/**
 * @file my_binding_context_test.c
 * @brief Unit tests for the binding context with a recording mock target.
 */
#include "mymvvm/my_binding_context.h"
#include "mymvvm/my_value_converter.h"

#include <stdio.h>
#include <string.h>

#include "mytest.h"

/* ---------------- recording mock target ---------------- */

typedef struct mock_target_t {
  my_binding_target_t base;
  my_emitter_t* emitter;
  char text[64];   /* "text" property */
  int32_t value;   /* "value" property */
  int set_text_calls;
  int set_value_calls;
} mock_target_t;

static my_ret_t mock_set_prop(my_binding_target_t* t, const char* name,
                              const my_value_t* v) {
  mock_target_t* m = (mock_target_t*)t;
  if (strcmp(name, "text") == 0) {
    const char* s = my_value_get_str(v);
    m->set_text_calls++;
    if (s != NULL) {
      strncpy(m->text, s, sizeof(m->text) - 1);
      m->text[sizeof(m->text) - 1] = '\0';
    }
    return MY_RET_OK;
  }
  if (strcmp(name, "value") == 0) {
    m->set_value_calls++;
    m->value = my_value_get_int32(v);
    return MY_RET_OK;
  }
  return MY_RET_NOT_FOUND;
}

static my_ret_t mock_get_prop(my_binding_target_t* t, const char* name,
                              my_value_t* v) {
  mock_target_t* m = (mock_target_t*)t;
  if (strcmp(name, "text") == 0) {
    return my_value_set_str(v, m->text);
  }
  if (strcmp(name, "value") == 0) {
    return my_value_set_int32(v, m->value);
  }
  return MY_RET_NOT_FOUND;
}

static uint32_t mock_on_event(my_binding_target_t* t, const char* event,
                              my_event_callback_t cb, void* ctx) {
  return my_emitter_on(((mock_target_t*)t)->emitter, event, cb, ctx);
}

static my_ret_t mock_off_event(my_binding_target_t* t, uint32_t id) {
  return my_emitter_off(((mock_target_t*)t)->emitter, id);
}

static const my_binding_target_vtable_t MOCK_VTABLE = {mock_set_prop,
                                                       mock_get_prop,
                                                       mock_on_event,
                                                       mock_off_event,
                                                       NULL};

static void mock_init(mock_target_t* m) {
  memset(m, 0, sizeof(*m));
  m->base.vtable = &MOCK_VTABLE;
  m->emitter = my_emitter_create(NULL);
}

static void mock_destroy(mock_target_t* m) {
  my_emitter_destroy(m->emitter);
}

static void set_vm_str(my_view_model_t* vm, const char* name, const char* s) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_str(&v, s);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

static void set_vm_int(my_view_model_t* vm, const char* name, int32_t i) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_int32(&v, i);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

/* ---------------- tests ---------------- */

static void test_one_way_initial_push_and_live_update(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);

  set_vm_str(vm, "name", "alice");
  ctx = my_binding_context_create(NULL, vm);
  TEST_ASSERT_EQ_INT(my_binding_context_bind(ctx, &target.base,
                                             "v:text={name}"),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(target.text, "alice"); /* initial push */
  TEST_ASSERT_EQ_INT(target.set_text_calls, 1);

  set_vm_str(vm, "name", "bob"); /* dummy vm auto-notifies */
  TEST_ASSERT_EQ_STR(target.text, "bob");
  TEST_ASSERT_EQ_INT(target.set_text_calls, 2);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_mode_once_no_live_update(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);

  set_vm_str(vm, "name", "first");
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base, "v:text={name, Mode=Once}");
  TEST_ASSERT_EQ_STR(target.text, "first");

  set_vm_str(vm, "name", "second");
  TEST_ASSERT_EQ_STR(target.text, "first"); /* no live update */
  TEST_ASSERT_EQ_INT(target.set_text_calls, 1);

  /* manual full sync still pushes */
  my_binding_context_update_to_view(ctx);
  TEST_ASSERT_EQ_STR(target.text, "second");

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_two_way_writeback(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  my_value_t out;
  mock_init(&target);

  set_vm_str(vm, "name", "model");
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base, "v:text={name, Mode=TwoWay}");

  /* user edits the target */
  strcpy(target.text, "edited");
  my_emitter_emit(target.emitter, "changed", NULL);

  my_value_init(&out, NULL);
  my_view_model_get_prop(vm, "name", &out);
  TEST_ASSERT_EQ_STR(my_value_get_str(&out), "edited");
  my_value_reset(&out);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_converter_applied_both_directions(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  my_value_t out;
  mock_init(&target);

  set_vm_str(vm, "name", "hello");
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base,
                          "v:text={name, Mode=TwoWay, Converter=upper}");
  TEST_ASSERT_EQ_STR(target.text, "HELLO"); /* convert on push */

  strcpy(target.text, "WORLD");
  my_emitter_emit(target.emitter, "changed", NULL);
  my_value_init(&out, NULL);
  my_view_model_get_prop(vm, "name", &out);
  TEST_ASSERT_EQ_STR(my_value_get_str(&out), "WORLD"); /* upper is symmetric */
  my_value_reset(&out);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_validator_rejects_writeback(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  my_value_t out;
  mock_init(&target);

  set_vm_int(vm, "age", 30);
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base,
                          "v:value={age, Mode=TwoWay, Validator=range(0,150)}");
  TEST_ASSERT_EQ_INT(target.value, 30);

  /* user enters an out-of-range value: rejected, target restored */
  target.value = 999;
  my_emitter_emit(target.emitter, "changed", NULL);

  my_value_init(&out, NULL);
  my_view_model_get_prop(vm, "age", &out);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&out), 30); /* vm unchanged */
  TEST_ASSERT_EQ_INT(target.value, 30);             /* target restored */
  my_value_reset(&out);

  /* in-range writeback succeeds */
  target.value = 42;
  my_emitter_emit(target.emitter, "changed", NULL);
  my_view_model_get_prop(vm, "age", &out);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&out), 42);
  my_value_reset(&out);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static int g_saved = 0;
static char g_save_args[16];
static my_ret_t on_save(void* ctx, const char* args) {
  (void)ctx;
  g_saved++;
  if (args != NULL) {
    strncpy(g_save_args, args, sizeof(g_save_args) - 1);
  }
  return MY_RET_OK;
}

static void test_command_binding(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);
  g_saved = 0;
  g_save_args[0] = '\0';

  my_view_model_dummy_add_command(vm, "save", on_save, NULL);
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base, "v:on_click={save, Args=btn1}");

  my_emitter_emit(target.emitter, "click", NULL);
  TEST_ASSERT_EQ_INT(g_saved, 1);
  TEST_ASSERT_EQ_STR(g_save_args, "btn1");

  /* unregistered command: can_exec false, no crash */
  my_binding_context_bind(ctx, &target.base, "v:on_tap={missing}");
  my_emitter_emit(target.emitter, "tap", NULL);
  TEST_ASSERT_EQ_INT(g_saved, 1);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_set_view_model_rewires(void) {
  my_view_model_t* vm1 = my_view_model_dummy_create(NULL);
  my_view_model_t* vm2 = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);

  set_vm_str(vm1, "name", "one");
  set_vm_str(vm2, "name", "two");
  ctx = my_binding_context_create(NULL, vm1);
  my_binding_context_bind(ctx, &target.base, "v:text={name}");
  TEST_ASSERT_EQ_STR(target.text, "one");

  my_binding_context_set_view_model(ctx, vm2);
  TEST_ASSERT_EQ_STR(target.text, "two"); /* pushed on swap */

  /* old vm no longer drives the target, new one does */
  set_vm_str(vm1, "name", "ignored");
  TEST_ASSERT_EQ_STR(target.text, "two");
  set_vm_str(vm2, "name", "three");
  TEST_ASSERT_EQ_STR(target.text, "three");

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm1);
  my_view_model_unref(vm2);
}

static void test_update_to_vm_manual(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  my_value_t out;
  mock_init(&target);

  set_vm_int(vm, "n", 1);
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base, "v:value={n, Mode=TwoWay}");

  target.value = 77; /* no "changed" event emitted */
  my_binding_context_update_to_vm(ctx);
  my_value_init(&out, NULL);
  my_view_model_get_prop(vm, "n", &out);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&out), 77);
  my_value_reset(&out);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_bad_rule_and_null(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);

  ctx = my_binding_context_create(NULL, vm);
  TEST_ASSERT_EQ_INT(my_binding_context_bind(ctx, &target.base, "bad"),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_binding_context_bind(ctx, &target.base, "v:items={l}"),
                     MY_RET_NOT_SUPPORTED);
  TEST_ASSERT_EQ_INT(my_binding_context_bind(NULL, &target.base, "v:t={a}"),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(
      my_binding_context_bind(ctx, &target.base, "v:text={a, Converter=nope}"),
      MY_RET_FAIL); /* unknown converter rejected */

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static my_ret_t money_convert(void* ctx, my_value_t* value) {
  char buf[32];
  (void)ctx;
  if (value->type != MY_VALUE_INT32) {
    return MY_RET_OK;
  }
  snprintf(buf, sizeof(buf), "%d.%02d", (int)(my_value_get_int32(value) / 100),
           (int)(my_value_get_int32(value) % 100));
  return my_value_set_str(value, buf);
}

static const my_value_converter_t MONEY_CONV = {money_convert, NULL, NULL};

static void test_custom_converter_in_rule(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);

  my_value_converter_register("money", &MONEY_CONV);
  set_vm_int(vm, "price", 1299);

  ctx = my_binding_context_create(NULL, vm);
  TEST_ASSERT_EQ_INT(my_binding_context_bind(ctx, &target.base,
                                             "v:text={price, Converter=money}"),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(target.text, "12.99");

  /* unregister: new binds referencing it fail cleanly */
  my_value_converter_unregister("money");
  TEST_ASSERT_EQ_INT(my_binding_context_bind(ctx, &target.base,
                                             "v:text={price, Converter=money}"),
                     MY_RET_FAIL);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_view_model_t* vm = my_view_model_dummy_create(dbg);
  mock_target_t target;
  my_binding_context_t* ctx;
  mock_init(&target);

  set_vm_str(vm, "name", "x");
  set_vm_int(vm, "age", 5);
  my_view_model_dummy_add_command(vm, "save", on_save, NULL);

  ctx = my_binding_context_create(dbg, vm);
  my_binding_context_bind(ctx, &target.base,
                          "v:text={name, Mode=TwoWay, Converter=upper}");
  my_binding_context_bind(ctx, &target.base,
                          "v:value={age, Mode=TwoWay, Validator=range(0,150)}");
  my_binding_context_bind(ctx, &target.base, "v:on_click={save}");

  strcpy(target.text, "Y");
  my_emitter_emit(target.emitter, "changed", NULL);
  my_emitter_emit(target.emitter, "click", NULL);

  my_binding_context_destroy(ctx);
  mock_destroy(&target);
  my_view_model_unref(vm);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_one_way_initial_push_and_live_update);
  MYTEST_RUN(test_mode_once_no_live_update);
  MYTEST_RUN(test_two_way_writeback);
  MYTEST_RUN(test_converter_applied_both_directions);
  MYTEST_RUN(test_validator_rejects_writeback);
  MYTEST_RUN(test_command_binding);
  MYTEST_RUN(test_set_view_model_rewires);
  MYTEST_RUN(test_update_to_vm_manual);
  MYTEST_RUN(test_bad_rule_and_null);
  MYTEST_RUN(test_custom_converter_in_rule);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
