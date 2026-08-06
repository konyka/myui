/**
 * @file my_window.c
 * @brief Top-level window implementation.
 */
#include "myui/my_window.h"

#include "myc/my_str.h"
#include "myr/my_vgcanvas_gles2.h"
#include "myr/my_vgcanvas_soft.h"
#include "myui/my_animator.h"
#include "myui/my_layout.h"

/* ---------------- widget vtable ---------------- */

static void window_on_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_window_t* win = (my_window_t*)widget;
  uint32_t bg = my_widget_style_get_color(widget, MY_STATE_NORMAL, "bg_color",
                                          my_color_to_rgba32(win->bg_color));
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(bg));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
}

static const my_widget_vtable_t s_window_vtable = {window_on_paint, NULL, NULL};

/* ---------------- lifecycle ---------------- */

/** @brief Root hook: a subtree is being removed -> drop dispatcher refs
 * and cancel its animations. */
static void window_on_subtree_removed(my_widget_t* root, my_widget_t* removed) {
  my_window_t* win = (my_window_t*)root;
  my_event_dispatcher_forget(&win->dispatcher, removed);
  if (root->anim_mgr != NULL) {
    my_animator_stop_widget((my_animator_manager_t*)root->anim_mgr, removed);
  }
}

static void window_destroy_chain(my_object_t* obj) {
  my_window_t* win = (my_window_t*)obj;
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
    my_vgcanvas_restore(vg);
  }
  my_vgcanvas_end_frame(vg);
  if (win->gl != NULL) {
    my_pal_gl_swap_buffers(win->gl);
  }
  my_dirty_rects_clear(&win->dirty);
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
    case MY_EVENT_KEY_DOWN:
    case MY_EVENT_KEY_UP:
      my_event_dispatch(&win->dispatcher, event);
      break;
    default:
      break;
  }
  /* immediate redraw of anything the dispatch dirtied */
  if (my_dirty_rects_count(&win->dirty) > 0) {
    my_window_paint(win);
  }
  return MY_RET_OK;
}
