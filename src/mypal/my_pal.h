/**
 * @file my_pal.h
 * @brief Platform abstraction layer — frozen vtables (M2).
 *
 * A PAL port provides exactly four things:
 *  1. windows (my_pal_window_t): title/resize/show/size + an my_lcd_t to
 *     draw into (drawing is the myr layer's business);
 *  2. a main loop (my_pal_main_loop_t): run/quit/post_event + timers;
 *  3. a monotonic clock (time_now_ms);
 *  4. the platform entry point my_pal_create() (port chosen at compile
 *     time: MYUI_PAL_X11 / MYUI_PAL_DUMMY, CMake option MYUI_PAL).
 *
 * Events (my_event_t) are delivered to the single application handler
 * registered with my_pal_set_event_handler(). Ports: dummy/ (headless,
 * tests) and x11/ (Linux desktop). See docs/porting.md to add a port.
 */
#ifndef MY_PAL_H
#define MY_PAL_H

#include "myc/my_error.h"
#include "myc/my_mem.h"
#include "mypal/my_event.h"
#include "mypal/my_timer.h"
#include "myr/my_lcd.h"

typedef struct my_pal_t my_pal_t;
typedef struct my_pal_window_t my_pal_window_t;
typedef struct my_pal_main_loop_t my_pal_main_loop_t;

/**
 * @brief Application event handler. window is the event source, or NULL
 * for window-less events (e.g. posted USER events). Return value is
 * reserved (currently ignored).
 */
typedef my_ret_t (*my_pal_event_handler_t)(void* ctx, my_pal_window_t* window,
                                           const my_event_t* event);

/* ---------------- window ---------------- */

/** @brief Window vtable. */
typedef struct my_pal_window_vtable_t {
  my_ret_t (*set_title)(my_pal_window_t* win, const char* title);
  my_ret_t (*resize)(my_pal_window_t* win, int32_t w, int32_t h);
  my_ret_t (*show)(my_pal_window_t* win);
  /** @brief Current client size; w/h may be NULL individually. */
  my_ret_t (*get_size)(my_pal_window_t* win, int32_t* w, int32_t* h);
  /**
   * @brief The lcd to draw this window's frame into (owned by the
   * window, do NOT destroy). Frame ends (my_lcd_end_frame) present it.
   */
  my_lcd_t* (*get_lcd)(my_pal_window_t* win);
  void (*destroy)(my_pal_window_t* win);
} my_pal_window_vtable_t;

/** @brief Window base "class". */
struct my_pal_window_t {
  const my_pal_window_vtable_t* vtable;
};

static inline my_ret_t my_pal_window_set_title(my_pal_window_t* win,
                                               const char* title) {
  return win->vtable->set_title(win, title);
}

static inline my_ret_t my_pal_window_resize(my_pal_window_t* win, int32_t w,
                                            int32_t h) {
  return win->vtable->resize(win, w, h);
}

static inline my_ret_t my_pal_window_show(my_pal_window_t* win) {
  return win->vtable->show(win);
}

static inline my_ret_t my_pal_window_get_size(my_pal_window_t* win, int32_t* w,
                                              int32_t* h) {
  return win->vtable->get_size(win, w, h);
}

static inline my_lcd_t* my_pal_window_get_lcd(my_pal_window_t* win) {
  return win->vtable->get_lcd(win);
}

static inline void my_pal_window_destroy(my_pal_window_t* win) {
  if (win != NULL) {
    win->vtable->destroy(win);
  }
}

/* ---------------- main loop ---------------- */

/** @brief Main loop vtable. */
typedef struct my_pal_main_loop_vtable_t {
  /** @brief Run until quit(); returns when the loop exits. */
  my_ret_t (*run)(my_pal_main_loop_t* loop);
  /** @brief Ask run() to return (safe from any thread). */
  my_ret_t (*quit)(my_pal_main_loop_t* loop);
  /**
   * @brief Enqueue a (copied) event for dispatch on the loop thread;
   * wakes the loop. Safe from any thread.
   */
  my_ret_t (*post_event)(my_pal_main_loop_t* loop, const my_event_t* event);
  /** @brief Add a timer driven by this loop (see my_timer.h). */
  uint32_t (*add_timer)(my_pal_main_loop_t* loop, my_timer_callback_t callback,
                        void* ctx, uint32_t interval_ms);
  my_ret_t (*remove_timer)(my_pal_main_loop_t* loop, uint32_t id);
  void (*destroy)(my_pal_main_loop_t* loop);
} my_pal_main_loop_vtable_t;

/** @brief Main loop base "class". */
struct my_pal_main_loop_t {
  const my_pal_main_loop_vtable_t* vtable;
};

static inline my_ret_t my_pal_main_loop_run(my_pal_main_loop_t* loop) {
  return loop->vtable->run(loop);
}

static inline my_ret_t my_pal_main_loop_quit(my_pal_main_loop_t* loop) {
  return loop->vtable->quit(loop);
}

static inline my_ret_t my_pal_main_loop_post_event(my_pal_main_loop_t* loop,
                                                   const my_event_t* event) {
  return loop->vtable->post_event(loop, event);
}

static inline uint32_t my_pal_main_loop_add_timer(my_pal_main_loop_t* loop,
                                                  my_timer_callback_t callback,
                                                  void* ctx,
                                                  uint32_t interval_ms) {
  return loop->vtable->add_timer(loop, callback, ctx, interval_ms);
}

static inline my_ret_t my_pal_main_loop_remove_timer(my_pal_main_loop_t* loop,
                                                     uint32_t id) {
  return loop->vtable->remove_timer(loop, id);
}

static inline void my_pal_main_loop_destroy(my_pal_main_loop_t* loop) {
  if (loop != NULL) {
    loop->vtable->destroy(loop);
  }
}

/* ---------------- platform ---------------- */

/** @brief Platform vtable. */
typedef struct my_pal_vtable_t {
  /** @brief Create a hidden window of w x h (title may be NULL). */
  my_pal_window_t* (*window_create)(my_pal_t* pal, int32_t w, int32_t h,
                                    const char* title);
  my_pal_main_loop_t* (*main_loop_create)(my_pal_t* pal);
  /** @brief Monotonic clock in milliseconds. */
  uint64_t (*time_now_ms)(my_pal_t* pal);
  /** @brief Register the single application event handler. */
  my_ret_t (*set_event_handler)(my_pal_t* pal, my_pal_event_handler_t handler,
                                void* ctx);
  void (*destroy)(my_pal_t* pal);
} my_pal_vtable_t;

/** @brief Platform base "class". */
struct my_pal_t {
  const my_pal_vtable_t* vtable;
};

static inline my_pal_window_t* my_pal_window_create(my_pal_t* pal, int32_t w,
                                                    int32_t h,
                                                    const char* title) {
  return pal->vtable->window_create(pal, w, h, title);
}

static inline my_pal_main_loop_t* my_pal_main_loop_create(my_pal_t* pal) {
  return pal->vtable->main_loop_create(pal);
}

static inline uint64_t my_pal_time_now_ms(my_pal_t* pal) {
  return pal->vtable->time_now_ms(pal);
}

static inline my_ret_t my_pal_set_event_handler(my_pal_t* pal,
                                                my_pal_event_handler_t handler,
                                                void* ctx) {
  return pal->vtable->set_event_handler(pal, handler, ctx);
}

static inline void my_pal_destroy(my_pal_t* pal) {
  if (pal != NULL) {
    pal->vtable->destroy(pal);
  }
}

/**
 * @brief Create the platform object for the compile-time selected port
 * (MYUI_PAL_X11 / MYUI_PAL_DUMMY). NULL allocator = default.
 */
my_pal_t* my_pal_create(const my_allocator_t* allocator);

#endif /* MY_PAL_H */
