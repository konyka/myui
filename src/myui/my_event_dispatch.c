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
    case MY_EVENT_POINTER_WHEEL:
      return "pointer_wheel";
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

/** @brief Depth-first traversal: next focusable (visible+enable) after w,
 * wrapping around the tree. direction: +1 forward, -1 backward. */
static my_widget_t* focus_step(my_widget_t* root, my_widget_t* current,
                               int direction) {
  my_widget_t* found = NULL;
  my_widget_t* first = NULL;
  my_widget_t* prev = NULL;
  if (root == NULL) {
    return NULL;
  }
  /* collect-walk: preorder via recursion-free loop using child index state
   * kept on the stack of parent pointers (trees are small) */
  {
    /* simple recursive walk implemented iteratively with an explicit
     * state: we walk using parent/child links only */
    size_t i;
    my_widget_t* order[256];
    size_t count = 0;
    /* stack-based preorder */
    my_widget_t* stack[256];
    size_t sp = 0;
    stack[sp++] = root;
    while (sp > 0 && count < 256) {
      my_widget_t* n = stack[--sp];
      size_t c = my_widget_child_count(n);
      if (n->visible && n->enable && n->focusable) {
        order[count++] = n;
      }
      /* push children in reverse so pop order matches tree order */
      i = c;
      while (i > 0 && sp < 256) {
        stack[sp++] = my_widget_get_child(n, i - 1);
        i--;
      }
    }
    for (i = 0; i < count; i++) {
      if (first == NULL) {
        first = order[i];
      }
      if (order[i] == current) {
        if (direction > 0) {
          found = i + 1 < count ? order[i + 1] : NULL;
        } else {
          found = prev;
        }
        break;
      }
      prev = order[i];
    }
    if (found == NULL) {
      /* wrap: forward -> first, backward -> last */
      if (count > 0) {
        found = direction > 0 ? first : order[count - 1];
      }
    }
  }
  return found;
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

void my_event_dispatcher_set_focus(my_event_dispatcher_t* dispatcher,
                                   my_widget_t* widget) {
  if (dispatcher != NULL) {
    set_focus(dispatcher, nearest_focusable(widget));
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
    case MY_EVENT_POINTER_WHEEL:
      target = my_widget_hit_test(dispatcher->root, event->u.pointer.x,
                                  event->u.pointer.y);
      return target != NULL ? deliver(target, event) : false;
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
      if (event->u.key.key == MY_KEY_TAB) {
        bool consumed = dispatcher->focused != NULL
                            ? deliver(dispatcher->focused, event)
                            : false;
        if (!consumed) {
          int dir = (event->u.key.modifiers & MY_KEYMOD_SHIFT) != 0 ? -1 : 1;
          my_widget_t* next = focus_step(dispatcher->root, dispatcher->focused,
                                         dir);
          if (next != NULL) {
            set_focus(dispatcher, next);
            return true;
          }
        }
        return consumed;
      }
      return dispatcher->focused != NULL ? deliver(dispatcher->focused, event)
                                         : false;
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
