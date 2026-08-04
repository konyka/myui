/**
 * @file my_pal_linux_fb_test.c
 * @brief linux_fb port tests against a scripted fake device (my_osal_t).
 */
#include "mypal/linux_fb/my_pal_linux_fb.h"

#include <string.h>

#include "mytest.h"

/* ---------------- fake device ---------------- */

#define FB_W 64
#define FB_H 48
#define FB_BPP 32

/* mirror of the port's private structs (layout-compatible) */
typedef struct fake_bitfield_t {
  uint32_t offset, length, msb_right;
} fake_bitfield_t;

typedef struct fake_vinfo_t {
  uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
  uint32_t bits_per_pixel, grayscale;
  fake_bitfield_t red, green, blue, transp;
  uint32_t pad[24];
} fake_vinfo_t;

typedef struct fake_finfo_t {
  char id[16];
  unsigned long smem_start;
  uint32_t smem_len;
  uint32_t type, type_aux, visual;
  uint16_t xpanstep, ypanstep, ywrapstep, pad_align;
  uint32_t line_length;
  uint32_t pad[16];
} fake_finfo_t;

typedef struct fake_input_event_t {
  long sec;
  long usec;
  uint16_t type;
  uint16_t code;
  int32_t value;
} fake_input_event_t;

typedef struct fake_dev_t {
  my_osal_t osal;
  const my_allocator_t* allocator;
  uint8_t* fb;
  size_t fb_size;
  fake_input_event_t events[16];
  int event_count;
  int event_pos;
  bool closed;
} fake_dev_t;

static int fake_open(void* ctx, const char* path, int flags) {
  (void)ctx;
  (void)path;
  (void)flags;
  return 3; /* any fd */
}

static int fake_close(void* ctx, int fd) {
  fake_dev_t* d = (fake_dev_t*)ctx;
  (void)fd;
  d->closed = true;
  return 0;
}

static int fake_ioctl(void* ctx, int fd, unsigned long request, void* arg) {
  fake_dev_t* d = (fake_dev_t*)ctx;
  (void)fd;
  if (request == 0x4600) { /* FBIOGET_VSCREENINFO */
    fake_vinfo_t* v = (fake_vinfo_t*)arg;
    memset(v, 0, sizeof(*v));
    v->xres = FB_W;
    v->yres = FB_H;
    v->bits_per_pixel = FB_BPP;
    v->red.offset = 16;
    v->red.length = 8;
    v->green.offset = 8;
    v->green.length = 8;
    v->blue.offset = 0;
    v->blue.length = 8;
    v->transp.offset = 24;
    v->transp.length = 8;
    return 0;
  }
  if (request == 0x4602) { /* FBIOGET_FSCREENINFO */
    fake_finfo_t* f = (fake_finfo_t*)arg;
    memset(f, 0, sizeof(*f));
    f->smem_len = (uint32_t)d->fb_size;
    f->line_length = FB_W * (FB_BPP / 8);
    return 0;
  }
  return -1;
}

static void* fake_mmap(void* ctx, size_t length, int fd) {
  fake_dev_t* d = (fake_dev_t*)ctx;
  (void)fd;
  (void)length;
  return d->fb;
}

static int fake_munmap(void* ctx, void* addr, size_t length) {
  fake_dev_t* d = (fake_dev_t*)ctx;
  (void)addr;
  (void)length;
  my_mem_free(d->allocator, d->fb);
  d->fb = NULL;
  return 0;
}

static int fake_poll(void* ctx, my_osal_pollfd_t* fds, int nfds,
                     int timeout_ms) {
  fake_dev_t* d = (fake_dev_t*)ctx;
  (void)nfds;
  (void)timeout_ms;
  if (d->event_pos < d->event_count) {
    fds[0].revents = MY_OSAL_POLLIN;
    return 1;
  }
  return 0;
}

static long fake_read(void* ctx, int fd, void* buf, size_t count) {
  fake_dev_t* d = (fake_dev_t*)ctx;
  size_t avail = (size_t)(d->event_count - d->event_pos) *
                 sizeof(fake_input_event_t);
  (void)fd;
  if (avail == 0) {
    return 0;
  }
  if (avail > count) {
    avail = count;
  }
  memcpy(buf, d->events + d->event_pos, avail);
  d->event_pos += (int)(avail / sizeof(fake_input_event_t));
  return (long)avail;
}

static void fake_dev_init(fake_dev_t* d, const my_allocator_t* allocator) {
  memset(d, 0, sizeof(*d));
  d->osal.open = fake_open;
  d->osal.close = fake_close;
  d->osal.ioctl = fake_ioctl;
  d->osal.mmap = fake_mmap;
  d->osal.munmap = fake_munmap;
  d->osal.poll = fake_poll;
  d->osal.read = fake_read;
  d->osal.ctx = d;
  d->allocator = allocator;
  d->fb_size = FB_W * (FB_BPP / 8) * FB_H;
  d->fb = (uint8_t*)my_mem_calloc(allocator, d->fb_size, 1);
}

static void fake_push_event(fake_dev_t* d, uint16_t type, uint16_t code,
                            int32_t value) {
  fake_input_event_t* e = &d->events[d->event_count++];
  e->type = type;
  e->code = code;
  e->value = value;
}

/* ---------------- event capture ---------------- */

typedef struct ev_log_t {
  int count;
  my_event_type_t last_type;
  int32_t last_x, last_y;
  uint32_t last_key;
  my_pal_main_loop_t* loop;
} ev_log_t;

static my_ret_t on_event(void* ctx, my_pal_window_t* win, const my_event_t* e) {
  ev_log_t* l = (ev_log_t*)ctx;
  (void)win;
  l->count++;
  l->last_type = e->type;
  if (e->type == MY_EVENT_POINTER_DOWN || e->type == MY_EVENT_POINTER_UP ||
      e->type == MY_EVENT_POINTER_MOVE) {
    l->last_x = e->u.pointer.x;
    l->last_y = e->u.pointer.y;
  }
  if (e->type == MY_EVENT_KEY_DOWN) {
    l->last_key = e->u.key.key;
  }
  if (e->type == MY_EVENT_QUIT && l->loop != NULL) {
    my_pal_main_loop_quit(l->loop);
  }
  if (e->type == MY_EVENT_KEY_DOWN && e->u.key.key == MY_KEY_RETURN &&
      l->loop != NULL) {
    my_pal_main_loop_quit(l->loop); /* scripted test exit */
  }
  return MY_RET_OK;
}

/* ---------------- tests ---------------- */

static void test_fb_window_pixels(void) {
  fake_dev_t dev;
  my_pal_t* pal;
  my_pal_window_t* win;
  my_lcd_t* lcd;
  const my_color_t RED = {255, 0, 0, 255};
  uint32_t off;

  fake_dev_init(&dev, NULL);
  pal = my_pal_linux_fb_create(NULL, &dev.osal, NULL, NULL);
  TEST_ASSERT_NOT_NULL(pal);

  win = my_pal_window_create(pal, FB_W, FB_H, NULL);
  TEST_ASSERT_NOT_NULL(win);
  lcd = my_pal_window_get_lcd(win);
  TEST_ASSERT_EQ_INT(my_lcd_get_format(lcd), MY_PIXEL_FORMAT_BGRA8888);

  my_lcd_fill_rect(lcd, &(my_rect_t){2, 3, 4, 4}, RED);
  off = (3u * FB_W + 2u) * 4u;
  TEST_ASSERT_EQ_INT(dev.fb[off + 2], 255); /* R at BGRA offset 2 */
  TEST_ASSERT_EQ_INT(dev.fb[off], 0);

  {
    int32_t w = 0, h = 0;
    my_pal_window_get_size(win, &w, &h);
    TEST_ASSERT_EQ_INT(w, FB_W);
    TEST_ASSERT_EQ_INT(h, FB_H);
  }

  my_pal_window_destroy(win);
  my_pal_destroy(pal);
  TEST_ASSERT(dev.closed);
}

static void test_input_events_dispatch(void) {
  fake_dev_t dev;
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  ev_log_t log;

  fake_dev_init(&dev, NULL);
  pal = my_pal_linux_fb_create(NULL, &dev.osal, NULL, NULL);
  loop = my_pal_main_loop_create(pal);
  memset(&log, 0, sizeof(log));
  log.loop = loop;
  my_pal_set_event_handler(pal, on_event, &log);

  /* touch at (30, 20), press, release, then ENTER key (quits via handler) */
  fake_push_event(&dev, 0x03, 0x00, 30); /* EV_ABS ABS_X */
  fake_push_event(&dev, 0x03, 0x01, 20); /* EV_ABS ABS_Y */
  fake_push_event(&dev, 0x01, 0x14a, 1); /* EV_KEY BTN_TOUCH down */
  fake_push_event(&dev, 0x01, 0x14a, 0); /* up */
  fake_push_event(&dev, 0x01, 28, 1);    /* KEY_ENTER down */

  my_pal_main_loop_run(loop);

  TEST_ASSERT(log.count >= 5);
  TEST_ASSERT_EQ_INT(log.last_key, MY_KEY_RETURN);
  /* the press carried the last touched coordinates */
  TEST_ASSERT_EQ_INT(log.last_x, 30);
  TEST_ASSERT_EQ_INT(log.last_y, 20);

  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_create_fails_gracefully(void) {
  fake_dev_t dev;
  my_pal_t* pal;
  fake_dev_init(&dev, NULL);
  my_mem_free(NULL, dev.fb);
  dev.fb = NULL; /* mmap will now return NULL */
  pal = my_pal_linux_fb_create(NULL, &dev.osal, NULL, NULL);
  TEST_ASSERT_NULL(pal);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  fake_dev_t dev;
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_pal_window_t* win;
  ev_log_t log;
  my_event_t quit_ev;

  fake_dev_init(&dev, dbg); /* fb buffer also from dbg allocator */
  pal = my_pal_linux_fb_create(dbg, &dev.osal, NULL, NULL);
  loop = my_pal_main_loop_create(pal);
  win = my_pal_window_create(pal, FB_W, FB_H, NULL);
  memset(&log, 0, sizeof(log));
  log.loop = loop;
  my_pal_set_event_handler(pal, on_event, &log);

  my_lcd_fill_rect(my_pal_window_get_lcd(win), &(my_rect_t){0, 0, 8, 8},
                   my_color_rgb(1, 2, 3));
  fake_push_event(&dev, 0x01, 0x14a, 1);
  quit_ev = my_event_init(MY_EVENT_QUIT);
  my_pal_main_loop_post_event(loop, &quit_ev);
  my_pal_main_loop_run(loop);

  my_pal_window_destroy(win);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_fb_window_pixels);
  MYTEST_RUN(test_input_events_dispatch);
  MYTEST_RUN(test_create_fails_gracefully);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
