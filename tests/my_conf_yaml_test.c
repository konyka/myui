/**
 * @file my_conf_yaml_test.c
 * @brief YAML subset tests (M17b): nested maps, block/flow sequences,
 * maps inside sequences, flow maps, scalar type inference, quotes,
 * comments, and the documented hard errors (multi-doc/anchors/tags/
 * folds/tabs/inconsistent indent). Leaks checked.
 */
#include "myc/myconf/my_conf.h"

#include <string.h>

#include "mytest.h"

static my_conf_node_t* parse(const char* s) {
  return my_conf_parse_yaml(NULL, s, strlen(s), NULL);
}

static void test_yaml_nested_map(void) {
  static const char* DOC =
      "# top comment\n"
      "name: dxx\n"
      "server:\n"
      "  host: example.com\n"
      "  port: 8080\n"
      "  tls:\n"
      "    enabled: true\n"
      "    level: 2\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(root), MY_CONF_OBJECT);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "name", "?"), "dxx");
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "server.host", "?"),
                     "example.com");
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "server.port", -1), 8080);
  TEST_ASSERT(my_conf_get_bool(root, "server.tls.enabled", false));
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "server.tls.level", -1), 2);
  my_conf_destroy(root);
}

static void test_yaml_block_and_flow_sequences(void) {
  static const char* DOC =
      "items:\n"
      "  - 1\n"
      "  - two\n"
      "  - true\n"
      "flow: [1, 2, \"three\"]\n"
      "map: {a: 1, b: 'x'}\n"
      "empty_list: []\n"
      "empty_map: {}\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "items.0", -1), 1);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "items.1", "?"), "two");
  TEST_ASSERT(my_conf_get_bool(root, "items.2", false));
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "flow.1", -1), 2);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "flow.2", "?"), "three");
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "map.a", -1), 1);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "map.b", "?"), "x");
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "empty_list")),
                     MY_CONF_ARRAY);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "empty_map")),
                     MY_CONF_OBJECT);
  my_conf_destroy(root);
}

static void test_yaml_sequence_of_maps(void) {
  static const char* DOC =
      "users:\n"
      "  - name: alice\n"
      "    id: 1\n"
      "  - name: bob\n"
      "    id: 2\n"
      "    tags:\n"
      "      - x\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "users.0.name", "?"), "alice");
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "users.1.id", -1), 2);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "users.1.tags.0", "?"), "x");
  my_conf_destroy(root);
}

static void test_yaml_scalar_inference(void) {
  static const char* DOC =
      "s1: \"123\"\n"      /* quoted: stays STR */
      "s2: 'true'\n"      /* quoted bool word: STR */
      "n1: 123\n"
      "n2: -4.5\n"
      "b1: true\n"
      "b2: false\n"
      "z1: null\n"
      "z2: ~\n"
      "z3:\n"             /* empty value = NULL */
      "plain: hello world\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "s1")), MY_CONF_STR);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "s1", "?"), "123");
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "s2")), MY_CONF_STR);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "n1")), MY_CONF_INT64);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "n1", -1), 123);
  TEST_ASSERT(my_conf_get_double(root, "n2", 0) == -4.5);
  TEST_ASSERT(my_conf_get_bool(root, "b1", false));
  TEST_ASSERT(!my_conf_get_bool(root, "b2", true));
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "z1")), MY_CONF_NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "z2")), MY_CONF_NULL);
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "z3")), MY_CONF_NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "plain", "?"), "hello world");
  my_conf_destroy(root);
}

static void test_yaml_quotes_and_comment_rules(void) {
  static const char* DOC =
      "dq: \"a\\nb\\\"c\"\n"
      "sq: 'it''s'\n"           /* single-quote doubling */
      "hash1: a#b\n"            /* # inside plain scalar stays */
      "hash2: a # dropped\n";   /* space+# starts a comment */
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "dq", "?"), "a\nb\"c");
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "sq", "?"), "it's");
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "hash1", "?"), "a#b");
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "hash2", "?"), "a");
  my_conf_destroy(root);
}

static void test_yaml_hard_errors(void) {
  static const char* const BAD[] = {
      "---\na: 1\n",            /* multi-document marker */
      "a: &anchor 1\n",         /* anchor */
      "a: !tag 1\n",            /* tag */
      "a: >\n  folded\n",       /* folded scalar */
      "a: |\n  literal\n",      /* literal block scalar */
      "\ta: 1\n",               /* tab indentation */
      "a:\n   b: 1\n  c: 2\n",  /* inconsistent indent (3 then 2) */
      "a: [1, 2\n",             /* unterminated flow seq */
      "a: {k: 1\n",             /* unterminated flow map */
      "a: \"unclosed\n",        /* unterminated string */
  };
  size_t i;
  for (i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
    my_conf_error_t err;
    my_conf_node_t* n =
        my_conf_parse_yaml(NULL, BAD[i], strlen(BAD[i]), &err);
    if (n != NULL) {
      fprintf(stderr, "yaml malformed #%zu unexpectedly parsed: %s", i,
              BAD[i]);
    }
    TEST_ASSERT(n == NULL);
    TEST_ASSERT(err.msg[0] != '\0');
    TEST_ASSERT(err.line >= 1);
    my_conf_destroy(n);
  }
}

static void test_yaml_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  static const char* DOC = "a:\n  - {x: 1}\n  - [2, 3]\nb: 's'\n";
  my_conf_node_t* root = my_conf_parse_yaml(dbg, DOC, strlen(DOC), NULL);
  TEST_ASSERT(root != NULL);
  my_conf_destroy(root);
  root = my_conf_parse_yaml(dbg, "a:\n  - [1,", 10, NULL);
  TEST_ASSERT(root == NULL);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_yaml_nested_map);
  MYTEST_RUN(test_yaml_block_and_flow_sequences);
  MYTEST_RUN(test_yaml_sequence_of_maps);
  MYTEST_RUN(test_yaml_scalar_inference);
  MYTEST_RUN(test_yaml_quotes_and_comment_rules);
  MYTEST_RUN(test_yaml_hard_errors);
  MYTEST_RUN(test_yaml_no_leak);
MYTEST_MAIN_END()
