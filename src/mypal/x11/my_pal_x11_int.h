/**
 * @file my_pal_x11_int.h
 * @brief X11 port internals shared between my_pal_x11.c and the IME
 * module (my_pal_x11_ime.c, M13a). NOT a public header.
 */
#ifndef MY_PAL_X11_INT_H
#define MY_PAL_X11_INT_H

#include <X11/Xlib.h>

#include "myc/my_darray.h"
#include "mypal/my_pal.h"

#if defined(MYUI_PAL_GL_EGL)
#include <EGL/egl.h>
#endif

typedef struct x11_pal_t {
  my_pal_t base;
  const my_allocator_t* allocator;
  Display* display;
  int screen;
  Atom wm_delete;
  my_darray_t* windows; /**< x11_window_t* registry for event routing */
  my_pal_event_handler_t handler;
  void* handler_ctx;
  float scale; /**< Xft.dpi/96 or physical DPI estimate (M12c) */
  char* clipboard;        /**< cached text (we serve it when we own) */
  Atom atom_clipboard;
  Atom atom_utf8;
  Atom atom_targets;
  Atom atom_clip_prop;
  Atom atom_incr;
  struct {
    bool active;
    Window requestor;
    Atom property;
    Atom target;
    size_t offset;   /**< bytes already appended */
    size_t len;      /**< strlen at transfer start */
    size_t chunk;    /**< per-segment payload */
  } incr_tx[4];      /**< concurrent INCR transfers (M12d) */
  /** @brief XIM connection (M13a; NULL = no input method, plain keys). */
  XIM xim;
  /** @brief Cached font cursors by my_cursor_t (M21a; lazy, 0 = uncached). */
  Cursor cursors[3];
  bool cursors_init;
#if defined(MYUI_PAL_GL_EGL)
  EGLDisplay egl_dpy; /**< shared EGL display (lazy, EGL_NO_DISPLAY off) */
  EGLConfig egl_cfg;
  int egl_state; /**< 0 = untried, 1 = ready, -1 = unavailable */
  bool egl_msaa; /**< the shared config carries EGL_SAMPLES=4 (M11c) */
#endif
} x11_pal_t;

typedef struct x11_window_t {
  my_pal_window_t base;
  x11_pal_t* pal;
  const my_allocator_t* allocator;
  Window xwin;
  GC gc;
  int32_t w;          /**< LOGICAL size (M12c) */
  int32_t h;
  int32_t pw;         /**< physical buffer size = logical*scale */
  int32_t ph;
  my_lcd_t* mem_lcd;   /**< back buffer (owned) */
  my_lcd_t* front_lcd; /**< wrapper lcd: end_frame presents (owned) */
  XImage* ximage;      /**< wraps the back buffer (data borrowed) */
  my_pal_gl_t* gl;     /**< GL mount after gl_enable (owned, M10c) */
  XIC ic;              /**< XIM input context (M13a; NULL without IM) */
  void* ime_ctx;       /**< per-window IME state (M13a, owned by ime) */
} x11_window_t;

#endif /* MY_PAL_X11_INT_H */
