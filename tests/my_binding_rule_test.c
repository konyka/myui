/**
 * @file my_binding_rule_test.c
 * @brief Unit tests for the binding rule parser.
 */
#include "mymvvm/my_binding_rule.h"

#include "mytest.h"

static void test_simple_data_rule(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name}", &r), MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.type, MY_RULE_DATA);
  TEST_ASSERT_EQ_STR(r.widget_prop, "text");
  TEST_ASSERT_EQ_STR(r.vm_prop, "name");
  TEST_ASSERT_EQ_INT(r.mode, MY_BINDING_ONE_WAY);
  TEST_ASSERT_EQ_STR(r.converter, "");
  TEST_ASSERT(!r.close_window);
}

static void test_mode_and_converter(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:text={name, Mode=TwoWay, Converter=upper}", &r),
      MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.mode, MY_BINDING_TWO_WAY);
  TEST_ASSERT_EQ_STR(r.converter, "upper");

  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={a, Mode=Once}", &r),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.mode, MY_BINDING_ONCE);
}

static void test_validator_with_args(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:value={age, Validator=range(0,150)}", &r),
      MY_RET_OK);
  TEST_ASSERT_EQ_STR(r.validator, "range");
  TEST_ASSERT_EQ_STR(r.validator_args, "0,150");

  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:text={n, Validator=not_empty}", &r), MY_RET_OK);
  TEST_ASSERT_EQ_STR(r.validator, "not_empty");
  TEST_ASSERT_EQ_STR(r.validator_args, "");
}

static void test_command_rules(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:on_click={save}", &r), MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.type, MY_RULE_COMMAND);
  TEST_ASSERT_EQ_STR(r.widget_prop, "on_click");
  TEST_ASSERT_EQ_STR(r.vm_prop, "save");

  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:on_click={save, Args=btn1}", &r), MY_RET_OK);
  TEST_ASSERT_EQ_STR(r.args, "btn1");

  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:on_click={close, CloseWindow=true}", &r),
      MY_RET_OK);
  TEST_ASSERT(r.close_window);
}

static void test_items_rule(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:items={persons, ItemTemplate=person_item}", &r),
      MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.type, MY_RULE_ITEMS);
  TEST_ASSERT_EQ_STR(r.vm_prop, "persons");
  TEST_ASSERT_EQ_STR(r.item_template, "person_item");

  /* Condition as an option (not the body form) is still rejected */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={x, Condition=y}", &r),
                     MY_RET_NOT_SUPPORTED);
}

static void test_condition_rules(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:visible={Condition=is_admin}", &r),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.type, MY_RULE_CONDITION);
  TEST_ASSERT_EQ_STR(r.vm_prop, "is_admin");
  TEST_ASSERT(!r.condition_negate);

  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:visible={Condition=!is_admin}", &r),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(r.vm_prop, "is_admin");
  TEST_ASSERT(r.condition_negate);
}

static void test_command_topage(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:on_click={goto, ToPage=detail, Args=Id={id}}",
                            &r),
      MY_RET_OK);
  TEST_ASSERT_EQ_INT(r.type, MY_RULE_COMMAND);
  TEST_ASSERT_EQ_STR(r.to_page, "detail");
  TEST_ASSERT_EQ_STR(r.args, "Id={id}");
}

static void test_malformed(void) {
  my_binding_rule_t r;
  TEST_ASSERT_EQ_INT(my_binding_rule_parse(NULL, &r), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name}", NULL),
                     MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("text={name}", &r),
                     MY_RET_INVALID_PARAMS); /* missing v: */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text=name", &r),
                     MY_RET_INVALID_PARAMS); /* missing braces */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={}", &r),
                     MY_RET_INVALID_PARAMS); /* empty body */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name", &r),
                     MY_RET_INVALID_PARAMS); /* unclosed */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name}}", &r),
                     MY_RET_INVALID_PARAMS); /* trailing junk */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name, Mode=Fast}", &r),
                     MY_RET_INVALID_PARAMS); /* bad mode */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name, Foo=1}", &r),
                     MY_RET_INVALID_PARAMS); /* unknown key */
  TEST_ASSERT_EQ_INT(my_binding_rule_parse("v:text={name, Mode}", &r),
                     MY_RET_INVALID_PARAMS); /* missing value */
  TEST_ASSERT_EQ_INT(
      my_binding_rule_parse("v:text={n, Validator=range(0,150}", &r),
      MY_RET_INVALID_PARAMS); /* unclosed paren */
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_simple_data_rule);
  MYTEST_RUN(test_mode_and_converter);
  MYTEST_RUN(test_validator_with_args);
  MYTEST_RUN(test_command_rules);
  MYTEST_RUN(test_items_rule);
  MYTEST_RUN(test_condition_rules);
  MYTEST_RUN(test_command_topage);
  MYTEST_RUN(test_malformed);
MYTEST_MAIN_END()
