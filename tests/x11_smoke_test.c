/**
 * @file x11_smoke_test.c
 * @brief X11 runtime smoke test: window + frame present + loop + timer.
 *
 * Skips (exit 0) when DISPLAY is unset or the X server is unreachable, so
 * headless CI still passes. Only registered when the x11 port is built.
 */
#include <stdio.h>
#include <stdlib.h>

#include "mypal/x11/my_pal_x11.h"
#include "myr/my_vgcanvas_soft.h"

#include "mytest.h"

static my_ret_t on_quit_timer(void* ctx) {
  my_pal_main_loop_t* loop = (my_pal_main_loop_t*)ctx;
  my_pal_main_loop_quit(loop);
  return MY_RET_FAIL; /* one-shot */
}

static my_ret_t on_event(void* ctx, my_pal_window_t* window,
                         const my_event_t* event) {
  int* event_count = (int*)ctx;
  (void)window;
  (void)event;
  (*event_count)++;
  return MY_RET_OK;
}

static void test_x11_smoke(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  my_pal_main_loop_t* loop;
  my_vgcanvas_t* vg;
  int event_count = 0;

  pal = my_pal_x11_create(NULL);
  if (pal == NULL) {
    fprintf(stdout, "SKIP: cannot connect to X server\n");
    return;
  }

  win = my_pal_window_create(pal, 64, 48, "x11_smoke");
  loop = my_pal_main_loop_create(pal);
  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_NOT_NULL(loop);

  my_pal_set_event_handler(pal, on_event, &event_count);
  my_pal_window_show(win);

  /* draw one frame through the software backend; end_frame presents it */
  vg = my_vgcanvas_soft_create(NULL, my_pal_window_get_lcd(win));
  TEST_ASSERT_NOT_NULL(vg);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(200, 30, 30));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){4, 4, 56, 40});
  my_vgcanvas_end_frame(vg);

  /* run the real loop; a timer quits it after 100 ms */
  my_pal_main_loop_add_timer(loop, on_quit_timer, loop, 100);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);

  /* clipboard: in-app roundtrip (owns CLIPBOARD selection) */
  {
    char cbuf[64];
    TEST_ASSERT_EQ_INT(my_pal_clipboard_set_text(pal, "myui-x11"), MY_RET_OK);
    TEST_ASSERT_EQ_INT(my_pal_clipboard_get_text(pal, cbuf, sizeof(cbuf)),
                       MY_RET_OK);
    TEST_ASSERT_EQ_STR(cbuf, "myui-x11");
  }

  my_vgcanvas_destroy(vg);
  my_pal_main_loop_destroy(loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
  fprintf(stdout, "x11 smoke: ran, %d events\n", event_count);
}

MYTEST_MAIN_BEGIN()
  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
  } else {
    MYTEST_RUN(test_x11_smoke);
  }
MYTEST_MAIN_END()
