/**
 * @file my_conf_bson_test.c
 * @brief BSON codec tests (M17a): type mapping (incl. objectId hex,
 * datetime, int32/int64 normalization), writer (int32 vs int64 choice),
 * malformed safety (truncation fuzz), golden round-trip, leaks.
 */
#include "myc/myconf/my_conf.h"

#include <string.h>

#include "mytest.h"

static void test_bson_type_mapping(void) {
  /* build with the writer, then verify the reads (writer test below
   * covers the byte-level choices) */
  {
    my_conf_node_t* root = my_conf_new_object(NULL);
    my_conf_node_t* obj = my_conf_new_object(NULL);
    my_conf_node_t* arr = my_conf_new_array(NULL);
    my_conf_node_t* back;
    uint8_t* buf;
    size_t len;
    my_conf_object_set(root, "d", my_conf_new_double(NULL, 1.5));
    my_conf_object_set(root, "s", my_conf_new_str(NULL, "hi"));
    my_conf_object_set(obj, "a", my_conf_new_int64(NULL, 7));
    my_conf_object_set(root, "o", obj);
    my_conf_array_push(arr, my_conf_new_int64(NULL, 5));
    my_conf_object_set(root, "arr", arr);
    my_conf_object_set(root, "b", my_conf_new_bool(NULL, true));
    my_conf_object_set(root, "n", my_conf_new_null(NULL));
    my_conf_object_set(root, "i32", my_conf_new_int64(NULL, 42));
    my_conf_object_set(root, "i64",
                       my_conf_new_int64(NULL, 5000000000LL));
    buf = my_conf_to_bson(NULL, root, &len);
    TEST_ASSERT(buf != NULL && len > 0);
    back = my_conf_parse_bson(NULL, buf, len, NULL);
    TEST_ASSERT(back != NULL);
    TEST_ASSERT(my_conf_get_double(back, "d", 0) == 1.5);
    TEST_ASSERT_EQ_STR(my_conf_get_str(back, "s", "?"), "hi");
    TEST_ASSERT_EQ_INT(my_conf_get_int64(back, "o.a", -1), 7);
    TEST_ASSERT_EQ_INT(my_conf_get_int64(back, "arr.0", -1), 5);
    TEST_ASSERT(my_conf_get_bool(back, "b", false));
    TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(back, "n")), MY_CONF_NULL);
    TEST_ASSERT_EQ_INT(my_conf_get_int64(back, "i32", -1), 42);
    TEST_ASSERT_EQ_INT(my_conf_get_int64(back, "i64", -1), 5000000000LL);
    my_mem_free(NULL, buf);
    my_conf_destroy(back);
    my_conf_destroy(root);
  }
}

/** @brief Known-good BSON from the spec: {"hello": "world"} */
static void test_bson_spec_example(void) {
  static const uint8_t DOC[] = {
      0x16, 0x00, 0x00, 0x00, /* len 22 */
      0x02,                               /* utf8 */
      'h', 'e', 'l', 'l', 'o', 0x00,      /* e-name */
      0x06, 0x00, 0x00, 0x00,             /* strlen 6 */
      'w', 'o', 'r', 'l', 'd', 0x00,      /* value */
      0x00                                /* terminator */
  };
  my_conf_node_t* root = my_conf_parse_bson(NULL, DOC, sizeof(DOC), NULL);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(root), MY_CONF_OBJECT);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "hello", "?"), "world");
  my_conf_destroy(root);
}

/** @brief Writer: int32-range INT64 goes out as 0x10, larger as 0x12. */
static void test_bson_writer_int_widths(void) {
  my_conf_node_t* root = my_conf_new_object(NULL);
  uint8_t* buf;
  size_t len;
  size_t i;
  int saw_10 = 0, saw_12 = 0;
  my_conf_object_set(root, "a", my_conf_new_int64(NULL, 42));
  my_conf_object_set(root, "b", my_conf_new_int64(NULL, 5000000000LL));
  buf = my_conf_to_bson(NULL, root, &len);
  TEST_ASSERT(buf != NULL);
  for (i = 4; i < len; i++) {
    if (buf[i] == 0x10) saw_10 = 1;
    if (buf[i] == 0x12) saw_12 = 1;
  }
  TEST_ASSERT(saw_10 && saw_12);
  my_mem_free(NULL, buf);
  my_conf_destroy(root);
}

/** @brief objectId (0x07) reads as 24 hex chars; datetime (0x09) as
 * INT64 ms. */
static void test_bson_objectid_and_datetime(void) {
  /* {"id": objectId(00 01 .. 0B), "t": datetime(1000)} */
  static const uint8_t DOC[] = {
      0x20, 0x00, 0x00, 0x00, /* len 32 */
      0x07, 'i', 'd', 0x00,
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
      0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
      0x09, 't', 0x00,
      0xE8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 1000 */
      0x00};
  my_conf_node_t* root = my_conf_parse_bson(NULL, DOC, sizeof(DOC), NULL);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "id", "?"),
                     "000102030405060708090a0b");
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "t", -1), 1000);
  my_conf_destroy(root);
}

/** @brief Unsupported element types are errors, not silent skips. */
static void test_bson_unsupported_type_errors(void) {
  /* {"r": regex //pattern//} — 0x0B */
  static const uint8_t DOC[] = {
      0x0F, 0x00, 0x00, 0x00,
      0x0B, 'r', 0x00, 'a', 0x00, 0x00,
      0x00};
  my_conf_error_t err;
  my_conf_node_t* root = my_conf_parse_bson(NULL, DOC, sizeof(DOC), &err);
  TEST_ASSERT(root == NULL);
  TEST_ASSERT(err.msg[0] != '\0');
}

/** @brief Every prefix of a valid document must fail cleanly (no crash,
 * no out-of-bounds), never succeed. */
static void test_bson_truncation_fuzz(void) {
  my_conf_node_t* root = my_conf_new_object(NULL);
  my_conf_node_t* sub = my_conf_new_object(NULL);
  uint8_t* buf;
  size_t len, i;
  my_conf_object_set(sub, "x", my_conf_new_str(NULL, "abcdef"));
  my_conf_object_set(root, "sub", sub);
  my_conf_object_set(root, "v", my_conf_new_double(NULL, 3.25));
  buf = my_conf_to_bson(NULL, root, &len);
  TEST_ASSERT(buf != NULL);
  for (i = 0; i < len; i++) {
    my_conf_node_t* n = my_conf_parse_bson(NULL, buf, i, NULL);
    TEST_ASSERT(n == NULL); /* a prefix is never a valid document */
    my_conf_destroy(n);
  }
  my_mem_free(NULL, buf);
  my_conf_destroy(root);
}

/** @brief Self-inconsistent lengths are rejected. */
static void test_bson_bad_lengths(void) {
  /* length says 100 but only 8 bytes present */
  static const uint8_t DOC1[] = {0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  /* length smaller than the minimum document */
  static const uint8_t DOC2[] = {0x02, 0x00, 0x00, 0x00, 0x00};
  /* missing trailing terminator */
  static const uint8_t DOC3[] = {0x05, 0x00, 0x00, 0x00, 0x01};
  TEST_ASSERT(my_conf_parse_bson(NULL, DOC1, sizeof(DOC1), NULL) == NULL);
  TEST_ASSERT(my_conf_parse_bson(NULL, DOC2, sizeof(DOC2), NULL) == NULL);
  TEST_ASSERT(my_conf_parse_bson(NULL, DOC3, sizeof(DOC3), NULL) == NULL);
}

static void test_bson_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_conf_node_t* root = my_conf_new_object(dbg);
  uint8_t* buf;
  size_t len;
  my_conf_object_set(root, "a", my_conf_new_int64(dbg, 1));
  my_conf_object_set(root, "s", my_conf_new_str(dbg, "x"));
  buf = my_conf_to_bson(dbg, root, &len);
  my_mem_free(dbg, buf);
  my_conf_destroy(root);
  /* parse failure path must not leak */
  {
    static const uint8_t BAD[] = {0x64, 0x00, 0x00, 0x00, 0x02, 'k'};
    root = my_conf_parse_bson(dbg, BAD, sizeof(BAD), NULL);
    TEST_ASSERT(root == NULL);
  }
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_bson_type_mapping);
  MYTEST_RUN(test_bson_spec_example);
  MYTEST_RUN(test_bson_writer_int_widths);
  MYTEST_RUN(test_bson_objectid_and_datetime);
  MYTEST_RUN(test_bson_unsupported_type_errors);
  MYTEST_RUN(test_bson_truncation_fuzz);
  MYTEST_RUN(test_bson_bad_lengths);
  MYTEST_RUN(test_bson_no_leak);
MYTEST_MAIN_END()
