/**
 * @file my_value_converter.h
 * @brief Value converters (delegate pattern) + a small name registry.
 *
 * convert() transforms model -> view, convert_back() view -> model.
 * Built-ins are shared singletons: "upper", "lower", "int_to_str",
 * "bool_negate".
 */
#ifndef MY_VALUE_CONVERTER_H
#define MY_VALUE_CONVERTER_H

#include "myc/my_value.h"

/** @brief Value converter delegate. */
typedef struct my_value_converter_t {
  my_ret_t (*convert)(void* ctx, my_value_t* value);
  my_ret_t (*convert_back)(void* ctx, my_value_t* value);
  void* ctx;
} my_value_converter_t;

/** @brief Find a built-in converter by name (NULL when unknown). */
const my_value_converter_t* my_value_converter_find(const char* name);

/** @brief Apply converter (model -> view). NULL conv = pass-through. */
my_ret_t my_value_convert(const my_value_converter_t* conv, my_value_t* value);

/** @brief Apply converter (view -> model). NULL conv = pass-through. */
my_ret_t my_value_convert_back(const my_value_converter_t* conv, my_value_t* value);

#endif /* MY_VALUE_CONVERTER_H */
