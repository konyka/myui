/**
 * @file my_pal_x11.c
 * @brief X11 PAL port: window (XImage over my_lcd_mem), event translation,
 * main loop (select on X fd + self-wakeup pipe), monotonic clock.
 *
 * Present path: the app draws into the window's lcd (a wrapper around an
 * my_lcd_mem BGRA8888 back buffer); my_lcd_end_frame() pushes the whole
 * frame with XPutImage (full-frame; dirty-rect partial present is a TODO).
 * Not done yet: IME, HiDPI, multi-window focus management, clipboard.
 */
/* POSIX (clock_gettime/select/pipe/write) + glibc misc (suseconds_t)
 * under strict -std=c99; must precede ALL system header includes. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mypal/x11/my_pal_x11.h"

#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "myc/my_darray.h"
#include "mypal/x11/my_pal_x11_keymap.h"
#include "myr/my_lcd_mem.h"

/* ---------------- platform ---------------- */

typedef struct x11_pal_t {
  my_pal_t base;
  const my_allocator_t* allocator;
  Display* display;
  int screen;
  Atom wm_delete;
  my_darray_t* windows; /**< x11_window_t* registry for event routing */
  my_pal_event_handler_t handler;
  void* handler_ctx;
} x11_pal_t;

/* ---------------- window ---------------- */

typedef struct x11_window_t {
  my_pal_window_t base;
  x11_pal_t* pal;
  const my_allocator_t* allocator;
  Window xwin;
  GC gc;
  int32_t w;
  int32_t h;
  my_lcd_t* mem_lcd;   /**< back buffer (owned) */
  my_lcd_t* front_lcd; /**< wrapper lcd: end_frame presents (owned) */
  XImage* ximage;      /**< wraps the back buffer (data borrowed) */
} x11_window_t;

static uint64_t x11_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void x11_present(x11_window_t* win) {
  x11_pal_t* p = win->pal;
  XPutImage(p->display, win->xwin, win->gc, win->ximage, 0, 0, 0, 0,
            (unsigned)win->w, (unsigned)win->h);
  XFlush(p->display);
}

static XImage* x11_image_create(x11_pal_t* p, my_lcd_t* mem_lcd, int32_t w,
                                int32_t h) {
  return XCreateImage(p->display, DefaultVisual(p->display, p->screen), 24,
                      ZPixmap, 0, (char*)my_lcd_mem_get_buffer(mem_lcd),
                      (unsigned)w, (unsigned)h, 32,
                      (int)my_lcd_mem_get_stride(mem_lcd));
}

static void x11_image_destroy(XImage* image) {
  if (image != NULL) {
    image->data = NULL; /* buffer belongs to the mem lcd, do not free */
    XDestroyImage(image);
  }
}

/* wrapper lcd: forwards everything to mem_lcd, presents on end_frame */
typedef struct x11_lcd_t {
  my_lcd_t base;
  my_lcd_t* mem;
  x11_window_t* win;
} x11_lcd_t;

static uint32_t x11_lcd_get_width(my_lcd_t* lcd) {
  return my_lcd_get_width(((x11_lcd_t*)lcd)->mem);
}

static uint32_t x11_lcd_get_height(my_lcd_t* lcd) {
  return my_lcd_get_height(((x11_lcd_t*)lcd)->mem);
}

static my_pixel_format_t x11_lcd_get_format(my_lcd_t* lcd) {
  return my_lcd_get_format(((x11_lcd_t*)lcd)->mem);
}

static my_ret_t x11_lcd_begin_frame(my_lcd_t* lcd, const my_rect_t* dirty) {
  return my_lcd_begin_frame(((x11_lcd_t*)lcd)->mem, dirty);
}

static my_ret_t x11_lcd_end_frame(my_lcd_t* lcd) {
  x11_lcd_t* x = (x11_lcd_t*)lcd;
  my_ret_t ret = my_lcd_end_frame(x->mem);
  x11_present(x->win);
  return ret;
}

static my_ret_t x11_lcd_draw_pixels(my_lcd_t* lcd, const void* pixels, int32_t x,
                                    int32_t y, uint32_t w, uint32_t h) {
  return my_lcd_draw_pixels(((x11_lcd_t*)lcd)->mem, pixels, x, y, w, h);
}

static my_ret_t x11_lcd_fill_rect(my_lcd_t* lcd, const my_rect_t* rect,
                                  my_color_t color) {
  return my_lcd_fill_rect(((x11_lcd_t*)lcd)->mem, rect, color);
}

static void x11_lcd_destroy(my_lcd_t* lcd) {
  x11_lcd_t* x = (x11_lcd_t*)lcd;
  if (x != NULL) {
    my_mem_free(((x11_window_t*)x->win)->allocator, x);
  }
}

static const my_lcd_vtable_t s_x11_lcd_vtable = {
    x11_lcd_get_width,  x11_lcd_get_height, x11_lcd_get_format,
    x11_lcd_begin_frame, x11_lcd_end_frame, x11_lcd_draw_pixels,
    x11_lcd_fill_rect,  x11_lcd_destroy};

/* window vtable */

static my_ret_t x11_win_set_title(my_pal_window_t* win, const char* title) {
  x11_window_t* w = (x11_window_t*)win;
  XStoreName(w->pal->display, w->xwin, title != NULL ? title : "");
  return MY_RET_OK;
}

static my_ret_t x11_win_resize(my_pal_window_t* win, int32_t width, int32_t height) {
  x11_window_t* w = (x11_window_t*)win;
  if (width <= 0 || height <= 0) {
    return MY_RET_INVALID_PARAMS;
  }
  XResizeWindow(w->pal->display, w->xwin, (unsigned)width, (unsigned)height);
  /* buffers are re-created on the ConfigureNotify event */
  return MY_RET_OK;
}

static my_ret_t x11_win_show(my_pal_window_t* win) {
  x11_window_t* w = (x11_window_t*)win;
  XMapWindow(w->pal->display, w->xwin);
  XFlush(w->pal->display);
  return MY_RET_OK;
}

static my_ret_t x11_win_get_size(my_pal_window_t* win, int32_t* w, int32_t* h) {
  x11_window_t* win_ = (x11_window_t*)win;
  if (w != NULL) {
    *w = win_->w;
  }
  if (h != NULL) {
    *h = win_->h;
  }
  return MY_RET_OK;
}

static my_lcd_t* x11_win_get_lcd(my_pal_window_t* win) {
  return ((x11_window_t*)win)->front_lcd;
}

/** @brief Re-create back buffer + XImage at the current size. */
static my_ret_t x11_win_recreate_buffers(x11_window_t* w) {
  x11_pal_t* p = w->pal;
  my_lcd_t* mem = my_lcd_mem_create(w->allocator, (uint32_t)w->w, (uint32_t)w->h,
                                    MY_PIXEL_FORMAT_BGRA8888);
  XImage* image;
  if (mem == NULL) {
    return MY_RET_OOM;
  }
  image = x11_image_create(p, mem, w->w, w->h);
  if (image == NULL) {
    my_lcd_destroy(mem);
    return MY_RET_FAIL;
  }
  x11_image_destroy(w->ximage);
  my_lcd_destroy(w->mem_lcd);
  w->mem_lcd = mem;
  w->ximage = image;
  ((x11_lcd_t*)w->front_lcd)->mem = mem;
  return MY_RET_OK;
}

static void x11_win_destroy(my_pal_window_t* win) {
  x11_window_t* w = (x11_window_t*)win;
  x11_pal_t* p;
  size_t i, n;
  if (w == NULL) {
    return;
  }
  p = w->pal;
  /* unregister from the pal's event routing table */
  n = my_darray_size(p->windows);
  for (i = 0; i < n; i++) {
    if (my_darray_get(p->windows, i) == w) {
      my_darray_remove_at(p->windows, i);
      break;
    }
  }
  x11_image_destroy(w->ximage);
  my_lcd_destroy(w->mem_lcd);
  my_mem_free(w->allocator, w->front_lcd);
  XFreeGC(p->display, w->gc);
  XDestroyWindow(p->display, w->xwin);
  XFlush(p->display);
  my_mem_free(w->allocator, w);
}

static const my_pal_window_vtable_t s_x11_window_vtable = {
    x11_win_set_title, x11_win_resize,  x11_win_show,
    x11_win_get_size,  x11_win_get_lcd, x11_win_destroy};

static my_pal_window_t* x11_window_create(my_pal_t* pal, int32_t w, int32_t h,
                                          const char* title) {
  x11_pal_t* p = (x11_pal_t*)pal;
  x11_window_t* win;
  if (w <= 0 || h <= 0) {
    return NULL;
  }
  win = (x11_window_t*)my_mem_calloc(p->allocator, 1, sizeof(x11_window_t));
  if (win == NULL) {
    return NULL;
  }
  win->base.vtable = &s_x11_window_vtable;
  win->pal = p;
  win->allocator = p->allocator;
  win->w = w;
  win->h = h;

  win->xwin = XCreateSimpleWindow(p->display, RootWindow(p->display, p->screen),
                                  0, 0, (unsigned)w, (unsigned)h, 0, 0,
                                  (unsigned long)BlackPixel(p->display, p->screen));
  win->gc = XCreateGC(p->display, win->xwin, 0, NULL);
  XSelectInput(p->display, win->xwin,
               ExposureMask | StructureNotifyMask | ButtonPressMask |
                   ButtonReleaseMask | PointerMotionMask | KeyPressMask |
                   KeyReleaseMask);
  {
    Atom protocol = p->wm_delete;
    XSetWMProtocols(p->display, win->xwin, &protocol, 1);
  }

  win->front_lcd = (my_lcd_t*)my_mem_calloc(p->allocator, 1, sizeof(x11_lcd_t));
  win->mem_lcd = my_lcd_mem_create(p->allocator, (uint32_t)w, (uint32_t)h,
                                   MY_PIXEL_FORMAT_BGRA8888);
  if (win->front_lcd == NULL || win->mem_lcd == NULL) {
    x11_win_destroy((my_pal_window_t*)win);
    return NULL;
  }
  win->front_lcd->vtable = &s_x11_lcd_vtable;
  ((x11_lcd_t*)win->front_lcd)->mem = win->mem_lcd;
  ((x11_lcd_t*)win->front_lcd)->win = win;
  win->ximage = x11_image_create(p, win->mem_lcd, w, h);
  if (win->ximage == NULL) {
    x11_win_destroy((my_pal_window_t*)win);
    return NULL;
  }

  my_darray_push(p->windows, win);
  if (title != NULL) {
    x11_win_set_title((my_pal_window_t*)win, title);
  }
  return (my_pal_window_t*)win;
}

/* ---------------- event translation ---------------- */

static x11_window_t* x11_find_window(x11_pal_t* p, Window xwin) {
  size_t i, n = my_darray_size(p->windows);
  for (i = 0; i < n; i++) {
    x11_window_t* w = (x11_window_t*)my_darray_get(p->windows, i);
    if (w->xwin == xwin) {
      return w;
    }
  }
  return NULL;
}

static uint8_t x11_modifiers(unsigned int state) {
  uint8_t mods = 0;
  if ((state & ShiftMask) != 0u) {
    mods |= MY_KEYMOD_SHIFT;
  }
  if ((state & ControlMask) != 0u) {
    mods |= MY_KEYMOD_CTRL;
  }
  if ((state & Mod1Mask) != 0u) {
    mods |= MY_KEYMOD_ALT;
  }
  return mods;
}

static void x11_dispatch(x11_pal_t* p, const XEvent* xev) {
  my_event_t e;
  x11_window_t* w;
  if (p->handler == NULL) {
    return;
  }
  e = my_event_init(MY_EVENT_NONE);
  e.time_ms = x11_now_ms();
  w = x11_find_window(p, xev->xany.window);

  switch (xev->type) {
    case Expose:
      if (xev->xexpose.count == 0 && w != NULL) {
        e.type = MY_EVENT_PAINT;
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
      }
      break;
    case ConfigureNotify:
      if (w != NULL &&
          (xev->xconfigure.width != w->w || xev->xconfigure.height != w->h)) {
        w->w = xev->xconfigure.width;
        w->h = xev->xconfigure.height;
        x11_win_recreate_buffers(w);
        e.type = MY_EVENT_RESIZE;
        e.u.resize.w = w->w;
        e.u.resize.h = w->h;
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
      }
      break;
    case ButtonPress:
    case ButtonRelease:
      if (w != NULL && xev->xbutton.button <= 3) {
        e.type = xev->type == ButtonPress ? MY_EVENT_POINTER_DOWN
                                          : MY_EVENT_POINTER_UP;
        e.u.pointer.x = xev->xbutton.x;
        e.u.pointer.y = xev->xbutton.y;
        e.u.pointer.button = (uint8_t)xev->xbutton.button;
        e.u.pointer.modifiers = x11_modifiers(xev->xbutton.state);
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
      }
      break;
    case MotionNotify:
      if (w != NULL) {
        e.type = MY_EVENT_POINTER_MOVE;
        e.u.pointer.x = xev->xmotion.x;
        e.u.pointer.y = xev->xmotion.y;
        e.u.pointer.button = 0;
        e.u.pointer.modifiers = x11_modifiers(xev->xmotion.state);
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
      }
      break;
    case KeyPress:
    case KeyRelease:
      if (w != NULL) {
        e.type = xev->type == KeyPress ? MY_EVENT_KEY_DOWN : MY_EVENT_KEY_UP;
        e.u.key.key =
            my_pal_x11_key_from_keysym(XLookupKeysym((XKeyEvent*)&xev->xkey, 0));
        e.u.key.modifiers = x11_modifiers(xev->xkey.state);
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
      }
      break;
    case ClientMessage:
      if (w != NULL && (Atom)xev->xclient.data.l[0] == p->wm_delete) {
        e.type = MY_EVENT_QUIT;
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
      }
      break;
    default:
      break;
  }
}

/* ---------------- main loop ---------------- */

typedef struct posted_event_t {
  my_event_t event;
} posted_event_t;

typedef struct x11_loop_t {
  my_pal_main_loop_t base;
  x11_pal_t* pal;
  const my_allocator_t* allocator;
  my_timer_manager_t* timers;
  my_darray_t* posted; /**< posted_event_t*, cross-thread queue */
  pthread_mutex_t lock;
  bool lock_initialized;
  int wake_pipe[2];
  bool quit;
} x11_loop_t;

static uint64_t x11_timer_now(void* ctx) {
  (void)ctx;
  return x11_now_ms();
}

static my_ret_t x11_loop_post_event(my_pal_main_loop_t* loop,
                                    const my_event_t* event) {
  x11_loop_t* l = (x11_loop_t*)loop;
  posted_event_t* pe;
  if (event == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  pe = (posted_event_t*)my_mem_calloc(l->allocator, 1, sizeof(posted_event_t));
  if (pe == NULL) {
    return MY_RET_OOM;
  }
  pe->event = *event;
  pthread_mutex_lock(&l->lock);
  if (my_darray_push(l->posted, pe) != MY_RET_OK) {
    pthread_mutex_unlock(&l->lock);
    my_mem_free(l->allocator, pe);
    return MY_RET_OOM;
  }
  pthread_mutex_unlock(&l->lock);
  {
    char byte = 1;
    ssize_t ignored = write(l->wake_pipe[1], &byte, 1); /* self-wakeup */
    (void)ignored;
  }
  return MY_RET_OK;
}

static void x11_loop_drain_posted(x11_loop_t* l) {
  for (;;) {
    posted_event_t* pe = NULL;
    pthread_mutex_lock(&l->lock);
    if (my_darray_size(l->posted) > 0) {
      pe = (posted_event_t*)my_darray_get(l->posted, 0);
      my_darray_remove_at(l->posted, 0);
    }
    pthread_mutex_unlock(&l->lock);
    if (pe == NULL) {
      break;
    }
    if (l->pal->handler != NULL) {
      l->pal->handler(l->pal->handler_ctx, NULL, &pe->event);
    }
    my_mem_free(l->allocator, pe);
  }
}

static my_ret_t x11_loop_run(my_pal_main_loop_t* loop) {
  x11_loop_t* l = (x11_loop_t*)loop;
  Display* dpy = l->pal->display;
  int xfd = ConnectionNumber(dpy);

  l->quit = false;
  while (!l->quit) {
    fd_set rfds;
    int maxfd = xfd > l->wake_pipe[0] ? xfd : l->wake_pipe[0];
    uint32_t due = my_timer_manager_due_in_ms(l->timers);
    struct timeval tv;
    struct timeval* tvp = NULL;

    while (XPending(dpy) > 0 && !l->quit) {
      XEvent xev;
      XNextEvent(dpy, &xev);
      x11_dispatch(l->pal, &xev);
    }
    x11_loop_drain_posted(l);
    my_timer_manager_fire(l->timers);
    if (l->quit) {
      break;
    }

    due = my_timer_manager_due_in_ms(l->timers);
    if (due != UINT32_MAX) {
      tv.tv_sec = (time_t)(due / 1000u);
      tv.tv_usec = (suseconds_t)(due % 1000u) * 1000;
      tvp = &tv;
    }
    FD_ZERO(&rfds);
    FD_SET(xfd, &rfds);
    FD_SET(l->wake_pipe[0], &rfds);
    if (select(maxfd + 1, &rfds, NULL, NULL, tvp) > 0 &&
        FD_ISSET(l->wake_pipe[0], &rfds)) {
      char buf[32];
      ssize_t ignored = read(l->wake_pipe[0], buf, sizeof(buf));
      (void)ignored;
    }
  }
  return MY_RET_OK;
}

static my_ret_t x11_loop_quit(my_pal_main_loop_t* loop) {
  x11_loop_t* l = (x11_loop_t*)loop;
  l->quit = true;
  {
    char byte = 1; /* wake select() so run() notices */
    ssize_t ignored = write(l->wake_pipe[1], &byte, 1);
    (void)ignored;
  }
  return MY_RET_OK;
}

static uint32_t x11_loop_add_timer(my_pal_main_loop_t* loop,
                                   my_timer_callback_t callback, void* ctx,
                                   uint32_t interval_ms) {
  x11_loop_t* l = (x11_loop_t*)loop;
  uint32_t id = my_timer_add(l->timers, callback, ctx, interval_ms);
  /* wake select() so the loop recomputes its timeout */
  {
    char byte = 1;
    ssize_t ignored = write(l->wake_pipe[1], &byte, 1);
    (void)ignored;
  }
  return id;
}

static my_ret_t x11_loop_remove_timer(my_pal_main_loop_t* loop, uint32_t id) {
  return my_timer_remove(((x11_loop_t*)loop)->timers, id);
}

static void x11_loop_destroy(my_pal_main_loop_t* loop) {
  x11_loop_t* l = (x11_loop_t*)loop;
  if (l == NULL) {
    return;
  }
  if (l->wake_pipe[0] >= 0) {
    close(l->wake_pipe[0]);
  }
  if (l->wake_pipe[1] >= 0) {
    close(l->wake_pipe[1]);
  }
  while (my_darray_size(l->posted) > 0) {
    posted_event_t* pe = (posted_event_t*)my_darray_get(l->posted, 0);
    my_darray_remove_at(l->posted, 0);
    my_mem_free(l->allocator, pe);
  }
  my_darray_destroy(l->posted);
  my_timer_manager_destroy(l->timers);
  if (l->lock_initialized) {
    pthread_mutex_destroy(&l->lock);
  }
  my_mem_free(l->allocator, l);
}

static const my_pal_main_loop_vtable_t s_x11_loop_vtable = {
    x11_loop_run,        x11_loop_quit,        x11_loop_post_event,
    x11_loop_add_timer,  x11_loop_remove_timer, x11_loop_destroy};

static my_pal_main_loop_t* x11_main_loop_create(my_pal_t* pal) {
  x11_pal_t* p = (x11_pal_t*)pal;
  x11_loop_t* l = (x11_loop_t*)my_mem_calloc(p->allocator, 1, sizeof(x11_loop_t));
  if (l == NULL) {
    return NULL;
  }
  l->base.vtable = &s_x11_loop_vtable;
  l->pal = p;
  l->allocator = p->allocator;
  l->wake_pipe[0] = -1;
  l->wake_pipe[1] = -1;
  l->posted = my_darray_create(p->allocator, 0);
  l->timers = my_timer_manager_create(p->allocator, x11_timer_now, NULL);
  if (l->posted == NULL || l->timers == NULL) {
    x11_loop_destroy((my_pal_main_loop_t*)l);
    return NULL;
  }
  if (pthread_mutex_init(&l->lock, NULL) != 0) {
    x11_loop_destroy((my_pal_main_loop_t*)l);
    return NULL;
  }
  l->lock_initialized = true;
  if (pipe(l->wake_pipe) != 0) {
    x11_loop_destroy((my_pal_main_loop_t*)l);
    return NULL;
  }
  return (my_pal_main_loop_t*)l;
}

/* ---------------- platform vtable ---------------- */

static uint64_t x11_pal_time_now_ms(my_pal_t* pal) {
  (void)pal;
  return x11_now_ms();
}

static my_ret_t x11_pal_set_event_handler(my_pal_t* pal,
                                          my_pal_event_handler_t handler,
                                          void* ctx) {
  x11_pal_t* p = (x11_pal_t*)pal;
  p->handler = handler;
  p->handler_ctx = ctx;
  return MY_RET_OK;
}

static void x11_pal_destroy(my_pal_t* pal) {
  x11_pal_t* p = (x11_pal_t*)pal;
  if (p == NULL) {
    return;
  }
  my_darray_destroy(p->windows);
  XCloseDisplay(p->display);
  my_mem_free(p->allocator, p);
}

static const my_pal_vtable_t s_x11_pal_vtable = {
    x11_window_create, x11_main_loop_create, x11_pal_time_now_ms,
    x11_pal_set_event_handler, x11_pal_destroy};

my_pal_t* my_pal_x11_create(const my_allocator_t* allocator) {
  x11_pal_t* p;
  Display* display = XOpenDisplay(NULL);
  if (display == NULL) {
    return NULL; /* headless: caller may fall back to the dummy port */
  }
  p = (x11_pal_t*)my_mem_calloc(allocator, 1, sizeof(x11_pal_t));
  if (p == NULL) {
    XCloseDisplay(display);
    return NULL;
  }
  p->base.vtable = &s_x11_pal_vtable;
  p->allocator = allocator;
  p->display = display;
  p->screen = DefaultScreen(display);
  p->wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  p->windows = my_darray_create(allocator, 0);
  if (p->windows == NULL) {
    XCloseDisplay(display);
    my_mem_free(allocator, p);
    return NULL;
  }
  return (my_pal_t*)p;
}
