/**
 * @file my_pal_wayland.c
 * @brief Wayland PAL port: xdg-shell window over wl_shm buffers.
 *
 * Buffer/present model: one shm buffer per window (memfd + mmap, format
 * XRGB8888 = our BGRA8888). The app draws into it via the window's lcd;
 * end_frame attaches + commits only when the compositor released the
 * buffer (wl_buffer.release), and a wl_callback frame event provides
 * the vsync cadence. Events: wl_pointer + wl_keyboard (xkbcommon),
 * xdg_toplevel close -> QUIT.
 */
/* POSIX/GNU (memfd_create/pipe/poll/clock_gettime) under strict -std=c99 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mypal/wayland/my_pal_wayland.h"

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#if defined(MYUI_PAL_GL_EGL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif
#endif

#include "myc/my_darray.h"
#include "mypal/wayland/my_pal_wayland_keymap.h"
#include "myr/my_lcd_mem.h"

#include "xdg-shell-client-protocol.h"

/* ---------------- platform ---------------- */

typedef struct wl_pal_t {
  my_pal_t base;
  const my_allocator_t* allocator;
  struct wl_display* display;
  struct wl_registry* registry;
  struct wl_compositor* compositor;
  struct wl_shm* shm;
  struct xdg_wm_base* wm_base;
  struct wl_seat* seat;
  struct wl_pointer* pointer;
  struct wl_keyboard* keyboard;
  struct xkb_context* xkb_ctx;
  struct xkb_keymap* xkb_keymap;
  struct xkb_state* xkb_state;
  my_darray_t* windows; /**< wl_window_t* registry */
  my_pal_event_handler_t handler;
  void* handler_ctx;
  struct wl_output* output; /**< first output (M12c scale source) */
  int32_t output_scale;   /**< wl_output.scale (integer, default 1) */
  char* clipboard; /* payload cache (memory roundtrip + data_source) */
  /** @brief wl_data_device clipboard (M12b). */
  struct wl_data_device_manager* data_mgr;
  struct wl_data_device* data_device;
  struct wl_data_source* data_source;    /**< our offer (owned, NULL ok) */
  struct wl_data_offer* selection_offer; /**< current selection (owned) */
  uint32_t kb_enter_serial;              /**< last keyboard enter serial */
#if defined(MYUI_PAL_GL_EGL)
  EGLDisplay egl_dpy; /**< shared EGL display (lazy, EGL_NO_DISPLAY off) */
  EGLConfig egl_cfg;
  int egl_state; /**< 0 = untried, 1 = ready, -1 = unavailable */
  bool egl_msaa; /**< the shared config carries EGL_SAMPLES=4 (M11c) */
#endif
} wl_pal_t;

static void dispatch_event(wl_pal_t* p, my_pal_window_t* win, my_event_t* e) {
  if (p->handler != NULL) {
    p->handler(p->handler_ctx, win, e);
  }
}

/* ---------------- window ---------------- */

typedef struct wl_window_t {
  my_pal_window_t base;
  wl_pal_t* pal;
  const my_allocator_t* allocator;
  struct wl_surface* surface;
  struct xdg_surface* xsurface;
  struct xdg_toplevel* toplevel;
  struct wl_buffer* wlbuf;
  int shm_fd;
  uint8_t* pixels;
  size_t shm_size;
  int32_t w, h;
  my_lcd_t* lcd;      /**< over pixels (owned wrapper) */
  bool configured;    /**< got first xdg_surface.configure */
  bool buffer_busy;   /**< compositor hasn't released it yet */
  bool closed;
  int32_t pointer_x, pointer_y;
  struct wl_callback* frame_cb;
  my_pal_gl_t* gl;    /**< GL mount after gl_enable (owned, M10c) */
#if defined(MYUI_PAL_GL_EGL)
  struct wl_egl_window* egl_win; /**< owned by the GL mount */
#endif
} wl_window_t;

static void present(wl_window_t* w) {
  if (w->wlbuf == NULL || w->buffer_busy || !w->configured) {
    return;
  }
  wl_surface_attach(w->surface, w->wlbuf, 0, 0);
  /* damage is in BUFFER (physical) coordinates */
  wl_surface_damage(w->surface, 0, 0, w->w * w->pal->output_scale,
                    w->h * w->pal->output_scale);
  wl_surface_commit(w->surface);
  w->buffer_busy = true;
}

/* lcd wrapper: forwards to mem lcd, presents on end_frame */
typedef struct wl_lcd_t {
  my_lcd_t base;
  my_lcd_t* mem;
  wl_window_t* win;
} wl_lcd_t;

static uint32_t wl_lcd_w(my_lcd_t* lcd) {
  return my_lcd_get_width(((wl_lcd_t*)lcd)->mem);
}
static uint32_t wl_lcd_h(my_lcd_t* lcd) {
  return my_lcd_get_height(((wl_lcd_t*)lcd)->mem);
}
static my_pixel_format_t wl_lcd_fmt(my_lcd_t* lcd) {
  return my_lcd_get_format(((wl_lcd_t*)lcd)->mem);
}
static my_ret_t wl_lcd_begin(my_lcd_t* lcd, const my_rect_t* dirty) {
  return my_lcd_begin_frame(((wl_lcd_t*)lcd)->mem, dirty);
}
static my_ret_t wl_lcd_end(my_lcd_t* lcd) {
  wl_lcd_t* x = (wl_lcd_t*)lcd;
  my_ret_t ret = my_lcd_end_frame(x->mem);
  present(x->win);
  return ret;
}
static my_ret_t wl_lcd_pixels(my_lcd_t* lcd, const void* px, int32_t x,
                              int32_t y, uint32_t w, uint32_t h) {
  return my_lcd_draw_pixels(((wl_lcd_t*)lcd)->mem, px, x, y, w, h);
}
static my_ret_t wl_lcd_fill(my_lcd_t* lcd, const my_rect_t* r, my_color_t c) {
  return my_lcd_fill_rect(((wl_lcd_t*)lcd)->mem, r, c);
}
static my_ret_t wl_lcd_blend(my_lcd_t* lcd, int32_t x, int32_t y,
                               const uint8_t* alpha, int32_t n,
                               my_color_t color) {
  return my_lcd_blend_span(((wl_lcd_t*)lcd)->mem, x, y, alpha, n, color);
}

static void wl_lcd_destroy(my_lcd_t* lcd) {
  wl_lcd_t* x = (wl_lcd_t*)lcd;
  if (x != NULL) {
    my_mem_free(((wl_window_t*)x->win)->allocator, x);
  }
}

static const my_lcd_vtable_t s_wl_lcd_vtable = {wl_lcd_w,      wl_lcd_h,
                                                wl_lcd_fmt,    wl_lcd_begin,
                                                wl_lcd_end,    wl_lcd_pixels,
                                                wl_lcd_fill,   wl_lcd_blend,
                                                wl_lcd_destroy};

/* shm buffer create (memfd + mmap, WL_SHM_FORMAT_XRGB8888). Window w/h
 * is LOGICAL; the buffer is physical = logical*output_scale (M12c),
 * presented with wl_surface_set_buffer_scale. */
static bool wl_buffer_create(wl_window_t* w) {
  struct wl_shm_pool* pool;
  int32_t bw = w->w * w->pal->output_scale;
  int32_t bh = w->h * w->pal->output_scale;
  w->shm_size = (size_t)bw * (size_t)bh * 4u;
  w->shm_fd = memfd_create("myui-wl", 0);
  if (w->shm_fd < 0 || ftruncate(w->shm_fd, (off_t)w->shm_size) != 0) {
    return false;
  }
  w->pixels = (uint8_t*)mmap(NULL, w->shm_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, w->shm_fd, 0);
  if (w->pixels == MAP_FAILED) {
    w->pixels = NULL;
    return false;
  }
  pool = wl_shm_create_pool(w->pal->shm, w->shm_fd, (int)w->shm_size);
  w->wlbuf = wl_shm_pool_create_buffer(pool, 0, bw, bh, bw * 4,
                                       WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  return w->wlbuf != NULL;
}

/* ---------------- wayland listeners ---------------- */

static void on_buffer_release(void* data, struct wl_buffer* buffer) {
  wl_window_t* w = (wl_window_t*)data;
  (void)buffer;
  w->buffer_busy = false;
  present(w); /* flush any frame skipped while busy */
}

static const struct wl_buffer_listener BUFFER_LISTENER = {
    .release = on_buffer_release};

/* frame callback: re-arm on each done event (vsync cadence; the actual
 * repaint is driven by the window manager's dirty tick) */
static void on_frame_done(void* data, struct wl_callback* cb, uint32_t time);

static const struct wl_callback_listener FRAME_LISTENER = {.done = on_frame_done};

static void on_frame_done(void* data, struct wl_callback* cb, uint32_t time) {
  wl_window_t* w = (wl_window_t*)data;
  (void)time;
  if (cb != NULL) {
    wl_callback_destroy(cb);
  }
  w->frame_cb = wl_surface_frame(w->surface);
  wl_callback_add_listener(w->frame_cb, &FRAME_LISTENER, w);
}

static void on_xsurface_configure(void* data, struct xdg_surface* xs,
                                  uint32_t serial) {
  wl_window_t* w = (wl_window_t*)data;
  xdg_surface_ack_configure(xs, serial);
  if (!w->configured) {
    w->configured = true;
    present(w);
  }
}

static const struct xdg_surface_listener XSURFACE_LISTENER = {
    .configure = on_xsurface_configure};

static void on_toplevel_configure(void* data, struct xdg_toplevel* tl,
                                  int32_t width, int32_t height,
                                  struct wl_array* states) {
  wl_window_t* w = (wl_window_t*)data;
  (void)tl;
  (void)states;
  if (width > 0 && height > 0 && (width != w->w || height != w->h)) {
    my_event_t e;
    /* resize: recreate the shm buffer + lcd at the new size */
    if (w->wlbuf != NULL) {
      wl_buffer_destroy(w->wlbuf);
      w->wlbuf = NULL;
    }
    if (w->pixels != NULL) {
      munmap(w->pixels, w->shm_size);
      w->pixels = NULL;
    }
    if (w->shm_fd >= 0) {
      close(w->shm_fd);
      w->shm_fd = -1;
    }
    w->w = width;
    w->h = height;
#if defined(MYUI_PAL_GL_EGL)
    if (w->egl_win != NULL) {
      /* keep the EGL window surface in sync: physical pixels */
      wl_egl_window_resize(w->egl_win, width * w->pal->output_scale,
                           height * w->pal->output_scale, 0, 0);
    }
#endif
    if (wl_buffer_create(w)) {
      wl_buffer_add_listener(w->wlbuf, &BUFFER_LISTENER, w);
      my_lcd_destroy(w->lcd);
      {
        wl_lcd_t* xl = (wl_lcd_t*)my_mem_calloc(w->allocator, 1, sizeof(wl_lcd_t));
        int32_t bw = width * w->pal->output_scale;
        int32_t bh = height * w->pal->output_scale;
        xl->base.vtable = &s_wl_lcd_vtable;
        xl->mem = my_lcd_mem_create_from_buffer(w->allocator, (uint32_t)bw,
                                                (uint32_t)bh,
                                                MY_PIXEL_FORMAT_BGRA8888,
                                                w->pixels, (uint32_t)bw * 4u);
        xl->win = w;
        w->lcd = (my_lcd_t*)xl;
      }
      w->buffer_busy = false;
      e = my_event_init(MY_EVENT_RESIZE);
      e.u.resize.w = width;
      e.u.resize.h = height;
      dispatch_event(w->pal, (my_pal_window_t*)w, &e);
    }
  }
}

static void on_toplevel_close(void* data, struct xdg_toplevel* tl) {
  wl_window_t* w = (wl_window_t*)data;
  my_event_t e;
  (void)tl;
  e = my_event_init(MY_EVENT_QUIT);
  dispatch_event(w->pal, (my_pal_window_t*)w, &e);
}

static const struct xdg_toplevel_listener TOPLEVEL_LISTENER = {
    .configure = on_toplevel_configure, .close = on_toplevel_close};

static wl_window_t* find_by_surface(wl_pal_t* p, struct wl_surface* surface) {
  size_t i, n = my_darray_size(p->windows);
  for (i = 0; i < n; i++) {
    wl_window_t* w = (wl_window_t*)my_darray_get(p->windows, i);
    if (w->surface == surface) {
      return w;
    }
  }
  return NULL;
}

static struct wl_surface* g_last_surface = NULL;

static void on_pointer_enter(void* data, struct wl_pointer* ptr,
                             uint32_t serial, struct wl_surface* surface,
                             wl_fixed_t sx, wl_fixed_t sy) {
  wl_pal_t* p = (wl_pal_t*)data;
  wl_window_t* w;
  (void)ptr;
  (void)serial;
  g_last_surface = surface;
  w = find_by_surface(p, surface);
  if (w != NULL) {
    w->pointer_x = wl_fixed_to_int(sx);
    w->pointer_y = wl_fixed_to_int(sy);
  }
}

static void on_pointer_leave(void* data, struct wl_pointer* ptr,
                             uint32_t serial, struct wl_surface* surface) {
  (void)data;
  (void)ptr;
  (void)serial;
  (void)surface;
  g_last_surface = NULL;
}

static void on_pointer_motion(void* data, struct wl_pointer* ptr,
                              uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
  wl_pal_t* p = (wl_pal_t*)data;
  wl_window_t* w = find_by_surface(p, g_last_surface);
  my_event_t e;
  (void)ptr;
  (void)time;
  if (w == NULL) {
    return;
  }
  w->pointer_x = wl_fixed_to_int(sx);
  w->pointer_y = wl_fixed_to_int(sy);
  e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.x = w->pointer_x;
  e.u.pointer.y = w->pointer_y;
  dispatch_event(p, (my_pal_window_t*)w, &e);
}

static void on_pointer_button(void* data, struct wl_pointer* ptr,
                              uint32_t serial, uint32_t time, uint32_t button,
                              uint32_t state) {
  wl_pal_t* p = (wl_pal_t*)data;
  wl_window_t* w = find_by_surface(p, g_last_surface);
  my_event_t e;
  (void)ptr;
  (void)serial;
  (void)time;
  if (w == NULL) {
    return;
  }
  e = my_event_init(state == WL_POINTER_BUTTON_STATE_PRESSED
                        ? MY_EVENT_POINTER_DOWN
                        : MY_EVENT_POINTER_UP);
  e.u.pointer.x = w->pointer_x;
  e.u.pointer.y = w->pointer_y;
  e.u.pointer.button = button == 0x111 ? 2 : (button == 0x112 ? 3 : 1);
  dispatch_event(p, (my_pal_window_t*)w, &e);
}

static void on_pointer_axis(void* data, struct wl_pointer* ptr, uint32_t time,
                            uint32_t axis, wl_fixed_t value) {
  wl_pal_t* p = (wl_pal_t*)data;
  wl_window_t* w = find_by_surface(p, g_last_surface);
  my_event_t e;
  (void)ptr;
  (void)time;
  if (w == NULL || axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
    return;
  }
  e = my_event_init(MY_EVENT_POINTER_WHEEL);
  e.u.pointer.x = w->pointer_x;
  e.u.pointer.y = w->pointer_y;
  e.u.pointer.delta = wl_fixed_to_int(value) < 0 ? 1 : -1;
  dispatch_event(p, (my_pal_window_t*)w, &e);
}

static const struct wl_pointer_listener POINTER_LISTENER = {
    .enter = on_pointer_enter, .leave = on_pointer_leave,
    .motion = on_pointer_motion, .button = on_pointer_button,
    .axis = on_pointer_axis};

static void on_kb_enter(void* data, struct wl_keyboard* kb, uint32_t serial,
                        struct wl_surface* surface, struct wl_array* keys) {
  wl_pal_t* p = (wl_pal_t*)data;
  (void)kb;
  (void)surface;
  (void)keys;
  p->kb_enter_serial = serial; /* needed by set_selection (M12b) */
}

static void on_kb_leave(void* data, struct wl_keyboard* kb, uint32_t serial,
                        struct wl_surface* surface) {
  (void)data;
  (void)kb;
  (void)serial;
  (void)surface;
}

static void on_kb_keymap(void* data, struct wl_keyboard* kb, uint32_t format,
                         int32_t fd, uint32_t size) {
  wl_pal_t* p = (wl_pal_t*)data;
  char* map;
  (void)kb;
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  map = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map != MAP_FAILED) {
    struct xkb_keymap* km = xkb_keymap_new_from_string(
        p->xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (km != NULL) {
      struct xkb_state* st = xkb_state_new(km);
      if (st != NULL) {
        if (p->xkb_state != NULL) {
          xkb_state_unref(p->xkb_state);
        }
        if (p->xkb_keymap != NULL) {
          xkb_keymap_unref(p->xkb_keymap);
        }
        p->xkb_keymap = km;
        p->xkb_state = st;
      } else {
        xkb_keymap_unref(km);
      }
    }
  }
  close(fd);
}

static void on_kb_key(void* data, struct wl_keyboard* kb, uint32_t serial,
                      uint32_t time, uint32_t key, uint32_t state) {
  wl_pal_t* p = (wl_pal_t*)data;
  my_event_t e;
  xkb_keysym_t sym;
  (void)kb;
  (void)serial;
  (void)time;
  if (p->xkb_state == NULL) {
    return;
  }
  sym = xkb_state_key_get_one_sym(p->xkb_state, key + 8);
  e = my_event_init(state == WL_KEYBOARD_KEY_STATE_PRESSED ? MY_EVENT_KEY_DOWN
                                                           : MY_EVENT_KEY_UP);
  e.u.key.key = my_pal_wayland_key_from_keysym((uint32_t)sym);
  e.u.key.modifiers = 0;
  dispatch_event(p, NULL, &e);
}

static void on_kb_modifiers(void* data, struct wl_keyboard* kb,
                            uint32_t serial, uint32_t depressed,
                            uint32_t latched, uint32_t locked,
                            uint32_t group) {
  wl_pal_t* p = (wl_pal_t*)data;
  (void)kb;
  (void)serial;
  if (p->xkb_state != NULL) {
    xkb_state_update_mask(p->xkb_state, depressed, latched, locked, 0, 0,
                          group);
  }
}

static void on_kb_repeat_info(void* data, struct wl_keyboard* kb, int32_t rate,
                              int32_t delay) {
  (void)data;
  (void)kb;
  (void)rate;
  (void)delay;
}

static const struct wl_keyboard_listener KEYBOARD_LISTENER = {
    .keymap = on_kb_keymap, .enter = on_kb_enter, .leave = on_kb_leave,
    .key = on_kb_key, .modifiers = on_kb_modifiers,
    .repeat_info = on_kb_repeat_info};

/* ---------------- wl_data_device clipboard (M12b) ---------------- */

static void on_ds_target(void* data, struct wl_data_source* src,
                         const char* mime) {
  (void)data;
  (void)src;
  (void)mime;
}

static void on_ds_send(void* data, struct wl_data_source* src,
                       const char* mime, int32_t fd) {
  wl_pal_t* p = (wl_pal_t*)data;
  const char* text = p->clipboard;
  size_t left;
  (void)src;
  (void)mime;
  if (text == NULL) {
    close(fd);
    return;
  }
  left = strlen(text);
  while (left > 0) {
    ssize_t n = write(fd, text, left);
    if (n <= 0) {
      break;
    }
    text += n;
    left -= (size_t)n;
  }
  close(fd);
}

static void on_ds_cancelled(void* data, struct wl_data_source* src) {
  wl_pal_t* p = (wl_pal_t*)data;
  wl_data_source_destroy(src);
  if (p->data_source == src) {
    p->data_source = NULL;
  }
}

static void on_ds_dnd_performed(void* data, struct wl_data_source* src) {
  (void)data;
  (void)src;
}
static void on_ds_dnd_finished(void* data, struct wl_data_source* src) {
  (void)data;
  (void)src;
}
static void on_ds_action(void* data, struct wl_data_source* src,
                         uint32_t action) {
  (void)data;
  (void)src;
  (void)action;
}

static const struct wl_data_source_listener DS_LISTENER = {
    .target = on_ds_target,
    .send = on_ds_send,
    .cancelled = on_ds_cancelled,
    .dnd_drop_performed = on_ds_dnd_performed,
    .dnd_finished = on_ds_dnd_finished,
    .action = on_ds_action,
};

static void on_dd_data_offer(void* data, struct wl_data_device* dd,
                             struct wl_data_offer* offer) {
  (void)data;
  (void)dd;
  if (offer != NULL) {
    wl_data_offer_destroy(offer); /* no drag-and-drop (clipboard only) */
  }
}

static void on_dd_selection(void* data, struct wl_data_device* dd,
                            struct wl_data_offer* offer) {
  wl_pal_t* p = (wl_pal_t*)data;
  (void)dd;
  if (p->selection_offer != NULL) {
    wl_data_offer_destroy(p->selection_offer);
  }
  p->selection_offer = offer; /* NULL when the selection is cleared */
}

static void on_dd_enter(void* data, struct wl_data_device* dd,
                        uint32_t serial, struct wl_surface* surface,
                        wl_fixed_t x, wl_fixed_t y,
                        struct wl_data_offer* offer) {
  (void)data;
  (void)dd;
  (void)serial;
  (void)surface;
  (void)x;
  (void)y;
  (void)offer;
}
static void on_dd_leave(void* data, struct wl_data_device* dd) {
  (void)data;
  (void)dd;
}
static void on_dd_motion(void* data, struct wl_data_device* dd,
                         uint32_t time, wl_fixed_t x, wl_fixed_t y) {
  (void)data;
  (void)dd;
  (void)time;
  (void)x;
  (void)y;
}
static void on_dd_drop(void* data, struct wl_data_device* dd) {
  (void)data;
  (void)dd;
}

static const struct wl_data_device_listener DD_LISTENER = {
    .data_offer = on_dd_data_offer,
    .enter = on_dd_enter,
    .leave = on_dd_leave,
    .motion = on_dd_motion,
    .drop = on_dd_drop,
    .selection = on_dd_selection,
};

/** @brief Lazily create the seat's data device. */
static bool wl_data_device_ready(wl_pal_t* p) {
  if (p->data_device != NULL) {
    return true;
  }
  if (p->data_mgr == NULL || p->seat == NULL) {
    return false;
  }
  p->data_device = wl_data_device_manager_get_data_device(p->data_mgr,
                                                          p->seat);
  if (p->data_device != NULL) {
    wl_data_device_add_listener(p->data_device, &DD_LISTENER, p);
  }
  return p->data_device != NULL;
}

static void on_seat_capabilities(void* data, struct wl_seat* seat,
                                 uint32_t caps) {
  wl_pal_t* p = (wl_pal_t*)data;
  if ((caps & WL_SEAT_CAPABILITY_POINTER) != 0u && p->pointer == NULL) {
    p->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(p->pointer, &POINTER_LISTENER, p);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0u && p->keyboard == NULL) {
    p->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(p->keyboard, &KEYBOARD_LISTENER, p);
  }
}

static void on_seat_name(void* data, struct wl_seat* seat, const char* name) {
  (void)data;
  (void)seat;
  (void)name;
}

static const struct wl_seat_listener SEAT_LISTENER = {
    .capabilities = on_seat_capabilities, .name = on_seat_name};

/* ---------------- wl_output (M12c: display scale source) ------------- */

static void on_output_geometry(void* data, struct wl_output* output, int32_t x,
                               int32_t y, int32_t pw, int32_t ph, int32_t sub,
                               const char* make, const char* model,
                               int32_t transform) {
  (void)data;
  (void)output;
  (void)x;
  (void)y;
  (void)pw;
  (void)ph;
  (void)sub;
  (void)make;
  (void)model;
  (void)transform;
}

static void on_output_mode(void* data, struct wl_output* output,
                           uint32_t flags, int32_t w, int32_t h,
                           int32_t refresh) {
  (void)data;
  (void)output;
  (void)flags;
  (void)w;
  (void)h;
  (void)refresh;
}

static void on_output_done(void* data, struct wl_output* output) {
  (void)data;
  (void)output;
}

static void on_output_scale(void* data, struct wl_output* output,
                            int32_t factor) {
  wl_pal_t* p = (wl_pal_t*)data;
  (void)output;
  if (factor > 0) {
    p->output_scale = factor;
  }
}

static const struct wl_output_listener s_output_listener = {
    .geometry = on_output_geometry,
    .mode = on_output_mode,
    .done = on_output_done,
    .scale = on_output_scale,
};


static void on_registry_global(void* data, struct wl_registry* registry,
                               uint32_t name, const char* interface,
                               uint32_t version) {
  wl_pal_t* p = (wl_pal_t*)data;
  (void)version;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    p->compositor =
        (struct wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    p->shm = (struct wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    p->wm_base =
        (struct xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    p->seat = (struct wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 5);
    wl_seat_add_listener(p->seat, &SEAT_LISTENER, p);
  } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
    p->data_mgr = (struct wl_data_device_manager*)wl_registry_bind(
        registry, name, &wl_data_device_manager_interface, 3);
  } else if (strcmp(interface, wl_output_interface.name) == 0 &&
             p->output == NULL) {
    p->output = (struct wl_output*)wl_registry_bind(registry, name,
                                                    &wl_output_interface, 2);
    wl_output_add_listener(p->output, &s_output_listener, p);
  }
}


static void on_registry_remove(void* data, struct wl_registry* registry,
                               uint32_t name) {
  (void)data;
  (void)registry;
  (void)name;
}

static const struct wl_registry_listener REGISTRY_LISTENER = {
    .global = on_registry_global, .global_remove = on_registry_remove};

/* ---------------- window vtable ---------------- */

static my_ret_t wl_win_set_title(my_pal_window_t* win, const char* title) {
  wl_window_t* w = (wl_window_t*)win;
  xdg_toplevel_set_title(w->toplevel, title != NULL ? title : "");
  return MY_RET_OK;
}

static my_ret_t wl_win_resize(my_pal_window_t* win, int32_t width,
                              int32_t height) {
  (void)win;
  (void)width;
  (void)height;
  return MY_RET_NOT_SUPPORTED; /* compositor drives sizes on wayland */
}

static my_ret_t wl_win_show(my_pal_window_t* win) {
  (void)win;
  return MY_RET_OK; /* mapped on first commit */
}

static my_ret_t wl_win_get_size(my_pal_window_t* win, int32_t* w, int32_t* h) {
  wl_window_t* ww = (wl_window_t*)win;
  if (w != NULL) {
    *w = ww->w;
  }
  if (h != NULL) {
    *h = ww->h;
  }
  return MY_RET_OK;
}

static my_lcd_t* wl_win_get_lcd(my_pal_window_t* win) {
  return ((wl_window_t*)win)->lcd;
}

static void wl_win_destroy(my_pal_window_t* win) {
  wl_window_t* w = (wl_window_t*)win;
  wl_pal_t* p;
  size_t i, n;
  if (w == NULL) {
    return;
  }
  p = w->pal;
  n = my_darray_size(p->windows);
  for (i = 0; i < n; i++) {
    if (my_darray_get(p->windows, i) == w) {
      my_darray_remove_at(p->windows, i);
      break;
    }
  }
  if (w->gl != NULL) {
    my_pal_gl_destroy(w->gl); /* before wl_surface_destroy */
    w->gl = NULL;
  }
  if (w->frame_cb != NULL) {
    wl_callback_destroy(w->frame_cb);
  }
  if (w->wlbuf != NULL) {
    wl_buffer_destroy(w->wlbuf);
  }
  if (w->pixels != NULL) {
    munmap(w->pixels, w->shm_size);
  }
  if (w->shm_fd >= 0) {
    close(w->shm_fd);
  }
  my_lcd_destroy(w->lcd);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xsurface);
  wl_surface_destroy(w->surface);
  my_mem_free(w->allocator, w);
}

/* ---------------- GL mount (M10c): wl_egl_window + EGL window surface ---- */

#if defined(MYUI_PAL_GL_EGL)

typedef struct wl_gl_t {
  my_pal_gl_t base;
  wl_window_t* win; /**< borrowed (the window owns this handle) */
  EGLContext ctx;
  EGLSurface surf;
} wl_gl_t;

/** @brief Lazy one-time EGL display/config init (shared by all windows;
 * never eglTerminate'd -- the EGL display outlives individual windows
 * and is reclaimed at process exit). Config negotiation (M11c): prefer
 * EGL_SAMPLES=4 (MSAA), fall back to a plain config when unavailable. */
static bool wl_egl_init(wl_pal_t* p) {
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
  p->egl_dpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, p->display,
                                     NULL);
  if (p->egl_dpy == EGL_NO_DISPLAY) {
    p->egl_dpy = eglGetDisplay((EGLNativeDisplayType)p->display);
  }
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

static my_ret_t wl_gl_make_current(my_pal_gl_t* gl) {
  wl_gl_t* g = (wl_gl_t*)gl;
  return eglMakeCurrent(g->win->pal->egl_dpy, g->surf, g->surf, g->ctx)
             ? MY_RET_OK
             : MY_RET_FAIL;
}

static my_ret_t wl_gl_swap(my_pal_gl_t* gl) {
  wl_gl_t* g = (wl_gl_t*)gl;
  /* eglSwapBuffers attaches+commits; mesa throttles to the compositor's
   * frame callbacks at swap interval 1 (vsync semantics preserved) */
  return eglSwapBuffers(g->win->pal->egl_dpy, g->surf) ? MY_RET_OK
                                                       : MY_RET_FAIL;
}

static my_ret_t wl_gl_get_size(my_pal_gl_t* gl, int32_t* w, int32_t* h) {
  wl_gl_t* g = (wl_gl_t*)gl;
  if (w != NULL) {
    *w = g->win->w * g->win->pal->output_scale; /* drawable = physical */
  }
  if (h != NULL) {
    *h = g->win->h * g->win->pal->output_scale;
  }
  return MY_RET_OK;
}

static bool wl_gl_has_multisample(my_pal_gl_t* gl) {
  return ((wl_gl_t*)gl)->win->pal->egl_msaa;
}

static void wl_gl_destroy(my_pal_gl_t* gl) {
  wl_gl_t* g = (wl_gl_t*)gl;
  if (g != NULL) {
    EGLDisplay dpy = g->win->pal->egl_dpy;
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, g->surf);
    eglDestroyContext(dpy, g->ctx);
    wl_egl_window_destroy(g->win->egl_win);
    g->win->egl_win = NULL;
    g->win->gl = NULL; /* the window forgets it (double-destroy safe) */
    my_mem_free(g->win->allocator, g);
  }
}

static const my_pal_gl_vtable_t s_wl_gl_vtable = {
    wl_gl_make_current, wl_gl_swap, wl_gl_get_size,
    wl_gl_has_multisample, wl_gl_destroy};

static my_pal_gl_t* wl_win_gl_enable(my_pal_window_t* win) {
  wl_window_t* w = (wl_window_t*)win;
  wl_pal_t* p = w->pal;
  wl_gl_t* g;
  EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  if (w->gl != NULL) {
    return w->gl;
  }
  if (!wl_egl_init(p)) {
    return NULL;
  }
  g = (wl_gl_t*)my_mem_calloc(w->allocator, 1, sizeof(wl_gl_t));
  if (g == NULL) {
    return NULL;
  }
  g->base.vtable = &s_wl_gl_vtable;
  g->win = w;
  w->egl_win = wl_egl_window_create(w->surface, w->w * p->output_scale,
                                    w->h * p->output_scale);
  if (w->egl_win == NULL) {
    my_mem_free(w->allocator, g);
    return NULL;
  }
  g->ctx = eglCreateContext(p->egl_dpy, p->egl_cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g->ctx == EGL_NO_CONTEXT) {
    wl_egl_window_destroy(w->egl_win);
    w->egl_win = NULL;
    my_mem_free(w->allocator, g);
    return NULL;
  }
  g->surf = eglCreateWindowSurface(p->egl_dpy, p->egl_cfg,
                                   (EGLNativeWindowType)w->egl_win, NULL);
  if (g->surf == EGL_NO_SURFACE) {
    eglDestroyContext(p->egl_dpy, g->ctx);
    wl_egl_window_destroy(w->egl_win);
    w->egl_win = NULL;
    my_mem_free(w->allocator, g);
    return NULL;
  }
  w->gl = (my_pal_gl_t*)g;
  if (wl_gl_make_current(w->gl) == MY_RET_OK) {
    eglSwapInterval(p->egl_dpy, 1); /* vsync */
    /* MSAA (M11c) is surface-driven on ES2 (no core toggle); the config
     * already carries EGL_SAMPLES=4 when egl_msaa is true */
  }
  return w->gl;
}

#else /* !MYUI_PAL_GL_EGL */

static my_pal_gl_t* wl_win_gl_enable(my_pal_window_t* win) {
  (void)win;
  return NULL; /* built without EGL/wayland-egl */
}

#endif /* MYUI_PAL_GL_EGL */

static const my_pal_window_vtable_t s_wl_window_vtable = {
    wl_win_set_title, wl_win_resize,  wl_win_show,
    wl_win_get_size,  wl_win_get_lcd, wl_win_destroy,
    wl_win_gl_enable};

static my_pal_window_t* wl_window_create(my_pal_t* pal, int32_t w, int32_t h,
                                         const char* title) {
  wl_pal_t* p = (wl_pal_t*)pal;
  wl_window_t* win;
  wl_lcd_t* xl;
  if (w <= 0 || h <= 0 || p->compositor == NULL || p->wm_base == NULL) {
    return NULL;
  }
  win = (wl_window_t*)my_mem_calloc(p->allocator, 1, sizeof(wl_window_t));
  if (win == NULL) {
    return NULL;
  }
  win->base.vtable = &s_wl_window_vtable;
  win->pal = p;
  win->allocator = p->allocator;
  win->w = w;
  win->h = h;
  win->shm_fd = -1;

  win->surface = wl_compositor_create_surface(p->compositor);
  win->xsurface = xdg_wm_base_get_xdg_surface(p->wm_base, win->surface);
  xdg_surface_add_listener(win->xsurface, &XSURFACE_LISTENER, win);
  win->toplevel = xdg_surface_get_toplevel(win->xsurface);
  xdg_toplevel_add_listener(win->toplevel, &TOPLEVEL_LISTENER, win);
  if (title != NULL) {
    xdg_toplevel_set_title(win->toplevel, title);
  }
  wl_surface_commit(win->surface); /* initial commit: wait for configure */
  wl_surface_set_buffer_scale(win->surface, p->output_scale); /* M12c */

  if (!wl_buffer_create(win)) {
    wl_win_destroy((my_pal_window_t*)win);
    return NULL;
  }
  wl_buffer_add_listener(win->wlbuf, &BUFFER_LISTENER, win);

  xl = (wl_lcd_t*)my_mem_calloc(p->allocator, 1, sizeof(wl_lcd_t));
  if (xl == NULL) {
    wl_win_destroy((my_pal_window_t*)win);
    return NULL;
  }
  xl->base.vtable = &s_wl_lcd_vtable;
  xl->mem = my_lcd_mem_create_from_buffer(
      p->allocator, (uint32_t)(w * p->output_scale),
      (uint32_t)(h * p->output_scale), MY_PIXEL_FORMAT_BGRA8888, win->pixels,
      (uint32_t)(w * p->output_scale) * 4u);
  xl->win = win;
  win->lcd = (my_lcd_t*)xl;
  if (xl->mem == NULL) {
    wl_win_destroy((my_pal_window_t*)win);
    return NULL;
  }

  my_darray_push(p->windows, win);
  wl_display_flush(p->display);
  return (my_pal_window_t*)win;
}

/* ---------------- main loop ---------------- */

typedef struct wl_loop_t {
  my_pal_main_loop_t base;
  wl_pal_t* pal;
  const my_allocator_t* allocator;
  my_timer_manager_t* timers;
  my_darray_t* posted;
  bool quit;
} wl_loop_t;

static uint64_t wl_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint64_t wl_timer_now(void* ctx) {
  (void)ctx;
  return wl_now_ms();
}

static my_ret_t wl_loop_post_event(my_pal_main_loop_t* loop,
                                   const my_event_t* event) {
  wl_loop_t* l = (wl_loop_t*)loop;
  my_event_t* copy;
  if (event == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  copy = (my_event_t*)my_mem_alloc(l->allocator, sizeof(my_event_t));
  if (copy == NULL) {
    return MY_RET_OOM;
  }
  *copy = *event;
  if (my_darray_push(l->posted, copy) != MY_RET_OK) {
    my_mem_free(l->allocator, copy);
    return MY_RET_OOM;
  }
  return MY_RET_OK;
}

static my_ret_t wl_loop_run(my_pal_main_loop_t* loop) {
  wl_loop_t* l = (wl_loop_t*)loop;
  wl_pal_t* p = l->pal;
  int fd = wl_display_get_fd(p->display);

  l->quit = false;
  while (!l->quit) {
    struct pollfd pfd;
    uint32_t due;

    while (wl_display_dispatch_pending(p->display) > 0) {
      /* dispatched */
    }
    while (my_darray_size(l->posted) > 0) {
      my_event_t* e = (my_event_t*)my_darray_get(l->posted, 0);
      my_darray_remove_at(l->posted, 0);
      dispatch_event(p, NULL, e);
      my_mem_free(l->allocator, e);
    }
    my_timer_manager_fire(l->timers);
    if (l->quit) {
      break;
    }
    wl_display_flush(p->display);

    due = my_timer_manager_due_in_ms(l->timers);
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, due == UINT32_MAX ? 16 : (int)due) > 0 &&
        (pfd.revents & POLLIN) != 0) {
      wl_display_dispatch(p->display);
    }
  }
  return MY_RET_OK;
}

static my_ret_t wl_loop_quit(my_pal_main_loop_t* loop) {
  ((wl_loop_t*)loop)->quit = true;
  return MY_RET_OK;
}

static uint32_t wl_loop_add_timer(my_pal_main_loop_t* loop,
                                  my_timer_callback_t callback, void* ctx,
                                  uint32_t interval_ms) {
  return my_timer_add(((wl_loop_t*)loop)->timers, callback, ctx, interval_ms);
}

static my_ret_t wl_loop_remove_timer(my_pal_main_loop_t* loop, uint32_t id) {
  return my_timer_remove(((wl_loop_t*)loop)->timers, id);
}

static void wl_loop_destroy(my_pal_main_loop_t* loop) {
  wl_loop_t* l = (wl_loop_t*)loop;
  if (l != NULL) {
    while (my_darray_size(l->posted) > 0) {
      my_event_t* e = (my_event_t*)my_darray_get(l->posted, 0);
      my_darray_remove_at(l->posted, 0);
      my_mem_free(l->allocator, e);
    }
    my_darray_destroy(l->posted);
    my_timer_manager_destroy(l->timers);
    my_mem_free(l->allocator, l);
  }
}

static const my_pal_main_loop_vtable_t s_wl_loop_vtable = {
    wl_loop_run,       wl_loop_quit,        wl_loop_post_event,
    wl_loop_add_timer, wl_loop_remove_timer, wl_loop_destroy};

static my_pal_main_loop_t* wl_main_loop_create(my_pal_t* pal) {
  wl_pal_t* p = (wl_pal_t*)pal;
  wl_loop_t* l = (wl_loop_t*)my_mem_calloc(p->allocator, 1, sizeof(wl_loop_t));
  if (l == NULL) {
    return NULL;
  }
  l->base.vtable = &s_wl_loop_vtable;
  l->pal = p;
  l->allocator = p->allocator;
  l->posted = my_darray_create(p->allocator, 0);
  l->timers = my_timer_manager_create(p->allocator, wl_timer_now, NULL);
  if (l->posted == NULL || l->timers == NULL) {
    wl_loop_destroy((my_pal_main_loop_t*)l);
    return NULL;
  }
  return (my_pal_main_loop_t*)l;
}

/* ---------------- platform vtable ---------------- */

static uint64_t wl_time_now_ms(my_pal_t* pal) {
  (void)pal;
  return wl_now_ms();
}

static my_ret_t wl_set_event_handler(my_pal_t* pal,
                                     my_pal_event_handler_t handler,
                                     void* ctx) {
  wl_pal_t* p = (wl_pal_t*)pal;
  p->handler = handler;
  p->handler_ctx = ctx;
  return MY_RET_OK;
}

static my_ret_t wl_clipboard_set(my_pal_t* pal, const char* text) {
  wl_pal_t* p = (wl_pal_t*)pal;
  size_t len = text != NULL ? strlen(text) : 0;
  char* copy = (char*)my_mem_alloc(p->allocator, len + 1);
  if (copy == NULL) {
    return MY_RET_OOM;
  }
  memcpy(copy, text != NULL ? text : "", len + 1);
  my_mem_free(p->allocator, p->clipboard);
  p->clipboard = copy;
  /* wl_data_device (M12b): offer the new clipboard as a data source.
   * set_selection needs a recent keyboard-enter serial (compositor
   * focus policy); without one the source is prepared but likely does
   * not become the active selection -- the in-memory cache still
   * covers the in-app roundtrip either way. */
  if (text != NULL && wl_data_device_ready(p)) {
    struct wl_data_source* src =
        wl_data_device_manager_create_data_source(p->data_mgr);
    if (src != NULL) {
      if (p->data_source != NULL) {
        wl_data_source_destroy(p->data_source);
      }
      p->data_source = src;
      wl_data_source_offer(src, "text/plain;charset=utf-8");
      wl_data_source_offer(src, "text/plain");
      wl_data_source_add_listener(src, &DS_LISTENER, p);
      wl_data_device_set_selection(p->data_device, src,
                                   p->kb_enter_serial);
      wl_display_flush(p->display);
    }
  }
  return MY_RET_OK;
}

/** @brief Receive the current selection offer into buf (sync pump on
 * the display fd + the transfer pipe, ~2s timeout). */
static my_ret_t wl_clipboard_receive(wl_pal_t* p, char* buf, size_t size) {
  int fds[2] = {-1, -1};
  uint64_t deadline = wl_now_ms() + 2000;
  size_t total = 0;
  bool eof = false;
  if (p->selection_offer == NULL || pipe(fds) != 0) {
    return MY_RET_NOT_FOUND;
  }
  wl_data_offer_receive(p->selection_offer, "text/plain;charset=utf-8",
                        fds[1]);
  close(fds[1]);
  wl_display_flush(p->display);
  while (!eof && total < size - 1 && wl_now_ms() < deadline) {
    struct pollfd pf[2];
    pf[0].fd = wl_display_get_fd(p->display);
    pf[0].events = POLLIN;
    pf[0].revents = 0;
    pf[1].fd = fds[0];
    pf[1].events = POLLIN;
    pf[1].revents = 0;
    wl_display_dispatch_pending(p->display);
    if (poll(pf, 2, 50) <= 0) {
      continue;
    }
    if (pf[0].revents != 0) {
      wl_display_dispatch(p->display);
    }
    if ((pf[1].revents & (POLLIN | POLLHUP)) != 0) {
      ssize_t n = read(fds[0], buf + total, size - 1 - total);
      if (n > 0) {
        total += (size_t)n;
      } else {
        eof = true;
      }
    }
  }
  close(fds[0]);
  buf[total] = '\0';
  return total > 0 ? MY_RET_OK : MY_RET_NOT_FOUND;
}

static my_ret_t wl_clipboard_get(my_pal_t* pal, char* buf, size_t size) {
  wl_pal_t* p = (wl_pal_t*)pal;
  if (buf == NULL || size == 0) {
    return MY_RET_INVALID_PARAMS;
  }
  /* external selection first (M12b), then the in-app memory cache */
  if (wl_data_device_ready(p)) {
    wl_display_roundtrip(p->display); /* pick up the latest selection */
    if (wl_clipboard_receive(p, buf, size) == MY_RET_OK) {
      return MY_RET_OK;
    }
  }
  if (p->clipboard == NULL) {
    return MY_RET_NOT_FOUND;
  }
  snprintf(buf, size, "%s", p->clipboard);
  return MY_RET_OK;
}

static float wl_get_scale(my_pal_t* pal) {
  return (float)((wl_pal_t*)pal)->output_scale;
}

static void wl_pal_destroy(my_pal_t* pal) {
  wl_pal_t* p = (wl_pal_t*)pal;
  if (p == NULL) {
    return;
  }
  my_mem_free(p->allocator, p->clipboard);
  my_darray_destroy(p->windows);
  if (p->selection_offer != NULL) {
    wl_data_offer_destroy(p->selection_offer);
  }
  if (p->data_source != NULL) {
    wl_data_source_destroy(p->data_source);
  }
  if (p->data_device != NULL) {
    wl_data_device_destroy(p->data_device);
  }
  if (p->data_mgr != NULL) {
    wl_data_device_manager_destroy(p->data_mgr);
  }
  if (p->output != NULL) {
    wl_output_destroy(p->output);
  }
  if (p->xkb_state != NULL) {
    xkb_state_unref(p->xkb_state);
  }
  if (p->xkb_keymap != NULL) {
    xkb_keymap_unref(p->xkb_keymap);
  }
  if (p->xkb_ctx != NULL) {
    xkb_context_unref(p->xkb_ctx);
  }
  if (p->pointer != NULL) {
    wl_pointer_destroy(p->pointer);
  }
  if (p->keyboard != NULL) {
    wl_keyboard_destroy(p->keyboard);
  }
  if (p->seat != NULL) {
    wl_seat_destroy(p->seat);
  }
  if (p->wm_base != NULL) {
    xdg_wm_base_destroy(p->wm_base);
  }
  if (p->shm != NULL) {
    wl_shm_destroy(p->shm);
  }
  if (p->compositor != NULL) {
    wl_compositor_destroy(p->compositor);
  }
  if (p->registry != NULL) {
    wl_registry_destroy(p->registry);
  }
  wl_display_disconnect(p->display);
  my_mem_free(p->allocator, p);
}

static const my_pal_vtable_t s_wl_pal_vtable = {wl_window_create,
                                                wl_main_loop_create,
                                                wl_time_now_ms,
                                                wl_set_event_handler,
                                                wl_clipboard_set,
                                                wl_clipboard_get,
                                                wl_get_scale,
                                                wl_pal_destroy};

my_pal_t* my_pal_wayland_create(const my_allocator_t* allocator) {
  wl_pal_t* p;
  struct wl_display* display = wl_display_connect(NULL);
  if (display == NULL) {
    return NULL;
  }
  p = (wl_pal_t*)my_mem_calloc(allocator, 1, sizeof(wl_pal_t));
  if (p == NULL) {
    wl_display_disconnect(display);
    return NULL;
  }
  p->base.vtable = &s_wl_pal_vtable;
  p->allocator = allocator;
  p->display = display;
  p->output_scale = 1;
  p->windows = my_darray_create(allocator, 0);
  p->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (p->windows == NULL || p->xkb_ctx == NULL) {
    if (p->xkb_ctx != NULL) {
      xkb_context_unref(p->xkb_ctx);
    }
    my_darray_destroy(p->windows);
    wl_display_disconnect(display);
    my_mem_free(allocator, p);
    return NULL;
  }
  p->registry = wl_display_get_registry(display);
  wl_registry_add_listener(p->registry, &REGISTRY_LISTENER, p);
  wl_display_roundtrip(display); /* bind globals */
  if (p->compositor == NULL || p->shm == NULL || p->wm_base == NULL) {
    wl_pal_destroy((my_pal_t*)p);
    return NULL;
  }
  return (my_pal_t*)p;
}
