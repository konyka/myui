/**
 * @file my_pal_x11_keymap.h
 * @brief X11 KeySym -> my_key_t translation (pure logic, unit-testable).
 */
#ifndef MY_PAL_X11_KEYMAP_H
#define MY_PAL_X11_KEYMAP_H

#include "mypal/my_event.h"

/**
 * @brief Translate an X11 KeySym to a my_key_t key code.
 * Printable ASCII keysyms (32..126) map to themselves; unmapped keysyms
 * return MY_KEY_UNKNOWN.
 */
uint32_t my_pal_x11_key_from_keysym(unsigned long keysym);

#endif /* MY_PAL_X11_KEYMAP_H */
