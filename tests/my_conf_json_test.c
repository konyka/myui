/**
 * @file my_conf_json_test.c
 * @brief JSON codec tests (M17a): RFC 8259 examples, escapes incl.
 * surrogate pairs, number boundaries, malformed inputs with line/col
 * errors, pretty/compact round-trips, leaks.
 */
#include "myc/myconf/my_conf.h"

#include <string.h>

#include "mytest.h"

static my_conf_node_t* parse(const char* s) {
  return my_conf_parse_json(NULL, s, strlen(s), NULL);
}

static void test_rfc8259_example(void) {
  /* RFC 8259 section 13 example (trimmed to the essentials) */
  static const char* DOC =
      "{"
      "\"Image\": {"
      "\"Width\": 800, \"Height\": 600, \"Title\": \"View from 15th Floor\","
      "\"Thumbnail\": {\"Url\": \"http://www.example.com/image/481975943\","
      "\"Height\": 125, \"Width\": 100},"
      "\"Animated\": false, \"IDs\": [116, 943, 234, 38793]"
      "}}";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "Image.Width", -1), 800);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "Image.Thumbnail.Height", -1),
                     125);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "Image.IDs.3", -1), 38793);
  TEST_ASSERT(my_conf_get_bool(root, "Image.Animated", true) == false);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "Image.Title", "?"),
                     "View from 15th Floor");
  my_conf_destroy(root);
}

static void test_scalars_and_types(void) {
  my_conf_node_t* root = parse("{\"a\":null,\"b\":true,\"c\":false,\"d\":-17,"
                               "\"e\":2.5,\"f\":1e10,\"g\":-0,\"h\":9223372036854775807}");
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "a")), MY_CONF_NULL);
  TEST_ASSERT(my_conf_get_bool(root, "b", false));
  TEST_ASSERT(!my_conf_get_bool(root, "c", true));
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "d", 0), -17);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "e")), MY_CONF_DOUBLE);
  TEST_ASSERT(my_conf_get_double(root, "e", 0.0) == 2.5);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "f")), MY_CONF_DOUBLE);
  TEST_ASSERT(my_conf_get_double(root, "f", 0.0) == 1e10);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "g")), MY_CONF_INT64);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "g", 1), 0);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "h", 0), 9223372036854775807LL);
  my_conf_destroy(root);
}

static void test_int_overflow_becomes_double(void) {
  my_conf_node_t* root = parse("{\"big\":9223372036854775808}");
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "big")), MY_CONF_DOUBLE);
  my_conf_destroy(root);
}

static void test_escapes_and_unicode(void) {
  my_conf_node_t* root =
      parse("{\"s\":\"a\\\"b\\\\c\\/d\\be\\ff\\ng\\rh\\t\",\"u\":\"\\u0041\\u4E2D\"}");
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "s", "?"), "a\"b\\c/d\be\ff\ng\rh\t");
  /* A + 中 (U+4E2D) */
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "u", "?"), "A\xE4\xB8\xAD");
  my_conf_destroy(root);
  /* surrogate pair: U+1F600 😀 = F0 9F 98 80 */
  root = parse("{\"e\":\"\\uD83D\\uDE00\"}");
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "e", "?"), "\xF0\x9F\x98\x80");
  my_conf_destroy(root);
}

static void test_whitespace_and_nesting(void) {
  my_conf_node_t* root =
      parse("  \n\t { \n \"a\" : [ 1 , { \"b\" : [ true ] } ] } \r\n ");
  TEST_ASSERT(root != NULL);
  TEST_ASSERT(my_conf_get_bool(root, "a.1.b.0", false));
  my_conf_destroy(root);
}

/** @brief Each malformed doc must fail with an error position. */
static void test_malformed(void) {
  static const char* const BAD[] = {
      "{",                    /* unterminated object */
      "[1,2",                 /* unterminated array */
      "{\"a\":1,}",           /* trailing comma */
      "{a:1}",                /* unquoted key */
      "{\"a\":1",             /* missing } */
      "\"\\x\"",              /* bad escape */
      "\"\\u12\"",            /* short \u */
      "\"\\uD83D x\"",        /* lone high surrogate */
      "NaN",                  /* bare NaN rejected */
      "{\"a\":01}",           /* leading zero */
      "[1 2]",                /* missing comma */
      "",                     /* empty */
      "nul",                  /* bad literal */
      "{\"a\":1} trailing",   /* trailing garbage */
  };
  size_t i;
  for (i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
    my_conf_error_t err;
    my_conf_node_t* n =
        my_conf_parse_json(NULL, BAD[i], strlen(BAD[i]), &err);
    if (n != NULL) {
      fprintf(stderr, "malformed #%zu unexpectedly parsed: %s\n", i, BAD[i]);
    }
    TEST_ASSERT(n == NULL);
    TEST_ASSERT(err.msg[0] != '\0');
    my_conf_destroy(n);
  }
  /* error position sanity: line 2 for a doc broken on line 2 */
  {
    my_conf_error_t err;
    my_conf_node_t* n = my_conf_parse_json(NULL, "{\n\"a\": tru\n}", 12, &err);
    TEST_ASSERT(n == NULL);
    TEST_ASSERT_EQ_INT(err.line, 2);
  }
}

static void test_serialize_compact_and_pretty(void) {
  my_conf_node_t* root;
  my_conf_node_t* back;
  char* s;
  root = my_conf_new_object(NULL);
  my_conf_object_set(root, "a", my_conf_new_int64(NULL, 1));
  my_conf_object_set(root, "s", my_conf_new_str(NULL, "x\"y\n"));
  {
    my_conf_node_t* arr = my_conf_new_array(NULL);
    my_conf_array_push(arr, my_conf_new_bool(NULL, true));
    my_conf_array_push(arr, my_conf_new_null(NULL));
    my_conf_array_push(arr, my_conf_new_double(NULL, 1.5));
    my_conf_object_set(root, "arr", arr);
  }
  s = my_conf_to_json_str(NULL, root, false);
  TEST_ASSERT(s != NULL);
  TEST_ASSERT_EQ_STR(s, "{\"a\":1,\"s\":\"x\\\"y\\n\",\"arr\":[true,null,1.5]}");
  my_mem_free(NULL, s);
  /* pretty: indented with newlines */
  s = my_conf_to_json_str(NULL, root, true);
  TEST_ASSERT(s != NULL);
  TEST_ASSERT(strstr(s, "\n  \"a\": 1") != NULL);
  my_mem_free(NULL, s);
  /* round-trip equivalence */
  s = my_conf_to_json_str(NULL, root, false);
  back = parse(s);
  TEST_ASSERT(back != NULL);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(back, "a", -1), 1);
  TEST_ASSERT_EQ_STR(my_conf_get_str(back, "s", "?"), "x\"y\n");
  TEST_ASSERT(my_conf_get_bool(back, "arr.0", false));
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(back, "arr.1")), MY_CONF_NULL);
  TEST_ASSERT(my_conf_get_double(back, "arr.2", 0) == 1.5);
  my_mem_free(NULL, s);
  my_conf_destroy(back);
  my_conf_destroy(root);
}

static void test_json_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  static const char* DOC = "{\"a\":[1,{\"b\":\"c\"},null,true,2.5],\"z\":{}}";
  my_conf_node_t* root = my_conf_parse_json(dbg, DOC, strlen(DOC), NULL);
  char* s = my_conf_to_json_str(dbg, root, true);
  my_mem_free(dbg, s);
  my_conf_destroy(root);
  /* and a parse FAILURE must not leak either */
  root = my_conf_parse_json(dbg, "{\"a\":[1,", 9, NULL);
  TEST_ASSERT(root == NULL);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_rfc8259_example);
  MYTEST_RUN(test_scalars_and_types);
  MYTEST_RUN(test_int_overflow_becomes_double);
  MYTEST_RUN(test_escapes_and_unicode);
  MYTEST_RUN(test_whitespace_and_nesting);
  MYTEST_RUN(test_malformed);
  MYTEST_RUN(test_serialize_compact_and_pretty);
  MYTEST_RUN(test_json_no_leak);
MYTEST_MAIN_END()
