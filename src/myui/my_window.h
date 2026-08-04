/**
 * @file my_window.h
 * @brief Top-level window: a root widget bound to a PAL window.
 *
 * A window owns its PAL window and a vgcanvas (software backend over the
 * PAL window's lcd by default; injectable for tests). Dirty rects from
 * my_widget_invalidate() collect here (the root's dirty_sink); painting
 * redraws only the dirty regions.
 */
#ifndef MY_WINDOW_H
#define MY_WINDOW_H

#include "mypal/my_pal.h"
#include "myui/my_event_dispatch.h"

/** @brief Top-level window (IS-A widget: embed as first member). */
typedef struct my_window_t {
  my_widget_t base;                  /**< root widget of the window */
  const my_allocator_t* allocator;
  my_pal_t* pal;                     /**< borrowed */
  my_pal_window_t* pal_window;       /**< owned */
  my_vgcanvas_t* vg;                 /**< soft backend, or injected (tests) */
  bool vg_owned;
  my_color_t bg_color;
  my_theme_t* theme;                 /**< active theme */
  bool theme_owned;
  my_dirty_rects_t dirty;            /**< frame dirty collector (sink) */
  my_event_dispatcher_t dispatcher;
  bool modal;
} my_window_t;

/** @brief Create a window (hidden) of w x h with the given title. */
my_window_t* my_window_create(const my_allocator_t* allocator, my_pal_t* pal,
                              int32_t w, int32_t h, const char* title);

/** @brief The root widget (for add_child etc.). Same pointer as win. */
static inline my_widget_t* my_window_widget(my_window_t* win) {
  return (my_widget_t*)win;
}

/**
 * @brief Paint if dirty: relayout when needed, then for each dirty rect
 * clip and repaint the tree; clears the dirty set. No-op when clean.
 */
void my_window_paint(my_window_t* win);

/**
 * @brief Route one PAL event (PAINT/RESIZE/POINTER_x/KEY_x) into the
 * window. Paints immediately when the dispatch left the window dirty.
 */
my_ret_t my_window_on_pal_event(my_window_t* win, const my_event_t* event);

/** @brief Test hook: use this vgcanvas instead of creating a soft one. */
void my_window_set_vgcanvas(my_window_t* win, my_vgcanvas_t* vg);

/**
 * @brief Switch the window's theme (and apply it to the widget tree).
 * When take_ownership is true the window destroys it on replace/destroy.
 */
void my_window_set_theme(my_window_t* win, my_theme_t* theme,
                         bool take_ownership);

#endif /* MY_WINDOW_H */
