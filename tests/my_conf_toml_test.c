/**
 * @file my_conf_toml_test.c
 * @brief TOML subset tests (M17b): official-example core, nested tables,
 * table arrays, inline tables, mixed arrays, scalar forms, duplicate/
 * malformed errors with positions, leaks.
 */
#include "myc/myconf/my_conf.h"

#include <string.h>

#include "mytest.h"

static my_conf_node_t* parse(const char* s) {
  return my_conf_parse_toml(NULL, s, strlen(s), NULL);
}

static void test_toml_official_core(void) {
  static const char* DOC =
      "# comment\n"
      "title = \"TOML Example\"\n"
      "\n"
      "[owner]\n"
      "name = \"Tom Preston-Werner\"\n"
      "dob = 1979-05-27T07:32:00-08:00\n"
      "\n"
      "[database]\n"
      "enabled = true\n"
      "ports = [ 8000, 8001, 8002 ]\n"
      "temp_targets = [ 0.2, 71.5 ]\n"
      "\n"
      "[servers]\n"
      "  [servers.alpha]\n"
      "  ip = \"10.0.0.1\"\n"
      "  role = \"frontend\"\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "title", "?"), "TOML Example");
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "owner.name", "?"),
                     "Tom Preston-Werner");
  /* datetime kept as verbatim STR */
  TEST_ASSERT_EQ_INT(my_conf_type(my_conf_get(root, "owner.dob")),
                     MY_CONF_STR);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "owner.dob", "?"),
                     "1979-05-27T07:32:00-08:00");
  TEST_ASSERT(my_conf_get_bool(root, "database.enabled", false));
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "database.ports.2", -1), 8002);
  TEST_ASSERT(my_conf_get_double(root, "database.temp_targets.1", 0) == 71.5);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "servers.alpha.role", "?"),
                     "frontend");
  my_conf_destroy(root);
}

static void test_toml_table_arrays_and_inline(void) {
  static const char* DOC =
      /* TOML semantics: kv pairs after [[products]] belong to the LAST
       * products table — so these go FIRST */
      "point = { x = 1, y = 2 }\n"
      "mixed = [ 1, \"two\", true ]\n"
      "\n"
      "[[products]]\n"
      "name = \"Hammer\"\n"
      "sku = 738594937\n"
      "\n"
      "[[products]]\n"
      "\n"
      "[[products]]\n"
      "name = \"Nail\"\n"
      "sku = 284758393\n"
      "color = \"gray\"\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT((int)my_conf_child_count(my_conf_get(root, "products")),
                     3);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "products.0.sku", -1),
                     738594937);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "products.2.color", "?"), "gray");
  /* the empty middle table exists */
  TEST_ASSERT_EQ_INT(
      (int)my_conf_child_count(my_conf_get(root, "products.1")), 0);
  /* inline table */
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "point.y", -1), 2);
  /* mixed-type array (BSON-style, documented) */
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "mixed.0", -1), 1);
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "mixed.1", "?"), "two");
  TEST_ASSERT(my_conf_get_bool(root, "mixed.2", false));
  my_conf_destroy(root);
}

static void test_toml_scalar_forms(void) {
  static const char* DOC =
      "dec = 99\n"
      "hex = 0x10\n"
      "oct = 0o17\n"
      "bin = 0b101\n"
      "underscored = 1_000_000\n"
      "neg = -17\n"
      "floaty = 3.14\n"
      "exp = -1e3\n"
      "infv = inf\n"
      "ninf = -inf\n"
      "nanv = nan\n"
      "lit = 'C:\\path\\no-escape'\n"
      "esc = \"a\\nb\\\"c\"\n";
  my_conf_node_t* root = parse(DOC);
  TEST_ASSERT(root != NULL);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "dec", -1), 99);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "hex", -1), 16);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "oct", -1), 15);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "bin", -1), 5);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "underscored", -1), 1000000);
  TEST_ASSERT_EQ_INT(my_conf_get_int64(root, "neg", -1), -17);
  TEST_ASSERT(my_conf_get_double(root, "floaty", 0) == 3.14);
  TEST_ASSERT(my_conf_get_double(root, "exp", 0) == -1000.0);
  TEST_ASSERT(my_conf_get_double(root, "infv", 0) > 1e308);
  TEST_ASSERT(my_conf_get_double(root, "ninf", 0) < -1e308);
  TEST_ASSERT(my_conf_get_double(root, "nanv", 0) !=
              my_conf_get_double(root, "nanv", 0)); /* nan != nan */
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "lit", "?"), "C:\\path\\no-escape");
  TEST_ASSERT_EQ_STR(my_conf_get_str(root, "esc", "?"), "a\nb\"c");
  my_conf_destroy(root);
}

static void test_toml_malformed(void) {
  static const char* const BAD[] = {
      "a = 1\na = 2\n",        /* duplicate key */
      "a = \"unclosed\n",      /* unterminated string */
      "[unclosed\n",           /* unterminated table header */
      "a = 1\n[t]\n[t]\n",     /* table redefined */
      "a = \n",                /* missing value */
      "a 1\n",                 /* missing '=' */
      "a = 09\n",              /* bad octal-ish / leading zero digit soup */
      "a = [1, 2\n",           /* unterminated array */
      "a = { x = 1 \n",        /* unterminated inline table */
      "a = 0x\n",              /* empty hex */
      "= 1\n",                 /* missing key */
  };
  size_t i;
  for (i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
    my_conf_error_t err;
    my_conf_node_t* n =
        my_conf_parse_toml(NULL, BAD[i], strlen(BAD[i]), &err);
    if (n != NULL) {
      fprintf(stderr, "toml malformed #%zu unexpectedly parsed: %s", i,
              BAD[i]);
    }
    TEST_ASSERT(n == NULL);
    TEST_ASSERT(err.msg[0] != '\0');
    TEST_ASSERT(err.line >= 1);
    my_conf_destroy(n);
  }
  /* duplicate key reports its line */
  {
    my_conf_error_t err;
    my_conf_node_t* n =
        my_conf_parse_toml(NULL, "a = 1\nb = 2\na = 3\n", 17, &err);
    TEST_ASSERT(n == NULL);
    TEST_ASSERT_EQ_INT(err.line, 3);
  }
}

static void test_toml_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  static const char* DOC =
      "[a]\nb = [1, { x = \"y\" }]\n[[c]]\nd = true\n[[c]]\nd = false\n";
  my_conf_node_t* root = my_conf_parse_toml(dbg, DOC, strlen(DOC), NULL);
  TEST_ASSERT(root != NULL);
  my_conf_destroy(root);
  /* parse-failure path must not leak either */
  root = my_conf_parse_toml(dbg, "[a]\nb = [1,", 11, NULL);
  TEST_ASSERT(root == NULL);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_toml_official_core);
  MYTEST_RUN(test_toml_table_arrays_and_inline);
  MYTEST_RUN(test_toml_scalar_forms);
  MYTEST_RUN(test_toml_malformed);
  MYTEST_RUN(test_toml_no_leak);
MYTEST_MAIN_END()
