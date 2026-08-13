/**
 * @file my_window.c
 * @brief Top-level window implementation.
 */
#include "myui/my_window.h"

#include <string.h>

#include "myc/my_str.h"
#include "myr/my_vgcanvas_gles2.h"
#include "myr/my_vgcanvas_soft.h"
#include "myui/my_animator.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"

/* ---------------- widget vtable ---------------- */

static void tip_cancel_timer(my_window_t* win);
static void tip_hide(my_window_t* win);
static void csd_bar_layout(my_widget_t* widget);

static void window_on_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_window_t* win = (my_window_t*)widget;
  uint32_t bg = my_widget_style_get_color(widget, MY_STATE_NORMAL, "bg_color",
                                          my_color_to_rgba32(win->bg_color));
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(bg));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
}

static const my_widget_vtable_t s_window_vtable = {window_on_paint, NULL, NULL};

/* ---------------- CSD title bar (M16) ---------------- */

#define CSD_BAR_H 36
#define CSD_BAR_BG 0x3C4043FFu  /**< GNOME-ish dark grey */
#define CSD_CLOSE_HOVER 0xE81123FFu /**< convention red */

typedef struct csd_bar_t {
  my_widget_t base;
  my_window_t* win; /**< weak */
} csd_bar_t;

typedef struct csd_close_t {
  my_widget_t base;
  my_window_t* win; /**< weak */
} csd_close_btn_t;

static void csd_bar_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  csd_bar_t* bar = (csd_bar_t*)widget;
  my_window_t* win = bar->win;
  int32_t tw = 0, th = 0;
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(CSD_BAR_BG));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
  if (win->title != NULL) {
    my_vgcanvas_set_font(vg, NULL, 13);
    if (my_vgcanvas_measure_text(vg, win->title, &tw, &th) != MY_RET_OK) {
      tw = (int32_t)strlen(win->title) * 8;
      th = 13;
    }
    my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(0xFFFFFFFFu));
    my_vgcanvas_draw_text(vg, win->title,
                          ((float)widget->rect.w - (float)tw) / 2.0f,
                          ((float)widget->rect.h - (float)th) / 2.0f);
  }
}

static my_ret_t csd_bar_event(my_widget_t* widget, const my_event_t* event) {
  csd_bar_t* bar = (csd_bar_t*)widget;
  /* only direct hits on the bar body reach here: the close button is a
   * child and eats its own DOWN/UP first */
  if (event->type == MY_EVENT_POINTER_DOWN) {
    my_pal_window_begin_move(bar->win->pal_window);
    return MY_RET_OK;
  }
  return MY_RET_FAIL;
}

/** @brief Deferred close ctx (sync close would free the tree
 * mid-dispatch — same pattern as my_dialog's deferred close). */
typedef struct csd_close_req_t {
  struct my_window_manager_t* wm;
  my_window_t* win;
} csd_close_req_t;

static my_ret_t csd_close_idle(void* ctx) {
  csd_close_req_t* req = (csd_close_req_t*)ctx;
  my_window_manager_close(req->wm, req->win);
  my_mem_free(NULL, req);
  return MY_RET_FAIL; /* one-shot */
}

static void csd_close_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_vgcanvas_set_fill_color(
      vg, my_color_from_rgba32(widget->hovered ? CSD_CLOSE_HOVER
                                               : CSD_BAR_BG));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
  my_vgcanvas_set_font(vg, NULL, 14);
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(0xFFFFFFFFu));
  my_vgcanvas_draw_text(vg, "\xC3\x97" /* × */, 11, 10);
}

static my_ret_t csd_close_event(my_widget_t* widget, const my_event_t* event) {
  csd_close_btn_t* btn = (csd_close_btn_t*)widget;
  if (event->type == MY_EVENT_POINTER_DOWN) {
    return MY_RET_OK;
  }
  if (event->type == MY_EVENT_POINTER_UP) {
    my_window_t* win = btn->win;
    csd_close_req_t* req;
    if (win->wm == NULL || win->loop == NULL) {
      /* not under a window manager: nothing to route a close through
       * (the QUIT path lives in the wm) — no-op by design */
      return MY_RET_OK;
    }
    req = (csd_close_req_t*)my_mem_calloc(NULL, 1, sizeof(csd_close_req_t));
    if (req == NULL) {
      return MY_RET_OK;
    }
    req->wm = win->wm;
    req->win = win;
    if (my_pal_main_loop_add_timer(win->loop, csd_close_idle, req, 1) == 0) {
      my_mem_free(NULL, req);
    }
    return MY_RET_OK;
  }
  return MY_RET_FAIL;
}

static const my_widget_vtable_t s_csd_bar_vtable = {csd_bar_paint,
                                                    csd_bar_event,
                                                    csd_bar_layout};
static const my_widget_vtable_t s_csd_close_vtable = {csd_close_paint,
                                                      csd_close_event, NULL};

/** @brief Keep the close button glued to the bar's right edge. */
static void csd_bar_layout(my_widget_t* widget) {
  size_t n = my_widget_child_count(widget);
  if (n > 0) {
    my_widget_t* close_btn = my_widget_get_child(widget, n - 1);
    close_btn->rect.x = widget->rect.w - 32;
    close_btn->rect.y = 0;
    close_btn->rect.w = 32;
    close_btn->rect.h = widget->rect.h;
  }
}

/** @brief Build the CSD title bar + content container under the root
 * (vertical linear: bar h:36 + content h:1f). */
static void window_setup_csd(my_window_t* win) {
  my_widget_t* root = (my_widget_t*)win;
  csd_bar_t* bar;
  csd_close_btn_t* close_btn;
  my_widget_t* content;
  my_widget_set_layouter(root, my_layouter_linear_create(win->allocator,
                                                         false, 0));
  bar = (csd_bar_t*)my_mem_calloc(win->allocator, 1, sizeof(csd_bar_t));
  content = my_widget_create(win->allocator, "csd_content");
  if (bar == NULL || content == NULL ||
      my_widget_init((my_widget_t*)bar, win->allocator, &s_csd_bar_vtable,
                     "csd_bar") != MY_RET_OK) {
    my_mem_free(win->allocator, bar);
    if (content != NULL) {
      my_widget_unref(content);
    }
    return; /* out of memory: stay undecorated rather than broken */
  }
  bar->win = win;
  my_widget_set_layout_params((my_widget_t*)bar, "h:36");
  close_btn =
      (csd_close_btn_t*)my_mem_calloc(win->allocator, 1, sizeof(csd_close_btn_t));
  if (close_btn != NULL &&
      my_widget_init((my_widget_t*)close_btn, win->allocator,
                     &s_csd_close_vtable, "csd_close") == MY_RET_OK) {
    close_btn->win = win;
    ((my_widget_t*)close_btn)->rect =
        my_rect_init(0, 0, 32, CSD_BAR_H); /* x set in csd_bar_layout */
    my_widget_add_child((my_widget_t*)bar, (my_widget_t*)close_btn);
    my_widget_unref((my_widget_t*)close_btn);
  } else {
    my_mem_free(win->allocator, close_btn);
  }
  my_widget_set_layout_params(content, "h:1f");
  my_widget_add_child(root, (my_widget_t*)bar);
  my_widget_unref((my_widget_t*)bar);
  my_widget_add_child(root, content);
  my_widget_unref(content);
  /* my_window_widget() hands the content container to apps, and the
   * universal pattern `unref(my_window_widget(win))` after wm_open
   * expects to own one reference — give it one (the tree keeps its
   * own, so this balances exactly like the root does in non-CSD). */
  win->csd_content = my_widget_ref(content);
  win->csd = true;
}

/* ---------------- lifecycle ---------------- */

/** @brief Root hook: a subtree is being removed -> drop dispatcher refs
 * and cancel its animations. */
static void window_on_subtree_removed(my_widget_t* root, my_widget_t* removed) {
  my_window_t* win = (my_window_t*)root;
  my_widget_t* w;
  my_event_dispatcher_forget(&win->dispatcher, removed);
  if (root->anim_mgr != NULL) {
    my_animator_stop_widget((my_animator_manager_t*)root->anim_mgr, removed);
  }
  /* tooltip (M13c): drop hover state pointing into the removed subtree */
  for (w = win->tip_target; w != NULL; w = w->parent) {
    if (w == removed) {
      win->tip_target = NULL;
      tip_cancel_timer(win);
      if (win->tip_widget != NULL) {
        tip_hide(win); /* safe: tip is never inside `removed` */
      }
      break;
    }
  }
}

static void window_destroy_chain(my_object_t* obj) {
  my_window_t* win = (my_window_t*)obj;
  tip_cancel_timer(win);
  tip_hide(win); /* we hold one ref; the tree holds the other */
  win->tip_target = NULL;
  if (((my_widget_t*)win)->anim_mgr != NULL) {
    my_animator_stop_widget((my_animator_manager_t*)((my_widget_t*)win)->anim_mgr,
                            (my_widget_t*)win);
  }
  if (win->vg_owned) {
    my_vgcanvas_destroy(win->vg);
  }
  win->vg = NULL;
  if (win->gl_owned) {
    my_pal_gl_destroy(win->gl);
  }
  win->gl = NULL;
  if (win->theme_owned) {
    my_theme_destroy(win->theme);
  }
  win->theme = NULL;
  my_mem_free(win->allocator, win->title);
  win->title = NULL;
  my_pal_window_destroy(win->pal_window);
  win->pal_window = NULL;
  my_widget_destroy((my_widget_t*)win);
  my_object_destroy(obj);
}

my_window_t* my_window_create(const my_allocator_t* allocator, my_pal_t* pal,
                              int32_t w, int32_t h, const char* title) {
  my_window_t* win;
  if (pal == NULL || w <= 0 || h <= 0) {
    return NULL;
  }
  win = (my_window_t*)my_mem_calloc(allocator, 1, sizeof(my_window_t));
  if (win == NULL) {
    return NULL;
  }
  if (my_widget_init((my_widget_t*)win, allocator, &s_window_vtable, "window") !=
      MY_RET_OK) {
    my_mem_free(allocator, win);
    return NULL;
  }
  ((my_object_t*)win)->destroy = window_destroy_chain;
  win->allocator = allocator;
  win->pal = pal;
  win->pal_window = my_pal_window_create(pal, w, h, title);
  if (win->pal_window == NULL) {
    my_object_unref((my_object_t*)win);
    return NULL;
  }
  win->bg_color = my_color_rgb(32, 32, 32);
  win->title = my_strdup(allocator, title); /* M16: CSD bar text */
  win->scale = my_pal_get_scale_factor(pal); /* HiDPI (M12c): applied to
                                                the vgcanvas in ensure/GL */
  if (win->scale <= 0.0f) {
    win->scale = 1.0f;
  }
  win->vg = NULL;
  win->vg_owned = false;
  win->modal = false;
  win->theme = my_theme_default_create(allocator);
  win->theme_owned = win->theme != NULL;
  ((my_widget_t*)win)->rect = my_rect_init(0, 0, w, h);
  ((my_widget_t*)win)->widget_type = "window";
  ((my_widget_t*)win)->dirty_sink = &win->dirty;
  ((my_widget_t*)win)->theme = win->theme;
  ((my_widget_t*)win)->removed_hook = window_on_subtree_removed;
  my_dirty_rects_init(&win->dirty);
  my_event_dispatcher_init(&win->dispatcher, (my_widget_t*)win);
  if (my_pal_needs_client_decoration(pal)) {
    window_setup_csd(win); /* M16: compositor gives no SSD (mutter/wl) */
  }
  return win;
}

void my_window_set_vgcanvas(my_window_t* win, my_vgcanvas_t* vg) {
  if (win == NULL) {
    return;
  }
  if (win->vg_owned) {
    my_vgcanvas_destroy(win->vg);
  }
  win->vg = vg;
  win->vg_owned = false;
}

my_ret_t my_window_enable_gl(my_window_t* win) {
  my_pal_gl_t* gl;
  my_vgcanvas_t* vg;
  int32_t w = 0, h = 0;
  if (win == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  if (win->gl != NULL) {
    return MY_RET_OK; /* already enabled */
  }
  gl = my_pal_window_gl_enable(win->pal_window);
  if (gl == NULL) {
    return MY_RET_NOT_SUPPORTED;
  }
  if (my_pal_gl_make_current(gl) != MY_RET_OK) {
    my_pal_gl_destroy(gl);
    return MY_RET_FAIL;
  }
  my_pal_gl_get_size(gl, &w, &h);
  vg = my_vgcanvas_gles2_create(win->allocator, w, h);
  if (vg == NULL) {
    my_pal_gl_destroy(gl);
    return MY_RET_FAIL;
  }
  if (win->scale != 1.0f) {
    my_vgcanvas_gles2_set_scale(vg, win->scale);
  }
  if (win->vg_owned) {
    my_vgcanvas_destroy(win->vg);
  }
  win->vg = vg;
  win->vg_owned = true;
  win->gl = gl;
  win->gl_owned = true;
  if (win->font != NULL) {
    my_vgcanvas_set_font(win->vg, win->font, win->font_size);
  }
  return MY_RET_OK;
}

void my_window_set_theme(my_window_t* win, my_theme_t* theme,
                         bool take_ownership) {
  if (win == NULL) {
    return;
  }
  if (win->theme_owned) {
    my_theme_destroy(win->theme);
  }
  win->theme = theme;
  win->theme_owned = take_ownership;
  my_widget_apply_theme((my_widget_t*)win, theme);
}

/* ---------------- painting ---------------- */

static my_vgcanvas_t* window_ensure_vg(my_window_t* win) {
  if (win->vg == NULL) {
    win->vg = my_vgcanvas_soft_create(win->allocator,
                                      my_pal_window_get_lcd(win->pal_window));
    win->vg_owned = win->vg != NULL;
    if (win->vg != NULL && win->scale != 1.0f) {
      my_vgcanvas_soft_set_scale(win->vg, win->scale);
    }
    if (win->vg != NULL && win->font != NULL) {
      my_vgcanvas_set_font(win->vg, win->font, win->font_size);
    }
  }
  return win->vg;
}

void my_window_set_font(my_window_t* win, my_font_t* font, int32_t size) {
  if (win == NULL) {
    return;
  }
  win->font = font;
  win->font_size = size > 0 ? size : 16;
  if (win->vg != NULL) {
    my_vgcanvas_set_font(win->vg, font, win->font_size);
  }
}

void my_window_paint(my_window_t* win) {
  my_widget_t* root;
  my_vgcanvas_t* vg;
  size_t i, n;
  if (win == NULL || my_dirty_rects_count(&win->dirty) == 0) {
    return;
  }
  root = (my_widget_t*)win;
  if (root->need_layout) {
    my_widget_relayout(root);
  }
  vg = window_ensure_vg(win);
  if (vg == NULL) {
    return;
  }
  if (win->gl != NULL) {
    my_pal_gl_make_current(win->gl);
  }
  my_vgcanvas_begin_frame(vg, my_dirty_rects_get(&win->dirty, 0));
  n = my_dirty_rects_count(&win->dirty);
  for (i = 0; i < n; i++) {
    const my_rect_t* r = my_dirty_rects_get(&win->dirty, i);
    my_vgcanvas_save(vg);
    my_vgcanvas_clip_rect(vg, &(my_rectf_t){(float)r->x, (float)r->y, (float)r->w,
                                            (float)r->h});
    my_widget_paint(root, vg);
    if (win->scrim) {
      /* modal veil (M13c): darken the blocked window under a dialog */
      my_vgcanvas_set_fill_color(vg, my_color_rgba(0, 0, 0, 96));
      my_vgcanvas_fill_rect(vg, &(my_rectf_t){(float)r->x, (float)r->y,
                                              (float)r->w, (float)r->h});
    }
    my_vgcanvas_restore(vg);
  }
  my_vgcanvas_end_frame(vg);
  if (win->gl != NULL) {
    my_pal_gl_swap_buffers(win->gl);
  }
  my_dirty_rects_clear(&win->dirty);
}

/* ---------------- tooltip (M13c) ---------------- */

#define TIP_DELAY_MS 500
#define TIP_DX 12
#define TIP_DY 16

/** @brief Paint the floating tip (text stored in its own tooltip field). */
static void tip_on_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  uint32_t bg = my_widget_style_get_color(widget, MY_STATE_NORMAL,
                                          "bg_color", 0x323232F2u);
  uint32_t fg = my_widget_style_get_color(widget, MY_STATE_NORMAL,
                                          "fg_color", 0xF5F5F5FFu);
  uint32_t border = my_widget_style_get_color(widget, MY_STATE_NORMAL,
                                              "border_color", 0x616161FFu);
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(bg));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
  my_vgcanvas_set_stroke_color(vg, my_color_from_rgba32(border));
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                            (float)widget->rect.h});
  if (widget->tooltip != NULL) {
    my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(fg));
    my_vgcanvas_draw_text(vg, widget->tooltip, 6,
                          ((float)widget->rect.h - 8) / 2.0f);
  }
}

static const my_widget_vtable_t s_tip_vtable = {tip_on_paint, NULL, NULL};

/** @brief Remove the visible tip (reentrancy-safe via early NULL). */
static void tip_hide(my_window_t* win) {
  my_widget_t* tip = win->tip_widget;
  if (tip == NULL) {
    return;
  }
  win->tip_widget = NULL;
  my_widget_remove_child((my_widget_t*)win, tip);
  my_widget_unref(tip);
}

static void tip_cancel_timer(my_window_t* win) {
  if (win->tip_timer != 0 && win->loop != NULL) {
    my_pal_main_loop_remove_timer(win->loop, win->tip_timer);
  }
  win->tip_timer = 0;
}

/** @brief One-shot hover timer fired: pop the tip near the cursor. */
static my_ret_t tip_on_timer(void* ctx) {
  my_window_t* win = (my_window_t*)ctx;
  my_widget_t* root = (my_widget_t*)win;
  my_widget_t* tip;
  const char* text;
  int32_t w, h, x, y;
  win->tip_timer = 0;
  if (win->tip_target == NULL) {
    return MY_RET_FAIL;
  }
  text = win->tip_target->tooltip;
  if (text == NULL || text[0] == '\0') {
    return MY_RET_FAIL;
  }
  w = (int32_t)strlen(text) * 8 + 12;
  h = 22;
  x = win->tip_x + TIP_DX;
  y = win->tip_y + TIP_DY;
  if (x + w > root->rect.w) {
    x = root->rect.w - w;
  }
  if (y + h > root->rect.h) {
    y = win->tip_y - TIP_DY - h; /* flip above the cursor */
  }
  if (x < 0) {
    x = 0;
  }
  if (y < 0) {
    y = 0;
  }
  tip = my_widget_create(win->allocator, "tooltip");
  if (tip == NULL) {
    return MY_RET_FAIL;
  }
  tip->vtable = &s_tip_vtable;
  tip->floating = true;
  my_widget_set_tooltip(tip, text);
  my_widget_set_rect(tip, &(my_rect_t){x, y, w, h});
  tip_hide(win); /* paranoia: never two tips */
  if (my_widget_add_child(root, tip) != MY_RET_OK) {
    my_widget_unref(tip);
    return MY_RET_FAIL;
  }
  win->tip_widget = tip; /* tree + we hold one ref each */
  my_widget_invalidate(root, NULL);
  return MY_RET_FAIL; /* one-shot */
}

/** @brief Nearest ancestor-or-self with a tooltip (excluding the tip). */
static my_widget_t* tip_hover_target(my_window_t* win, my_widget_t* hit) {
  while (hit != NULL) {
    if (hit != win->tip_widget && hit->tooltip != NULL &&
        hit->tooltip[0] != '\0') {
      return hit;
    }
    hit = hit->parent;
  }
  return NULL;
}

/** @brief Track hover state before the event is dispatched. */
static void tip_track(my_window_t* win, const my_event_t* event) {
  if (event->type == MY_EVENT_POINTER_MOVE) {
    my_widget_t* hit = my_widget_hit_test((my_widget_t*)win, event->u.pointer.x,
                                          event->u.pointer.y);
    my_widget_t* target = tip_hover_target(win, hit);
    if (target == win->tip_target) {
      return; /* still hovering the same widget */
    }
    tip_cancel_timer(win);
    tip_hide(win);
    win->tip_target = target;
    if (target != NULL && win->loop != NULL) {
      win->tip_x = event->u.pointer.x;
      win->tip_y = event->u.pointer.y;
      win->tip_timer =
          my_pal_main_loop_add_timer(win->loop, tip_on_timer, win, TIP_DELAY_MS);
    }
    return;
  }
  if (event->type == MY_EVENT_POINTER_DOWN ||
      event->type == MY_EVENT_KEY_DOWN) {
    tip_cancel_timer(win);
    tip_hide(win);
    win->tip_target = NULL;
  }
}

/* ---------------- event routing ---------------- */

my_pal_t* my_window_pal_of_widget(my_widget_t* widget) {
  my_widget_t* root = widget;
  if (widget == NULL) {
    return NULL;
  }
  while (root->parent != NULL) {
    root = root->parent;
  }
  if (my_str_eq(root->widget_type, "window")) {
    return ((my_window_t*)root)->pal;
  }
  return NULL;
}

/** @brief The window's default font for a widget (NULL/size 0 when unset).
 * Widgets without an explicit font should use this so measurement and
 * rendering stay consistent (caret math bug, M16). */
void my_window_font_of_widget(my_widget_t* widget, my_font_t** font,
                              int32_t* font_size) {
  my_widget_t* root = widget;
  if (font != NULL) {
    *font = NULL;
  }
  if (font_size != NULL) {
    *font_size = 0;
  }
  if (widget == NULL) {
    return;
  }
  while (root->parent != NULL) {
    root = root->parent;
  }
  if (my_str_eq(root->widget_type, "window")) {
    my_window_t* win = (my_window_t*)root;
    if (font != NULL) {
      *font = win->font;
    }
    if (font_size != NULL) {
      *font_size = win->font_size;
    }
  }
}

my_pal_main_loop_t* my_window_loop_of_widget(my_widget_t* widget) {
  my_widget_t* root = widget;
  if (widget == NULL) {
    return NULL;
  }
  while (root->parent != NULL) {
    root = root->parent;
  }
  if (my_str_eq(root->widget_type, "window")) {
    return ((my_window_t*)root)->loop;
  }
  return NULL;
}

void my_window_set_undo_manager(my_window_t* win, void* mgr) {
  if (win != NULL) {
    win->undo_manager = mgr;
  }
}

void* my_window_undo_manager_of_widget(my_widget_t* widget) {
  my_widget_t* root = widget;
  if (widget == NULL) {
    return NULL;
  }
  while (root->parent != NULL) {
    root = root->parent;
  }
  if (my_str_eq(root->widget_type, "window")) {
    return ((my_window_t*)root)->undo_manager;
  }
  return NULL;
}

my_ret_t my_window_on_pal_event(my_window_t* win, const my_event_t* event) {
  my_widget_t* root;
  if (win == NULL || event == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  root = (my_widget_t*)win;
  switch (event->type) {
    case MY_EVENT_PAINT:
      my_window_paint(win);
      break;
    case MY_EVENT_RESIZE:
      root->rect.w = event->u.resize.w;
      root->rect.h = event->u.resize.h;
      root->need_layout = true;
      if (win->gl != NULL && win->vg != NULL) {
        my_pal_gl_make_current(win->gl);
        my_vgcanvas_gles2_resize(win->vg, event->u.resize.w,
                                 event->u.resize.h);
      }
      my_widget_invalidate(root, NULL);
      break;
    case MY_EVENT_POINTER_DOWN:
    case MY_EVENT_POINTER_MOVE:
    case MY_EVENT_POINTER_UP:
    case MY_EVENT_POINTER_WHEEL:
    case MY_EVENT_KEY_DOWN:
    case MY_EVENT_KEY_UP:
    case MY_EVENT_IME_PREEDIT:
    case MY_EVENT_IME_COMMIT:
      tip_track(win, event); /* hover tooltip bookkeeping (M13c) */
      my_event_dispatch(&win->dispatcher, event);
      break;
    default:
      break;
  }
  /* Dirty rects accumulate here and are painted by the window manager's
   * ~33 ms tick (frame coalescing): high-frequency event streams (wheel,
   * pointer motion) then cost at most one repaint per frame instead of one
   * full repaint per event. */
  return MY_RET_OK;
}
