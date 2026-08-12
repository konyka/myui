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

/**
 * @brief Test hook: true when every wayland listener slot required by the
 * interface versions we bind is non-NULL (libwayland aborts on NULL slots).
 */
bool my_pal_wayland_listeners_complete(void);

#endif /* MY_PAL_WAYLAND_H */
