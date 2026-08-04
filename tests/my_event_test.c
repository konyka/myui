/**
 * @file my_event_test.c
 * @brief Unit tests for my_event helpers.
 */
#include "mypal/my_event.h"

#include "mytest.h"

static void test_event_init(void) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
  TEST_ASSERT_EQ_INT(e.type, MY_EVENT_POINTER_DOWN);
  TEST_ASSERT_EQ_INT(e.time_ms, 0);

  e.u.pointer.x = 10;
  e.u.pointer.y = 20;
  e.u.pointer.button = 1;
  e.u.pointer.modifiers = MY_KEYMOD_SHIFT;
  TEST_ASSERT_EQ_INT(e.u.pointer.x, 10);
  TEST_ASSERT_EQ_INT(e.u.pointer.y, 20);
  TEST_ASSERT_EQ_INT(e.u.pointer.button, 1);
  TEST_ASSERT_EQ_INT(e.u.pointer.modifiers, MY_KEYMOD_SHIFT);
}

static void test_key_event(void) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = MY_KEY_RETURN;
  e.u.key.modifiers = MY_KEYMOD_CTRL | MY_KEYMOD_ALT;
  TEST_ASSERT_EQ_INT(e.u.key.key, MY_KEY_RETURN);
  TEST_ASSERT_EQ_INT(e.u.key.modifiers, MY_KEYMOD_CTRL | MY_KEYMOD_ALT);

  /* printable ASCII is its own key code */
  e.u.key.key = 'a';
  TEST_ASSERT_EQ_INT(e.u.key.key, 97);
}

static void test_special_keys_distinct_from_ascii(void) {
  TEST_ASSERT(MY_KEY_RETURN > 126);
  TEST_ASSERT(MY_KEY_F12 > MY_KEY_F1);
  TEST_ASSERT_EQ_INT(MY_KEYMOD_SHIFT | MY_KEYMOD_CTRL, 3);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_event_init);
  MYTEST_RUN(test_key_event);
  MYTEST_RUN(test_special_keys_distinct_from_ascii);
MYTEST_MAIN_END()
