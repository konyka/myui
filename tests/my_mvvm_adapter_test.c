/**
 * @file my_mvvm_adapter_test.c
 * @brief End-to-end tests for the myui MVVM adapter (dummy PAL).
 */
#include "mymvvm_myui/my_mvvm.h"
#include "mymvvm_myui/my_widget_target.h"

#include "mypal/dummy/my_pal_dummy.h"
#include "mymvvm/my_view_model_array.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_label.h"

#include "mytest.h"

static void set_vm_str(my_view_model_t* vm, const char* name, const char* s) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_str(&v, s);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

static void set_vm_ptr(my_view_model_t* vm, const char* name, void* p) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_pointer(&v, p);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

static my_view_model_t* person(const char* name) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  set_vm_str(vm, "name", name);
  return vm;
}

static void click_widget(my_window_t* win, my_widget_t* widget) {
  int32_t cx = widget->rect.w / 2;
  int32_t cy = widget->rect.h / 2;
  my_event_t e;
  my_widget_local_to_global(widget, &cx, &cy);
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  my_window_on_pal_event(win, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  my_window_on_pal_event(win, &e);
}

/* ---------------- widget target property mapping ---------------- */

static void test_widget_target_props(void) {
  my_widget_t* btn = my_button_create(NULL, "ok");
  my_widget_target_t* t = my_widget_target_create(NULL, btn);
  my_value_t v;
  my_value_init(&v, NULL);

  my_value_set_str(&v, "Apply");
  TEST_ASSERT_EQ_INT(my_binding_target_set_prop(&t->base, "text", &v),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(((my_button_t*)btn)->text, "Apply");

  my_value_reset(&v);
  my_value_init(&v, NULL);
  my_binding_target_get_prop(&t->base, "text", &v);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "Apply");

  my_value_reset(&v);
  my_value_init(&v, NULL);
  my_value_set_bool(&v, false);
  my_binding_target_set_prop(&t->base, "visible", &v);
  TEST_ASSERT(!btn->visible);
  my_binding_target_get_prop(&t->base, "visible", &v);
  TEST_ASSERT(!my_value_get_bool(&v));

  my_value_reset(&v);
  my_value_init(&v, NULL);
  my_value_set_int32(&v, 123);
  my_binding_target_set_prop(&t->base, "x", &v);
  TEST_ASSERT_EQ_INT(btn->rect.x, 123);

  my_widget_target_destroy(t);
  my_widget_unref(btn);
}

/* ---------------- end-to-end bind ---------------- */

typedef struct app_ctx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_view_model_t* vm;
  my_mvvm_context_t* mc;
} app_ctx_t;

static void app_init(app_ctx_t* a) {
  a->pal = my_pal_dummy_create(NULL);
  a->loop = my_pal_main_loop_create(a->pal);
  a->wm = my_window_manager_create(NULL, a->pal, a->loop);
  a->win = my_window_create(NULL, a->pal, 400, 300, "mvvm");
  a->vm = NULL;
  a->mc = NULL;
}

static void app_destroy(app_ctx_t* a) {
  my_mvvm_context_destroy(a->mc);
  if (a->vm != NULL) {
    my_view_model_unref(a->vm);
  }
  my_window_manager_destroy(a->wm);
  my_pal_main_loop_destroy(a->loop);
  my_pal_destroy(a->pal);
}

static int g_saves = 0;
static my_ret_t on_save(void* ctx, const char* args) {
  int* n = ctx != NULL ? (int*)ctx : &g_saves;
  (void)args;
  (*n)++;
  return MY_RET_OK;
}

static void test_bind_end_to_end(void) {
  app_ctx_t a;
  my_widget_t* label;
  my_widget_t* btn;
  app_init(&a);
  a.vm = my_view_model_dummy_create(NULL);
  g_saves = 0;
  my_view_model_dummy_add_command(a.vm, "save", on_save, &g_saves);
  set_vm_str(a.vm, "title", "hello");

  label = my_label_create(NULL, "?");
  my_widget_set_rect(label, &(my_rect_t){0, 0, 100, 20});
  my_widget_set_bind_rules(label, "v:text={title}");
  my_widget_add_child(my_window_widget(a.win), label);
  my_widget_unref(label);

  btn = my_button_create(NULL, "save");
  my_widget_set_rect(btn, &(my_rect_t){0, 40, 80, 30});
  my_widget_set_bind_rules(btn, "v:on_click={save}");
  my_widget_add_child(my_window_widget(a.win), btn);
  my_widget_unref(btn);

  a.mc = my_mvvm_bind(a.wm, a.win, a.vm);
  TEST_ASSERT_NOT_NULL(a.mc);
  TEST_ASSERT_EQ_STR(((my_label_t*)label)->text, "hello"); /* initial push */

  set_vm_str(a.vm, "title", "world"); /* vm change drives the widget */
  TEST_ASSERT_EQ_STR(((my_label_t*)label)->text, "world");

  click_widget(a.win, btn); /* widget event drives the vm command */
  TEST_ASSERT_EQ_INT(g_saves, 1);

  app_destroy(&a);
}

static void test_close_window_command(void) {
  app_ctx_t a;
  my_widget_t* btn;
  app_init(&a);
  a.vm = my_view_model_dummy_create(NULL);
  my_view_model_dummy_add_command(a.vm, "close", on_save, NULL);

  btn = my_button_create(NULL, "close");
  my_widget_set_rect(btn, &(my_rect_t){0, 0, 80, 30});
  my_widget_set_bind_rules(btn, "v:on_click={close, CloseWindow=true}");
  my_widget_add_child(my_window_widget(a.win), btn);
  my_widget_unref(btn);

  my_window_manager_open(a.wm, a.win);
  a.mc = my_mvvm_bind(a.wm, a.win, a.vm);
  TEST_ASSERT_EQ_INT(my_window_manager_count(a.wm), 1);

  click_widget(a.win, btn);
  TEST_ASSERT_EQ_INT(my_window_manager_count(a.wm), 0);
  TEST_ASSERT(a.wm->quit_requested);

  app_destroy(&a);
}

/* ---------------- items end-to-end ---------------- */

static my_widget_t* build_person_row(my_widget_t* parent, size_t index,
                                     my_item_props_fn_t props, void* props_ctx,
                                     void* builder_ctx) {
  my_value_t v;
  my_widget_t* label;
  (void)parent;
  (void)builder_ctx;
  my_value_init(&v, NULL);
  props(props_ctx, index, "name", &v);
  label = my_label_create(NULL, my_value_get_str(&v));
  my_value_reset(&v);
  my_widget_set_rect(label, &(my_rect_t){0, (int32_t)index * 24, 100, 20});
  return label;
}

static void test_items_end_to_end(void) {
  app_ctx_t a;
  my_view_model_array_t* arr;
  my_widget_t* list;
  app_init(&a);
  a.vm = my_view_model_dummy_create(NULL);
  arr = my_view_model_array_dummy_create(NULL);
  my_view_model_array_dummy_push(arr, person("alice"));
  my_view_model_array_dummy_push(arr, person("bob"));
  set_vm_ptr(a.vm, "persons", arr);

  my_mvvm_register_template("person_item", build_person_row, NULL);

  list = my_widget_create(NULL, "list");
  my_widget_set_rect(list, &(my_rect_t){0, 60, 200, 200});
  my_widget_set_bind_rules(list, "v:items={persons, ItemTemplate=person_item}");
  my_widget_add_child(my_window_widget(a.win), list);
  my_widget_unref(list);

  a.mc = my_mvvm_bind(a.wm, a.win, a.vm);
  TEST_ASSERT_NOT_NULL(a.mc);
  TEST_ASSERT_EQ_INT(my_widget_child_count(list), 2);
  TEST_ASSERT_EQ_STR(((my_label_t*)my_widget_get_child(list, 0))->text, "alice");

  /* array change rebuilds the rows */
  my_view_model_array_remove(arr, 0);
  TEST_ASSERT_EQ_INT(my_widget_child_count(list), 1);
  TEST_ASSERT_EQ_STR(((my_label_t*)my_widget_get_child(list, 0))->text, "bob");

  my_view_model_array_unref(arr);
  app_destroy(&a);
}

/* ---------------- navigator ---------------- */

static my_window_t* detail_page(my_pal_t* pal, const char* args, void* ctx) {
  (void)args;
  (void)ctx;
  return my_window_create(NULL, pal, 200, 100, "detail");
}

static void test_navigator_wm(void) {
  app_ctx_t a;
  my_widget_t* btn;
  my_navigator_wm_t* nav;
  app_init(&a);
  a.vm = my_view_model_dummy_create(NULL);
  nav = my_navigator_wm_create(NULL, a.wm, a.pal);
  my_navigator_wm_add_page(nav, "detail", detail_page, NULL);
  my_navigator_set_default((my_navigator_t*)nav);

  btn = my_button_create(NULL, "go");
  my_widget_set_rect(btn, &(my_rect_t){0, 0, 80, 30});
  my_widget_set_bind_rules(btn, "v:on_click={goto, ToPage=detail}");
  my_widget_add_child(my_window_widget(a.win), btn);
  my_widget_unref(btn);

  my_window_manager_open(a.wm, a.win);
  a.mc = my_mvvm_bind(a.wm, a.win, a.vm);

  click_widget(a.win, btn);
  TEST_ASSERT_EQ_INT(my_window_manager_count(a.wm), 2); /* detail opened */

  {
    my_navigator_request_t back = {MY_NAV_BACK, "", ""};
    my_navigator_request(&back);
  }
  TEST_ASSERT_EQ_INT(my_window_manager_count(a.wm), 1);

  my_navigator_set_default(NULL);
  my_navigator_wm_destroy(nav);
  app_destroy(&a);
}

/* ---------------- edit TwoWay end-to-end ---------------- */

static void type_into_edit(my_widget_t* edit, const char* keys) {
  const char* p;
  ((my_edit_t*)edit)->focused = true;
  for (p = keys; *p != '\0'; p++) {
    my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
    e.u.key.key = (uint8_t)*p;
    edit->vtable->on_event(edit, &e);
  }
}

static void test_edit_two_way_end_to_end(void) {
  app_ctx_t a;
  my_widget_t* edit;
  my_value_t out;
  app_init(&a);
  a.vm = my_view_model_dummy_create(NULL);
  set_vm_str(a.vm, "name", "init");

  edit = my_edit_create(NULL);
  my_widget_set_rect(edit, &(my_rect_t){0, 0, 200, 28});
  my_widget_set_bind_rules(edit, "v:text={name, Mode=TwoWay}");
  my_widget_add_child(my_window_widget(a.win), edit);
  my_widget_unref(edit);

  a.mc = my_mvvm_bind(a.wm, a.win, a.vm);
  TEST_ASSERT_NOT_NULL(a.mc);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "init"); /* vm -> edit */

  type_into_edit(edit, "X"); /* edit -> vm via "changed" */
  my_value_init(&out, NULL);
  my_view_model_get_prop(a.vm, "name", &out);
  TEST_ASSERT_EQ_STR(my_value_get_str(&out), "initX");

  set_vm_str(a.vm, "name", "from_vm"); /* vm -> edit again */
  my_value_reset(&out);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "from_vm");

  app_destroy(&a);
}

static void test_edit_validator_rejects(void) {
  app_ctx_t a;
  my_widget_t* edit;
  my_value_t out;
  app_init(&a);
  a.vm = my_view_model_dummy_create(NULL);
  set_vm_str(a.vm, "name", "alice");

  edit = my_edit_create(NULL);
  my_widget_set_rect(edit, &(my_rect_t){0, 0, 200, 28});
  my_widget_set_bind_rules(edit, "v:text={name, Mode=TwoWay, Validator=not_empty}");
  my_widget_add_child(my_window_widget(a.win), edit);
  my_widget_unref(edit);

  a.mc = my_mvvm_bind(a.wm, a.win, a.vm);

  /* select all + delete: empty writeback must be rejected and restored */
  {
    my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
    ((my_edit_t*)edit)->focused = true;
    e.u.key.key = 'a';
    e.u.key.modifiers = MY_KEYMOD_CTRL;
    edit->vtable->on_event(edit, &e);
    e.u.key.key = MY_KEY_BACKSPACE;
    e.u.key.modifiers = 0;
    edit->vtable->on_event(edit, &e);
  }

  my_value_init(&out, NULL);
  my_view_model_get_prop(a.vm, "name", &out);
  TEST_ASSERT_EQ_STR(my_value_get_str(&out), "alice"); /* vm unchanged */
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "alice"); /* edit restored */
  my_value_reset(&out);

  app_destroy(&a);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(dbg);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(dbg, pal, loop);
  my_window_t* win = my_window_create(dbg, pal, 300, 200, "leak");
  my_view_model_t* vm = my_view_model_dummy_create(dbg);
  my_view_model_array_t* arr = my_view_model_array_dummy_create(dbg);
  my_mvvm_context_t* mc;
  my_widget_t* label;
  my_widget_t* list;

  set_vm_str(vm, "t", "x");
  my_view_model_array_dummy_push(arr, person("p"));
  set_vm_ptr(vm, "persons", arr);
  my_mvvm_register_template("person_item", build_person_row, NULL);

  label = my_label_create(NULL, "?");
  my_widget_set_rect(label, &(my_rect_t){0, 0, 50, 20});
  my_widget_set_bind_rules(label, "v:text={t}");
  my_widget_add_child(my_window_widget(win), label);
  my_widget_unref(label);

  list = my_widget_create(NULL, "list");
  my_widget_set_rect(list, &(my_rect_t){0, 30, 100, 100});
  my_widget_set_bind_rules(list, "v:items={persons, ItemTemplate=person_item}");
  my_widget_add_child(my_window_widget(win), list);
  my_widget_unref(list);

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win)); /* manager holds the only ref */
  mc = my_mvvm_bind(wm, win, vm);
  set_vm_str(vm, "t", "y");
  my_view_model_array_dummy_push(arr, person("q"));

  my_mvvm_context_destroy(mc);
  my_view_model_unref(vm);
  my_view_model_array_unref(arr);
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_widget_target_props);
  MYTEST_RUN(test_bind_end_to_end);
  MYTEST_RUN(test_close_window_command);
  MYTEST_RUN(test_items_end_to_end);
  MYTEST_RUN(test_navigator_wm);
  MYTEST_RUN(test_edit_two_way_end_to_end);
  MYTEST_RUN(test_edit_validator_rejects);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
