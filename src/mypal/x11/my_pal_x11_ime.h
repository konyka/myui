/**
 * @file my_pal_x11_ime.h
 * @brief XIM input method integration for the x11 port (M13a). All XIM
 * calls live in my_pal_x11_ime.c; when XOpenIM fails the port stays on
 * the plain keyboard path with zero behavioral change.
 */
#ifndef MY_PAL_X11_IME_H
#define MY_PAL_X11_IME_H

#include "mypal/x11/my_pal_x11_int.h"

/** @brief Open the XIM connection (probes XMODIFIERS). Call once at pal
 * create; failure = no IM (everything else keeps working). */
my_ret_t x11_ime_init(x11_pal_t* pal);

/** @brief Close the XIM connection. */
void x11_ime_shutdown(x11_pal_t* pal);

/** @brief Create the window's input context (after the X window
 * exists). No-op without an IM. */
void x11_ime_window_attach(x11_pal_t* pal, x11_window_t* win);

/** @brief Destroy the window's input context. */
void x11_ime_window_detach(x11_pal_t* pal, x11_window_t* win);

/** @brief FocusIn/FocusOut -> XSetICFocus/XUnsetICFocus. */
void x11_ime_focus(x11_pal_t* pal, x11_window_t* win, bool focused);

/**
 * @brief Route a KeyPress through the IM. @return true when the event
 * was consumed (IM navigation, or a commit/preedit event was already
 * dispatched via the pal handler). false = process as a normal key.
 */
bool x11_ime_key_press(x11_pal_t* pal, x11_window_t* win, XKeyEvent* ev);

/** @brief Move the candidate window anchor (PHYSICAL pixels here; the
 * caller converts from logical using pal->scale). */
void x11_ime_set_spot(x11_pal_t* pal, x11_window_t* win, int32_t x,
                      int32_t y);

#endif /* MY_PAL_X11_IME_H */
