/**
 * @file my_widget_class_test.c
 * @brief Unit tests for the widget class registry (M24a).
 */
#include "myui/my_widget_class.h"

#include "myc/my_str.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_checkbox.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_list_view.h"
#include "myui/widgets/my_progress_bar.h"
#include "myui/widgets/my_slider.h"
#include "myui/widgets/my_text_area.h"

#include "mytest.h"

static const char* const BUILTIN_TAGS[] = {
    "widget", "button",  "label",     "edit",      "checkbox", "slider",
    "progress_bar",      "text_area", "list_view", "image",    "scroll_bar"};

static void test_find_builtins(void) {
  size_t i;
  for (i = 0; i < sizeof(BUILTIN_TAGS) / sizeof(BUILTIN_TAGS[0]); i++) {
    const my_widget_class_t* cls = my_widget_class_find(BUILTIN_TAGS[i]);
    TEST_ASSERT_NOT_NULL(cls);
    if (cls != NULL) {
      TEST_ASSERT_EQ_STR(cls->type, BUILTIN_TAGS[i]);
      TEST_ASSERT_NOT_NULL(cls->create);
    }
  }
  TEST_ASSERT_NULL(my_widget_class_find("no_such_widget"));
  TEST_ASSERT_NULL(my_widget_class_find(NULL));
}

static void test_create_via_class(void) {
  size_t i;
  for (i = 0; i < sizeof(BUILTIN_TAGS) / sizeof(BUILTIN_TAGS[0]); i++) {
    const my_widget_class_t* cls = my_widget_class_find(BUILTIN_TAGS[i]);
    my_widget_t* w;
    TEST_ASSERT_NOT_NULL(cls);
    if (cls == NULL) {
      continue;
    }
    w = cls->create(NULL);
    TEST_ASSERT_NOT_NULL(w);
    if (w != NULL) {
      /* plain containers keep the base "widget" type */
      if (my_str_eq(BUILTIN_TAGS[i], "widget")) {
        TEST_ASSERT_EQ_STR(w->widget_type, "widget");
      } else {
        TEST_ASSERT_EQ_STR(w->widget_type, BUILTIN_TAGS[i]);
      }
      my_widget_unref(w);
    }
  }
}

static void test_button_text_roundtrip(void) {
  my_widget_t* w = my_button_create(NULL, NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "text", "hi"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(((my_button_t*)w)->text, "hi");

  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "text", &v), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_STR);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "hi");
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_label_align_roundtrip(void) {
  my_widget_t* w = my_label_create(NULL, NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "align", "center"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(((my_label_t*)w)->align, MY_TEXT_ALIGN_CENTER);

  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "align", &v), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "center");
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_slider_range_and_value(void) {
  my_widget_t* w = my_slider_create(NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);

  /* prop order min,max,step,value must yield the same range as one
   * set_range call (min applied first, then max) */
  TEST_ASSERT_EQ_INT(my_widget_set_prop_float(w, "min", 10.0f), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_float(w, "max", 20.0f), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_float(w, "step", 5.0f), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_float(w, "value", 15.0f), MY_RET_OK);
  TEST_ASSERT(((my_slider_t*)w)->min == 10.0f);
  TEST_ASSERT(((my_slider_t*)w)->max == 20.0f);
  TEST_ASSERT(((my_slider_t*)w)->step == 5.0f);
  TEST_ASSERT(my_slider_get_value(w) == 15.0f);

  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "value", &v), MY_RET_OK);
  TEST_ASSERT(my_value_get_double(&v) == 15.0);
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "min", &v), MY_RET_OK);
  TEST_ASSERT(my_value_get_float(&v) == 10.0f);
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "max", &v), MY_RET_OK);
  TEST_ASSERT(my_value_get_float(&v) == 20.0f);
  my_value_reset(&v);

  /* numeric coercion: INT32/DOUBLE values are accepted for float props */
  my_value_init(&v, NULL);
  my_value_set_int32(&v, 20);
  TEST_ASSERT_EQ_INT(my_widget_set_prop(w, "value", &v), MY_RET_OK);
  my_value_reset(&v);
  TEST_ASSERT(my_slider_get_value(w) == 20.0f);

  /* step is write-only */
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "step", &v),
                     MY_RET_NOT_SUPPORTED);
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_checkbox_value_and_checked(void) {
  my_widget_t* w = my_checkbox_create(NULL, NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "text", "agree"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(((my_checkbox_t*)w)->text, "agree");

  /* "value" and "checked" are aliases (MVVM binds "value") */
  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "value", true), MY_RET_OK);
  TEST_ASSERT(my_checkbox_get_checked(w));
  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "checked", false), MY_RET_OK);
  TEST_ASSERT(!my_checkbox_get_checked(w));

  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "value", &v), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_type(&v), MY_VALUE_BOOL);
  TEST_ASSERT(!my_value_get_bool(&v));
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "checked", &v), MY_RET_OK);
  TEST_ASSERT(!my_value_get_bool(&v));
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "text", &v), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "agree");
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_progress_bar_value(void) {
  my_widget_t* w = my_progress_bar_create(NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_float(w, "value", 60.0f), MY_RET_OK);
  TEST_ASSERT(my_progress_bar_get_value(w) == 60.0f);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "value", &v), MY_RET_OK);
  TEST_ASSERT(my_value_get_double(&v) == 60.0);
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_text_area_props(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "wrap", true), MY_RET_OK);
  TEST_ASSERT(((my_text_area_t*)w)->wrap);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "wrap", &v), MY_RET_OK);
  TEST_ASSERT(my_value_get_bool(&v));
  my_value_reset(&v);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "align", "right"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(((my_text_area_t*)w)->align, MY_TEXT_ALIGN_RIGHT);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "align", &v), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "right");
  my_value_reset(&v);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "text", "l1"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "l1");
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "text", &v), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "l1");
  my_value_reset(&v);

  /* write-only props */
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "hint", "notes"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(((my_text_area_t*)w)->hint, "notes");
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "hint", &v),
                     MY_RET_NOT_SUPPORTED);
  my_value_reset(&v);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "max_len", 8), MY_RET_OK);
  TEST_ASSERT_EQ_INT(((my_text_area_t*)w)->max_len, 8);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "max_len", &v),
                     MY_RET_NOT_SUPPORTED);
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_list_view_row_height(void) {
  my_widget_t* w = my_list_view_create(NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "row_height", 33), MY_RET_OK);
  TEST_ASSERT_EQ_INT(((my_list_view_t*)w)->row_height, 33);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "row_height", &v), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 33);
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_edit_props(void) {
  my_widget_t* w = my_edit_create(NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "hint", "name"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(((my_edit_t*)w)->hint, "name");
  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "password", true), MY_RET_OK);
  TEST_ASSERT(((my_edit_t*)w)->password);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "readonly", true), MY_RET_OK);
  TEST_ASSERT(((my_edit_t*)w)->readonly);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "max_len", 8), MY_RET_OK);
  TEST_ASSERT_EQ_INT(((my_edit_t*)w)->max_len, 8);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "text", "abc"), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "abc");

  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "text", &v), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "abc");
  my_value_reset(&v);

  /* hint/password/readonly/max_len are write-only */
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "hint", &v),
                     MY_RET_NOT_SUPPORTED);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "password", &v),
                     MY_RET_NOT_SUPPORTED);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "readonly", &v),
                     MY_RET_NOT_SUPPORTED);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "max_len", &v),
                     MY_RET_NOT_SUPPORTED);
  my_value_reset(&v);
  my_widget_unref(w);
}

static void test_unknown_prop_not_supported(void) {
  my_widget_t* w = my_button_create(NULL, NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);
  my_value_init(&v, NULL);
  my_value_set_int32(&v, 1);
  TEST_ASSERT_EQ_INT(my_widget_set_prop(w, "nope", &v),
                     MY_RET_NOT_SUPPORTED);
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "nope", &v),
                     MY_RET_NOT_SUPPORTED);
  my_value_reset(&v);
  /* a prop the class does not have (widget has no "text") */
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(w, "row_height", "x"),
                     MY_RET_NOT_SUPPORTED);
  my_widget_unref(w);
}

static void test_common_props(void) {
  my_widget_t* w = my_button_create(NULL, NULL);
  my_value_t v;
  TEST_ASSERT_NOT_NULL(w);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "visible", false), MY_RET_OK);
  TEST_ASSERT(!w->visible);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "enable", false), MY_RET_OK);
  TEST_ASSERT(!w->enable);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "x", 7), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "y", 8), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "w", 80), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "h", 30), MY_RET_OK);
  TEST_ASSERT_EQ_INT(w->rect.x, 7);
  TEST_ASSERT_EQ_INT(w->rect.y, 8);
  TEST_ASSERT_EQ_INT(w->rect.w, 80);
  TEST_ASSERT_EQ_INT(w->rect.h, 30);

  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "visible", &v), MY_RET_OK);
  TEST_ASSERT(!my_value_get_bool(&v));
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "enable", &v), MY_RET_OK);
  TEST_ASSERT(!my_value_get_bool(&v));
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "x", &v), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 7);
  my_value_reset(&v);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "w", &v), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 80);
  my_value_reset(&v);

  /* common props work on every widget, even without a class prop table */
  my_widget_unref(w);
  w = my_widget_create(NULL, "c");
  TEST_ASSERT_NOT_NULL(w);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_bool(w, "visible", false), MY_RET_OK);
  TEST_ASSERT(!w->visible);
  my_widget_unref(w);
}

/* ---------------- custom class registration ---------------- */

static int32_t g_custom_level;

static my_widget_t* custom_create(const my_allocator_t* a) {
  return my_widget_create(a, "custom");
}

static my_ret_t custom_set_level(my_widget_t* w, const my_value_t* v) {
  (void)w;
  g_custom_level = v->type == MY_VALUE_INT32 ? my_value_get_int32(v) : 0;
  return MY_RET_OK;
}

static my_ret_t custom_get_level(const my_widget_t* w, my_value_t* v) {
  (void)w;
  return my_value_set_int32(v, g_custom_level);
}

static const my_prop_desc_t CUSTOM_PROPS[] = {
    {"level", MY_PROP_INT, custom_set_level, custom_get_level},
    {NULL, MY_PROP_STRING, NULL, NULL}};

static const my_widget_class_t CUSTOM_CLASS = {
    "custom", custom_create, CUSTOM_PROPS, NULL};

static void test_custom_class_register(void) {
  my_widget_t* w;
  my_value_t v;
  TEST_ASSERT_EQ_INT(my_widget_class_register(&CUSTOM_CLASS), MY_RET_OK);
  TEST_ASSERT(my_widget_class_find("custom") == &CUSTOM_CLASS);
  /* re-registering the same type overrides instead of growing */
  TEST_ASSERT_EQ_INT(my_widget_class_register(&CUSTOM_CLASS), MY_RET_OK);

  w = my_widget_create(NULL, "c");
  TEST_ASSERT_NOT_NULL(w);
  w->widget_type = "custom"; /* white-box: pretend a custom widget type */
  TEST_ASSERT_EQ_INT(my_widget_set_prop_int(w, "level", 42), MY_RET_OK);
  TEST_ASSERT_EQ_INT(g_custom_level, 42);
  my_value_init(&v, NULL);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(w, "level", &v), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 42);
  my_value_reset(&v);
  my_widget_unref(w);

  TEST_ASSERT_EQ_INT(my_widget_class_register(NULL), MY_RET_INVALID_PARAMS);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* button = my_button_create(dbg, NULL);
  my_widget_t* edit = my_edit_create(dbg);
  my_widget_t* ta = my_text_area_create(dbg);
  my_widget_t* cb = my_checkbox_create(dbg, NULL);
  my_widget_t* label = my_label_create(dbg, NULL);
  my_value_t v;

  TEST_ASSERT_NOT_NULL(button);
  TEST_ASSERT_NOT_NULL(edit);
  TEST_ASSERT_NOT_NULL(ta);
  TEST_ASSERT_NOT_NULL(cb);
  TEST_ASSERT_NOT_NULL(label);

  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(button, "text", "hi"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(edit, "hint", "h"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(edit, "text", "abc"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(ta, "text", "xyz"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(cb, "text", "agree"), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_widget_set_prop_str(label, "align", "center"),
                     MY_RET_OK);

  my_value_init(&v, dbg);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(button, "text", &v), MY_RET_OK);
  my_value_reset(&v);
  my_value_init(&v, dbg);
  TEST_ASSERT_EQ_INT(my_widget_get_prop(ta, "text", &v), MY_RET_OK);
  my_value_reset(&v);

  my_widget_unref(button);
  my_widget_unref(edit);
  my_widget_unref(ta);
  my_widget_unref(cb);
  my_widget_unref(label);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_find_builtins);
  MYTEST_RUN(test_create_via_class);
  MYTEST_RUN(test_button_text_roundtrip);
  MYTEST_RUN(test_label_align_roundtrip);
  MYTEST_RUN(test_slider_range_and_value);
  MYTEST_RUN(test_checkbox_value_and_checked);
  MYTEST_RUN(test_progress_bar_value);
  MYTEST_RUN(test_text_area_props);
  MYTEST_RUN(test_list_view_row_height);
  MYTEST_RUN(test_edit_props);
  MYTEST_RUN(test_unknown_prop_not_supported);
  MYTEST_RUN(test_common_props);
  MYTEST_RUN(test_custom_class_register);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
