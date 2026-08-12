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

#include <X11/Xlib.h>
#include <X11/keysym.h>

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

static int g_last_key;

/** @brief Non-fatal X error handler for the smoke (focus requests on a
 * not-yet-mapped window can BadMatch under some WMs). */
static int x11_smoke_on_x_error(Display* d, XErrorEvent* e) {
  (void)d;
  fprintf(stdout, "x11 smoke: X error %d (ignored)\n", e->error_code);
  return 0;
}

static my_ret_t on_key_event(void* ctx, my_pal_window_t* window,
                             const my_event_t* event) {
  int* count = (int*)ctx;
  (void)window;
  (*count)++;
  if (event->type == MY_EVENT_KEY_DOWN) {
    g_last_key = (int)event->u.key.key;
  }
  return MY_RET_OK;
}

/** @brief M13a: real XIM path — XOpenIM (ibus when present), XIC per
 * window, focus, spot location, and a synthetic KeyPress routed through
 * XFilterEvent + Xutf8LookupString (plain ASCII still arrives). */
static void test_x11_ime_smoke(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  my_pal_main_loop_t* loop;
  Display* dpy;
  Window xid;
  int events = 0;

  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
    return;
  }
  pal = my_pal_x11_create(NULL);
  TEST_ASSERT_NOT_NULL(pal);
  if (pal == NULL) {
    return;
  }
  fprintf(stdout, "x11 ime: XOpenIM %s\n",
          my_pal_x11_has_ime(pal) ? "connected" : "absent (plain keys)");

  win = my_pal_window_create(pal, 64, 48, "ime_smoke");
  loop = my_pal_main_loop_create(pal);
  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_NOT_NULL(loop);
  my_pal_set_event_handler(pal, on_key_event, &events);
  my_pal_window_show(win);
  xid = (Window)my_pal_x11_window_xid(win);
  my_pal_window_ime_set_spot(win, 10, 10); /* safe with or without an IC */

  dpy = XOpenDisplay(NULL);
  TEST_ASSERT_NOT_NULL(dpy);
  if (dpy != NULL) {
    XEvent ev;
    XSetErrorHandler(x11_smoke_on_x_error);
    g_last_key = 0;
    /* no XSetInputFocus (unmapped windows BadMatch): the synthetic key
     * still routes through XFilterEvent + Xutf8LookupString */
    memset(&ev, 0, sizeof(ev));
    ev.type = KeyPress;
    ev.xkey.display = dpy;
    ev.xkey.window = xid;
    ev.xkey.root = DefaultRootWindow(dpy);
    ev.xkey.subwindow = None;
    ev.xkey.time = CurrentTime;
    ev.xkey.same_screen = True;
    ev.xkey.keycode = XKeysymToKeycode(dpy, XK_a);
    XSendEvent(dpy, xid, True, KeyPressMask, &ev);
    XFlush(dpy);
  }

  my_pal_main_loop_add_timer(loop, on_quit_timer, loop, 300);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(loop), MY_RET_OK);
  if (dpy != NULL) {
    /* the synthetic 'a' survived the IM filter path */
    TEST_ASSERT_EQ_INT(g_last_key, 'a');
    XCloseDisplay(dpy);
  }

  my_pal_main_loop_destroy(loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

MYTEST_MAIN_BEGIN()
  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
  } else {
    MYTEST_RUN(test_x11_smoke);
    MYTEST_RUN(test_x11_ime_smoke);
  }
MYTEST_MAIN_END()
