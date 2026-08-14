/**
 * @file my_button.c
 * @brief Minimal push button widget.
 */
#include "myui/widgets/my_button.h"

#include "myc/my_str.h"
#include "myui/my_window.h"

static my_widget_state_t button_state(my_button_t* b) {
  my_widget_t* w = (my_widget_t*)b;
  if (!w->enable) {
    return MY_STATE_DISABLED;
  }
  if (b->pressed) {
    return MY_STATE_PRESSED;
  }
  if (w->hovered) { /* dispatcher-maintained (M14a) */
    return MY_STATE_HOVER;
  }
  return MY_STATE_NORMAL;
}

static my_color_t button_state_color(my_button_t* b) {
  my_widget_t* w = (my_widget_t*)b;
  my_color_t fallback;
  switch (button_state(b)) {
    case MY_STATE_DISABLED:
      fallback = b->color_disabled;
      break;
    case MY_STATE_PRESSED:
      fallback = b->color_pressed;
      break;
    case MY_STATE_HOVER:
      fallback = b->color_hover;
      break;
    default:
      fallback = b->color_normal;
      break;
  }
  return my_color_from_rgba32(my_widget_style_get_color(
      w, button_state(b), "bg_color", my_color_to_rgba32(fallback)));
}

static void button_on_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_button_t* b = (my_button_t*)widget;
  uint32_t border = my_widget_style_get_color(
      widget, button_state(b), "border_color", 0x000000FFu);
  int32_t radius =
      my_widget_style_get_int(widget, button_state(b), "round_radius", 4);
  my_vgcanvas_set_fill_color(vg, button_state_color(b));
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                                 (float)widget->rect.h},
                                (float)radius);
  my_vgcanvas_set_stroke_color(vg, my_color_from_rgba32(border));
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                            (float)widget->rect.h});
  /* text: real draw_text when a font is set on the backend (M7a) */
  if (b->text != NULL) {
    int32_t tw = 0, th = 0;
    int32_t font_size =
        my_widget_style_get_int(widget, button_state(b), "font_size", 14);
    my_vgcanvas_set_font(vg, NULL, font_size);
    if (my_vgcanvas_measure_text(vg, b->text, &tw, &th) == MY_RET_OK) {
      uint32_t fg = my_widget_style_get_color(widget, button_state(b),
                                              "fg_color", 0x212121FFu);
      my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(fg));
      my_vgcanvas_draw_text(vg, b->text,
                            ((float)widget->rect.w - (float)tw) / 2.0f,
                            ((float)widget->rect.h - (float)th) / 2.0f);
    } else {
      my_vgcanvas_draw_text(vg, b->text, 0, 0); /* placeholder, ignored */
    }
  }
}

static bool button_point_inside(my_widget_t* widget, int32_t gx, int32_t gy) {
  int32_t lx = gx, ly = gy;
  my_widget_global_to_local(widget, &lx, &ly);
  return lx >= 0 && ly >= 0 && lx < widget->rect.w && ly < widget->rect.h;
}

/* keep the pressed visual visible at least this long; with frame
 * coalescing a quick click would otherwise never paint it */
#define BUTTON_PRESS_MIN_MS 120

static my_ret_t button_release_cb(void* ctx) {
  my_button_t* b = (my_button_t*)ctx;
  b->release_timer = 0;
  b->pressed = false;
  my_widget_invalidate((my_widget_t*)b, NULL);
  return MY_RET_FAIL; /* one-shot */
}

static void button_release(my_button_t* b) {
  my_widget_t* w = (my_widget_t*)b;
  my_pal_t* pal = my_window_pal_of_widget(w);
  my_pal_main_loop_t* loop = my_window_loop_of_widget(w);
  uint64_t now = pal != NULL ? my_pal_time_now_ms(pal) : 0;
  if (loop != NULL && pal != NULL && now - b->down_ms < BUTTON_PRESS_MIN_MS) {
    if (b->release_timer == 0) {
      b->release_timer = my_pal_main_loop_add_timer(
          loop, button_release_cb, b,
          (uint32_t)(BUTTON_PRESS_MIN_MS - (now - b->down_ms)));
    }
    return;
  }
  b->pressed = false;
}

static my_ret_t button_on_event(my_widget_t* widget, const my_event_t* event) {
  my_button_t* b = (my_button_t*)widget;
  switch (event->type) {
    case MY_EVENT_POINTER_DOWN: {
      my_pal_t* pal;
      if (b->release_timer != 0) { /* a fresh press cancels a pending release */
        my_pal_main_loop_t* loop = my_window_loop_of_widget(widget);
        if (loop != NULL) {
          my_pal_main_loop_remove_timer(loop, b->release_timer);
        }
        b->release_timer = 0;
      }
      pal = my_window_pal_of_widget(widget);
      b->down_ms = pal != NULL ? my_pal_time_now_ms(pal) : 0;
      b->pressed = true;
      my_widget_invalidate(widget, NULL);
      return MY_RET_OK;
    }
    case MY_EVENT_POINTER_UP:
      if (!b->pressed) {
        return MY_RET_FAIL;
      }
      button_release(b);
      my_widget_invalidate(widget, NULL);
      if (button_point_inside(widget, event->u.pointer.x, event->u.pointer.y)) {
        my_emitter_emit(widget->emitter, "click", (void*)event);
      }
      return MY_RET_OK;
    default:
      return MY_RET_FAIL;
  }
}

static const my_widget_vtable_t s_button_vtable = {button_on_paint,
                                                   button_on_event, NULL};

static void button_destroy_chain(my_object_t* obj) {
  my_button_t* b = (my_button_t*)obj;
  if (b->release_timer != 0) {
    my_pal_main_loop_t* loop = my_window_loop_of_widget((my_widget_t*)b);
    if (loop != NULL) {
      my_pal_main_loop_remove_timer(loop, b->release_timer);
    }
    b->release_timer = 0;
  }
  my_mem_free(obj->allocator, b->text);
  my_widget_destroy((my_widget_t*)b);
  my_object_destroy(obj);
}

my_widget_t* my_button_create(const my_allocator_t* allocator, const char* text) {
  my_button_t* b = (my_button_t*)my_mem_calloc(allocator, 1, sizeof(my_button_t));
  if (b == NULL) {
    return NULL;
  }
  if (my_widget_init((my_widget_t*)b, allocator, &s_button_vtable, "button") !=
      MY_RET_OK) {
    my_mem_free(allocator, b);
    return NULL;
  }
  ((my_object_t*)b)->destroy = button_destroy_chain;
  if (text != NULL) {
    b->text = my_strdup(allocator, text);
    if (b->text == NULL) {
      my_object_unref((my_object_t*)b);
      return NULL;
    }
  }
  b->color_normal = my_color_rgb(200, 200, 200);
  b->color_hover = my_color_rgb(220, 220, 230);
  b->color_pressed = my_color_rgb(150, 150, 160);
  b->color_disabled = my_color_rgb(120, 120, 120);
  ((my_widget_t*)b)->focusable = true;
  ((my_widget_t*)b)->widget_type = "button";
  return (my_widget_t*)b;
}

my_ret_t my_button_set_text(my_widget_t* button, const char* text) {
  my_button_t* b = (my_button_t*)button;
  char* copy;
  if (button == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  copy = my_strdup(((my_object_t*)button)->allocator, text);
  if (text != NULL && copy == NULL) {
    return MY_RET_OOM;
  }
  my_mem_free(((my_object_t*)button)->allocator, b->text);
  b->text = copy;
  my_widget_invalidate(button, NULL);
  return MY_RET_OK;
}
