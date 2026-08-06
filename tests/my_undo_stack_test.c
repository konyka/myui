/**
 * @file my_undo_stack_test.c
 * @brief Unit tests for the generic undo/redo stack.
 */
#include "myui/my_undo_stack.h"

#include "mytest.h"

static void test_insert_batching(void) {
  my_undo_stack_t* s = my_undo_stack_create(NULL, 0);
  my_undo_op_t op;

  my_undo_stack_record_insert(s, 0, "a", 1);
  my_undo_stack_record_insert(s, 1, "b", 1);
  my_undo_stack_record_insert(s, 2, "c", 1);
  TEST_ASSERT_EQ_INT(my_undo_stack_size(s), 1); /* merged into one batch */

  my_undo_stack_break_batch(s);
  my_undo_stack_record_insert(s, 3, "X", 1);
  TEST_ASSERT_EQ_INT(my_undo_stack_size(s), 2);

  TEST_ASSERT(my_undo_stack_can_undo(s));
  my_undo_stack_undo(s, &op);
  TEST_ASSERT_EQ_INT(op.offset, 3);
  TEST_ASSERT_EQ_INT(op.remove_len, 1);
  TEST_ASSERT_EQ_INT(op.bytes_len, 0);

  my_undo_stack_undo(s, &op);
  TEST_ASSERT_EQ_INT(op.offset, 0);
  TEST_ASSERT_EQ_INT(op.remove_len, 3); /* whole "abc" batch at once */
  TEST_ASSERT_EQ_INT(op.bytes_len, 0);
  TEST_ASSERT(!my_undo_stack_can_undo(s));

  my_undo_stack_redo(s, &op);
  TEST_ASSERT_EQ_INT(op.offset, 0);
  TEST_ASSERT_EQ_INT(op.remove_len, 0);
  TEST_ASSERT_EQ_INT(op.bytes_len, 3);
  TEST_ASSERT_EQ_STR(op.bytes, "abc");

  my_undo_stack_destroy(s);
}

static void test_delete_batching(void) {
  my_undo_stack_t* s = my_undo_stack_create(NULL, 0);
  my_undo_op_t op;

  /* backspace stream: offsets decreasing and adjacent */
  my_undo_stack_record_delete(s, 5, "c", 1);
  my_undo_stack_record_delete(s, 4, "b", 1);
  my_undo_stack_record_delete(s, 3, "a", 1);
  TEST_ASSERT_EQ_INT(my_undo_stack_size(s), 1);

  my_undo_stack_undo(s, &op);
  TEST_ASSERT_EQ_INT(op.offset, 3);
  TEST_ASSERT_EQ_INT(op.remove_len, 0);
  TEST_ASSERT_EQ_INT(op.bytes_len, 3);
  TEST_ASSERT_EQ_STR(op.bytes, "abc"); /* prepended in original order */

  my_undo_stack_destroy(s);
}

static void test_break_rules(void) {
  my_undo_stack_t* s = my_undo_stack_create(NULL, 0);

  my_undo_stack_record_insert(s, 0, "a", 1);
  my_undo_stack_record_delete(s, 0, "a", 1); /* direction change: new entry */
  TEST_ASSERT_EQ_INT(my_undo_stack_size(s), 2);

  my_undo_stack_record_insert(s, 0, "a", 1);
  my_undo_stack_record_insert(s, 5, "b", 1); /* non-adjacent: new entry */
  /* e1=ins, e2=del, e3=ins, e4=ins: direction change and the delete in
   * between each force a fresh entry */
  TEST_ASSERT_EQ_INT(my_undo_stack_size(s), 4);

  my_undo_stack_destroy(s);
}

static void test_capacity_eviction(void) {
  my_undo_stack_t* s = my_undo_stack_create(NULL, 3);
  size_t i;
  for (i = 0; i < 6; i++) {
    my_undo_stack_record_insert(s, i, "x", 1);
    my_undo_stack_break_batch(s);
  }
  TEST_ASSERT_EQ_INT(my_undo_stack_size(s), 3); /* oldest evicted */

  my_undo_stack_destroy(s);
}

static void test_redo_killed_by_new_edit(void) {
  my_undo_stack_t* s = my_undo_stack_create(NULL, 0);
  my_undo_op_t op;

  my_undo_stack_record_insert(s, 0, "ab", 2);
  my_undo_stack_undo(s, &op);
  TEST_ASSERT(my_undo_stack_can_redo(s));

  my_undo_stack_record_insert(s, 0, "z", 1); /* new edit clears redo */
  TEST_ASSERT(!my_undo_stack_can_redo(s));
  TEST_ASSERT_EQ_INT(my_undo_stack_redo(s, &op), MY_RET_NOT_FOUND);

  my_undo_stack_destroy(s);
}

static void test_undo_redo_interleave(void) {
  my_undo_stack_t* s = my_undo_stack_create(NULL, 0);
  my_undo_op_t op;

  my_undo_stack_record_insert(s, 0, "a", 1);
  my_undo_stack_break_batch(s);
  my_undo_stack_record_insert(s, 1, "b", 1);
  my_undo_stack_undo(s, &op);
  my_undo_stack_undo(s, &op);
  my_undo_stack_redo(s, &op);
  TEST_ASSERT_EQ_INT(op.offset, 0);
  TEST_ASSERT_EQ_INT(op.bytes_len, 1);
  TEST_ASSERT_EQ_STR(op.bytes, "a");
  my_undo_stack_redo(s, &op);
  TEST_ASSERT_EQ_STR(op.bytes, "b");
  TEST_ASSERT(!my_undo_stack_can_redo(s));

  my_undo_stack_destroy(s);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_undo_stack_t* s = my_undo_stack_create(dbg, 4);
  size_t i;

  for (i = 0; i < 8; i++) {
    my_undo_stack_record_insert(s, i, "hello", 5);
    my_undo_stack_break_batch(s);
  }
  {
    my_undo_op_t op;
    my_undo_stack_undo(s, &op);
    my_undo_stack_undo(s, &op);
  }
  my_undo_stack_record_delete(s, 0, "old-text", 8);
  my_undo_stack_destroy(s); /* frees everything incl. evicted/redo branch */
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_insert_batching);
  MYTEST_RUN(test_delete_batching);
  MYTEST_RUN(test_break_rules);
  MYTEST_RUN(test_capacity_eviction);
  MYTEST_RUN(test_redo_killed_by_new_edit);
  MYTEST_RUN(test_undo_redo_interleave);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
