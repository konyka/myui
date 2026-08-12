/**
 * @file my_ui_loader_test.c
 * @brief Unit tests for the XML UI loader (dummy PAL).
 */
#include "myui/my_ui_loader.h"

#include "myc/my_str.h"

#include <stdio.h>

#include "mymvvm/my_view_model.h"
#include "mymvvm_myui/my_mvvm.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_layout.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_checkbox.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_progress_bar.h"
#include "myui/widgets/my_slider.h"
#include "myui/widgets/my_text_area.h"

#include "mytest.h"

#ifndef MYUI_UI_XML

MYTEST_MAIN_BEGIN()
  fprintf(stdout, "SKIP: MYUI_UI_XML off\n");
MYTEST_MAIN_END()

#else

static void test_load_basic_window(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* root = my_ui_load_str(
      NULL, pal,
      "<window w=\"320\" h=\"240\" title=\"t\">"
      "<label name=\"title\" x=\"10\" y=\"5\" w=\"100\" h=\"30\" text=\"hi\"/>"
      "<button x=\"10\" y=\"50\" w=\"80\" h=\"30\" text=\"OK\"/>"
      "</window>",
      NULL);
  my_widget_t* label;
  my_widget_t* btn;

  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_EQ_INT(root->rect.w, 320);
  label = my_widget_find_child(root, "title");
  TEST_ASSERT_NOT_NULL(label);
  TEST_ASSERT_EQ_STR(((my_label_t*)label)->text, "hi");
  TEST_ASSERT_EQ_INT(label->rect.x, 10);
  btn = my_widget_get_child(root, 1);
  TEST_ASSERT_NOT_NULL(btn);
  TEST_ASSERT_EQ_STR(((my_button_t*)btn)->text, "OK");

  my_widget_unref(root);
  my_pal_destroy(pal);
}

static void test_layout_and_lp(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* root = my_ui_load_str(
      NULL, pal,
      "<window w=\"200\" h=\"100\" layout=\"linear:v:4\">"
      "<widget lp=\"h:20\"/>"
      "<widget lp=\"h:1f\"/>"
      "</window>",
      NULL);
  my_widget_t* a;
  my_widget_t* b;
  TEST_ASSERT_NOT_NULL(root);
  a = my_widget_get_child(root, 0);
  b = my_widget_get_child(root, 1);

  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(a->rect.h, 20);
  TEST_ASSERT_EQ_INT(b->rect.y, 24);       /* 20 + spacing 4 */
  TEST_ASSERT_EQ_INT(b->rect.h, 100 - 24); /* flex takes the rest */
  TEST_ASSERT_EQ_INT(a->rect.w, 200);      /* cross axis fills */

  my_widget_unref(root);
  my_pal_destroy(pal);
}

static void test_widget_specific_attrs(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* root = my_ui_load_str(
      NULL, pal,
      "<window w=\"300\" h=\"200\">"
      "<edit hint=\"name\" password=\"true\" max_len=\"8\"/>"
      "<slider min=\"10\" max=\"20\" step=\"5\" value=\"15\"/>"
      "<progress_bar value=\"60\"/>"
      "<checkbox text=\"c\" checked=\"true\"/>"
      "<text_area hint=\"notes\" text=\"l1\\nl2\" readonly=\"false\" wrap=\"true\"/>"
      "</window>",
      NULL);
  my_widget_t* edit = my_widget_get_child(root, 0);
  my_widget_t* slider = my_widget_get_child(root, 1);
  my_widget_t* bar = my_widget_get_child(root, 2);
  my_widget_t* cb = my_widget_get_child(root, 3);
  my_widget_t* ta = my_widget_get_child(root, 4);

  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_EQ_STR(((my_edit_t*)edit)->hint, "name");
  TEST_ASSERT(((my_edit_t*)edit)->password);
  TEST_ASSERT_EQ_INT(((my_edit_t*)edit)->max_len, 8);
  TEST_ASSERT(my_slider_get_value(slider) == 15.0f);
  TEST_ASSERT(my_progress_bar_get_value(bar) == 60.0f);
  TEST_ASSERT(my_checkbox_get_checked(cb));
  TEST_ASSERT_EQ_STR(my_text_area_get_text(ta), "l1\\nl2"); /* XML attr is literal */
  TEST_ASSERT_EQ_STR(((my_text_area_t*)ta)->hint, "notes");

  my_widget_unref(root);
  my_pal_destroy(pal);
}

static void test_visibility_and_enable(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* root = my_ui_load_str(
      NULL, pal,
      "<window w=\"100\" h=\"100\">"
      "<button text=\"a\" visible=\"false\"/>"
      "<button text=\"b\" enable=\"false\" tooltip=\"hint b\"/>"
      "</window>",
      NULL);
  my_widget_t* a = my_widget_get_child(root, 0);
  my_widget_t* b = my_widget_get_child(root, 1);
  TEST_ASSERT(!a->visible);
  TEST_ASSERT(!b->enable);
  TEST_ASSERT(my_str_eq(my_widget_get_tooltip(b), "hint b"));
  my_widget_unref(root);
  my_pal_destroy(pal);
}

static void test_errors(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_ui_error_t err;
  my_widget_t* root;

  root = my_ui_load_str(NULL, pal, "<window><nope/></window>", &err);
  TEST_ASSERT_NULL(root);
  TEST_ASSERT(err.line > 0);
  TEST_ASSERT(err.message[0] != '\0');

  root = my_ui_load_str(NULL, pal, "<window><label lp=\"bad\"/></window>",
                        &err);
  TEST_ASSERT_NULL(root);

  root = my_ui_load_str(NULL, pal, "<window><unclosed></window>", &err);
  TEST_ASSERT_NULL(root); /* xml error surfaced with line */

  root = my_ui_load_str(NULL, NULL, "<window/>", &err);
  TEST_ASSERT_NULL(root); /* window needs pal */

  my_pal_destroy(pal);
}

static void test_style_and_theme(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = (my_window_t*)my_ui_load_str(
      NULL, pal,
      "<window w=\"100\" h=\"100\">"
      "<style>button.normal.bg_color=#FF4081</style>"
      "<button text=\"x\"/>"
      "</window>",
      NULL);
  const my_value_t* v;
  TEST_ASSERT_NOT_NULL(win);
  v = my_theme_get(win->theme, "button", NULL, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQ_INT(my_value_get_uint32(v), 0xFF4081FF);
  my_widget_unref((my_widget_t*)win);
  my_pal_destroy(pal);
}

static void test_bind_rules_passthrough_and_mvvm(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_mvvm_context_t* mc;
  my_value_t v;
  my_widget_t* root = my_ui_load_str(
      NULL, pal,
      "<window w=\"200\" h=\"100\">"
      "<label name=\"out\" lp=\"h:20\" v:text=\"{title}\"/>"
      "<button text=\"go\" v:on_click=\"{save}\"/>"
      "</window>",
      NULL);
  my_widget_t* label;

  TEST_ASSERT_NOT_NULL(root);
  label = my_widget_find_child(root, "out");
  TEST_ASSERT_NOT_NULL(label->bind_rules);
  TEST_ASSERT_NOT_NULL(strstr(label->bind_rules, "v:text={title}"));

  my_value_init(&v, NULL);
  my_value_set_str(&v, "hello");
  my_view_model_set_prop(vm, "title", &v);
  my_value_reset(&v);

  my_window_manager_open(wm, (my_window_t*)root);
  mc = my_mvvm_bind(wm, (my_window_t*)root, vm);
  my_widget_unref(root);
  TEST_ASSERT_NOT_NULL(mc);
  label = my_widget_find_child(my_window_widget(my_window_manager_top(wm)),
                               "out");
  TEST_ASSERT_EQ_STR(((my_label_t*)label)->text, "hello");

  my_mvvm_context_destroy(mc);
  my_view_model_unref(vm);
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(dbg);
  my_widget_t* root = my_ui_load_str(
      dbg, pal,
      "<window w=\"200\" h=\"100\">"
      "<style>label.font_size=16</style>"
      "<label text=\"a\" lp=\"h:20\"/>"
      "<widget layout=\"linear:h:4\"><button text=\"b\"/><edit hint=\"h\"/></widget>"
      "<slider value=\"5\"/><progress_bar value=\"1\"/>"
      "<checkbox text=\"c\"/>"
      "</window>",
      NULL);
  TEST_ASSERT_NOT_NULL(root);
  my_widget_relayout(root);
  my_widget_unref(root);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

#ifdef MY_UI2C_SAMPLE_C
/* the ui2c-generated builder (compiled from the sample XML at build time) */
#include MY_UI2C_SAMPLE_C

static void compare_trees(my_widget_t* a, my_widget_t* b) {
  size_t i, ac, bc;
  TEST_ASSERT_EQ_STR(a->widget_type, b->widget_type);
  TEST_ASSERT(my_str_eq(((my_object_t*)a)->name, ((my_object_t*)b)->name));
  TEST_ASSERT_EQ_INT(a->rect.x, b->rect.x);
  TEST_ASSERT_EQ_INT(a->rect.y, b->rect.y);
  TEST_ASSERT_EQ_INT(a->rect.w, b->rect.w);
  TEST_ASSERT_EQ_INT(a->rect.h, b->rect.h);
  TEST_ASSERT_EQ_INT(a->visible, b->visible);
  TEST_ASSERT_EQ_INT(a->enable, b->enable);
  TEST_ASSERT(my_str_eq(a->bind_rules, b->bind_rules));
  ac = my_widget_child_count(a);
  bc = my_widget_child_count(b);
  TEST_ASSERT_EQ_INT(ac, bc);
  for (i = 0; i < ac && i < bc; i++) {
    compare_trees(my_widget_get_child(a, i), my_widget_get_child(b, i));
  }
}

static void test_ui2c_golden_equivalence(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* runtime = my_ui_load_file(NULL, pal, MY_UI2C_SAMPLE_XML, NULL);
  my_widget_t* generated = ui2c_sample_create(NULL, pal);
  TEST_ASSERT_NOT_NULL(runtime);
  TEST_ASSERT_NOT_NULL(generated);
  if (runtime != NULL && generated != NULL) {
    compare_trees(runtime, generated);
  }
  my_widget_unref(runtime);
  my_widget_unref(generated);
  my_pal_destroy(pal);
}
#endif /* MY_UI2C_SAMPLE_C */

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_load_basic_window);
  MYTEST_RUN(test_layout_and_lp);
  MYTEST_RUN(test_widget_specific_attrs);
  MYTEST_RUN(test_visibility_and_enable);
  MYTEST_RUN(test_errors);
  MYTEST_RUN(test_style_and_theme);
  MYTEST_RUN(test_bind_rules_passthrough_and_mvvm);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
#ifdef MY_UI2C_SAMPLE_C
  MYTEST_RUN(test_ui2c_golden_equivalence);
#endif
MYTEST_MAIN_END()

#endif /* MYUI_UI_XML */
