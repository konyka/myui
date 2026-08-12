/**
 * @file my_pal_wayland_listener_test.c
 * @brief Regression: every listener slot for the interface versions we bind
 * must be non-NULL (libwayland aborts otherwise; wl_pointer v5 frame crash).
 */
#include "mypal/wayland/my_pal_wayland.h"
#include "mytest.h"

static void test_listeners_complete(void) {
  TEST_ASSERT(my_pal_wayland_listeners_complete());
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_listeners_complete);
MYTEST_MAIN_END()
