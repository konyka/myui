/**
 * @file my_pal_x11_keymap_test.c
 * @brief Unit tests for the X11 keysym -> my_key_t table (no X server
 * needed, only keysym constants).
 */
#include <X11/keysym.h>

#include "mypal/x11/my_pal_x11_keymap.h"

#include "mytest.h"

static void test_ascii_passthrough(void) {
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_a), 'a');
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Z), 'Z');
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_1), '1');
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_space), ' ');
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_exclam), '!');
}

static void test_special_keys(void) {
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Return), MY_KEY_RETURN);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Escape), MY_KEY_ESCAPE);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_BackSpace), MY_KEY_BACKSPACE);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Tab), MY_KEY_TAB);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Left), MY_KEY_LEFT);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Right), MY_KEY_RIGHT);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Up), MY_KEY_UP);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Down), MY_KEY_DOWN);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Home), MY_KEY_HOME);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_End), MY_KEY_END);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Prior), MY_KEY_PAGE_UP);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Next), MY_KEY_PAGE_DOWN);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Insert), MY_KEY_INSERT);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_Delete), MY_KEY_DELETE);
}

static void test_function_keys(void) {
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_F1), MY_KEY_F1);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_F12), MY_KEY_F12);
}

static void test_unmapped_returns_unknown(void) {
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(XK_ISO_Lock), MY_KEY_UNKNOWN);
  TEST_ASSERT_EQ_INT(my_pal_x11_key_from_keysym(0x7FFFFFFF), MY_KEY_UNKNOWN);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_ascii_passthrough);
  MYTEST_RUN(test_special_keys);
  MYTEST_RUN(test_function_keys);
  MYTEST_RUN(test_unmapped_returns_unknown);
MYTEST_MAIN_END()
