/**
 * @file my_css_test.c
 * @brief CSS subset parser + theme bridge tests (M18a): selector forms,
 * value forms, groups/descendants/pseudo, comments/@-skip, malformed
 * handling, cascade priority (id > class > type), source order,
 * coexistence with the text format, aliases, widget style_class,
 * leaks.
 */
#include "myui/my_css.h"

#include <string.h>

#include "myui/my_theme.h"
#include "myui/widgets/my_button.h"

#include "mytest.h"

/* ---------------- parser ---------------- */

static my_css_sheet_t* parse(const char* css) {
  return my_css_parse(NULL, css, strlen(css), NULL);
}

static void test_selector_forms(void) {
  my_css_sheet_t* s = parse("button { color: red }");
  const my_css_selector_t* sel;
  TEST_ASSERT(s != NULL);
  TEST_ASSERT_EQ_INT((int)my_css_rule_count(s), 1);
  sel = my_css_selector(my_css_rule(s, 0), 0);
  TEST_ASSERT_EQ_STR(sel->widget_type, "button");
  TEST_ASSERT(sel->id[0] == '\0' && sel->style_class[0] == '\0');
  TEST_ASSERT_EQ_INT(sel->state, -1);
  my_css_sheet_destroy(s);

  s = parse(".primary { color: red }");
  sel = my_css_selector(my_css_rule(s, 0), 0);
  TEST_ASSERT_EQ_STR(sel->style_class, "primary");
  TEST_ASSERT(sel->widget_type[0] == '\0');
  my_css_sheet_destroy(s);

  s = parse("#ok { color: red }");
  sel = my_css_selector(my_css_rule(s, 0), 0);
  TEST_ASSERT_EQ_STR(sel->id, "ok");
  my_css_sheet_destroy(s);

  s = parse("button.primary:hover { color: red }");
  sel = my_css_selector(my_css_rule(s, 0), 0);
  TEST_ASSERT_EQ_STR(sel->widget_type, "button");
  TEST_ASSERT_EQ_STR(sel->style_class, "primary");
  TEST_ASSERT_EQ_INT(sel->state, MY_STATE_HOVER);
  my_css_sheet_destroy(s);

  s = parse("button#ok:pressed { color: red }");
  sel = my_css_selector(my_css_rule(s, 0), 0);
  TEST_ASSERT_EQ_STR(sel->widget_type, "button");
  TEST_ASSERT_EQ_STR(sel->id, "ok");
  TEST_ASSERT_EQ_INT(sel->state, MY_STATE_PRESSED);
  my_css_sheet_destroy(s);

  /* descendant: window button (simplified to ancestor type) */
  s = parse("window button { color: red }");
  sel = my_css_selector(my_css_rule(s, 0), 0);
  TEST_ASSERT_EQ_STR(sel->ancestor_type, "window");
  TEST_ASSERT_EQ_STR(sel->widget_type, "button");
  my_css_sheet_destroy(s);

  /* comma group = two selectors on one rule */
  s = parse("button, .primary { color: red }");
  TEST_ASSERT_EQ_INT((int)my_css_selector_count(my_css_rule(s, 0)), 2);
  my_css_sheet_destroy(s);
}

static void test_value_forms(void) {
  my_css_sheet_t* s =
      parse("x { a: #f00; b: #ff0000; c: #ff000080; d: rgb(1,2,3);"
            "e: rgba(255,0,0,0.5); f: rgba(255,0,0,128); g: blue;"
            "h: 16px; i: 16; j: 0.5; k: \"quoted\"; l: some-ident }");
  const my_css_rule_t* r;
  TEST_ASSERT(s != NULL);
  r = my_css_rule(s, 0);
  TEST_ASSERT_EQ_INT((int)my_css_decl_count(r), 12);
#define DECL_RGB(i) my_value_get_uint32(&my_css_decl(r, i)->value)
  TEST_ASSERT(DECL_RGB(0) == 0xFF0000FFu);   /* #f00 */
  TEST_ASSERT(DECL_RGB(1) == 0xFF0000FFu);   /* #rrggbb */
  TEST_ASSERT(DECL_RGB(2) == 0xFF000080u);   /* #rrggbbaa */
  TEST_ASSERT(DECL_RGB(3) == 0x010203FFu);   /* rgb() */
  TEST_ASSERT(DECL_RGB(4) == 0xFF000080u);   /* rgba alpha 0-1 */
  TEST_ASSERT(DECL_RGB(5) == 0xFF000080u);   /* rgba alpha 0-255 */
  TEST_ASSERT(DECL_RGB(6) == 0x0000FFFFu);   /* blue */
  TEST_ASSERT(my_value_get_int32(&my_css_decl(r, 7)->value) == 16); /* px */
  TEST_ASSERT(my_value_get_int32(&my_css_decl(r, 8)->value) == 16); /* int */
  TEST_ASSERT(my_css_decl(r, 9)->value.type == MY_VALUE_DOUBLE);
  TEST_ASSERT(my_value_get_double(&my_css_decl(r, 9)->value) == 0.5);
  TEST_ASSERT_EQ_STR(my_value_get_str(&my_css_decl(r, 10)->value), "quoted");
  TEST_ASSERT_EQ_STR(my_value_get_str(&my_css_decl(r, 11)->value),
                     "some-ident");
  /* key names preserved */
  TEST_ASSERT_EQ_STR(my_css_decl(r, 0)->key, "a");
  my_css_sheet_destroy(s);
}

static void test_comments_atrule_skip(void) {
  my_css_sheet_t* s = parse("/* block\n comment */ button { color: red } "
                            "@media (max-width: 600px) { label { color: blue } } "
                            "label { color: green }");
  TEST_ASSERT(s != NULL);
  TEST_ASSERT_EQ_INT((int)my_css_rule_count(s), 2); /* @media skipped */
  TEST_ASSERT_EQ_STR(my_css_selector(my_css_rule(s, 0), 0)->widget_type,
                     "button");
  my_css_sheet_destroy(s);
}

static void test_malformed(void) {
  my_css_error_t err;
  my_css_sheet_t* s;
  /* structural errors are hard */
  s = my_css_parse(NULL, "button { color: red", 18, &err);
  TEST_ASSERT(s == NULL);
  TEST_ASSERT(err.line >= 1);
  s = my_css_parse(NULL, "window > button { color: red }",
                   strlen("window > button { color: red }"), &err);
  TEST_ASSERT(s == NULL); /* child combinator not supported */
  s = my_css_parse(NULL, "button:active { color: red }",
                   strlen("button:active { color: red }"), &err);
  TEST_ASSERT(s == NULL); /* unknown pseudo */
  /* declaration-level problems are lenient: skipped, rule survives */
  s = my_css_parse(NULL, "button { color #f00; width: 16px }",
                   strlen("button { color #f00; width: 16px }"), &err);
  TEST_ASSERT(s != NULL);
  TEST_ASSERT_EQ_INT((int)my_css_decl_count(my_css_rule(s, 0)), 1);
  TEST_ASSERT_EQ_STR(my_css_decl(my_css_rule(s, 0), 0)->key, "width");
  my_css_sheet_destroy(s);
}

/* ---------------- bridge ---------------- */

static void test_bridge_cascade_priority(void) {
  my_theme_t* t = my_theme_create(NULL);
  const my_value_t* v;
  my_widget_t* root = my_widget_create(NULL, "window");
  my_widget_t* btn = my_button_create(NULL, "x");
  my_widget_t* plain = my_button_create(NULL, "y");
  my_widget_set_name(btn, "ok");
  my_widget_set_style_class(btn, "primary big");
  my_widget_add_child(root, btn);
  my_widget_unref(btn);
  my_widget_add_child(root, plain);
  my_widget_unref(plain);
  my_theme_load_css(t,
                    "button { bg_color: #111111 } "
                    ".primary { bg_color: #222222 } "
                    "#ok { bg_color: #333333 }");
  /* id > class > type */
  v = my_theme_get_for_widget(t, btn, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT(v != NULL && my_value_get_uint32(v) == 0x333333FFu);
  v = my_theme_get_for_widget(t, plain, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT(v != NULL && my_value_get_uint32(v) == 0x111111FFu);
  my_widget_unref(root);
  my_theme_destroy(t);
}

static void test_bridge_states_and_source_order(void) {
  my_theme_t* t = my_theme_create(NULL);
  my_widget_t* btn = my_button_create(NULL, "x");
  const my_value_t* v;
  my_theme_load_css(t, "button:hover { bg_color: #aaaaaa } "
                       "button { bg_color: #bbbbbb } "
                       "button { bg_color: #cccccc }");
  /* hover rule only fills HOVER; no-pseudo fills all four states */
  v = my_theme_get_for_widget(t, btn, MY_STATE_HOVER, "bg_color");
  TEST_ASSERT(v != NULL && my_value_get_uint32(v) == 0xAAAAAAFFu);
  /* later source order wins on normal */
  v = my_theme_get_for_widget(t, btn, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT(v != NULL && my_value_get_uint32(v) == 0xCCCCCCFFu);
  my_widget_unref(btn);
  my_theme_destroy(t);
}

static void test_bridge_descendant_and_aliases(void) {
  my_theme_t* t = my_theme_create(NULL);
  my_widget_t* panel = my_widget_create(NULL, "panel");
  my_widget_t* btn = my_button_create(NULL, "x");
  my_widget_t* orphan = my_button_create(NULL, "y");
  const my_value_t* v;
  /* my_widget_create's name arg is the object name (#id), NOT the type —
   * set the container's type explicitly */
  panel->widget_type = "panel";
  my_widget_add_child(panel, btn);
  my_widget_unref(btn);
  my_theme_load_css(t, "panel button { background-color: #123456; "
                       "border-radius: 8px; font-size: 14 }");
  /* btn has a panel ancestor: matches; aliases mapped */
  v = my_theme_get_for_widget(t, btn, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT(v != NULL && my_value_get_uint32(v) == 0x123456FFu);
  v = my_theme_get_for_widget(t, btn, MY_STATE_NORMAL, "round_radius");
  TEST_ASSERT(v != NULL && my_value_get_int32(v) == 8);
  v = my_theme_get_for_widget(t, btn, MY_STATE_NORMAL, "font_size");
  TEST_ASSERT(v != NULL && my_value_get_int32(v) == 14);
  /* orphan: no panel ancestor -> no match */
  v = my_theme_get_for_widget(t, orphan, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT(v == NULL);
  my_widget_unref(panel);
  my_widget_unref(orphan);
  my_theme_destroy(t);
}

static void test_bridge_coexists_with_text_format(void) {
  my_theme_t* t = my_theme_create(NULL);
  const my_value_t* v;
  my_widget_t* btn = my_button_create(NULL, "x");
  my_theme_load_str(t, "button.normal.bg_color=#0A0A0A");
  my_theme_load_css(t, "button { bg_color: #0B0B0B }");
  /* same key, later write wins (CSS loaded after) */
  v = my_theme_get_for_widget(t, btn, MY_STATE_NORMAL, "bg_color");
  TEST_ASSERT(v != NULL && my_value_get_uint32(v) == 0x0B0B0BFFu);
  my_widget_unref(btn);
  my_theme_destroy(t);
}

static void test_widget_style_class(void) {
  my_widget_t* w = my_widget_create(NULL, "box");
  TEST_ASSERT(my_widget_get_style_class(w) == NULL);
  my_widget_set_style_class(w, "a b");
  TEST_ASSERT_EQ_STR(my_widget_get_style_class(w), "a b");
  my_widget_set_style_class(w, NULL);
  TEST_ASSERT(my_widget_get_style_class(w) == NULL);
  my_widget_unref(w);
}

static void test_css_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_css_sheet_t* s = my_css_parse(dbg,
                                   "window button, #ok:hover { "
                                   "bg_color: #fff; width: 16px } "
                                   "@media x { y { a: b } }",
                                   65, NULL);
  my_theme_t* t = my_theme_create(dbg);
  TEST_ASSERT(s != NULL);
  my_css_sheet_destroy(s);
  my_theme_load_css(t, "button { bg_color: red; border-radius: 4px }");
  my_theme_destroy(t);
  /* structural error path must not leak */
  s = my_css_parse(dbg, "button { color: red", 18, NULL);
  TEST_ASSERT(s == NULL);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_selector_forms);
  MYTEST_RUN(test_value_forms);
  MYTEST_RUN(test_comments_atrule_skip);
  MYTEST_RUN(test_malformed);
  MYTEST_RUN(test_bridge_cascade_priority);
  MYTEST_RUN(test_bridge_states_and_source_order);
  MYTEST_RUN(test_bridge_descendant_and_aliases);
  MYTEST_RUN(test_bridge_coexists_with_text_format);
  MYTEST_RUN(test_widget_style_class);
  MYTEST_RUN(test_css_no_leak);
MYTEST_MAIN_END()
