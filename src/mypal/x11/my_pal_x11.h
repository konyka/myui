/**
 * @file my_pal_x11.h
 * @brief X11 PAL port (Linux desktop).
 */
#ifndef MY_PAL_X11_H
#define MY_PAL_X11_H

#include "mypal/my_pal.h"

/**
 * @brief Create the X11 platform. Returns NULL when the X server cannot
 * be reached (e.g. no DISPLAY); callers may fall back to the dummy port.
 */
my_pal_t* my_pal_x11_create(const my_allocator_t* allocator);

/** @brief Xft.dpi -> scale factor (M12c; exported for unit tests). */
float my_pal_x11_scale_from_xft_dpi(double dpi);

/** @brief Parse "Xft.dpi:" out of an Xrm database string (M12c;
 * exported for unit tests). Returns 0 when absent. */
double my_pal_x11_parse_xft_dpi(const char* xrm_db);

/** @brief True when an XIM input method is connected (M13a). */
bool my_pal_x11_has_ime(my_pal_t* pal);

/** @brief The native X window id (tests/tools). */
unsigned long my_pal_x11_window_xid(my_pal_window_t* win);

#endif /* MY_PAL_X11_H */
