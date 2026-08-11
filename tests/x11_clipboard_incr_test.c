/**
 * @file x11_clipboard_incr_test.c
 * @brief INCR incremental clipboard transfer tests (M11b), two
 * directions against a forked child process on its own X connection:
 *  1. child owns CLIPBOARD with a 200KB text and serves it via INCR;
 *     the myui port must receive it intact (and the child must have
 *     sent more than one segment);
 *  2. the myui port owns a 200KB clipboard; the child fetches it,
 *     receives INCR segments, verifies integrity and segment count.
 * Results are passed through temp files. Skips when no DISPLAY.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "mypal/x11/my_pal_x11.h"

#include "mytest.h"

#define INCR_TEXT_LEN (200u * 1024u)
#define CHUNK_SZ 16384u
#define RX_FILE "/tmp/myui_incr_rx_result"
#define TX_FILE "/tmp/myui_incr_tx_result"

static char* make_big_text(void) {
  char* s = (char*)malloc(INCR_TEXT_LEN + 1);
  size_t i;
  if (s == NULL) {
    return NULL;
  }
  for (i = 0; i < INCR_TEXT_LEN; i++) {
    s[i] = (char)('a' + (int)(i % 26));
  }
  s[INCR_TEXT_LEN] = '\0';
  return s;
}

/** @brief Child: own CLIPBOARD with the big text, serve it via INCR for
 * ~5s; record the segment count into RX_FILE. */
static int run_incr_owner(void) {
  Display* dpy = XOpenDisplay(NULL);
  Window w;
  Atom clip, utf8, incr;
  char* text;
  size_t len, offset = 0;
  int segments = 0;
  uint64_t end;
  if (dpy == NULL) {
    return 1;
  }
  text = make_big_text();
  len = strlen(text);
  clip = XInternAtom(dpy, "CLIPBOARD", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  incr = XInternAtom(dpy, "INCR", False);
  w = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 0, 0, 8, 8, 0, 0, 0);
  XSetSelectionOwner(dpy, clip, w, CurrentTime);
  XFlush(dpy);
  end = (uint64_t)time(NULL) + 5;
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
          uint32_t lb = (uint32_t)len;
          XChangeProperty(dpy, req->requestor, p, incr, 32, PropModeReplace,
                          (unsigned char*)&lb, 1);
          XSelectInput(dpy, req->requestor, PropertyChangeMask);
          notify.property = p;
          offset = 0;
        }
        XSendEvent(dpy, req->requestor, False, 0, (XEvent*)&notify);
        XFlush(dpy);
      } else if (ev.type == PropertyNotify &&
                 ev.xproperty.state == PropertyDelete && offset < len + 1) {
        Window rq = ev.xproperty.window;
        if (offset < len) {
          size_t n = len - offset < CHUNK_SZ ? len - offset : CHUNK_SZ;
          XChangeProperty(dpy, rq, ev.xproperty.atom, utf8, 8, PropModeAppend,
                          (unsigned char*)(text + offset), (int)n);
          offset += n;
          segments++;
        } else {
          XChangeProperty(dpy, rq, ev.xproperty.atom, utf8, 8, PropModeAppend,
                          NULL, 0);
          offset = len + 1; /* done */
          XSelectInput(dpy, rq, 0);
        }
        XFlush(dpy);
      }
    }
    usleep(5000);
  }
  {
    FILE* f = fopen(RX_FILE, "w");
    if (f != NULL) {
      fprintf(f, "%d", segments);
      fclose(f);
    }
  }
  free(text);
  XCloseDisplay(dpy);
  return 0;
}

/** @brief Child: fetch CLIPBOARD (expecting INCR), verify integrity and
 * segment count; write "OK <segments>" or "BAD ..." into TX_FILE. */
static int run_incr_requestor_at(const char* expected,
                                 const char* outfile) {


  Display* dpy = XOpenDisplay(NULL);
  Window w;
  Atom clip, utf8, incr, prop;
  XEvent ev;
  uint64_t end;
  size_t total = 0;
  int segments = 0;
  int ok = 0;
  if (dpy == NULL) {
    return 1;
  }
  clip = XInternAtom(dpy, "CLIPBOARD", False);
  utf8 = XInternAtom(dpy, "UTF8_STRING", False);
  incr = XInternAtom(dpy, "INCR", False);
  prop = XInternAtom(dpy, "MYUI_INCR_TEST", False);
  w = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 0, 0, 8, 8, 0, 0, 0);
  XSelectInput(dpy, w, PropertyChangeMask);
  usleep(200000); /* let the parent own the selection first */
  XConvertSelection(dpy, clip, utf8, prop, w, CurrentTime);
  XFlush(dpy);
  end = (uint64_t)time(NULL) + 5;
  /* wait for SelectionNotify, then the segment loop */
  while ((uint64_t)time(NULL) < end) {
    if (XPending(dpy) == 0) {
      usleep(5000);
      continue;
    }
    XNextEvent(dpy, &ev);
    if (ev.type == SelectionNotify) {
      Atom actual;
      int format;
      unsigned long nitems, remaining;
      unsigned char* data = NULL;
      if (ev.xselection.property == None) {
        FILE* f = fopen(outfile, "w");
        if (f != NULL) {
          fprintf(f, "REFUSED");
          fclose(f);
        }
        XCloseDisplay(dpy);
        return 0;
      }
      if (XGetWindowProperty(dpy, w, prop, 0, 1, False, AnyPropertyType,
                             &actual, &format, &nitems, &remaining,
                             &data) != Success) {
        break;
      }
      if (actual != incr) { /* direct transfer (unexpected here) */
        FILE* f = fopen(outfile, "w");
        if (f != NULL) {
          fprintf(f, "BAD direct %lu", nitems);
          fclose(f);
        }
        if (data != NULL) {
          XFree(data);
        }
        XCloseDisplay(dpy);
        return 0;
      }
      if (data != NULL) {
        XFree(data);
      }
      /* segment loop */
      for (;;) {
        uint64_t seg_end = (uint64_t)time(NULL) + 3;
        int got = 0;
        XDeleteProperty(dpy, w, prop);
        XFlush(dpy);
        while ((uint64_t)time(NULL) < seg_end) {
          XEvent pev;
          if (XPending(dpy) == 0) {
            usleep(5000);
            continue;
          }
          XNextEvent(dpy, &pev);
          if (pev.type == PropertyNotify && pev.xproperty.atom == prop &&
              pev.xproperty.state == PropertyNewValue) {
            got = 1;
            break;
          }
        }
        if (!got) {
          break;
        }
        if (XGetWindowProperty(dpy, w, prop, 0, 1 << 20, False,
                               AnyPropertyType, &actual, &format, &nitems,
                               &remaining, &data) != Success || data == NULL) {
          break;
        }
        if (nitems == 0) {
          XFree(data);
          ok = 1;
          break;
        }
        segments++;
        if (memcmp(data, expected + total, nitems) != 0) {
          FILE* f = fopen(outfile, "w");
          if (f != NULL) {
            fprintf(f, "BAD content at %zu", total);
            fclose(f);
          }
          XFree(data);
          XCloseDisplay(dpy);
          return 0;
        }
        total += nitems;
        XFree(data);
      }
      break;
    }
  }
  {
    FILE* f = fopen(outfile, "w");
    if (f != NULL) {
      if (ok && total == strlen(expected)) {
        fprintf(f, "OK %d", segments);
      } else {
        fprintf(f, "BAD total=%zu ok=%d", total, ok);
      }
      fclose(f);
    }
  }
  XCloseDisplay(dpy);
  return 0;
}


static int run_incr_requestor(const char* expected) {
  return run_incr_requestor_at(expected, TX_FILE);
}

static int read_file_int(const char* path, char* buf, size_t size) {
  FILE* f = fopen(path, "r");
  size_t n;
  if (f == NULL) {
    return -1;
  }
  n = fread(buf, 1, size - 1, f);
  buf[n] = '\0';
  fclose(f);
  return 0;
}

static void test_incr_receive_from_external(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  char* buf;
  char* expected;
  char count_buf[32];
  pid_t pid;
  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
    return;
  }
  unlink(RX_FILE);
  pid = fork();
  if (pid == 0) {
    _exit(run_incr_owner());
  }
  TEST_ASSERT(pid > 0);
  usleep(300000); /* let the child own CLIPBOARD */

  pal = my_pal_x11_create(NULL);
  TEST_ASSERT_NOT_NULL(pal);
  win = my_pal_window_create(pal, 32, 32, "incr_rx");
  TEST_ASSERT_NOT_NULL(win);
  expected = make_big_text();
  buf = (char*)malloc(INCR_TEXT_LEN + 64);
  TEST_ASSERT_NOT_NULL(buf);
  memset(buf, 0, INCR_TEXT_LEN + 64);
  TEST_ASSERT_EQ_INT(
      my_pal_clipboard_get_text(pal, buf, INCR_TEXT_LEN + 64), MY_RET_OK);
  TEST_ASSERT_EQ_INT(strlen(buf), INCR_TEXT_LEN);
  TEST_ASSERT(strcmp(buf, expected) == 0);

  /* the child must have sent more than one segment */
  waitpid(pid, NULL, 0);
  TEST_ASSERT_EQ_INT(read_file_int(RX_FILE, count_buf, sizeof(count_buf)), 0);
  TEST_ASSERT(atoi(count_buf) > 1);

  free(buf);
  free(expected);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

static my_pal_main_loop_t* g_loop;

static my_ret_t quit_timer(void* ctx) {
  (void)ctx;
  my_pal_main_loop_quit(g_loop);
  return MY_RET_FAIL;
}

static void test_incr_serve_to_external(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  char* big;
  char result[64];
  pid_t pid;
  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
    return;
  }
  unlink(TX_FILE);
  pal = my_pal_x11_create(NULL);
  TEST_ASSERT_NOT_NULL(pal);
  win = my_pal_window_create(pal, 32, 32, "incr_tx");
  g_loop = my_pal_main_loop_create(pal);
  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_NOT_NULL(g_loop);
  big = make_big_text();
  my_pal_window_show(win);
  TEST_ASSERT_EQ_INT(my_pal_clipboard_set_text(pal, big), MY_RET_OK);

  pid = fork();
  if (pid == 0) {
    _exit(run_incr_requestor(big));
  }
  TEST_ASSERT(pid > 0);

  /* pump events (the transfer is driven by PropertyNotify) for ~4s */
  my_pal_main_loop_add_timer(g_loop, quit_timer, NULL, 4000);
  my_pal_main_loop_run(g_loop);
  waitpid(pid, NULL, 0);

  TEST_ASSERT_EQ_INT(read_file_int(TX_FILE, result, sizeof(result)), 0);
  TEST_ASSERT(result[0] == 'O' && result[1] == 'K');
  TEST_ASSERT(atoi(result + 3) > 1); /* more than one segment served */

  free(big);
  my_pal_main_loop_destroy(g_loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

/** @brief M12d: 5 concurrent requestors, 4 slots: four complete INCR
 * transfers run concurrently, the 5th is refused. */
static void test_incr_concurrent_transfers(void) {
  static const char* names[5] = {"/tmp/myui_incr_c0", "/tmp/myui_incr_c1",
                                 "/tmp/myui_incr_c2", "/tmp/myui_incr_c3",
                                 "/tmp/myui_incr_c4"};
  my_pal_t* pal;
  my_pal_window_t* win;
  char* big;
  pid_t pids[5];
  int i, ok_count = 0, refused_count = 0;
  if (getenv("DISPLAY") == NULL) {
    fprintf(stdout, "SKIP: DISPLAY not set\n");
    return;
  }
  for (i = 0; i < 5; i++) {
    unlink(names[i]);
  }
  pal = my_pal_x11_create(NULL);
  TEST_ASSERT_NOT_NULL(pal);
  win = my_pal_window_create(pal, 32, 32, "incr_conc");
  g_loop = my_pal_main_loop_create(pal);
  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_NOT_NULL(g_loop);
  big = make_big_text();
  my_pal_window_show(win);
  TEST_ASSERT_EQ_INT(my_pal_clipboard_set_text(pal, big), MY_RET_OK);

  for (i = 0; i < 5; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      _exit(run_incr_requestor_at(big, names[i]));
    }
    TEST_ASSERT(pids[i] > 0);
  }

  my_pal_main_loop_add_timer(g_loop, quit_timer, NULL, 6000);
  my_pal_main_loop_run(g_loop);
  for (i = 0; i < 5; i++) {
    waitpid(pids[i], NULL, 0);
  }

  for (i = 0; i < 5; i++) {
    char result[64];
    TEST_ASSERT_EQ_INT(read_file_int(names[i], result, sizeof(result)), 0);
    if (result[0] == 'O' && result[1] == 'K' && atoi(result + 3) > 1) {
      ok_count++;
    } else if (strcmp(result, "REFUSED") == 0) {
      refused_count++;
    } else {
      fprintf(stdout, "child %d: %s\n", i, result);
    }
  }
  TEST_ASSERT_EQ_INT(ok_count, 4);     /* slots serve 4 concurrently */
  TEST_ASSERT_EQ_INT(refused_count, 1); /* the 5th is refused */

  free(big);
  my_pal_main_loop_destroy(g_loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_incr_receive_from_external);
  MYTEST_RUN(test_incr_serve_to_external);
  MYTEST_RUN(test_incr_concurrent_transfers);
MYTEST_MAIN_END()
