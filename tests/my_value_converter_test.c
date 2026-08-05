/**
 * @file my_value_converter_test.c
 * @brief Unit tests for value converters and validators.
 */
#include "mymvvm/my_value_converter.h"
#include "mymvvm/my_value_validator.h"

#include <stdlib.h>

#include "mytest.h"

static void test_upper_lower(void) {
  const my_value_converter_t* up = my_value_converter_find("upper");
  const my_value_converter_t* low = my_value_converter_find("lower");
  my_value_t v;
  TEST_ASSERT_NOT_NULL(up);
  TEST_ASSERT_NOT_NULL(low);

  my_value_init(&v, NULL);
  my_value_set_str(&v, "Hello World");
  my_value_convert(up, &v);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "HELLO WORLD");
  my_value_convert_back(low, &v);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "hello world");
  my_value_reset(&v);
}

static void test_int_to_str_roundtrip(void) {
  const my_value_converter_t* c = my_value_converter_find("int_to_str");
  my_value_t v;
  my_value_init(&v, NULL);

  my_value_set_int32(&v, -42);
  my_value_convert(c, &v);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "-42");

  my_value_convert_back(c, &v);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), -42);

  my_value_set_str(&v, "12x");
  TEST_ASSERT_EQ_INT(my_value_convert_back(c, &v), MY_RET_INVALID_PARAMS);
  my_value_reset(&v);
}

static void test_bool_negate(void) {
  const my_value_converter_t* c = my_value_converter_find("bool_negate");
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_bool(&v, true);
  my_value_convert(c, &v);
  TEST_ASSERT(!my_value_get_bool(&v));
  my_value_convert_back(c, &v);
  TEST_ASSERT(my_value_get_bool(&v));
  my_value_reset(&v);
}

static void test_find_unknown_and_passthrough(void) {
  my_value_t v;
  my_value_init(&v, NULL);
  TEST_ASSERT_NULL(my_value_converter_find("nope"));
  TEST_ASSERT_NULL(my_value_converter_find(NULL));
  my_value_set_int32(&v, 5);
  TEST_ASSERT_EQ_INT(my_value_convert(NULL, &v), MY_RET_OK); /* pass-through */
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 5);
  my_value_reset(&v);
}

static void test_not_empty_validator(void) {
  const my_value_validator_t* val = my_value_validator_find("not_empty");
  my_value_t v;
  char msg[64];
  TEST_ASSERT_NOT_NULL(val);

  my_value_init(&v, NULL);
  my_value_set_str(&v, "x");
  TEST_ASSERT(my_value_validate(val, &v, msg, sizeof(msg)));

  my_value_set_str(&v, "");
  TEST_ASSERT(!my_value_validate(val, &v, msg, sizeof(msg)));
  TEST_ASSERT_EQ_STR(msg, "must not be empty");

  TEST_ASSERT(my_value_validate(NULL, &v, NULL, 0)); /* NULL = always ok */
  my_value_reset(&v);
}

static void test_range_validator(void) {
  my_value_validator_t* r = my_value_validator_range_create(NULL, 0, 150);
  my_value_t v;
  char msg[64];
  TEST_ASSERT_NOT_NULL(r);

  my_value_init(&v, NULL);
  my_value_set_int32(&v, 100);
  TEST_ASSERT(my_value_validate(r, &v, msg, sizeof(msg)));

  my_value_set_int32(&v, 200);
  TEST_ASSERT(!my_value_validate(r, &v, msg, sizeof(msg)));

  /* string values are coerced for the check */
  my_value_set_str(&v, "42");
  TEST_ASSERT(my_value_validate(r, &v, msg, sizeof(msg)));
  my_value_set_str(&v, "abc");
  TEST_ASSERT(!my_value_validate(r, &v, msg, sizeof(msg)));

  /* fix clamps */
  my_value_set_int32(&v, 999);
  r->fix(r->ctx, &v);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 150);

  my_value_reset(&v);
  my_value_validator_range_destroy(r);
}

/* cents (int32) <-> "12.34" string */
static my_ret_t money_convert(void* ctx, my_value_t* value) {
  char buf[32];
  int32_t cents;
  (void)ctx;
  if (value->type != MY_VALUE_INT32) {
    return MY_RET_OK;
  }
  cents = my_value_get_int32(value);
  snprintf(buf, sizeof(buf), "%d.%02d", (int)(cents / 100),
           (int)(cents % 100 < 0 ? -cents % 100 : cents % 100));
  return my_value_set_str(value, buf);
}

static my_ret_t money_convert_back(void* ctx, my_value_t* value) {
  const char* s;
  long whole, frac = 0;
  char* dot;
  char* end;
  (void)ctx;
  if (value->type != MY_VALUE_STR) {
    return MY_RET_OK;
  }
  s = my_value_get_str(value);
  whole = strtol(s, &end, 10);
  if (end == s) {
    return MY_RET_INVALID_PARAMS;
  }
  if (*end == '.') {
    char fbuf[3] = {0, 0, 0};
    fbuf[0] = end[1] != '\0' ? end[1] : '0';
    fbuf[1] = end[1] != '\0' && end[2] != '\0' ? end[2] : '0';
    frac = strtol(fbuf, &dot, 10);
  } else if (*end != '\0') {
    return MY_RET_INVALID_PARAMS;
  }
  return my_value_set_int32(value, (int32_t)(whole * 100 + frac));
}

static const my_value_converter_t MONEY = {money_convert, money_convert_back,
                                           NULL};

static void test_custom_converter_register_and_use(void) {
  const my_value_converter_t* found;
  my_value_t v;

  TEST_ASSERT_NULL(my_value_converter_find("money"));
  TEST_ASSERT_EQ_INT(my_value_converter_register("money", &MONEY), MY_RET_OK);

  found = my_value_converter_find("money");
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT(found == &MONEY);

  my_value_init(&v, NULL);
  my_value_set_int32(&v, 1234);
  my_value_convert(found, &v);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "12.34");
  my_value_convert_back(found, &v);
  TEST_ASSERT_EQ_INT(my_value_get_int32(&v), 1234);
  my_value_reset(&v);

  /* overriding a built-in replaces it (warning logged) */
  TEST_ASSERT_EQ_INT(my_value_converter_register("upper", &MONEY), MY_RET_OK);
  TEST_ASSERT(my_value_converter_find("upper") == &MONEY);
  TEST_ASSERT_EQ_INT(my_value_converter_unregister("upper"), MY_RET_OK);
  TEST_ASSERT(my_value_converter_find("upper") != &MONEY); /* builtin back */

  /* unregister then reference: gone */
  TEST_ASSERT_EQ_INT(my_value_converter_unregister("money"), MY_RET_OK);
  TEST_ASSERT_NULL(my_value_converter_find("money"));
  TEST_ASSERT_EQ_INT(my_value_converter_unregister("money"),
                     MY_RET_NOT_FOUND);
}

static bool even_is_valid(void* ctx, const my_value_t* value, char* msg,
                          size_t msg_size) {
  (void)ctx;
  if (msg != NULL && msg_size > 0) {
    msg[0] = '\0';
  }
  return value->type != MY_VALUE_INT32 ||
         my_value_get_int32(value) % 2 == 0;
}

static const my_value_validator_t EVEN_ONLY = {even_is_valid, NULL, NULL};

static void test_custom_validator_register(void) {
  my_value_t v;
  TEST_ASSERT_NULL(my_value_validator_find("even_only"));
  TEST_ASSERT_EQ_INT(my_value_validator_register("even_only", &EVEN_ONLY),
                     MY_RET_OK);
  TEST_ASSERT(my_value_validator_find("even_only") == &EVEN_ONLY);

  my_value_init(&v, NULL);
  my_value_set_int32(&v, 4);
  TEST_ASSERT(my_value_validate(my_value_validator_find("even_only"), &v,
                                NULL, 0));
  my_value_set_int32(&v, 3);
  TEST_ASSERT(!my_value_validate(my_value_validator_find("even_only"), &v,
                                 NULL, 0));
  my_value_reset(&v);

  TEST_ASSERT_EQ_INT(my_value_validator_unregister("even_only"), MY_RET_OK);
  TEST_ASSERT_NULL(my_value_validator_find("even_only"));
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_value_validator_t* r = my_value_validator_range_create(dbg, 1, 10);
  TEST_ASSERT_NOT_NULL(r);
  my_value_validator_range_destroy(r);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_upper_lower);
  MYTEST_RUN(test_int_to_str_roundtrip);
  MYTEST_RUN(test_bool_negate);
  MYTEST_RUN(test_find_unknown_and_passthrough);
  MYTEST_RUN(test_not_empty_validator);
  MYTEST_RUN(test_range_validator);
  MYTEST_RUN(test_custom_converter_register_and_use);
  MYTEST_RUN(test_custom_validator_register);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
