/**
 * @file my_event_dispatch_test.c
 * @brief Unit tests for event dispatch: hit, consume, bubble, grab, focus.
 */
#include "myui/my_event_dispatch.h"

#include "mytest.h"

typedef struct probe_t {
  int down;
  int move;
  int up;
  int key;
  my_ret_t consume; /**< returned from on_event */
} probe_t;

/* probe stored in a static registry keyed by widget pointer */
#define MAX_PROBES 8
static my_widget_t* g_probe_widget[MAX_PROBES];
static probe_t* g_probe_data[MAX_PROBES];
static int g_probe_count = 0;

static probe_t* probe_of(my_widget_t* widget) {
  int i;
  for (i = 0; i < g_probe_count; i++) {
    if (g_probe_widget[i] == widget) {
      return g_probe_data[i];
    }
  }
  return NULL;
}

static my_ret_t on_event_probe(my_widget_t* widget, const my_event_t* event) {
  probe_t* p = probe_of(widget);
  if (p == NULL) {
    return MY_RET_FAIL;
  }
  switch (event->type) {
    case MY_EVENT_POINTER_DOWN:
      p->down++;
      break;
    case MY_EVENT_POINTER_MOVE:
      p->move++;
      break;
    case MY_EVENT_POINTER_UP:
      p->up++;
      break;
    case MY_EVENT_KEY_DOWN:
      p->key++;
      break;
    default:
      break;
  }
  return p->consume;
}

static const my_widget_vtable_t PROBE_VTABLE = {NULL, on_event_probe, NULL};

static my_widget_t* probe_widget(my_widget_t* parent, probe_t* probe, int32_t x,
                                 int32_t y, int32_t w, int32_t h) {
  my_widget_t* widget = my_widget_create(NULL, "probe");
  widget->vtable = &PROBE_VTABLE;
  my_widget_set_rect(widget, &(my_rect_t){x, y, w, h});
  if (parent != NULL) {
    my_widget_add_child(parent, widget);
    my_widget_unref(widget);
  }
  g_probe_widget[g_probe_count] = widget;
  g_probe_data[g_probe_count] = probe;
  g_probe_count++;
  return widget;
}

static my_event_t pointer_ev(my_event_type_t type, int32_t x, int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  e.u.pointer.button = 1;
  return e;
}

static void reset_probes(void) {
  g_probe_count = 0;
}

static void test_hit_and_consume(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  probe_t pa = {0, 0, 0, 0, MY_RET_FAIL};
  probe_t pb = {0, 0, 0, 0, MY_RET_OK}; /* consumes */
  my_event_t e;
  my_event_dispatcher_t d;

  reset_probes();
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  root->vtable = &PROBE_VTABLE;
  g_probe_widget[g_probe_count] = root;
  g_probe_data[g_probe_count] = &pa;
  g_probe_count++;
  probe_widget(root, &pb, 10, 10, 30, 30);

  my_event_dispatcher_init(&d, root);

  /* b consumes: root never sees it */
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 20, 20);
  TEST_ASSERT(my_event_dispatch(&d, &e));
  TEST_ASSERT_EQ_INT(pb.down, 1);
  TEST_ASSERT_EQ_INT(pa.down, 0);

  my_widget_unref(root);
}

static void test_bubble_when_not_consumed(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  probe_t proot = {0, 0, 0, 0, MY_RET_FAIL};
  probe_t pchild = {0, 0, 0, 0, MY_RET_FAIL};
  my_event_t e;
  my_event_dispatcher_t d;

  reset_probes();
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  root->vtable = &PROBE_VTABLE;
  g_probe_widget[g_probe_count] = root;
  g_probe_data[g_probe_count] = &proot;
  g_probe_count++;
  probe_widget(root, &pchild, 10, 10, 30, 30);

  my_event_dispatcher_init(&d, root);
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 20, 20);
  TEST_ASSERT(!my_event_dispatch(&d, &e)); /* nobody consumed */
  TEST_ASSERT_EQ_INT(pchild.down, 1);      /* child first */
  TEST_ASSERT_EQ_INT(proot.down, 1);       /* then bubbles to root */

  my_widget_unref(root);
}

static void test_grab_drag(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  probe_t pa = {0, 0, 0, 0, MY_RET_FAIL};
  probe_t pb = {0, 0, 0, 0, MY_RET_FAIL};
  my_event_t e;
  my_event_dispatcher_t d;

  reset_probes();
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  probe_widget(root, &pa, 0, 0, 40, 40);   /* left */
  probe_widget(root, &pb, 60, 60, 40, 40); /* right */

  my_event_dispatcher_init(&d, root);

  e = pointer_ev(MY_EVENT_POINTER_DOWN, 10, 10); /* grab a */
  my_event_dispatch(&d, &e);
  TEST_ASSERT_EQ_INT(pa.down, 1);
  TEST_ASSERT(d.grabbed != NULL);

  /* move over b: still delivered to a (grabbed) */
  e = pointer_ev(MY_EVENT_POINTER_MOVE, 70, 70);
  my_event_dispatch(&d, &e);
  TEST_ASSERT_EQ_INT(pa.move, 1);
  TEST_ASSERT_EQ_INT(pb.move, 0);

  e = pointer_ev(MY_EVENT_POINTER_UP, 70, 70);
  my_event_dispatch(&d, &e);
  TEST_ASSERT_EQ_INT(pa.up, 1);
  TEST_ASSERT(d.grabbed == NULL);

  /* after release: move goes to the hit widget */
  e = pointer_ev(MY_EVENT_POINTER_MOVE, 70, 70);
  my_event_dispatch(&d, &e);
  TEST_ASSERT_EQ_INT(pb.move, 1);

  my_widget_unref(root);
}

static void test_focus_and_key(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  probe_t pa = {0, 0, 0, 0, MY_RET_FAIL};
  my_widget_t* a;
  my_event_t e;
  my_event_dispatcher_t d;

  reset_probes();
  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  a = probe_widget(root, &pa, 0, 0, 40, 40);

  my_event_dispatcher_init(&d, root);

  /* not focusable: key goes nowhere */
  e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = 'a';
  TEST_ASSERT(!my_event_dispatch(&d, &e));

  a->focusable = true;
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 10, 10);
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == a);

  e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = 'a';
  TEST_ASSERT(!my_event_dispatch(&d, &e)); /* delivered but not consumed */
  TEST_ASSERT_EQ_INT(pa.key, 1);

  my_widget_unref(root);
}

static void on_emit_count(void* ctx, const char* event, void* data) {
  int* n = (int*)ctx;
  (void)event;
  (void)data;
  (*n)++;
}

static void test_emitter_convenience_via_dispatch(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* child = my_widget_create(NULL, "c");
  int clicks = 0;
  my_event_t e;
  my_event_dispatcher_t d;

  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  my_widget_set_rect(child, &(my_rect_t){0, 0, 50, 50});
  my_widget_add_child(root, child);
  my_widget_unref(child);

  my_widget_on(child, "pointer_down", on_emit_count, &clicks);

  my_event_dispatcher_init(&d, root);
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 10, 10);
  my_event_dispatch(&d, &e);
  TEST_ASSERT_EQ_INT(clicks, 1);

  my_widget_unref(root);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_hit_and_consume);
  MYTEST_RUN(test_bubble_when_not_consumed);
  MYTEST_RUN(test_grab_drag);
  MYTEST_RUN(test_focus_and_key);
  MYTEST_RUN(test_emitter_convenience_via_dispatch);
MYTEST_MAIN_END()
