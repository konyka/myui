/**
 * @file my_xml_test.c
 * @brief Unit tests for the XML parser (valid round-trips + malformed suite).
 */
#include "myui/my_xml.h"

#include "mytest.h"

static void test_basic_document(void) {
  my_xml_doc_t* doc = my_xml_parse(
      NULL, "<window w=\"800\" h=\"480\"><label text='hi'/></window>", NULL);
  my_xml_node_t* root;
  my_xml_node_t* label;
  TEST_ASSERT_NOT_NULL(doc);
  root = doc->root;
  TEST_ASSERT_EQ_STR(root->name, "window");
  TEST_ASSERT_EQ_STR(my_xml_node_attr(root, "w"), "800");
  TEST_ASSERT_EQ_STR(my_xml_node_attr(root, "h"), "480");
  TEST_ASSERT_EQ_INT(root->child_count, 1);
  label = my_xml_node_child(root, 0);
  TEST_ASSERT_EQ_STR(label->name, "label");
  TEST_ASSERT_EQ_STR(my_xml_node_attr(label, "text"), "hi");
  TEST_ASSERT(my_xml_node_find(root, "label") == label);
  TEST_ASSERT_NULL(my_xml_node_find(root, "nope"));
  my_xml_doc_destroy(doc);
}

static void test_prolog_comments_entities_cdata(void) {
  my_xml_doc_t* doc = my_xml_parse(
      NULL,
      "<?xml version=\"1.0\"?>\n<!-- a comment -->\n"
      "<root>a &lt;b&gt; &amp; 'c' \"d\"<![CDATA[<raw>&amp;]]>tail</root>",
      NULL);
  TEST_ASSERT_NOT_NULL(doc);
  TEST_ASSERT_EQ_STR(doc->root->text, "a <b> & 'c' \"d\"<raw>&amp;tail");
  my_xml_doc_destroy(doc);
}

static void test_nested_children_and_text(void) {
  my_xml_doc_t* doc =
      my_xml_parse(NULL, "<a><b>one</b><b>two</b><c/></a>", NULL);
  TEST_ASSERT_NOT_NULL(doc);
  TEST_ASSERT_EQ_INT(doc->root->child_count, 3);
  TEST_ASSERT_EQ_STR(my_xml_node_child(doc->root, 0)->text, "one");
  TEST_ASSERT_EQ_STR(my_xml_node_child(doc->root, 1)->text, "two");
  TEST_ASSERT_EQ_STR(my_xml_node_child(doc->root, 2)->name, "c");
  my_xml_doc_destroy(doc);
}

static void test_error_positions(void) {
  my_xml_error_t err;
  my_xml_doc_t* doc;

  doc = my_xml_parse(NULL, "<a>\n  <b>\n</a>", &err);
  TEST_ASSERT_NULL(doc);
  TEST_ASSERT_EQ_INT(err.line, 3); /* mismatched close on line 3 */
  TEST_ASSERT(err.message[0] != '\0');

  doc = my_xml_parse(NULL, "<a x=1/>", &err);
  TEST_ASSERT_NULL(doc);

  doc = my_xml_parse(NULL, "<a>&xx;</a>", &err);
  TEST_ASSERT_NULL(doc);

  doc = my_xml_parse(NULL, "<a><!-- never closed", &err);
  TEST_ASSERT_NULL(doc);

  doc = my_xml_parse(NULL, "", &err);
  TEST_ASSERT_NULL(doc);

  doc = my_xml_parse(NULL, "<a/><b/>", &err);
  TEST_ASSERT_NULL(doc); /* two roots */

  doc = my_xml_parse(NULL, "<a><b></a></b>", &err);
  TEST_ASSERT_NULL(doc); /* nesting error */

  doc = my_xml_parse(NULL, "<a><![CDATA[never closed</a>", &err);
  TEST_ASSERT_NULL(doc);

  doc = my_xml_parse(NULL, "<a x=\"never closed</a>", &err);
  TEST_ASSERT_NULL(doc);

  doc = my_xml_parse(NULL, "<1bad/>", &err);
  TEST_ASSERT_NULL(doc);
}

static void test_self_close_and_empty(void) {
  my_xml_doc_t* doc = my_xml_parse(NULL, "<root><empty></empty><s/></root>",
                                   NULL);
  TEST_ASSERT_NOT_NULL(doc);
  TEST_ASSERT_EQ_INT(doc->root->child_count, 2);
  my_xml_doc_destroy(doc);
}

static void test_big_document(void) {
  /* 1000 elements: build "<r><i/><i/>...</r>" */
  static char buf[7000];
  size_t i, n = 0;
  my_xml_doc_t* doc;
  buf[n++] = '<';
  buf[n++] = 'r';
  buf[n++] = '>';
  for (i = 0; i < 1000; i++) {
    buf[n++] = '<';
    buf[n++] = 'i';
    buf[n++] = '/';
    buf[n++] = '>';
  }
  buf[n++] = '<';
  buf[n++] = '/';
  buf[n++] = 'r';
  buf[n++] = '>';
  buf[n] = '\0';
  doc = my_xml_parse(NULL, buf, NULL);
  TEST_ASSERT_NOT_NULL(doc);
  TEST_ASSERT_EQ_INT(doc->root->child_count, 1000);
  my_xml_doc_destroy(doc);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_xml_doc_t* doc = my_xml_parse(
      dbg, "<a x=\"1\" y='2'>text &amp; more<b><![CDATA[cd]]></b></a>", NULL);
  TEST_ASSERT_NOT_NULL(doc);
  my_xml_doc_destroy(doc);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);

  /* error path must not leak either */
  doc = my_xml_parse(dbg, "<a x=\"1\"><b>oops</a>", NULL);
  TEST_ASSERT_NULL(doc);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);

  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_basic_document);
  MYTEST_RUN(test_prolog_comments_entities_cdata);
  MYTEST_RUN(test_nested_children_and_text);
  MYTEST_RUN(test_error_positions);
  MYTEST_RUN(test_self_close_and_empty);
  MYTEST_RUN(test_big_document);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
