/**
 * @file my_value_converter.c
 * @brief Built-in value converters + registry.
 */
#include "mymvvm/my_value_converter.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "myc/my_str.h"

/* ---------------- upper / lower ---------------- */

static my_ret_t str_case(void* ctx, my_value_t* value, bool upper) {
  const char* s;
  char buf[128];
  size_t i, len;
  (void)ctx;
  if (value->type == MY_VALUE_STR) {
    s = value->u.str;
  } else if (value->type == MY_VALUE_INT32) {
    snprintf(buf, sizeof(buf), "%d", (int)my_value_get_int32(value));
    s = buf;
  } else {
    return MY_RET_OK; /* non-string: pass through */
  }
  len = strlen(s);
  if (len >= sizeof(buf)) {
    return MY_RET_INVALID_PARAMS;
  }
  for (i = 0; i <= len; i++) {
    buf[i] = (char)(upper ? toupper((unsigned char)s[i])
                          : tolower((unsigned char)s[i]));
  }
  return my_value_set_str(value, buf);
}

static my_ret_t conv_upper(void* ctx, my_value_t* value) {
  return str_case(ctx, value, true);
}

static my_ret_t conv_lower(void* ctx, my_value_t* value) {
  return str_case(ctx, value, false);
}

/* ---------------- int <-> str ---------------- */

static my_ret_t int_to_str_convert(void* ctx, my_value_t* value) {
  char buf[32];
  (void)ctx;
  if (value->type == MY_VALUE_INT32) {
    snprintf(buf, sizeof(buf), "%d", (int)my_value_get_int32(value));
    return my_value_set_str(value, buf);
  }
  if (value->type == MY_VALUE_INT64) {
    snprintf(buf, sizeof(buf), "%lld", (long long)my_value_get_int64(value));
    return my_value_set_str(value, buf);
  }
  return MY_RET_OK;
}

static my_ret_t int_to_str_back(void* ctx, my_value_t* value) {
  const char* s;
  char* end = NULL;
  long v;
  (void)ctx;
  if (value->type != MY_VALUE_STR) {
    return MY_RET_OK;
  }
  s = my_value_get_str(value);
  v = strtol(s, &end, 10);
  if (end == s || *end != '\0') {
    return MY_RET_INVALID_PARAMS;
  }
  return my_value_set_int32(value, (int32_t)v);
}

/* ---------------- bool negate ---------------- */

static my_ret_t bool_negate_convert(void* ctx, my_value_t* value) {
  (void)ctx;
  if (value->type == MY_VALUE_BOOL) {
    return my_value_set_bool(value, !my_value_get_bool(value));
  }
  return MY_RET_OK;
}

/* ---------------- registry ---------------- */

static const my_value_converter_t UPPER = {conv_upper, conv_upper, NULL};
static const my_value_converter_t LOWER = {conv_lower, conv_lower, NULL};
static const my_value_converter_t INT_TO_STR = {int_to_str_convert,
                                                int_to_str_back, NULL};
static const my_value_converter_t BOOL_NEGATE = {bool_negate_convert,
                                                 bool_negate_convert, NULL};

const my_value_converter_t* my_value_converter_find(const char* name) {
  if (name == NULL || *name == '\0') {
    return NULL;
  }
  if (my_str_eq(name, "upper")) {
    return &UPPER;
  }
  if (my_str_eq(name, "lower")) {
    return &LOWER;
  }
  if (my_str_eq(name, "int_to_str")) {
    return &INT_TO_STR;
  }
  if (my_str_eq(name, "bool_negate")) {
    return &BOOL_NEGATE;
  }
  return NULL;
}

my_ret_t my_value_convert(const my_value_converter_t* conv, my_value_t* value) {
  if (conv == NULL || conv->convert == NULL) {
    return MY_RET_OK;
  }
  return conv->convert(conv->ctx, value);
}

my_ret_t my_value_convert_back(const my_value_converter_t* conv,
                               my_value_t* value) {
  if (conv == NULL || conv->convert_back == NULL) {
    return MY_RET_OK;
  }
  return conv->convert_back(conv->ctx, value);
}
