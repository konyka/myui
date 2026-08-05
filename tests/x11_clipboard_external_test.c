/**
 * @file x11_clipboard_external_test.c
 * @brief External clipboard fetch: a child PROCESS owns CLIPBOARD on its
 * own X connection and serves SelectionRequest; the myui port must fetch
 * the text from it. Skips when no DISPLAY.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "mypal/x11/my_pal_x11.h"

#include "mytest.h"

#define EXT_TEXT "external-clip-42"

/** @brief Child process: own CLIPBOARD and serve requests for ~3s. */
static int run_clipboard_owner(void) {
  Display* dpy = XOpenDisplay(NULL);
  Window w;
  Atom clip, utf8;
  uint64_t end;
  if (dpy == NULL) {
    return 1;
  }
  clip = XInternAtom(dpy, "CLIPBOARD", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  w = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 0, 0, 8, 8, 0, 0, 0);
  XSetSelectionOwner(dpy, clip, w, CurrentTime);
  XFlush(dpy);
  end = (uint64_t)time(NULL) + 3;
  while ((uint64_t)time(NULL) < end) {
    while (XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      if (ev.type == SelectionRequest) {
        XSelectionRequestEvent* req = &ev.xselectionrequest;
        XSelectionEvent notify;
        Atom p = req->property != None ? req->property : req->target;
        memset(&notify, 0, sizeof(notify));
        notify.type = SelectionNotify;
        notify.requestor = req->requestor;
        notify.selection = req->selection;
        notify.target = req->target;
        notify.time = req->time;
        notify.property = None;
        if (req->target == utf8 || req->target == XA_STRING) {
          XChangeProperty(dpy, req->requestor, p, req->target, 8,
                          PropModeReplace, (unsigned char*)EXT_TEXT,
                          (int)strlen(EXT_TEXT));
          notify.property = p;
        }
        XSendEvent(dpy, req->requestor, False, 0, (XEvent*)&notify);
        XFlush(dpy);
      }
    }
    usleep(10000);
  }
  XCloseDisplay(dpy);
  return 0;
}

static void test_external_clipboard_fetch(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  char buf[64];
  pid_t pid;

  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
    return;
  }
  pal = my_pal_x11_create(NULL);
  if (pal == NULL) {
    fprintf(stdout, "SKIP: cannot connect to X\n");
    return;
  }
  win = my_pal_window_create(pal, 32, 32, "clip-ext");
  TEST_ASSERT_NOT_NULL(win);

  pid = fork();
  if (pid == 0) {
    _exit(run_clipboard_owner());
  }
  usleep(300000); /* let the child own the selection first */

  buf[0] = '\0';
  TEST_ASSERT_EQ_INT(my_pal_clipboard_get_text(pal, buf, sizeof(buf)),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(buf, EXT_TEXT);

  my_pal_window_destroy(win);
  my_pal_destroy(pal);
  if (pid > 0) {
    int status = 0;
    (void)status;
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
  }
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_external_clipboard_fetch);
MYTEST_MAIN_END()
