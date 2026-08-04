/**
 * @file my_theme_test.c
 * @brief Unit tests for my_style / my_theme / text loader / resolution.
 */
#include "myui/my_theme.h"
#include "myui/widgets/my_button.h"

#include "mytest.h"

static void test_style_set_get_and_state_fallback(void) {
  my_style_t s;
  my_style_init(&s, NULL);

  my_style_set_color(&s, MY_STATE_NORMAL, "bg_color", 0xFF0000FF);
  my_style_set_color(&s, MY_STATE_PRESSED, "bg_color", 0x00FF00FF);

  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_style_get(&s, MY_STATE_NORMAL, "bg_color")),
                     0xFF0000FF);
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_style_get(&s, MY_STATE_PRESSED, "bg_color")),
                     0x00FF00FF);
  /* hover unset: falls back to normal */
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_style_get(&s, MY_STATE_HOVER, "bg_color")),
                     0xFF0000FF);
  TEST_ASSERT_NULL(my_style_get(&s, MY_STATE_NORMAL, "nope"));
  TEST_ASSERT_NULL(my_style_get(NULL, MY_STATE_NORMAL, "x"));

  my_style_reset(&s);
  TEST_ASSERT_NULL(my_style_get(&s, MY_STATE_NORMAL, "bg_color"));
  my_style_reset(&s);
}

static void test_theme_resolution_order(void) {
  my_theme_t* t = my_theme_create(NULL);

  my_theme_set_color(t, "button", NULL, MY_STATE_NORMAL, "bg_color", 0x111111FF);
  my_theme_set_color(t, "button", "ok", MY_STATE_NORMAL, "bg_color", 0x222222FF);
  my_theme_set_color(t, "button", NULL, MY_STATE_PRESSED, "bg_color", 0x333333FF);

  /* name match wins */
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_theme_get(t, "button", "ok",
                                                     MY_STATE_NORMAL, "bg_color")),
                     0x222222FF);
  /* name rule hit falls back to (name, normal) before (type, pressed) */
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_theme_get(t, "button", "ok",
                                                     MY_STATE_PRESSED, "bg_color")),
                     0x222222FF);
  /* type rule */
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_theme_get(t, "button", NULL,
                                                     MY_STATE_NORMAL, "bg_color")),
                     0x111111FF);
  /* missing key */
  TEST_ASSERT_NULL(my_theme_get(t, "button", NULL, MY_STATE_NORMAL, "fg_color"));
  TEST_ASSERT_NULL(my_theme_get(t, "label", NULL, MY_STATE_NORMAL, "bg_color"));

  my_theme_destroy(t);
}

static void test_load_str(void) {
  my_theme_t* t = my_theme_create(NULL);
  const char* text =
      "; comment line\n"
      "\n"
      "button.normal.bg_color=#FF4081\n"
      "button[ok].pressed.bg_color=#C60055\n"
      "label.font_size=16\n"
      "label.normal.fg_color=#11223344\n";

  TEST_ASSERT_EQ_INT(my_theme_load_str(t, text), MY_RET_OK);

  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_theme_get(t, "button", NULL,
                                                     MY_STATE_NORMAL, "bg_color")),
                     0xFF4081FF);
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_theme_get(t, "button", "ok",
                                                     MY_STATE_PRESSED, "bg_color")),
                     0xC60055FF);
  /* no state in the rule: applied to ALL states */
  TEST_ASSERT_EQ_INT(my_value_get_int32(my_theme_get(t, "label", NULL,
                                                    MY_STATE_DISABLED, "font_size")),
                     16);
  TEST_ASSERT_EQ_INT(my_value_get_uint32(my_theme_get(t, "label", NULL,
                                                     MY_STATE_NORMAL, "fg_color")),
                     0x11223344);

  my_theme_destroy(t);
}

static void test_load_str_invalid(void) {
  my_theme_t* t = my_theme_create(NULL);
  TEST_ASSERT_EQ_INT(my_theme_load_str(t, "button.bg_color"), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_theme_load_str(t, "button.normal.bg_color=#XYZ"),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_theme_load_str(t, "button[.normal.bg_color=#112233"),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_theme_load_str(NULL, "a.b=1"), MY_RET_INVALID_PARAMS);
  my_theme_destroy(t);
}

static void test_widget_query_priority(void) {
  my_theme_t* t = my_theme_create(NULL);
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* btn = my_button_create(NULL, "hi");
  my_value_t v;

  my_widget_add_child(root, btn);
  my_widget_unref(btn);
  my_widget_set_name(btn, "hi"); /* theme [name] selector matches widget name */

  my_theme_set_color(t, "button", NULL, MY_STATE_NORMAL, "bg_color", 0xAAAAAAFF);
  my_theme_set_color(t, "button", "hi", MY_STATE_NORMAL, "bg_color", 0xBBBBBBFF);
  my_widget_apply_theme(root, t);

  /* theme name match */
  TEST_ASSERT_EQ_INT(my_widget_style_get_color(btn, MY_STATE_NORMAL, "bg_color", 0),
                     0xBBBBBBFF);
  /* local override wins over everything */
  my_value_init(&v, NULL);
  my_value_set_uint32(&v, 0xCCCCCCFF);
  my_widget_style_set(btn, MY_STATE_NORMAL, "bg_color", &v);
  TEST_ASSERT_EQ_INT(my_widget_style_get_color(btn, MY_STATE_NORMAL, "bg_color", 0),
                     0xCCCCCCFF);
  /* unknown key falls back to the caller default */
  TEST_ASSERT_EQ_INT(my_widget_style_get_color(btn, MY_STATE_NORMAL, "zz", 42), 42);
  TEST_ASSERT_EQ_INT(my_widget_style_get_int(btn, MY_STATE_NORMAL, "zz", 7), 7);

  my_widget_unref(root);
  my_theme_destroy(t);
}

static void test_default_theme(void) {
  my_theme_t* t = my_theme_default_create(NULL);
  TEST_ASSERT_NOT_NULL(t);
  TEST_ASSERT_NOT_NULL(my_theme_get(t, "button", NULL, MY_STATE_NORMAL, "bg_color"));
  TEST_ASSERT_NOT_NULL(my_theme_get(t, "label", NULL, MY_STATE_NORMAL, "fg_color"));
  TEST_ASSERT_NOT_NULL(my_theme_get(t, "window", NULL, MY_STATE_NORMAL, "bg_color"));
  my_theme_destroy(t);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_theme_t* t = my_theme_default_create(dbg);
  my_widget_t* root = my_widget_create(dbg, "root");
  my_widget_t* btn = my_button_create(dbg, "x");
  my_value_t v;

  my_widget_add_child(root, btn);
  my_widget_unref(btn);
  my_theme_load_str(t, "button.normal.bg_color=#FF4081\nlabel.font_size=16\n");
  my_widget_apply_theme(root, t);

  my_value_init(&v, dbg);
  my_value_set_str(&v, "a string value");
  my_widget_style_set(btn, MY_STATE_NORMAL, "title", &v);
  my_value_reset(&v);
  TEST_ASSERT_EQ_STR(my_value_get_str(
                         my_widget_style_get(btn, MY_STATE_NORMAL, "title")),
                     "a string value");

  my_widget_unref(root);
  my_theme_destroy(t);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_style_set_get_and_state_fallback);
  MYTEST_RUN(test_theme_resolution_order);
  MYTEST_RUN(test_load_str);
  MYTEST_RUN(test_load_str_invalid);
  MYTEST_RUN(test_widget_query_priority);
  MYTEST_RUN(test_default_theme);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
