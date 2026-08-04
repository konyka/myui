/**
 * @file my_pal_wayland.h
 * @brief Wayland PAL port (wl_shm + xdg-shell).
 */
#ifndef MY_PAL_WAYLAND_H
#define MY_PAL_WAYLAND_H

#include "mypal/my_pal.h"

/**
 * @brief Create the Wayland platform. Returns NULL when the compositor
 * cannot be reached (no WAYLAND_DISPLAY); callers may fall back.
 */
my_pal_t* my_pal_wayland_create(const my_allocator_t* allocator);

#endif /* MY_PAL_WAYLAND_H */
