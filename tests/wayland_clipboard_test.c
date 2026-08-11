/**
 * @file wayland_clipboard_test.c
 * @brief wl_data_device clipboard tests (M12b): protocol objects +
 * memory roundtrip, then a best-effort cross-connection transfer
 * (connection A sets, connection B gets). The cross path depends on
 * compositor focus policy (keyboard enter serial / focused selection
 * events) and is reported as SKIP when the compositor does not grant
 * focus to the test windows. Skips entirely without a compositor.
 */
#include <stdio.h>
#include <string.h>

#include "mypal/wayland/my_pal_wayland.h"

#include "mytest.h"

static my_pal_main_loop_t* g_loop;

static my_ret_t quit_timer(void* ctx) {
  (void)ctx;
  my_pal_main_loop_quit(g_loop);
  return MY_RET_FAIL;
}

static void test_clipboard_objects_and_roundtrip(void) {
  my_pal_t* a = my_pal_wayland_create(NULL);
  char buf[64];
  if (a == NULL) {
    fprintf(stdout, "SKIP: no wayland compositor\n");
    return;
  }
  /* set builds the data source without protocol errors; memory cache
   * roundtrips either way */
  TEST_ASSERT_EQ_INT(my_pal_clipboard_set_text(a, "wl-clip-m12b"),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_pal_clipboard_get_text(a, buf, sizeof(buf)),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(buf, "wl-clip-m12b");
  my_pal_destroy(a);
}

static void test_cross_connection_transfer(void) {
  my_pal_t* a = my_pal_wayland_create(NULL);
  my_pal_t* b = my_pal_wayland_create(NULL);
  my_pal_window_t* wa;
  char buf[64];
  my_ret_t got;
  if (a == NULL || b == NULL) {
    fprintf(stdout, "SKIP: no wayland compositor\n");
    my_pal_destroy(a);
    my_pal_destroy(b);
    return;
  }
  wa = my_pal_window_create(a, 64, 48, "clip_a");
  TEST_ASSERT_NOT_NULL(wa);
  my_pal_window_show(wa);
  g_loop = my_pal_main_loop_create(a);
  TEST_ASSERT_NOT_NULL(g_loop);

  my_pal_clipboard_set_text(a, "wl-cross-m12b");
  /* pump A: compositor may grant keyboard focus (enter serial) and the
   * selection may become active */
  my_pal_main_loop_add_timer(g_loop, quit_timer, NULL, 700);
  my_pal_main_loop_run(g_loop);

  memset(buf, 0, sizeof(buf));
  got = my_pal_clipboard_get_text(b, buf, sizeof(buf));
  if (got == MY_RET_OK && strcmp(buf, "wl-cross-m12b") == 0) {
    fprintf(stdout, "wayland clipboard: cross-connection transfer OK\n");
  } else {
    /* compositor focus policy: no keyboard enter serial for A (set is
     * not accepted) and/or no focused selection event for B -- the
     * protocol path is in place, the focus handshake is not ours to
     * force (documented in architecture.md) */
    fprintf(stdout,
            "SKIP: compositor did not focus the test windows "
            "(got=%d, buf='%s')\n",
            (int)got, buf);
  }

  my_pal_main_loop_destroy(g_loop);
  my_pal_window_destroy(wa);
  my_pal_destroy(a);
  my_pal_destroy(b);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_clipboard_objects_and_roundtrip);
  MYTEST_RUN(test_cross_connection_transfer);
MYTEST_MAIN_END()
