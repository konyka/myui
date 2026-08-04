/**
 * @file my_event_dispatch.c
 * @brief Dispatch PAL events into a widget tree.
 */
#include "myui/my_event_dispatch.h"

void my_event_dispatcher_init(my_event_dispatcher_t* dispatcher,
                              my_widget_t* root) {
  if (dispatcher != NULL) {
    dispatcher->root = root;
    dispatcher->grabbed = NULL;
    dispatcher->focused = NULL;
  }
}

static const char* event_name_of(my_event_type_t type) {
  switch (type) {
    case MY_EVENT_POINTER_DOWN:
      return "pointer_down";
    case MY_EVENT_POINTER_MOVE:
      return "pointer_move";
    case MY_EVENT_POINTER_UP:
      return "pointer_up";
    case MY_EVENT_KEY_DOWN:
      return "key_down";
    case MY_EVENT_KEY_UP:
      return "key_up";
    default:
      return "unknown";
  }
}

/** @brief Deliver to target, then bubble to parents until consumed. */
static bool deliver(my_widget_t* target, const my_event_t* event) {
  my_widget_t* w = target;
  bool consumed = false;
  while (w != NULL) {
    if (w->enable) {
      if (w->vtable != NULL && w->vtable->on_event != NULL &&
          w->vtable->on_event(w, event) == MY_RET_OK) {
        consumed = true;
      }
      my_emitter_emit(w->emitter, event_name_of(event->type), (void*)event);
      if (consumed) {
        return true;
      }
    }
    w = w->parent;
  }
  return false;
}

/** @brief Nearest focusable widget at or above w (NULL when none). */
static my_widget_t* nearest_focusable(my_widget_t* w) {
  while (w != NULL && !w->focusable) {
    w = w->parent;
  }
  return w;
}

/** @brief Switch key focus, emitting "blur"/"focus" on the widgets. */
static void set_focus(my_event_dispatcher_t* d, my_widget_t* widget) {
  if (d->focused == widget) {
    return;
  }
  if (d->focused != NULL) {
    my_emitter_emit(d->focused->emitter, "blur", NULL);
  }
  d->focused = widget;
  if (widget != NULL) {
    my_emitter_emit(widget->emitter, "focus", NULL);
  }
}

bool my_event_dispatch(my_event_dispatcher_t* dispatcher,
                       const my_event_t* event) {
  my_widget_t* target;
  if (dispatcher == NULL || event == NULL || dispatcher->root == NULL) {
    return false;
  }

  switch (event->type) {
    case MY_EVENT_POINTER_DOWN:
      target = my_widget_hit_test(dispatcher->root, event->u.pointer.x,
                                  event->u.pointer.y);
      dispatcher->grabbed = target;
      if (target != NULL) {
        set_focus(dispatcher, nearest_focusable(target));
        return deliver(target, event);
      }
      set_focus(dispatcher, NULL); /* click on empty space: blur */
      return false;
    case MY_EVENT_POINTER_MOVE:
      target = dispatcher->grabbed;
      if (target == NULL) {
        target = my_widget_hit_test(dispatcher->root, event->u.pointer.x,
                                    event->u.pointer.y);
      }
      return target != NULL ? deliver(target, event) : false;
    case MY_EVENT_POINTER_UP:
      target = dispatcher->grabbed;
      if (target == NULL) {
        target = my_widget_hit_test(dispatcher->root, event->u.pointer.x,
                                    event->u.pointer.y);
      }
      dispatcher->grabbed = NULL;
      return target != NULL ? deliver(target, event) : false;
    case MY_EVENT_KEY_DOWN:
    case MY_EVENT_KEY_UP:
      return dispatcher->focused != NULL ? deliver(dispatcher->focused, event)
                                         : false;
    default:
      return false;
  }
}

static bool is_self_or_descendant(my_widget_t* w, my_widget_t* ancestor) {
  my_widget_t* p = w;
  while (p != NULL) {
    if (p == ancestor) {
      return true;
    }
    p = p->parent;
  }
  return false;
}

void my_event_dispatcher_forget(my_event_dispatcher_t* dispatcher,
                                my_widget_t* widget) {
  if (dispatcher == NULL || widget == NULL) {
    return;
  }
  if (dispatcher->grabbed != NULL &&
      is_self_or_descendant(dispatcher->grabbed, widget)) {
    dispatcher->grabbed = NULL;
  }
  if (dispatcher->focused != NULL &&
      is_self_or_descendant(dispatcher->focused, widget)) {
    dispatcher->focused = NULL;
  }
}
