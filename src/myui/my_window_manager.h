/**
 * @file my_window_manager.h
 * @brief Window stack manager + application entry point.
 *
 * The manager registers itself as the PAL event handler and routes
 * events to the window owning the source PAL window. Windows form a
 * stack (later = on top); closing the last window quits the main loop
 * (observable via quit_requested).
 */
#ifndef MY_WINDOW_MANAGER_H
#define MY_WINDOW_MANAGER_H

#include "myui/my_animator.h"
#include "myui/my_window.h"

/** @brief Window stack manager. */
typedef struct my_window_manager_t {
  const my_allocator_t* allocator;
  my_pal_t* pal;            /**< borrowed */
  my_pal_main_loop_t* loop; /**< borrowed */
  my_darray_t* windows;     /**< stack of owned refs (my_window_t*) */
  my_animator_manager_t* anim_mgr; /**< owned: drives widget animations */
  uint32_t paint_timer_id;  /**< periodic dirty-window repaint tick */
  bool quit_requested;      /**< set when the last window was closed */
} my_window_manager_t;

/** @brief Create a manager; registers the PAL event handler. */
my_window_manager_t* my_window_manager_create(const my_allocator_t* allocator,
                                              my_pal_t* pal,
                                              my_pal_main_loop_t* loop);

/** @brief Push a window (refs it), show it, invalidate fully. */
my_ret_t my_window_manager_open(my_window_manager_t* wm, my_window_t* win);

/**
 * @brief Close (remove + unref) a window. Closing the last one calls
 * main_loop quit and sets quit_requested.
 */
my_ret_t my_window_manager_close(my_window_manager_t* wm, my_window_t* win);

/** @brief Top window of the stack (borrowed), NULL when empty. */
my_window_t* my_window_manager_top(my_window_manager_t* wm);

/** @brief Close all windows above the bottom one. */
my_ret_t my_window_manager_back_to_home(my_window_manager_t* wm);

/** @brief Number of open windows. */
size_t my_window_manager_count(my_window_manager_t* wm);

/** @brief Close all windows and unregister the PAL handler. */
void my_window_manager_destroy(my_window_manager_t* wm);

/** @brief Factory for the app's first window (my_app_run). */
typedef my_window_t* (*my_app_window_factory_t)(my_pal_t* pal, void* ctx);

/**
 * @brief Convenience entry: create main loop + window manager, build the
 * first window via factory, open it, run the loop, clean up.
 */
my_ret_t my_app_run(my_pal_t* pal, my_app_window_factory_t factory, void* ctx);

#endif /* MY_WINDOW_MANAGER_H */
