/**
 * @file my_pal_x11.c
 * @brief X11 PAL port: window (XImage over my_lcd_mem), event translation,
 * main loop (select on X fd + self-wakeup pipe), monotonic clock.
 *
 * Present path: the app draws into the window's lcd (a wrapper around an
 * my_lcd_mem BGRA8888 back buffer); my_lcd_end_frame() pushes the whole
 * frame with XPutImage (full-frame; dirty-rect partial present is a TODO).
 * Clipboard: owner of CLIPBOARD + direct/INCR serving and external fetch
 * (M8c/M9d/M11b). Not done yet: IME, HiDPI, multi-window focus.
 */
/* POSIX (clock_gettime/select/pipe/write) + glibc misc (suseconds_t)
 * under strict -std=c99; must precede ALL system header includes. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mypal/x11/my_pal_x11.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#if defined(MYUI_PAL_GL_EGL)
#include <EGL/egl.h>
#endif

#include "myc/my_darray.h"
#include "myc/my_str.h"
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
  char* clipboard;        /**< cached text (we serve it when we own) */
  Atom atom_clipboard;
  Atom atom_utf8;
  Atom atom_targets;
  Atom atom_clip_prop;
  Atom atom_incr;
  /** @brief INCR sender state (M11b; one transfer at a time). */
  struct {
    bool active;
    Window requestor;
    Atom property;
    Atom target;
    size_t offset;   /**< bytes already appended */
    size_t len;      /**< strlen at transfer start */
    size_t chunk;    /**< per-segment payload */
  } incr_tx;
#if defined(MYUI_PAL_GL_EGL)
  EGLDisplay egl_dpy; /**< shared EGL display (lazy, EGL_NO_DISPLAY off) */
  EGLConfig egl_cfg;
  int egl_state; /**< 0 = untried, 1 = ready, -1 = unavailable */
  bool egl_msaa; /**< the shared config carries EGL_SAMPLES=4 (M11c) */
#endif
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
  my_pal_gl_t* gl;     /**< GL mount after gl_enable (owned, M10c) */
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

static my_ret_t x11_lcd_blend_span(my_lcd_t* lcd, int32_t x, int32_t y,
                                     const uint8_t* alpha, int32_t n,
                                     my_color_t color) {
  return my_lcd_blend_span(((x11_lcd_t*)lcd)->mem, x, y, alpha, n, color);
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
    x11_lcd_fill_rect, x11_lcd_blend_span, x11_lcd_destroy};

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
  if (w->gl != NULL) {
    my_pal_gl_destroy(w->gl); /* before XDestroyWindow */
    w->gl = NULL;
  }
  x11_image_destroy(w->ximage);
  my_lcd_destroy(w->mem_lcd);
  my_mem_free(w->allocator, w->front_lcd);
  XFreeGC(p->display, w->gc);
  XDestroyWindow(p->display, w->xwin);
  XFlush(p->display);
  my_mem_free(w->allocator, w);
}

/* ---------------- GL mount (M10c): EGL on X11 ---------------- */

#if defined(MYUI_PAL_GL_EGL)

typedef struct x11_gl_t {
  my_pal_gl_t base;
  x11_window_t* win; /**< borrowed (the window owns this handle) */
  EGLContext ctx;
  EGLSurface surf;
} x11_gl_t;

/** @brief Lazy one-time EGL display/config init (shared by all windows;
 * never eglTerminate'd -- the EGL display outlives individual windows
 * and is reclaimed at process exit). Config negotiation (M11c): prefer
 * EGL_SAMPLES=4 (MSAA), fall back to a plain config when unavailable. */
static bool x11_egl_init(x11_pal_t* p) {
  EGLint major = 0, minor = 0, n = 0;
  EGLint cfg_msaa_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                             EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                             EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,
                             8, EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4,
                             EGL_NONE};
  EGLint cfg_attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                        EGL_NONE};
  if (p->egl_state != 0) {
    return p->egl_state > 0;
  }
  p->egl_dpy = eglGetDisplay((EGLNativeDisplayType)p->display);
  if (p->egl_dpy == EGL_NO_DISPLAY ||
      !eglInitialize(p->egl_dpy, &major, &minor) ||
      !eglBindAPI(EGL_OPENGL_ES_API)) {
    p->egl_state = -1;
    return false;
  }
  if (eglChooseConfig(p->egl_dpy, cfg_msaa_attrs, &p->egl_cfg, 1, &n) &&
      n > 0) {
    p->egl_msaa = true;
  } else if (eglChooseConfig(p->egl_dpy, cfg_attrs, &p->egl_cfg, 1, &n) &&
             n > 0) {
    p->egl_msaa = false; /* no MSAA config: documented fallback */
  } else {
    p->egl_state = -1;
    return false;
  }
  p->egl_state = 1;
  return true;
}

static my_ret_t x11_gl_make_current(my_pal_gl_t* gl) {
  x11_gl_t* g = (x11_gl_t*)gl;
  return eglMakeCurrent(g->win->pal->egl_dpy, g->surf, g->surf, g->ctx)
             ? MY_RET_OK
             : MY_RET_FAIL;
}

static my_ret_t x11_gl_swap(my_pal_gl_t* gl) {
  x11_gl_t* g = (x11_gl_t*)gl;
  return eglSwapBuffers(g->win->pal->egl_dpy, g->surf) ? MY_RET_OK
                                                       : MY_RET_FAIL;
}

static my_ret_t x11_gl_get_size(my_pal_gl_t* gl, int32_t* w, int32_t* h) {
  x11_gl_t* g = (x11_gl_t*)gl;
  if (w != NULL) {
    *w = g->win->w;
  }
  if (h != NULL) {
    *h = g->win->h;
  }
  return MY_RET_OK;
}

static bool x11_gl_has_multisample(my_pal_gl_t* gl) {
  return ((x11_gl_t*)gl)->win->pal->egl_msaa;
}

static void x11_gl_destroy(my_pal_gl_t* gl) {
  x11_gl_t* g = (x11_gl_t*)gl;
  if (g != NULL) {
    EGLDisplay dpy = g->win->pal->egl_dpy;
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, g->surf);
    eglDestroyContext(dpy, g->ctx);
    g->win->gl = NULL; /* the window forgets it (double-destroy safe) */
    my_mem_free(g->win->allocator, g);
  }
}

static const my_pal_gl_vtable_t s_x11_gl_vtable = {
    x11_gl_make_current, x11_gl_swap, x11_gl_get_size,
    x11_gl_has_multisample, x11_gl_destroy};

static my_pal_gl_t* x11_win_gl_enable(my_pal_window_t* win) {
  x11_window_t* w = (x11_window_t*)win;
  x11_pal_t* p = w->pal;
  x11_gl_t* g;
  EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  if (w->gl != NULL) {
    return w->gl;
  }
  if (!x11_egl_init(p)) {
    return NULL;
  }
  g = (x11_gl_t*)my_mem_calloc(w->allocator, 1, sizeof(x11_gl_t));
  if (g == NULL) {
    return NULL;
  }
  g->base.vtable = &s_x11_gl_vtable;
  g->win = w;
  g->ctx = eglCreateContext(p->egl_dpy, p->egl_cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g->ctx == EGL_NO_CONTEXT) {
    my_mem_free(w->allocator, g);
    return NULL;
  }
  g->surf = eglCreateWindowSurface(p->egl_dpy, p->egl_cfg,
                                   (EGLNativeWindowType)w->xwin, NULL);
  if (g->surf == EGL_NO_SURFACE) {
    eglDestroyContext(p->egl_dpy, g->ctx);
    my_mem_free(w->allocator, g);
    return NULL;
  }
  w->gl = (my_pal_gl_t*)g;
  if (x11_gl_make_current(w->gl) == MY_RET_OK) {
    eglSwapInterval(p->egl_dpy, 1); /* vsync */
    /* MSAA (M11c) is surface-driven on ES2 (no core toggle); the config
     * already carries EGL_SAMPLES=4 when egl_msaa is true */
  }
  return w->gl;
}

#else /* !MYUI_PAL_GL_EGL */

static my_pal_gl_t* x11_win_gl_enable(my_pal_window_t* win) {
  (void)win;
  return NULL; /* built without EGL */
}

#endif /* MYUI_PAL_GL_EGL */

static const my_pal_window_vtable_t s_x11_window_vtable = {
    x11_win_set_title, x11_win_resize,  x11_win_show,
    x11_win_get_size,  x11_win_get_lcd, x11_win_destroy,
    x11_win_gl_enable};

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
                   KeyReleaseMask | PropertyChangeMask);
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

static void x11_dispatch(x11_pal_t* p, const XEvent* xev);

/* ---------------- INCR incremental transfer (M11b, ICCCM 2.7.2) ------ */

/** @brief Payloads above this go through INCR instead of one write. */
#define X11_INCR_THRESHOLD 65536u
/** @brief Receiver-side total cap (anti-hang). */
#define X11_INCR_RX_MAX (16u * 1024u * 1024u)

static void x11_incr_tx_cancel(x11_pal_t* p) {
  if (p->incr_tx.active) {
    /* close politely: a zero-length segment ends the requestor's loop */
    XChangeProperty(p->display, p->incr_tx.requestor, p->incr_tx.property,
                    p->incr_tx.target, 8, PropModeAppend, NULL, 0);
    XSelectInput(p->display, p->incr_tx.requestor, 0);
    XFlush(p->display);
    p->incr_tx.active = false;
  }
}

/** @brief PropertyDelete on the transfer property: append the next
 * segment (zero-length once everything was sent). */
static void x11_incr_tx_advance(x11_pal_t* p) {
  size_t remain;
  if (!p->incr_tx.active) {
    return;
  }
  if (p->clipboard == NULL) {
    x11_incr_tx_cancel(p); /* document replaced mid-transfer */
    return;
  }
  remain = p->incr_tx.len - p->incr_tx.offset;
  if (remain > 0) {
    size_t n = remain < p->incr_tx.chunk ? remain : p->incr_tx.chunk;
    XChangeProperty(p->display, p->incr_tx.requestor, p->incr_tx.property,
                    p->incr_tx.target, 8, PropModeAppend,
                    (const unsigned char*)(p->clipboard + p->incr_tx.offset),
                    (int)n);
    p->incr_tx.offset += n;
    XFlush(p->display);
    return;
  }
  /* the requestor deleted the last data segment: zero-length ends it */
  XChangeProperty(p->display, p->incr_tx.requestor, p->incr_tx.property,
                  p->incr_tx.target, 8, PropModeAppend, NULL, 0);
  XSelectInput(p->display, p->incr_tx.requestor, 0);
  XFlush(p->display);
  p->incr_tx.active = false;
}

/** @brief INCR receive loop: delete -> wait for the next segment -> read
 * and append, until a zero-length segment. Drains (and discards) even
 * when buf is full so the owner can finish; capped at X11_INCR_RX_MAX. */
static my_ret_t x11_clipboard_fetch_incr(x11_pal_t* p, Atom prop, char* buf,
                                         size_t size) {
  Display* dpy = p->display;
  x11_window_t* w = (x11_window_t*)my_darray_get(p->windows, 0);
  size_t total = 0, seen = 0;
  if (w == NULL) {
    return MY_RET_NOT_FOUND;
  }
  for (;;) {
    uint64_t deadline = x11_now_ms() + 2000;
    bool got_chunk = false;
    Atom actual;
    int format;
    unsigned long nitems, remaining;
    unsigned char* data = NULL;
    XDeleteProperty(dpy, w->xwin, prop); /* ack: request the next segment */
    XFlush(dpy);
    while (!got_chunk && x11_now_ms() < deadline) {
      while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == PropertyNotify && ev.xproperty.window == w->xwin &&
            ev.xproperty.atom == prop &&
            ev.xproperty.state == PropertyNewValue) {
          got_chunk = true;
        } else {
          x11_dispatch(p, &ev); /* keep the app (and any tx) progressing */
        }
      }
      if (!got_chunk) {
        struct timeval tv;
        fd_set rfds;
        int fd = ConnectionNumber(dpy);
        tv.tv_sec = 0;
        tv.tv_usec = 10000;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        select(fd + 1, &rfds, NULL, NULL, &tv);
      }
    }
    if (!got_chunk) {
      break; /* owner stalled: give up with what we have */
    }
    if (XGetWindowProperty(dpy, w->xwin, prop, 0, 1 << 22, False,
                           AnyPropertyType, &actual, &format, &nitems,
                           &remaining, &data) != Success ||
        data == NULL) {
      break;
    }
    if (nitems == 0) { /* final segment */
      XFree(data);
      buf[total] = '\0';
      return MY_RET_OK;
    }
    seen += nitems;
    if (total < size - 1) {
      size_t room = size - 1 - total;
      size_t n = nitems < room ? nitems : room;
      memcpy(buf + total, data, n);
      total += n;
    }
    XFree(data);
    if (seen >= X11_INCR_RX_MAX) {
      break; /* runaway owner: stop acking, keep the prefix */
    }
  }
  buf[total] = '\0';
  return total > 0 ? MY_RET_OK : MY_RET_FAIL;
}

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

/** @brief Serve our cached clipboard text (UTF8_STRING/STRING/TARGETS);
 * large payloads go INCR. Handler-independent: clipboard serving must
 * work even without a registered app handler (M11b). */
static void x11_serve_selection_request(x11_pal_t* p,
                                        const XSelectionRequestEvent* req) {
  XSelectionEvent notify;
  Atom target = req->target;
  Atom prop = req->property;
  if (prop == None) {
    prop = target;
  }
  memset(&notify, 0, sizeof(notify));
  notify.type = SelectionNotify;
  notify.requestor = req->requestor;
  notify.selection = req->selection;
  notify.target = target;
  notify.time = req->time;
  notify.property = None;
  if (target == p->atom_targets) {
    Atom types[3];
    types[0] = p->atom_targets;
    types[1] = p->atom_utf8;
    types[2] = XA_STRING;
    XChangeProperty(p->display, req->requestor, prop, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)types, 3);
    notify.property = prop;
  } else if ((target == p->atom_utf8 || target == XA_STRING) &&
             p->clipboard != NULL) {
    size_t clen = strlen(p->clipboard);
    if (clen <= X11_INCR_THRESHOLD) {
      XChangeProperty(p->display, req->requestor, prop, target, 8,
                      PropModeReplace, (unsigned char*)p->clipboard,
                      (int)clen);
      notify.property = prop;
    } else if (!p->incr_tx.active) {
      /* large payload: answer INCR, then serve segments as the
       * requestor deletes the property (ICCCM 2.7.2) */
      uint32_t lower_bound = (uint32_t)clen;
      long maxb = XMaxRequestSize(p->display) * 4 - 64;
      p->incr_tx.active = true;
      p->incr_tx.requestor = req->requestor;
      p->incr_tx.property = prop;
      p->incr_tx.target = target;
      p->incr_tx.offset = 0;
      p->incr_tx.len = clen;
      p->incr_tx.chunk = maxb > 1024 ? (size_t)maxb : 1024;
      if (p->incr_tx.chunk > X11_INCR_THRESHOLD) {
        p->incr_tx.chunk = X11_INCR_THRESHOLD;
      }
      XChangeProperty(p->display, req->requestor, prop, p->atom_incr, 32,
                      PropModeReplace, (unsigned char*)&lower_bound, 1);
      XSelectInput(p->display, req->requestor, PropertyChangeMask);
      notify.property = prop;
    }
    /* else: one transfer at a time; refuse (property stays None) */
  }
  XSendEvent(p->display, req->requestor, False, 0, (XEvent*)&notify);
}

static void x11_dispatch(x11_pal_t* p, const XEvent* xev) {
  my_event_t e;
  x11_window_t* w;
  /* clipboard serving + INCR sender progress are handler-independent */
  if (xev->type == PropertyNotify && p->incr_tx.active &&
      xev->xproperty.window == p->incr_tx.requestor &&
      xev->xproperty.atom == p->incr_tx.property &&
      xev->xproperty.state == PropertyDelete) {
    x11_incr_tx_advance(p);
    return;
  }
  if (xev->type == SelectionRequest) {
    x11_serve_selection_request(p, (const XSelectionRequestEvent*)&xev->xselectionrequest);
    return;
  }
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
      if (w != NULL && (xev->xbutton.button == 4 || xev->xbutton.button == 5) &&
          xev->type == ButtonPress) {
        e.type = MY_EVENT_POINTER_WHEEL;
        e.u.pointer.x = xev->xbutton.x;
        e.u.pointer.y = xev->xbutton.y;
        e.u.pointer.delta = xev->xbutton.button == 4 ? 1 : -1;
        p->handler(p->handler_ctx, (my_pal_window_t*)w, &e);
        break;
      }
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
    case SelectionRequest:
      break; /* served handler-independently (see x11_dispatch top) */
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

static my_ret_t x11_clipboard_set(my_pal_t* pal, const char* text) {
  x11_pal_t* p = (x11_pal_t*)pal;
  char* copy = my_strdup(p->allocator, text);
  x11_window_t* w;
  if (text != NULL && copy == NULL) {
    return MY_RET_OOM;
  }
  x11_incr_tx_cancel(p); /* replacing the document mid-transfer (M11b) */
  my_mem_free(p->allocator, p->clipboard);
  p->clipboard = copy;
  /* own the selection via the first window (in-app roundtrip; serving
   * other apps works via SelectionRequest; requesting FROM other apps is
   * a TODO -> get falls back to the cache) */
  if (my_darray_size(p->windows) > 0) {
    w = (x11_window_t*)my_darray_get(p->windows, 0);
    XSetSelectionOwner(p->display, p->atom_clipboard, w->xwin, CurrentTime);
  }
  return MY_RET_OK;
}

/**
 * @brief Fetch from an EXTERNAL selection owner: XConvertSelection +
 * synchronous wait for SelectionNotify (~500ms). Other events are
 * dispatched normally while waiting (reentrancy-safe: the app handler
 * may paint/post). Large payloads arrive via INCR: the incremental
 * receive loop runs in x11_clipboard_fetch_incr (M11b).
 */
static my_ret_t x11_clipboard_fetch(x11_pal_t* p, Atom target, char* buf,
                                    size_t size) {
  Display* dpy = p->display;
  x11_window_t* w = (x11_window_t*)my_darray_get(p->windows, 0);
  Atom prop = p->atom_clip_prop;
  uint64_t deadline = x11_now_ms() + 500;
  if (w == NULL) {
    return MY_RET_NOT_FOUND;
  }
  XDeleteProperty(dpy, w->xwin, prop);
  XConvertSelection(dpy, p->atom_clipboard, target, prop, w->xwin,
                    CurrentTime);
  while (x11_now_ms() < deadline) {
    while (XPending(dpy) > 0) {
      XEvent ev;
      XNextEvent(dpy, &ev);
      if (ev.type == SelectionNotify &&
          ev.xselection.selection == p->atom_clipboard) {
        if (ev.xselection.property == None) {
          return MY_RET_NOT_SUPPORTED; /* owner cannot provide this target */
        }
        {
          Atom actual;
          int format;
          unsigned long nitems, remaining;
          unsigned char* data = NULL;
          my_ret_t ret = MY_RET_FAIL;
          if (XGetWindowProperty(dpy, w->xwin, prop, 0, 1 << 20, True,
                                 AnyPropertyType, &actual, &format, &nitems,
                                 &remaining, &data) == Success &&
              data != NULL) {
            if (actual == p->atom_incr) {
              /* large payload: enter the incremental receive loop (M11b) */
              XFree(data);
              return x11_clipboard_fetch_incr(p, prop, buf, size);
            }
            {
              size_t n = nitems < size - 1 ? nitems : size - 1;
              memcpy(buf, data, n);
              buf[n] = '\0';
              XFree(data);
              ret = MY_RET_OK;
            }
          } else if (data != NULL) {
            XFree(data);
          }
          return ret;
        }
      } else {
        x11_dispatch(p, &ev); /* keep the app responsive while waiting */
      }
    }
    {
      struct timeval tv;
      fd_set rfds;
      int fd = ConnectionNumber(dpy);
      tv.tv_sec = 0;
      tv.tv_usec = 10000;
      FD_ZERO(&rfds);
      FD_SET(fd, &rfds);
      select(fd + 1, &rfds, NULL, NULL, &tv);
    }
  }
  return MY_RET_NOT_FOUND; /* timeout */
}

static my_ret_t x11_clipboard_get(my_pal_t* pal, char* buf, size_t size) {
  x11_pal_t* p = (x11_pal_t*)pal;
  Window owner;
  if (buf == NULL || size == 0) {
    return MY_RET_INVALID_PARAMS;
  }
  owner = XGetSelectionOwner(p->display, p->atom_clipboard);
  if (owner != None && p->clipboard != NULL) {
    /* we own it (or owned it recently): serve the cache */
    size_t i, n = my_darray_size(p->windows);
    bool ours = false;
    for (i = 0; i < n; i++) {
      if (((x11_window_t*)my_darray_get(p->windows, i))->xwin == owner) {
        ours = true;
      }
    }
    if (ours) {
      snprintf(buf, size, "%s", p->clipboard);
      return MY_RET_OK;
    }
  }
  if (owner == None) {
    return MY_RET_NOT_FOUND;
  }
  /* external owner: prefer UTF8_STRING, fall back to STRING */
  if (x11_clipboard_fetch(p, p->atom_utf8, buf, size) == MY_RET_OK) {
    return MY_RET_OK;
  }
  return x11_clipboard_fetch(p, XA_STRING, buf, size);
}

static void x11_pal_destroy(my_pal_t* pal) {
  x11_pal_t* p = (x11_pal_t*)pal;
  if (p == NULL) {
    return;
  }
  my_mem_free(p->allocator, p->clipboard);
  my_darray_destroy(p->windows);
  XCloseDisplay(p->display);
  my_mem_free(p->allocator, p);
}

static const my_pal_vtable_t s_x11_pal_vtable = {
    x11_window_create, x11_main_loop_create, x11_pal_time_now_ms,
    x11_pal_set_event_handler, x11_clipboard_set, x11_clipboard_get,
    x11_pal_destroy};

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
  p->atom_clipboard = XInternAtom(display, "CLIPBOARD", False);
  p->atom_utf8 = XInternAtom(display, "UTF8_STRING", False);
  p->atom_targets = XInternAtom(display, "TARGETS", False);
  p->atom_clip_prop = XInternAtom(display, "MYUI_CLIP_PROP", False);
  p->atom_incr = XInternAtom(display, "INCR", False);
  p->windows = my_darray_create(allocator, 0);
  if (p->windows == NULL) {
    XCloseDisplay(display);
    my_mem_free(allocator, p);
    return NULL;
  }
  return (my_pal_t*)p;
}
