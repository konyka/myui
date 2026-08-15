/**
 * @file my_window_manager.c
 * @brief Window stack manager + my_app_run entry point.
 */
#include <stdio.h>
#include <stdlib.h>
#include "myui/my_window_manager.h"

#include "myui/my_animator.h"

/* ---------------- event routing from PAL ---------------- */

static my_window_t* wm_find_window(my_window_manager_t* wm,
                                   my_pal_window_t* pal_window) {
  size_t i, n = my_darray_size(wm->windows);
  for (i = 0; i < n; i++) {
    my_window_t* win = (my_window_t*)my_darray_get(wm->windows, i);
    if (win->pal_window == pal_window) {
      return win;
    }
  }
  return NULL;
}

static my_ret_t wm_on_pal_event(void* ctx, my_pal_window_t* pal_window,
                                const my_event_t* event) {
  my_window_manager_t* wm = (my_window_manager_t*)ctx;
  my_window_t* win;
  my_window_t* top;
  if (pal_window == NULL) {
    return MY_RET_OK; /* window-less event (posted USER events): ignore */
  }
  win = wm_find_window(wm, pal_window);
  if (win == NULL) {
    if (getenv("MYUI_WL_TRACE") != NULL &&
        (event->type == MY_EVENT_POINTER_DOWN ||
         event->type == MY_EVENT_POINTER_UP)) {
      fprintf(stderr, "[wltrace] wm route: pal_window=%p NOT FOUND type=%d\n",
              (void*)pal_window, (int)event->type);
    }
    return MY_RET_OK;
  }
  if (getenv("MYUI_WL_TRACE") != NULL &&
      (event->type == MY_EVENT_POINTER_DOWN ||
       event->type == MY_EVENT_POINTER_UP)) {
    my_window_t* t = my_window_manager_top(wm);
    fprintf(stderr,
            "[wltrace] wm route: win=%p top=%p top_modal=%d type=%d xy=(%d,%d)\n",
            (void*)win, (void*)t, t != NULL ? (int)t->modal : -1,
            (int)event->type, event->u.pointer.x, event->u.pointer.y);
  }
  if (event->type == MY_EVENT_QUIT) {
    my_window_manager_close(wm, win);
    return MY_RET_OK;
  }
  /* modal enforcement (M13c): while a modal window is on top, input
   * events go only to it; lower windows are veiled and blocked */
  top = my_window_manager_top(wm);
  if (top != NULL && top->modal && win != top &&
      (event->type == MY_EVENT_POINTER_DOWN ||
       event->type == MY_EVENT_POINTER_MOVE ||
       event->type == MY_EVENT_POINTER_UP ||
       event->type == MY_EVENT_POINTER_WHEEL ||
       event->type == MY_EVENT_KEY_DOWN || event->type == MY_EVENT_KEY_UP ||
       event->type == MY_EVENT_IME_PREEDIT ||
       event->type == MY_EVENT_IME_COMMIT)) {
    return MY_RET_OK;
  }
  return my_window_on_pal_event(win, event);
}

/* ---------------- lifecycle ---------------- */

/** @brief ~60fps repaint tick: paint every window that collected dirty
 * rects since the last frame (no-op for clean windows). Covers redraws
 * triggered outside event dispatch (animations, timers, model changes). */
static my_ret_t wm_paint_tick(void* ctx) {
  my_window_manager_t* wm = (my_window_manager_t*)ctx;
  size_t i, n = my_darray_size(wm->windows);
  for (i = 0; i < n; i++) {
    my_window_paint((my_window_t*)my_darray_get(wm->windows, i));
  }
  return MY_RET_OK;
}

my_window_manager_t* my_window_manager_create(const my_allocator_t* allocator,
                                              my_pal_t* pal,
                                              my_pal_main_loop_t* loop) {
  my_window_manager_t* wm;
  if (pal == NULL || loop == NULL) {
    return NULL;
  }
  wm = (my_window_manager_t*)my_mem_calloc(allocator, 1,
                                           sizeof(my_window_manager_t));
  if (wm == NULL) {
    return NULL;
  }
  wm->allocator = allocator;
  wm->pal = pal;
  wm->loop = loop;
  wm->windows = my_darray_create(allocator, 0);
  wm->anim_mgr = my_animator_manager_create(allocator, pal, loop);
  if (wm->windows == NULL || wm->anim_mgr == NULL) {
    my_darray_destroy(wm->windows);
    my_animator_manager_destroy(wm->anim_mgr);
    my_mem_free(allocator, wm);
    return NULL;
  }
  wm->paint_timer_id = my_pal_main_loop_add_timer(loop, wm_paint_tick, wm, 33);
  my_pal_set_event_handler(pal, wm_on_pal_event, wm);
  return wm;
}

my_ret_t my_window_manager_open(my_window_manager_t* wm, my_window_t* win) {
  if (wm == NULL || win == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  if (my_darray_push(wm->windows, my_widget_ref((my_widget_t*)win)) !=
      MY_RET_OK) {
    my_widget_unref((my_widget_t*)win);
    return MY_RET_OOM;
  }
  ((my_widget_t*)win)->anim_mgr = wm->anim_mgr;
  win->loop = wm->loop;
  win->wm = wm; /* M16: CSD close button routes through this */
  my_pal_window_show(win->pal_window);
  my_widget_invalidate((my_widget_t*)win, NULL);
  return MY_RET_OK;
}

my_ret_t my_window_manager_close(my_window_manager_t* wm, my_window_t* win) {
  size_t i, n;
  if (wm == NULL || win == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  n = my_darray_size(wm->windows);
  for (i = 0; i < n; i++) {
    if (my_darray_get(wm->windows, i) == win) {
      my_darray_remove_at(wm->windows, i);
      win->wm = NULL; /* no longer managed */
      my_widget_unref((my_widget_t*)win);
      if (win->csd) {
        /* M16 CSD: the app's `unref(my_window_widget(win))` balances the
         * content container's extra ref, NOT the window's create ref —
         * the manager absorbs the create ref here so the window dies
         * with its last manager reference like non-CSD windows. */
        my_widget_unref((my_widget_t*)win);
      }
      if (my_darray_size(wm->windows) == 0) {
        wm->quit_requested = true;
        my_pal_main_loop_quit(wm->loop);
      }
      return MY_RET_OK;
    }
  }
  return MY_RET_NOT_FOUND;
}

my_window_t* my_window_manager_top(my_window_manager_t* wm) {
  size_t n;
  if (wm == NULL) {
    return NULL;
  }
  n = my_darray_size(wm->windows);
  return n > 0 ? (my_window_t*)my_darray_get(wm->windows, n - 1) : NULL;
}

my_ret_t my_window_manager_back_to_home(my_window_manager_t* wm) {
  if (wm == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  while (my_darray_size(wm->windows) > 1) {
    my_window_t* top =
        (my_window_t*)my_darray_get(wm->windows, my_darray_size(wm->windows) - 1);
    my_darray_remove_at(wm->windows, my_darray_size(wm->windows) - 1);
    my_widget_unref((my_widget_t*)top);
  }
  return MY_RET_OK;
}

size_t my_window_manager_count(my_window_manager_t* wm) {
  return wm != NULL ? my_darray_size(wm->windows) : 0;
}

void my_window_manager_destroy(my_window_manager_t* wm) {
  if (wm == NULL) {
    return;
  }
  my_pal_set_event_handler(wm->pal, NULL, NULL);
  if (wm->paint_timer_id > 0) {
    my_pal_main_loop_remove_timer(wm->loop, wm->paint_timer_id);
    wm->paint_timer_id = 0;
  }
  /* windows first: their destroy chains cancel animations via anim_mgr */
  while (my_darray_size(wm->windows) > 0) {
    my_window_t* top =
        (my_window_t*)my_darray_get(wm->windows, my_darray_size(wm->windows) - 1);
    my_darray_remove_at(wm->windows, my_darray_size(wm->windows) - 1);
    {
      bool csd = top->csd; /* read BEFORE unref: the first unref may
                            * destroy the window (M16 latent UAF) */
      top->wm = NULL;
      my_widget_unref((my_widget_t*)top);
      if (csd) {
        my_widget_unref((my_widget_t*)top); /* M16: absorb the create ref */
      }
    }
  }
  my_animator_manager_destroy(wm->anim_mgr);
  wm->anim_mgr = NULL;
  my_darray_destroy(wm->windows);
  my_mem_free(wm->allocator, wm);
}

/* ---------------- application entry ---------------- */

my_ret_t my_app_run(my_pal_t* pal, my_app_window_factory_t factory, void* ctx) {
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  if (pal == NULL || factory == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  loop = my_pal_main_loop_create(pal);
  wm = my_window_manager_create(NULL, pal, loop);
  if (loop == NULL || wm == NULL) {
    my_window_manager_destroy(wm);
    my_pal_main_loop_destroy(loop);
    return MY_RET_OOM;
  }
  win = factory(pal, ctx);
  if (win == NULL) {
    my_window_manager_destroy(wm);
    my_pal_main_loop_destroy(loop);
    return MY_RET_FAIL;
  }
  my_window_manager_open(wm, win);
  my_widget_unref((my_widget_t*)win); /* manager holds the only ref now */
  my_pal_main_loop_run(loop);
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  return MY_RET_OK;
}
