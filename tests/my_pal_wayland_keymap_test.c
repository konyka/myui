/**
 * @file my_pal_wayland_keymap_test.c
 * @brief Unit tests for the wayland xkb keysym -> my_key_t table.
 */
#include <xkbcommon/xkbcommon-keysyms.h>

#include "mypal/wayland/my_pal_wayland_keymap.h"

#include "mytest.h"

static void test_ascii_passthrough(void) {
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_a), 'a');
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Z), 'Z');
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_space), ' ');
}

static void test_special_keys(void) {
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Return),
                     MY_KEY_RETURN);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Escape),
                     MY_KEY_ESCAPE);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_BackSpace),
                     MY_KEY_BACKSPACE);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Tab), MY_KEY_TAB);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Left), MY_KEY_LEFT);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Right),
                     MY_KEY_RIGHT);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Up), MY_KEY_UP);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Down), MY_KEY_DOWN);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Prior),
                     MY_KEY_PAGE_UP);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Next),
                     MY_KEY_PAGE_DOWN);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_Delete),
                     MY_KEY_DELETE);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_F1), MY_KEY_F1);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_F12), MY_KEY_F12);
}

static void test_unmapped(void) {
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(XKB_KEY_ISO_Lock),
                     MY_KEY_UNKNOWN);
  TEST_ASSERT_EQ_INT(my_pal_wayland_key_from_keysym(0x7FFFFFFF),
                     MY_KEY_UNKNOWN);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_ascii_passthrough);
  MYTEST_RUN(test_special_keys);
  MYTEST_RUN(test_unmapped);
MYTEST_MAIN_END()
