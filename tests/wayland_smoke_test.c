/**
 * @file wayland_smoke_test.c
 * @brief Wayland runtime smoke test: connect, window, present, loop.
 * Skips (exit 0) when no compositor is reachable. Registered only when
 * the wayland port is built.
 */
#include <stdio.h>
#include <stdlib.h>

#include "mypal/wayland/my_pal_wayland.h"
#include "myr/my_vgcanvas_soft.h"

#include "mytest.h"

static my_ret_t on_quit_timer(void* ctx) {
  my_pal_main_loop_t* loop = (my_pal_main_loop_t*)ctx;
  my_pal_main_loop_quit(loop);
  return MY_RET_FAIL;
}

static my_ret_t on_event(void* ctx, my_pal_window_t* win, const my_event_t* e) {
  int* n = (int*)ctx;
  (void)win;
  (void)e;
  (*n)++;
  return MY_RET_OK;
}

static void test_wayland_smoke(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  my_pal_main_loop_t* loop;
  my_vgcanvas_t* vg;
  int events = 0;

  pal = my_pal_wayland_create(NULL);
  if (pal == NULL) {
    fprintf(stdout, "SKIP: no wayland compositor\n");
    return;
  }

  win = my_pal_window_create(pal, 64, 48, "wl_smoke");
  loop = my_pal_main_loop_create(pal);
  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_NOT_NULL(loop);
  my_pal_set_event_handler(pal, on_event, &events);
  my_pal_window_show(win);

  vg = my_vgcanvas_soft_create(NULL, my_pal_window_get_lcd(win));
  TEST_ASSERT_NOT_NULL(vg);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(30, 30, 200));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){4, 4, 56, 40});
  my_vgcanvas_end_frame(vg);

  my_pal_main_loop_add_timer(loop, on_quit_timer, loop, 300);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);

  my_vgcanvas_destroy(vg);
  my_pal_main_loop_destroy(loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
  fprintf(stdout, "wayland smoke: ran, %d events\n", events);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_wayland_smoke);
MYTEST_MAIN_END()
