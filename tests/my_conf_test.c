/**
 * @file my_conf_test.c
 * @brief Document tree tests (M17a): constructors, object/array building,
 * dot paths, typed getters, file io, leaks.
 */
#include "myc/myconf/my_conf.h"

#include <stdio.h>

#include "mytest.h"

static my_conf_node_t* sample_tree(const my_allocator_t* alloc) {
  /* {"name":"dxx", "debug":true, "limit":42, "ratio":0.5,
   *  "list":[10,20], "obj":{"sub":{"x":7}}} */
  my_conf_node_t* root = my_conf_new_object(alloc);
  my_conf_node_t* list = my_conf_new_array(alloc);
  my_conf_node_t* obj = my_conf_new_object(alloc);
  my_conf_node_t* sub = my_conf_new_object(alloc);
  my_conf_object_set(root, "name", my_conf_new_str(alloc, "dxx"));
  my_conf_object_set(root, "debug", my_conf_new_bool(alloc, true));
  my_conf_object_set(root, "limit", my_conf_new_int64(alloc, 42));
  my_conf_object_set(root, "ratio", my_conf_new_double(alloc, 0.5));
  my_conf_array_push(list, my_conf_new_int64(alloc, 10));
  my_conf_array_push(list, my_conf_new_int64(alloc, 20));
  my_conf_object_set(root, "list", list);
  my_conf_object_set(sub, "x", my_conf_new_int64(alloc, 7));
  my_conf_object_set(obj, "sub", sub);
  my_conf_object_set(root, "obj", obj);
  return root;
}

static void test_build_and_query(void) {
  my_conf_node_t* root = sample_tree(NULL);
  my_conf_node_t* n;
  TEST_ASSERT_EQ_INT(my_conf_type(root), MY_CONF_OBJECT);
  TEST_ASSERT_EQ_INT((int)my_conf_child_count(root), 6);
  /* insertion order kept */
  TEST_ASSERT_EQ_STR(my_conf_key(my_conf_child(root, 0)), "name");
  TEST_ASSERT_EQ_STR(my_conf_key(my_conf_child(root, 5)), "obj");
  /* dot paths */
  n = my_conf_get(root, "obj.sub.x");
  TEST_ASSERT(n != NULL);
  TEST_ASSERT_EQ_INT(my_conf_as_int64(n, -1), 7);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "list.1", -1), 20);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "list.0", -1), 10);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "name", "?"), "dxx");
  TEST_ASSERT(my_conf_get_bool(root, "debug", false));
  TEST_ASSERT(my_conf_get_double(root, "ratio", 0.0) == 0.5);
  /* missing paths -> defaults */
  TEST_ASSERT(my_conf_get(root, "nope.nope") == NULL);
  TEST_ASSERT(my_conf_get(root, "list.5") == NULL);
  TEST_ASSERT(my_conf_get(root, "list.name") == NULL); /* array needs digits */
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "nope", -9), -9);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "nope", "d"), "d");
  /* type mismatch -> default */
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "name", -7), -7);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "limit", "d"), "d");
  my_conf_destroy(root);
}

static void test_object_set_replaces(void) {
  my_conf_node_t* root = my_conf_new_object(NULL);
  my_conf_object_set(root, "k", my_conf_new_int64(NULL, 1));
  my_conf_object_set(root, "k", my_conf_new_int64(NULL, 2));
  TEST_ASSERT_EQ_INT((int)my_conf_child_count(root), 1);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "k", -1), 2);
  /* set on non-object fails (child not taken over) */
  {
    my_conf_node_t* scalar = my_conf_new_int64(NULL, 1);
    my_conf_node_t* child = my_conf_new_null(NULL);
    TEST_ASSERT(my_conf_object_set(scalar, "k", child) != MY_RET_OK);
    my_conf_destroy(child);
    my_conf_destroy(scalar);
  }
  my_conf_destroy(root);
}

static void test_file_io_roundtrip(void) {
  const char* path = "/tmp/my_conf_io_test.json";
  my_conf_node_t* root = sample_tree(NULL);
  my_conf_node_t* back;
  my_conf_error_t err;
  TEST_ASSERT(my_conf_save_file(root, path) == MY_RET_OK);
  back = my_conf_load_file(NULL, path, &err);
  TEST_ASSERT(back != NULL);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(back, "obj.sub.x", -1), 7);
  TEST_ASSERT_EQ_STR(my_conf_get_str(back, "name", "?"), "dxx");
  my_conf_destroy(back);
  /* missing file -> NULL + error filled */
  back = my_conf_load_file(NULL, "/nonexistent/none.json", &err);
  TEST_ASSERT(back == NULL);
  my_conf_destroy(root);
  remove(path);
}

static void test_conf_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_conf_node_t* root = sample_tree(dbg);
  my_conf_destroy(root);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_build_and_query);
  MYTEST_RUN(test_object_set_replaces);
  MYTEST_RUN(test_file_io_roundtrip);
  MYTEST_RUN(test_conf_no_leak);
MYTEST_MAIN_END()
