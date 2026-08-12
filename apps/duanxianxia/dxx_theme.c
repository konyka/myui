/**
 * @file dxx_theme.c
 * @brief Site theme: default theme + white window background.
 */
#include "dxx_theme.h"

my_theme_t* dxx_theme_create(const my_allocator_t* allocator) {
  my_theme_t* t = my_theme_default_create(allocator);
  if (t == NULL) {
    return NULL;
  }
  my_theme_set_color(t, "window", NULL, MY_STATE_NORMAL, "bg_color",
                     DXX_COLOR_WHITE);
  my_theme_set_color(t, "label", NULL, MY_STATE_NORMAL, "bg_color",
                     DXX_COLOR_WHITE);
  my_theme_set_color(t, "label", NULL, MY_STATE_NORMAL, "fg_color",
                     DXX_COLOR_TEXT);
  return t;
}
