/**
 * @file my_pal_wayland_keymap.h
 * @brief xkb keysym -> my_key_t translation (pure logic, unit-testable).
 */
#ifndef MY_PAL_WAYLAND_KEYMAP_H
#define MY_PAL_WAYLAND_KEYMAP_H

#include "mypal/my_event.h"

/**
 * @brief Translate an xkb keysym to a my_key_t key code.
 * Printable ASCII keysyms (32..126) map to themselves; unmapped keysyms
 * return MY_KEY_UNKNOWN.
 */
uint32_t my_pal_wayland_key_from_keysym(uint32_t keysym);

#endif /* MY_PAL_WAYLAND_KEYMAP_H */
