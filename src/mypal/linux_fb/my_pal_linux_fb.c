/**
 * @file my_pal_linux_fb.c
 * @brief Linux framebuffer PAL port.
 */
/* POSIX (clock_gettime) under strict -std=c99 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mypal/linux_fb/my_pal_linux_fb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "myc/my_darray.h"
#include "myr/my_lcd_mem.h"

/* fb ioctls / structs (layout-compatible subset of <linux/fb.h>) */
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

typedef struct fb_bitfield_t {
  uint32_t offset;
  uint32_t length;
  uint32_t msb_right;
} fb_bitfield_t;

/** @brief Matches the head of struct fb_var_screeninfo (kernel: 160 B). */
typedef struct fb_var_screeninfo_t {
  uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
  uint32_t bits_per_pixel, grayscale;
  fb_bitfield_t red, green, blue, transp;
  uint32_t pad[24];
} fb_var_screeninfo_t;

/** @brief Matches struct fb_fix_screeninfo up to line_length (72 B). */
typedef struct fb_fix_screeninfo_t {
  char id[16];
  unsigned long smem_start;
  uint32_t smem_len;
  uint32_t type, type_aux, visual;
  uint16_t xpanstep, ypanstep, ywrapstep, pad_align;
  uint32_t line_length;
  uint32_t pad[16];
} fb_fix_screeninfo_t;

/* evdev subset (<linux/input.h>) */
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define REL_X 0x00
#define REL_Y 0x01
#define ABS_X 0x00
#define ABS_Y 0x01
#define BTN_LEFT 0x110
#define BTN_TOUCH 0x14a
#define KEY_ESC 1
#define KEY_ENTER 28
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_UP 103
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_DOWN 108

/** @brief Matches struct input_event on 64-bit Linux. */
typedef struct fb_input_event_t {
  long sec;
  long usec;
  uint16_t type;
  uint16_t code;
  int32_t value;
} fb_input_event_t;

/* ---------------- platform ---------------- */

typedef struct fb_pal_t {
  my_pal_t base;
  const my_allocator_t* allocator;
  my_osal_t osal;
  int fb_fd;
  int input_fd;
  uint8_t* fb_map;
  size_t fb_size;
  uint32_t w, h, stride;
  my_pixel_format_t format;
  my_pal_event_handler_t handler;
  void* handler_ctx;
  char* clipboard;
  float scale; /**< MYUI_SCALE env or 1.0 (M12c) */
} fb_pal_t;

static uint64_t fb_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ---------------- window (single fullscreen window) ---------------- */

typedef struct fb_window_t {
  my_pal_window_t base;
  fb_pal_t* pal;
  const my_allocator_t* allocator;
  my_lcd_t* lcd; /**< over the mmap'd fb (owned wrapper, not the buffer) */
} fb_window_t;

static my_ret_t fb_win_set_title(my_pal_window_t* win, const char* title) {
  (void)win;
  (void)title; /* no window manager on raw fb */
  return MY_RET_OK;
}

static my_ret_t fb_win_resize(my_pal_window_t* win, int32_t w, int32_t h) {
  (void)win;
  (void)w;
  (void)h;
  return MY_RET_NOT_SUPPORTED; /* fb size is fixed by hardware */
}

static my_ret_t fb_win_show(my_pal_window_t* win) {
  (void)win;
  return MY_RET_OK;
}

static my_ret_t fb_win_get_size(my_pal_window_t* win, int32_t* w, int32_t* h) {
  fb_window_t* fw = (fb_window_t*)win;
  if (w != NULL) {
    *w = (int32_t)fw->pal->w;
  }
  if (h != NULL) {
    *h = (int32_t)fw->pal->h;
  }
  return MY_RET_OK;
}

static my_lcd_t* fb_win_get_lcd(my_pal_window_t* win) {
  return ((fb_window_t*)win)->lcd;
}

static void fb_win_destroy(my_pal_window_t* win) {
  fb_window_t* fw = (fb_window_t*)win;
  if (fw != NULL) {
    my_lcd_destroy(fw->lcd);
    my_mem_free(fw->allocator, fw);
  }
}

static my_pal_gl_t* fb_win_gl_enable(my_pal_window_t* win) {
  (void)win;
  return NULL; /* linux_fb: no GL support */
}

static const my_pal_window_vtable_t s_fb_window_vtable = {
    fb_win_set_title, fb_win_resize,  fb_win_show,
    fb_win_get_size,  fb_win_get_lcd, fb_win_destroy,
    fb_win_gl_enable};

static my_pal_window_t* fb_window_create(my_pal_t* pal, int32_t w, int32_t h,
                                         const char* title) {
  fb_pal_t* p = (fb_pal_t*)pal;
  fb_window_t* win;
  (void)w;
  (void)h;
  (void)title; /* fb is a single fullscreen surface */
  win = (fb_window_t*)my_mem_calloc(p->allocator, 1, sizeof(fb_window_t));
  if (win == NULL) {
    return NULL;
  }
  win->base.vtable = &s_fb_window_vtable;
  win->pal = p;
  win->allocator = p->allocator;
  win->lcd = my_lcd_mem_create_from_buffer(p->allocator, p->w, p->h, p->format,
                                           p->fb_map, p->stride);
  if (win->lcd == NULL) {
    my_mem_free(p->allocator, win);
    return NULL;
  }
  return (my_pal_window_t*)win;
}

/* ---------------- input translation ---------------- */

static uint32_t fb_key_from_code(uint16_t code) {
  switch (code) {
    case KEY_ENTER:
      return MY_KEY_RETURN;
    case KEY_ESC:
      return MY_KEY_ESCAPE;
    case KEY_BACKSPACE:
      return MY_KEY_BACKSPACE;
    case KEY_TAB:
      return MY_KEY_TAB;
    case KEY_UP:
      return MY_KEY_UP;
    case KEY_DOWN:
      return MY_KEY_DOWN;
    case KEY_LEFT:
      return MY_KEY_LEFT;
    case KEY_RIGHT:
      return MY_KEY_RIGHT;
    default:
      return MY_KEY_UNKNOWN;
  }
}

static void fb_dispatch_input(fb_pal_t* p, const fb_input_event_t* ev,
                              int32_t* cur_x, int32_t* cur_y) {
  my_event_t e;
  if (p->handler == NULL) {
    return;
  }
  e = my_event_init(MY_EVENT_NONE);
  e.time_ms = fb_now_ms();
  switch (ev->type) {
    case EV_ABS:
      if (ev->code == ABS_X) {
        *cur_x = ev->value;
      } else if (ev->code == ABS_Y) {
        *cur_y = ev->value;
      } else {
        return;
      }
      e.type = MY_EVENT_POINTER_MOVE;
      e.u.pointer.x = *cur_x;
      e.u.pointer.y = *cur_y;
      break;
    case EV_REL:
      if (ev->code == 0x08) { /* REL_WHEEL */
        e.type = MY_EVENT_POINTER_WHEEL;
        e.u.pointer.x = *cur_x;
        e.u.pointer.y = *cur_y;
        e.u.pointer.delta = ev->value;
        break;
      }
      if (ev->code == REL_X) {
        *cur_x += ev->value;
      } else if (ev->code == REL_Y) {
        *cur_y += ev->value;
      } else {
        return;
      }
      e.type = MY_EVENT_POINTER_MOVE;
      e.u.pointer.x = *cur_x;
      e.u.pointer.y = *cur_y;
      break;
    case EV_KEY:
      if (ev->code == BTN_LEFT || ev->code == BTN_TOUCH) {
        e.type = ev->value != 0 ? MY_EVENT_POINTER_DOWN : MY_EVENT_POINTER_UP;
        e.u.pointer.x = *cur_x;
        e.u.pointer.y = *cur_y;
        e.u.pointer.button = 1;
      } else {
        uint32_t key = fb_key_from_code(ev->code);
        if (key == MY_KEY_UNKNOWN) {
          return;
        }
        e.type = ev->value != 0 ? MY_EVENT_KEY_DOWN : MY_EVENT_KEY_UP;
        e.u.key.key = key;
      }
      break;
    default:
      return;
  }
  /* window-less: the single fb window is implied */
  p->handler(p->handler_ctx, NULL, &e);
}

/* ---------------- main loop ---------------- */

typedef struct fb_loop_t {
  my_pal_main_loop_t base;
  fb_pal_t* pal;
  const my_allocator_t* allocator;
  my_timer_manager_t* timers;
  my_darray_t* posted; /**< my_event_t* queue */
  bool quit;
} fb_loop_t;

static uint64_t fb_timer_now(void* ctx) {
  (void)ctx;
  return fb_now_ms();
}

static my_ret_t fb_loop_run(my_pal_main_loop_t* loop) {
  fb_loop_t* l = (fb_loop_t*)loop;
  fb_pal_t* p = l->pal;
  int32_t cur_x = 0, cur_y = 0;

  l->quit = false;
  while (!l->quit) {
    my_osal_pollfd_t pfd;
    uint32_t due;
    int ready;

    /* posted events first */
    while (my_darray_size(l->posted) > 0) {
      my_event_t* e = (my_event_t*)my_darray_get(l->posted, 0);
      my_darray_remove_at(l->posted, 0);
      if (p->handler != NULL) {
        p->handler(p->handler_ctx, NULL, e);
      }
      my_mem_free(l->allocator, e);
    }
    my_timer_manager_fire(l->timers);
    if (l->quit) {
      break;
    }

    due = my_timer_manager_due_in_ms(l->timers);
    pfd.fd = p->input_fd;
    pfd.events = MY_OSAL_POLLIN;
    pfd.revents = 0;
    ready = p->osal.poll(p->osal.ctx, &pfd, 1,
                         due == UINT32_MAX ? 16 : (int)due);
    if (ready > 0 && (pfd.revents & MY_OSAL_POLLIN) != 0) {
      fb_input_event_t evs[8];
      long n = p->osal.read(p->osal.ctx, p->input_fd, evs, sizeof(evs));
      long i, count = n > 0 ? n / (long)sizeof(fb_input_event_t) : 0;
      for (i = 0; i < count; i++) {
        fb_dispatch_input(p, &evs[i], &cur_x, &cur_y);
      }
    }
  }
  return MY_RET_OK;
}

static my_ret_t fb_loop_quit(my_pal_main_loop_t* loop) {
  ((fb_loop_t*)loop)->quit = true;
  return MY_RET_OK;
}

static my_ret_t fb_loop_post_event(my_pal_main_loop_t* loop,
                                   const my_event_t* event) {
  fb_loop_t* l = (fb_loop_t*)loop;
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

static uint32_t fb_loop_add_timer(my_pal_main_loop_t* loop,
                                  my_timer_callback_t callback, void* ctx,
                                  uint32_t interval_ms) {
  return my_timer_add(((fb_loop_t*)loop)->timers, callback, ctx, interval_ms);
}

static my_ret_t fb_loop_remove_timer(my_pal_main_loop_t* loop, uint32_t id) {
  return my_timer_remove(((fb_loop_t*)loop)->timers, id);
}

static void fb_loop_destroy(my_pal_main_loop_t* loop) {
  fb_loop_t* l = (fb_loop_t*)loop;
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

static const my_pal_main_loop_vtable_t s_fb_loop_vtable = {
    fb_loop_run,        fb_loop_quit,        fb_loop_post_event,
    fb_loop_add_timer,  fb_loop_remove_timer, fb_loop_destroy};

static my_pal_main_loop_t* fb_main_loop_create(my_pal_t* pal) {
  fb_pal_t* p = (fb_pal_t*)pal;
  fb_loop_t* l = (fb_loop_t*)my_mem_calloc(p->allocator, 1, sizeof(fb_loop_t));
  if (l == NULL) {
    return NULL;
  }
  l->base.vtable = &s_fb_loop_vtable;
  l->pal = p;
  l->allocator = p->allocator;
  l->posted = my_darray_create(p->allocator, 0);
  l->timers = my_timer_manager_create(p->allocator, fb_timer_now, NULL);
  if (l->posted == NULL || l->timers == NULL) {
    fb_loop_destroy((my_pal_main_loop_t*)l);
    return NULL;
  }
  return (my_pal_main_loop_t*)l;
}

/* ---------------- platform vtable ---------------- */

static uint64_t fb_time_now_ms(my_pal_t* pal) {
  (void)pal;
  return fb_now_ms();
}

static my_ret_t fb_set_event_handler(my_pal_t* pal,
                                     my_pal_event_handler_t handler, void* ctx) {
  fb_pal_t* p = (fb_pal_t*)pal;
  p->handler = handler;
  p->handler_ctx = ctx;
  return MY_RET_OK;
}

static my_ret_t fb_clipboard_set(my_pal_t* pal, const char* text) {
  fb_pal_t* p = (fb_pal_t*)pal;
  size_t len = text != NULL ? strlen(text) : 0;
  char* copy = (char*)my_mem_alloc(p->allocator, len + 1);
  if (copy == NULL) {
    return MY_RET_OOM;
  }
  memcpy(copy, text != NULL ? text : "", len + 1);
  my_mem_free(p->allocator, p->clipboard);
  p->clipboard = copy;
  return MY_RET_OK;
}

static my_ret_t fb_clipboard_get(my_pal_t* pal, char* buf, size_t size) {
  fb_pal_t* p = (fb_pal_t*)pal;
  if (buf == NULL || size == 0) {
    return MY_RET_INVALID_PARAMS;
  }
  if (p->clipboard == NULL) {
    return MY_RET_NOT_FOUND;
  }
  snprintf(buf, size, "%s", p->clipboard);
  return MY_RET_OK;
}

static void fb_pal_destroy(my_pal_t* pal) {
  fb_pal_t* p = (fb_pal_t*)pal;
  if (p == NULL) {
    return;
  }
  my_mem_free(p->allocator, p->clipboard);
  if (p->fb_map != NULL) {
    p->osal.munmap(p->osal.ctx, p->fb_map, p->fb_size);
  }
  if (p->fb_fd >= 0) {
    p->osal.close(p->osal.ctx, p->fb_fd);
  }
  if (p->input_fd >= 0) {
    p->osal.close(p->osal.ctx, p->input_fd);
  }
  my_mem_free(p->allocator, p);
}

static float fb_get_scale(my_pal_t* pal) {
  return ((fb_pal_t*)pal)->scale;
}

static const my_pal_vtable_t s_fb_pal_vtable = {fb_window_create,
                                                fb_main_loop_create,
                                                fb_time_now_ms,
                                                fb_set_event_handler,
                                                fb_clipboard_set,
                                                fb_clipboard_get,
                                                fb_get_scale,
                                                fb_pal_destroy};

my_pal_t* my_pal_linux_fb_create(const my_allocator_t* allocator,
                                 const my_osal_t* osal, const char* fb_dev,
                                 const char* input_dev) {
  fb_pal_t* p;
  fb_var_screeninfo_t vinfo;
  fb_fix_screeninfo_t finfo;

  if (osal == NULL) {
    osal = my_osal_default();
  }
  p = (fb_pal_t*)my_mem_calloc(allocator, 1, sizeof(fb_pal_t));
  if (p == NULL) {
    return NULL;
  }
  p->base.vtable = &s_fb_pal_vtable;
  p->allocator = allocator;
  p->osal = *osal;
  p->fb_fd = -1;
  p->input_fd = -1;
  /* HiDPI override (M12c): MYUI_SCALE=float, default 1.0. The fb window
   * is the physical fullscreen surface; scale only drives vg scaling. */
  p->scale = 1.0f;
  {
    const char* env = getenv("MYUI_SCALE");
    if (env != NULL) {
      float v = (float)atof(env);
      if (v > 0.0f) {
        p->scale = v;
      }
    }
  }

  p->fb_fd = osal->open(osal->ctx, fb_dev != NULL ? fb_dev : "/dev/fb0", 2);
  if (p->fb_fd < 0) {
    my_mem_free(allocator, p);
    return NULL;
  }
  memset(&vinfo, 0, sizeof(vinfo));
  memset(&finfo, 0, sizeof(finfo));
  if (osal->ioctl(osal->ctx, p->fb_fd, FBIOGET_VSCREENINFO, &vinfo) != 0 ||
      osal->ioctl(osal->ctx, p->fb_fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
    fb_pal_destroy((my_pal_t*)p);
    return NULL;
  }
  p->w = vinfo.xres;
  p->h = vinfo.yres;
  p->stride = finfo.line_length;
  if (vinfo.bits_per_pixel == 32) {
    p->format = MY_PIXEL_FORMAT_BGRA8888;
  } else if (vinfo.bits_per_pixel == 16) {
    p->format = MY_PIXEL_FORMAT_RGB565;
  } else {
    fb_pal_destroy((my_pal_t*)p);
    return NULL;
  }
  if (p->stride == 0) {
    p->stride = p->w * (vinfo.bits_per_pixel / 8u);
  }
  p->fb_size = (size_t)p->stride * p->h;
  p->fb_map = (uint8_t*)osal->mmap(osal->ctx, p->fb_size, p->fb_fd);
  if (p->fb_map == NULL || p->fb_map == (uint8_t*)-1) {
    p->fb_map = NULL;
    fb_pal_destroy((my_pal_t*)p);
    return NULL;
  }
  p->input_fd =
      osal->open(osal->ctx, input_dev != NULL ? input_dev : "/dev/input/event0",
                 0);
  /* input device is optional: loop still works with fd -1 (poll fails) */
  return (my_pal_t*)p;
}
