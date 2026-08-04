/**
 * @file my_widget_target.c
 * @brief Widget <-> binding target adapter.
 */
#include "mymvvm_myui/my_widget_target.h"

#include <string.h>

#include "myc/my_str.h"
#include "mymvvm_myui/my_mvvm.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_checkbox.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_progress_bar.h"
#include "myui/widgets/my_slider.h"

/* ---------------- properties ---------------- */

static my_ret_t target_set_prop(my_binding_target_t* t, const char* name,
                                const my_value_t* v) {
  my_widget_target_t* wt = (my_widget_target_t*)t;
  my_widget_t* w = wt->widget;
  if (strcmp(name, "text") == 0) {
    const char* s = my_value_get_str(v);
    if (my_str_eq(w->widget_type, "button")) {
      return my_button_set_text(w, s);
    }
    if (my_str_eq(w->widget_type, "label")) {
      return my_label_set_text(w, s);
    }
    if (my_str_eq(w->widget_type, "edit")) {
      return my_edit_set_text(w, s);
    }
    return MY_RET_NOT_SUPPORTED;
  }
  if (strcmp(name, "hint") == 0) {
    if (my_str_eq(w->widget_type, "edit")) {
      return my_edit_set_hint(w, my_value_get_str(v));
    }
    return MY_RET_NOT_SUPPORTED;
  }
  if (strcmp(name, "value") == 0) {
    if (my_str_eq(w->widget_type, "checkbox")) {
      bool b = v->type == MY_VALUE_BOOL ? my_value_get_bool(v) : false;
      return my_checkbox_set_checked(w, b);
    }
    if (my_str_eq(w->widget_type, "slider")) {
      double d = v->type == MY_VALUE_DOUBLE ? my_value_get_double(v)
                 : v->type == MY_VALUE_FLOAT ? (double)my_value_get_float(v)
                 : v->type == MY_VALUE_INT32 ? (double)my_value_get_int32(v)
                                             : 0.0;
      return my_slider_set_value(w, (float)d);
    }
    if (my_str_eq(w->widget_type, "progress_bar")) {
      double d = v->type == MY_VALUE_DOUBLE ? my_value_get_double(v)
                 : v->type == MY_VALUE_FLOAT ? (double)my_value_get_float(v)
                 : v->type == MY_VALUE_INT32 ? (double)my_value_get_int32(v)
                                             : 0.0;
      return my_progress_bar_set_value(w, (float)d);
    }
  }
  if (strcmp(name, "visible") == 0) {
    return my_widget_set_visible(w, v->type == MY_VALUE_BOOL
                                      ? my_value_get_bool(v)
                                      : true);
  }
  if (strcmp(name, "enable") == 0) {
    if (v->type == MY_VALUE_BOOL) {
      w->enable = my_value_get_bool(v);
      my_widget_invalidate(w, NULL);
    }
    return MY_RET_OK;
  }
  if (strcmp(name, "value") == 0) {
    my_value_reset(&wt->value);
    my_value_init(&wt->value, wt->allocator);
    return my_value_copy(&wt->value, v);
  }
  if (strlen(name) == 1 && strchr("xywh", name[0]) != NULL &&
      v->type == MY_VALUE_INT32) {
    my_rect_t r = w->rect;
    int32_t n = my_value_get_int32(v);
    if (name[0] == 'x') {
      r.x = n;
    } else if (name[0] == 'y') {
      r.y = n;
    } else if (name[0] == 'w') {
      r.w = n;
    } else {
      r.h = n;
    }
    return my_widget_set_rect(w, &r);
  }
  return MY_RET_NOT_SUPPORTED;
}

static my_ret_t target_get_prop(my_binding_target_t* t, const char* name,
                                my_value_t* v) {
  my_widget_target_t* wt = (my_widget_target_t*)t;
  my_widget_t* w = wt->widget;
  if (strcmp(name, "text") == 0) {
    const char* s = NULL;
    if (my_str_eq(w->widget_type, "button")) {
      s = ((my_button_t*)w)->text;
    } else if (my_str_eq(w->widget_type, "label")) {
      s = ((my_label_t*)w)->text;
    } else if (my_str_eq(w->widget_type, "edit")) {
      s = my_edit_get_text(w);
    }
    return my_value_set_str(v, s);
  }
  if (strcmp(name, "visible") == 0) {
    return my_value_set_bool(v, w->visible);
  }
  if (strcmp(name, "enable") == 0) {
    return my_value_set_bool(v, w->enable);
  }
  if (strcmp(name, "value") == 0) {
    if (my_str_eq(w->widget_type, "checkbox")) {
      return my_value_set_bool(v, my_checkbox_get_checked(w));
    }
    if (my_str_eq(w->widget_type, "slider")) {
      return my_value_set_double(v, (double)my_slider_get_value(w));
    }
    if (my_str_eq(w->widget_type, "progress_bar")) {
      return my_value_set_double(v, (double)my_progress_bar_get_value(w));
    }
    return my_value_copy(v, &wt->value);
  }
  if (strlen(name) == 1 && strchr("xywh", name[0]) != NULL) {
    int32_t n = name[0] == 'x'   ? w->rect.x
                : name[0] == 'y' ? w->rect.y
                : name[0] == 'w' ? w->rect.w
                                 : w->rect.h;
    return my_value_set_int32(v, n);
  }
  return MY_RET_NOT_SUPPORTED;
}

/* ---------------- events ---------------- */

static uint32_t target_on_event(my_binding_target_t* t, const char* event,
                                my_event_callback_t cb, void* ctx) {
  return my_widget_on(((my_widget_target_t*)t)->widget, event, cb, ctx);
}

static my_ret_t target_off_event(my_binding_target_t* t, uint32_t id) {
  return my_widget_off(((my_widget_target_t*)t)->widget, id);
}

/* ---------------- items ---------------- */

static my_ret_t target_rebuild_items(my_binding_target_t* t,
                                     const char* item_template, size_t count,
                                     my_item_props_fn_t props,
                                     void* props_ctx) {
  my_widget_target_t* wt = (my_widget_target_t*)t;
  my_widget_t* container = wt->widget;
  size_t i;
  const my_item_template_t* tmpl = my_mvvm_find_template(item_template);

  while (my_widget_child_count(container) > 0) {
    my_widget_remove_child(container, my_widget_get_child(container, 0));
  }
  if (tmpl == NULL) {
    return MY_RET_NOT_FOUND;
  }
  for (i = 0; i < count; i++) {
    my_widget_t* child = tmpl->build(container, i, props, props_ctx, tmpl->ctx);
    if (child != NULL) {
      my_widget_add_child(container, child);
      my_widget_unref(child);
    }
  }
  my_widget_invalidate(container, NULL);
  return MY_RET_OK;
}

/* ---------------- lifecycle ---------------- */

static const my_binding_target_vtable_t WIDGET_TARGET_VTABLE = {
    target_set_prop, target_get_prop, target_on_event, target_off_event,
    target_rebuild_items};

my_widget_target_t* my_widget_target_create(const my_allocator_t* allocator,
                                            my_widget_t* widget) {
  my_widget_target_t* wt;
  if (widget == NULL) {
    return NULL;
  }
  wt = (my_widget_target_t*)my_mem_calloc(allocator, 1,
                                          sizeof(my_widget_target_t));
  if (wt == NULL) {
    return NULL;
  }
  wt->base.vtable = &WIDGET_TARGET_VTABLE;
  wt->allocator = allocator;
  wt->widget = widget;
  my_value_init(&wt->value, allocator);
  return wt;
}

void my_widget_target_destroy(my_widget_target_t* target) {
  if (target != NULL) {
    my_value_reset(&target->value);
    my_mem_free(target->allocator, target);
  }
}
