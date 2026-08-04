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

#endif /* MY_PAL_X11_H */
