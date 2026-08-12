/**
 * @file my_pal_x11_ime.c
 * @brief XIM input method integration for the x11 port (M13a).
 *
 * Model: one XIC per window, XIMPreeditCallbacks style when the IM
 * supports it (ibus does) so the widget paints the composing text
 * itself; falls back to XIMPreeditNothing (IM-owned preedit window,
 * commits only). KeyPress routing: XFilterEvent first (IM navigation
 * consumes), then Xutf8LookupString -- a multibyte result is dispatched
 * as MY_EVENT_IME_COMMIT, single-byte/keysym results take the plain key
 * path. Preedit callbacks dispatch MY_EVENT_IME_PREEDIT. chg_first/
 * chg_length are CHARACTER offsets into the composing text.
 */
/* POSIX (clock_gettime) under strict -std=c99; must precede system
 * headers (same rule as my_pal_x11.c). */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mypal/x11/my_pal_x11_ime.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlocale.h>

#include "myc/my_str.h"
#include "myr/my_font.h" /* my_utf8_next */

/** @brief Per-window IME state (owned by the ime module). */
typedef struct x11_ime_ctx_t {
  x11_pal_t* pal;
  x11_window_t* win;
  char* preedit;       /**< composing text, UTF-8 (owned) */
  size_t preedit_cps;  /**< codepoint length */
  int32_t caret;       /**< caret in codepoints */
  XIMCallback cbs[4];  /**< start/done/draw/caret (storage for Xlib) */
} x11_ime_ctx_t;

static uint64_t ime_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void ime_dispatch(x11_ime_ctx_t* ctx, my_event_type_t type,
                         const char* text, int32_t caret) {
  x11_pal_t* p = ctx->pal;
  my_event_t e;
  if (p->handler == NULL) {
    return;
  }
  e = my_event_init(type);
  e.time_ms = ime_now_ms();
  e.u.ime.text = text;
  e.u.ime.cursor = caret;
  p->handler(p->handler_ctx, (my_pal_window_t*)ctx->win, &e);
}

/** @brief Replace the composing buffer with text ("" = clear). */
static void ime_set_preedit(x11_ime_ctx_t* ctx, const char* text) {
  x11_pal_t* p = ctx->pal;
  char* copy = my_strdup(p->allocator, text != NULL ? text : "");
  if (copy == NULL) {
    return;
  }
  my_mem_free(p->allocator, ctx->preedit);
  ctx->preedit = copy;
  ctx->preedit_cps = my_str_utf8_strlen(copy);
}

/* ---------------- preedit callbacks (XIM) ---------------- */

static void preedit_start_cb(XIC ic, XPointer client_data, XPointer call) {
  x11_ime_ctx_t* ctx = (x11_ime_ctx_t*)client_data;
  (void)ic;
  (void)call;
  ime_set_preedit(ctx, "");
  ctx->caret = 0;
}

static void preedit_done_cb(XIC ic, XPointer client_data, XPointer call) {
  x11_ime_ctx_t* ctx = (x11_ime_ctx_t*)client_data;
  (void)ic;
  (void)call;
  ime_set_preedit(ctx, "");
  ctx->caret = 0;
  ime_dispatch(ctx, MY_EVENT_IME_PREEDIT, "", 0);
}

/** @brief Apply one preedit draw: replace chg_length chars at chg_first
 * (CHARACTER offsets) with the given UTF-8 bytes, then dispatch. */
static void preedit_draw_cb(XIC ic, XPointer client_data, XPointer call) {
  x11_ime_ctx_t* ctx = (x11_ime_ctx_t*)client_data;
  XIMPreeditDrawCallbackStruct* d = (XIMPreeditDrawCallbackStruct*)call;
  x11_pal_t* p = ctx->pal;
  const char* ins = "";
  size_t ins_len = 0;
  size_t head_bytes = 0, tail_bytes = 0, tail_cps, i;
  const char* cur;
  size_t new_len;
  char* nb;
  (void)ic;
  if (d == NULL) {
    return;
  }
  if (d->text != NULL && !d->text->encoding_is_wchar &&
      d->text->string.multi_byte != NULL) {
    ins = d->text->string.multi_byte;
    ins_len = (size_t)d->text->length; /* multi_byte: byte length */
  }
  /* byte offsets of the [chg_first, chg_first + chg_length) region in
   * CHARACTER units of the current preedit */
  cur = ctx->preedit != NULL ? ctx->preedit : "";
  for (i = 0; i < (size_t)d->chg_first && *cur != '\0'; i++) {
    cur += my_str_utf8_char_len(cur);
  }
  head_bytes = (size_t)(cur - (ctx->preedit != NULL ? ctx->preedit : ""));
  {
    const char* q = cur;
    for (i = 0; i < (size_t)d->chg_length && *q != '\0'; i++) {
      q += my_str_utf8_char_len(q);
    }
    tail_cps = 0;
    tail_bytes = strlen(q);
    {
      const char* r = q;
      while (*r != '\0') {
        r += my_str_utf8_char_len(r);
        tail_cps++;
      }
    }
    cur = q;
  }
  new_len = head_bytes + ins_len + tail_bytes;
  nb = (char*)my_mem_alloc(p->allocator, new_len + 1);
  if (nb == NULL) {
    return;
  }
  if (head_bytes > 0) {
    memcpy(nb, ctx->preedit, head_bytes);
  }
  if (ins_len > 0) {
    memcpy(nb + head_bytes, ins, ins_len);
  }
  if (tail_bytes > 0) {
    memcpy(nb + head_bytes + ins_len, cur, tail_bytes);
  }
  nb[new_len] = '\0';
  my_mem_free(p->allocator, ctx->preedit);
  ctx->preedit = nb;
  ctx->preedit_cps = my_str_utf8_strlen(nb);
  (void)tail_cps;
  ctx->caret = d->caret;
  ime_dispatch(ctx, MY_EVENT_IME_PREEDIT, ctx->preedit, ctx->caret);
}

static void preedit_caret_cb(XIC ic, XPointer client_data, XPointer call) {
  x11_ime_ctx_t* ctx = (x11_ime_ctx_t*)client_data;
  XIMPreeditCaretCallbackStruct* c = (XIMPreeditCaretCallbackStruct*)call;
  (void)ic;
  if (c != NULL) {
    ctx->caret = c->position;
    ime_dispatch(ctx, MY_EVENT_IME_PREEDIT,
                 ctx->preedit != NULL ? ctx->preedit : "", ctx->caret);
  }
}

/* ---------------- module API ---------------- */

my_ret_t x11_ime_init(x11_pal_t* pal) {
  if (pal == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  pal->xim = XOpenIM(pal->display, NULL, NULL, NULL);
  if (pal->xim == NULL) {
    return MY_RET_NOT_SUPPORTED; /* no IM (XMODIFIERS unset/down): plain
                                    keyboard path stays */
  }
  return MY_RET_OK;
}

void x11_ime_shutdown(x11_pal_t* pal) {
  if (pal != NULL && pal->xim != NULL) {
    XCloseIM(pal->xim);
    pal->xim = NULL;
  }
}

void x11_ime_window_attach(x11_pal_t* pal, x11_window_t* win) {
  x11_ime_ctx_t* ctx;
  void* attrs;
  if (pal == NULL || win == NULL || pal->xim == NULL) {
    return;
  }
  ctx = (x11_ime_ctx_t*)my_mem_calloc(pal->allocator, 1, sizeof(*ctx));
  if (ctx == NULL) {
    return;
  }
  ctx->pal = pal;
  ctx->win = win;
  ctx->cbs[0].client_data = (XPointer)ctx;
  ctx->cbs[0].callback = (XIMProc)preedit_start_cb;
  ctx->cbs[1].client_data = (XPointer)ctx;
  ctx->cbs[1].callback = (XIMProc)preedit_done_cb;
  ctx->cbs[2].client_data = (XPointer)ctx;
  ctx->cbs[2].callback = (XIMProc)preedit_draw_cb;
  ctx->cbs[3].client_data = (XPointer)ctx;
  ctx->cbs[3].callback = (XIMProc)preedit_caret_cb;
  attrs = XVaCreateNestedList(0, XNPreeditStartCallback, &ctx->cbs[0],
                              XNPreeditDoneCallback, &ctx->cbs[1],
                              XNPreeditDrawCallback, &ctx->cbs[2],
                              XNPreeditCaretCallback, &ctx->cbs[3], NULL);
  win->ic = XCreateIC(pal->xim, XNInputStyle,
                      XIMPreeditCallbacks | XIMStatusNothing,
                      XNClientWindow, win->xwin, XNFocusWindow, win->xwin,
                      XNPreeditAttributes, attrs, NULL);
  free(attrs);
  if (win->ic == NULL) {
    /* IM without callback style (or rejects it): commit-only, the IM
     * draws its own preedit window at the spot location */
    win->ic = XCreateIC(pal->xim, XNInputStyle,
                        XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win->xwin, XNFocusWindow,
                        win->xwin, NULL);
  }
  if (win->ic == NULL) {
    my_mem_free(pal->allocator, ctx);
    return;
  }
  win->ime_ctx = ctx;
}

void x11_ime_window_detach(x11_pal_t* pal, x11_window_t* win) {
  x11_ime_ctx_t* ctx;
  if (pal == NULL || win == NULL) {
    return;
  }
  ctx = (x11_ime_ctx_t*)win->ime_ctx;
  if (win->ic != NULL) {
    XDestroyIC(win->ic);
    win->ic = NULL;
  }
  if (ctx != NULL) {
    my_mem_free(pal->allocator, ctx->preedit);
    my_mem_free(pal->allocator, ctx);
    win->ime_ctx = NULL;
  }
}

void x11_ime_focus(x11_pal_t* pal, x11_window_t* win, bool focused) {
  (void)pal;
  if (win != NULL && win->ic != NULL) {
    if (focused) {
      XSetICFocus(win->ic);
    } else {
      XUnsetICFocus(win->ic);
    }
  }
}

bool x11_ime_key_press(x11_pal_t* pal, x11_window_t* win, XKeyEvent* ev) {
  char buf[64];
  KeySym ks = NoSymbol;
  Status st = XLookupNone;
  int n;
  if (pal == NULL || win == NULL || win->ic == NULL) {
    return false;
  }
  if (XFilterEvent((XEvent*)ev, ev->window)) {
    return true; /* consumed by the IM (candidate navigation etc.) */
  }
  n = Xutf8LookupString(win->ic, ev, buf, (int)sizeof(buf) - 1, &ks, &st);
  if (st == XBufferOverflow || n < 0) {
    return false; /* oversized commit: ignore rather than truncate */
  }
  if (n > 0 && (st == XLookupChars || st == XLookupBoth)) {
    buf[n] = '\0';
    if (n > 1 || (unsigned char)buf[0] >= 0x80u) {
      /* multibyte text: an IM commit (single-byte printable stays on
       * the plain key path) */
      x11_ime_ctx_t* ctx = (x11_ime_ctx_t*)win->ime_ctx;
      if (ctx != NULL) {
        ime_dispatch(ctx, MY_EVENT_IME_COMMIT, buf, 0);
      }
      return true;
    }
  }
  return false;
}

void x11_ime_set_spot(x11_pal_t* pal, x11_window_t* win, int32_t x,
                      int32_t y) {
  XPoint spot;
  void* attrs;
  (void)pal;
  if (win == NULL || win->ic == NULL) {
    return;
  }
  spot.x = (short)x;
  spot.y = (short)y;
  attrs = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
  XSetICValues(win->ic, XNPreeditAttributes, attrs, NULL);
  free(attrs);
}

bool my_pal_x11_has_ime(my_pal_t* pal) {
  return pal != NULL && ((x11_pal_t*)pal)->xim != NULL;
}

unsigned long my_pal_x11_window_xid(my_pal_window_t* win) {
  return win != NULL ? (unsigned long)((x11_window_t*)win)->xwin : 0;
}
