/**
 * @file views.h
 * @brief duanxianxia clone: section builders (M14b).
 */
#ifndef DXX_VIEWS_H
#define DXX_VIEWS_H

#include "myui/my_window_manager.h"
#include "myui/widgets/my_menu.h"

#define DXX_MENU_COUNT 4
#define DXX_TOPBAR_H 50

/** @brief Topbar handle: the bar widget + the dropdown menu models
 * (owned by the caller; destroy with dxx_topbar_destroy). The struct
 * must outlive the bar (trigger callbacks reference it). */
typedef struct dxx_topbar_t {
  my_widget_t* bar;
  my_menu_t* menus[DXX_MENU_COUNT];
  struct dxx_trigger_t {
    my_window_t* win;
    struct dxx_topbar_t* tb;
    int menu_index;      /**< >= 0: dropdown; -1: flat item */
    my_widget_t* anchor; /**< weak: popup position */
    const char* log_name;
  } triggers[16];
} dxx_topbar_t;

/** @brief Build the 50px #444 topbar into parent (full parent width). */
void dxx_build_topbar(my_window_t* win, my_widget_t* parent,
                      dxx_topbar_t* out);

/** @brief Destroy the dropdown menu models (not the bar widget). */
void dxx_topbar_destroy(dxx_topbar_t* tb);

/** @brief Build the 12-column index strip into parent (strip rect set
 * by the caller after attaching; columns reflow on layout). */
my_widget_t* dxx_build_index_strip(my_widget_t* parent);

/** @brief Build the two-line footer into parent (rect by the caller). */
my_widget_t* dxx_build_footer(my_widget_t* parent);

/** @brief Build the two-column live area into parent at (x, y), total
 * width w (750 left + 20 gap + 530 right). @return the area height. */
int32_t dxx_build_live_area(my_widget_t* parent, int32_t x, int32_t y,
                            int32_t w);

/** @brief Build the 涨停股票池 table into parent with width w; the
 * table's own rect height is set to the measured content height. */
my_widget_t* dxx_build_ztpool(my_window_manager_t* wm, my_widget_t* parent,
                              int32_t w);

#endif /* DXX_VIEWS_H */
