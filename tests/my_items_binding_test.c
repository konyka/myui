/**
 * @file my_items_binding_test.c
 * @brief Unit tests for view_model_array, items/condition binding, navigator.
 */
#include "mymvvm/my_items_binding.h"
#include "mymvvm/my_navigator.h"

#include <string.h>

#include "mytest.h"

/* ---------------- array dummy ---------------- */

static void on_hit(void* ctx, const char* e, void* d) {
  int* n = (int*)ctx;
  (void)e;
  (void)d;
  (*n)++;
}

static my_view_model_t* person(const char* name, int32_t age) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_str(&v, name);
  my_view_model_set_prop(vm, "name", &v);
  my_value_set_int32(&v, age);
  my_view_model_set_prop(vm, "age", &v);
  my_value_reset(&v);
  return vm;
}

static void test_array_crud_and_notify(void) {
  my_view_model_array_t* arr = my_view_model_array_dummy_create(NULL);
  int hits = 0;
  my_view_model_t* p;

  my_emitter_on(arr->emitter, "items_changed", on_hit, &hits);
  TEST_ASSERT_EQ_INT(my_view_model_array_get_count(arr), 0);

  my_view_model_array_dummy_push(arr, person("a", 1));
  my_view_model_array_dummy_push(arr, person("b", 2));
  TEST_ASSERT_EQ_INT(my_view_model_array_get_count(arr), 2);
  TEST_ASSERT_EQ_INT(hits, 2);

  p = my_view_model_array_get_item(arr, 1);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_NULL(my_view_model_array_get_item(arr, 5));

  my_view_model_array_insert(arr, 0, person("c", 3));
  TEST_ASSERT_EQ_INT(my_view_model_array_get_count(arr), 3);
  {
    my_value_t out;
    my_value_init(&out, NULL);
    my_view_model_get_prop(my_view_model_array_get_item(arr, 0), "name", &out);
    TEST_ASSERT_EQ_STR(my_value_get_str(&out), "c");
    my_value_reset(&out);
  }

  my_view_model_array_remove(arr, 0);
  TEST_ASSERT_EQ_INT(my_view_model_array_get_count(arr), 2);
  TEST_ASSERT_EQ_INT(my_view_model_array_remove(arr, 9), MY_RET_NOT_FOUND);

  my_view_model_array_clear(arr);
  TEST_ASSERT_EQ_INT(my_view_model_array_get_count(arr), 0);

  my_view_model_array_unref(arr);
}

/* ---------------- mock target with rebuild_items ---------------- */

typedef struct list_mock_t {
  my_binding_target_t base;
  my_emitter_t* emitter;
  int rebuild_calls;
  size_t last_count;
  char last_template[32];
  char row_names[8][32];
  int32_t row_ages[8];
  bool visible;
} list_mock_t;

static my_ret_t list_set_prop(my_binding_target_t* t, const char* name,
                              const my_value_t* v) {
  list_mock_t* m = (list_mock_t*)t;
  if (strcmp(name, "visible") == 0) {
    m->visible = my_value_get_bool(v);
    return MY_RET_OK;
  }
  return MY_RET_NOT_FOUND;
}

static my_ret_t list_get_prop(my_binding_target_t* t, const char* name,
                              my_value_t* v) {
  (void)t;
  (void)name;
  (void)v;
  return MY_RET_NOT_FOUND;
}

static uint32_t list_on_event(my_binding_target_t* t, const char* event,
                              my_event_callback_t cb, void* ctx) {
  return my_emitter_on(((list_mock_t*)t)->emitter, event, cb, ctx);
}

static my_ret_t list_off_event(my_binding_target_t* t, uint32_t id) {
  return my_emitter_off(((list_mock_t*)t)->emitter, id);
}

static my_ret_t list_rebuild(my_binding_target_t* t, const char* item_template,
                             size_t count, my_item_props_fn_t props,
                             void* props_ctx) {
  list_mock_t* m = (list_mock_t*)t;
  size_t i;
  m->rebuild_calls++;
  m->last_count = count;
  strncpy(m->last_template, item_template, sizeof(m->last_template) - 1);
  for (i = 0; i < count && i < 8; i++) {
    my_value_t v;
    my_value_init(&v, NULL);
    props(props_ctx, i, "name", &v);
    if (v.type == MY_VALUE_STR) {
      strncpy(m->row_names[i], my_value_get_str(&v), sizeof(m->row_names[i]) - 1);
    }
    my_value_reset(&v);
    my_value_init(&v, NULL);
    props(props_ctx, i, "age", &v);
    if (v.type == MY_VALUE_INT32) {
      m->row_ages[i] = my_value_get_int32(&v);
    }
    my_value_reset(&v);
  }
  return MY_RET_OK;
}

static const my_binding_target_vtable_t LIST_VTABLE = {
    list_set_prop, list_get_prop, list_on_event, list_off_event, list_rebuild};

static void list_mock_init(list_mock_t* m) {
  memset(m, 0, sizeof(*m));
  m->base.vtable = &LIST_VTABLE;
  m->emitter = my_emitter_create(NULL);
}

static void set_vm_ptr(my_view_model_t* vm, const char* name, void* ptr) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_pointer(&v, ptr);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

static void test_items_binding_rebuild(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_view_model_array_t* arr = my_view_model_array_dummy_create(NULL);
  list_mock_t target;
  my_binding_context_t* ctx;

  list_mock_init(&target);
  my_view_model_array_dummy_push(arr, person("alice", 30));
  my_view_model_array_dummy_push(arr, person("bob", 25));
  set_vm_ptr(vm, "persons", arr);

  ctx = my_binding_context_create(NULL, vm);
  TEST_ASSERT_EQ_INT(
      my_binding_context_bind(ctx, &target.base,
                              "v:items={persons, ItemTemplate=person_item}"),
      MY_RET_OK);

  TEST_ASSERT_EQ_INT(target.rebuild_calls, 1); /* initial build */
  TEST_ASSERT_EQ_INT(target.last_count, 2);
  TEST_ASSERT_EQ_STR(target.last_template, "person_item");
  TEST_ASSERT_EQ_STR(target.row_names[0], "alice");
  TEST_ASSERT_EQ_INT(target.row_ages[1], 25);

  /* array mutation triggers rebuild */
  my_view_model_array_dummy_push(arr, person("carol", 40));
  TEST_ASSERT_EQ_INT(target.rebuild_calls, 2);
  TEST_ASSERT_EQ_INT(target.last_count, 3);
  TEST_ASSERT_EQ_STR(target.row_names[2], "carol");

  my_view_model_array_remove(arr, 0);
  TEST_ASSERT_EQ_INT(target.rebuild_calls, 3);
  TEST_ASSERT_EQ_STR(target.row_names[0], "bob");

  my_binding_context_destroy(ctx);
  my_emitter_destroy(target.emitter);
  my_view_model_unref(vm);
  my_view_model_array_unref(arr);
}

static void test_condition_binding(void) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  list_mock_t target;
  my_binding_context_t* ctx;
  my_value_t v;
  list_mock_init(&target);

  my_value_init(&v, NULL);
  my_value_set_bool(&v, true);
  my_view_model_set_prop(vm, "is_admin", &v);

  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base, "v:visible={Condition=is_admin}");
  TEST_ASSERT(target.visible);

  my_value_set_bool(&v, false);
  my_view_model_set_prop(vm, "is_admin", &v);
  TEST_ASSERT(!target.visible);

  /* negated form */
  my_binding_context_bind(ctx, &target.base, "v:visible={Condition=!is_admin}");
  TEST_ASSERT(target.visible); /* !false */

  my_value_reset(&v);
  my_binding_context_destroy(ctx);
  my_emitter_destroy(target.emitter);
  my_view_model_unref(vm);
}

/* ---------------- navigator ---------------- */

static my_navigator_request_t g_last_req;
static int g_nav_calls = 0;
static my_ret_t nav_handle(my_navigator_t* nav,
                           const my_navigator_request_t* req) {
  (void)nav;
  g_nav_calls++;
  g_last_req = *req;
  return MY_RET_OK;
}

static void test_navigator_request(void) {
  my_navigator_t nav = {nav_handle};
  my_navigator_request_t req;
  g_nav_calls = 0;
  memset(&g_last_req, 0, sizeof(g_last_req));

  memset(&req, 0, sizeof(req));
  req.type = MY_NAV_TO;
  strcpy(req.target, "detail");
  strcpy(req.args, "Id=7");

  my_navigator_set_default(NULL);
  TEST_ASSERT_EQ_INT(my_navigator_request(&req), MY_RET_NOT_FOUND);

  my_navigator_set_default(&nav);
  TEST_ASSERT_EQ_INT(my_navigator_request(&req), MY_RET_OK);
  TEST_ASSERT_EQ_INT(g_nav_calls, 1);
  TEST_ASSERT_EQ_STR(g_last_req.target, "detail");
  TEST_ASSERT_EQ_STR(g_last_req.args, "Id=7");

  my_navigator_set_default(NULL);
}

static void test_command_topage_with_prop_args(void) {
  my_navigator_t nav = {nav_handle};
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  list_mock_t target;
  my_binding_context_t* ctx;
  my_value_t v;
  list_mock_init(&target);
  g_nav_calls = 0;
  memset(&g_last_req, 0, sizeof(g_last_req));

  my_value_init(&v, NULL);
  my_value_set_int32(&v, 42);
  my_view_model_set_prop(vm, "id", &v);
  my_value_reset(&v);

  my_navigator_set_default(&nav);
  ctx = my_binding_context_create(NULL, vm);
  my_binding_context_bind(ctx, &target.base,
                          "v:on_click={goto, ToPage=detail, Args=Id={id}}");

  my_emitter_emit(target.emitter, "click", NULL);
  TEST_ASSERT_EQ_INT(g_nav_calls, 1);
  TEST_ASSERT_EQ_STR(g_last_req.target, "detail");
  TEST_ASSERT_EQ_STR(g_last_req.args, "Id=42"); /* {id} substituted */

  my_navigator_set_default(NULL);
  my_binding_context_destroy(ctx);
  my_emitter_destroy(target.emitter);
  my_view_model_unref(vm);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_view_model_t* vm = my_view_model_dummy_create(dbg);
  my_view_model_array_t* arr = my_view_model_array_dummy_create(dbg);
  list_mock_t target;
  my_binding_context_t* ctx;
  list_mock_init(&target);

  my_view_model_array_dummy_push(arr, person("x", 1));
  my_view_model_array_dummy_push(arr, person("y", 2));
  set_vm_ptr(vm, "persons", arr);

  ctx = my_binding_context_create(dbg, vm);
  my_binding_context_bind(ctx, &target.base,
                          "v:items={persons, ItemTemplate=row}");
  my_binding_context_bind(ctx, &target.base, "v:visible={Condition=!flag}");
  my_view_model_array_remove(arr, 0);

  my_binding_context_destroy(ctx);
  my_emitter_destroy(target.emitter);
  my_view_model_unref(vm);
  my_view_model_array_unref(arr);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_array_crud_and_notify);
  MYTEST_RUN(test_items_binding_rebuild);
  MYTEST_RUN(test_condition_binding);
  MYTEST_RUN(test_navigator_request);
  MYTEST_RUN(test_command_topage_with_prop_args);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
