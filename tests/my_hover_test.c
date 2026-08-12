/**
 * @file my_hover_test.c
 * @brief Hover tracking tests (M14a): hover_enter/leave sequence, nested
 * widgets, MY_STATE_HOVER style switching, pressed-over-hover priority.
 */
#include "myui/my_event_dispatch.h"
#include "myui/widgets/my_button.h"

#include <string.h>

#include "mytest.h"
#include "rec_vgcanvas.h"

static char g_log[256];
static void log_cb(void* ctx, const char* event, void* data) {
  const char* tag = (const char*)ctx;
  (void)data;
  strcat(g_log, tag);
  strcat(g_log, ":");
  strcat(g_log, event);
  strcat(g_log, " ");
}

/** @brief Whether g_log ends with `suffix`. */
static bool log_ends_with(const char* suffix) {
  size_t a = strlen(g_log), b = strlen(suffix);
  return a >= b && strcmp(g_log + a - b, suffix) == 0;
}

typedef struct fx_t {
  my_widget_t* root;
  my_event_dispatcher_t d;
  my_widget_t* btn_a;
  my_widget_t* btn_b;
} fx_t;

static void fx_init(fx_t* f) {
  f->root = my_widget_create(NULL, "root");
  my_widget_set_rect(f->root, &(my_rect_t){0, 0, 400, 300});
  f->btn_a = my_button_create(NULL, "a");
  my_widget_set_rect(f->btn_a, &(my_rect_t){10, 10, 100, 40});
  f->btn_b = my_button_create(NULL, "b");
  my_widget_set_rect(f->btn_b, &(my_rect_t){200, 10, 100, 40});
  my_widget_add_child(f->root, f->btn_a);
  my_widget_add_child(f->root, f->btn_b);
  my_widget_unref(f->btn_a);
  my_widget_unref(f->btn_b);
  my_event_dispatcher_init(&f->d, f->root);
  g_log[0] = '\0';
  my_widget_on(f->btn_a, "hover_enter", log_cb, "A");
  my_widget_on(f->btn_a, "hover_leave", log_cb, "A");
  my_widget_on(f->btn_b, "hover_enter", log_cb, "B");
  my_widget_on(f->btn_b, "hover_leave", log_cb, "B");
}

static void fx_move(fx_t* f, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_event_dispatch(&f->d, &e);
}

static void test_enter_leave_sequence(void) {
  fx_t f;
  fx_init(&f);
  fx_move(&f, 20, 20); /* onto A */
  TEST_ASSERT(f.btn_a->hovered);
  TEST_ASSERT_EQ_STR(g_log, "A:hover_enter ");
  fx_move(&f, 220, 20); /* A -> B */
  TEST_ASSERT(!f.btn_a->hovered);
  TEST_ASSERT(f.btn_b->hovered);
  TEST_ASSERT_EQ_STR(g_log, "A:hover_enter A:hover_leave B:hover_enter ");
  fx_move(&f, 500, 400); /* outside the root: hover clears */
  TEST_ASSERT(!f.btn_b->hovered);
  TEST_ASSERT(log_ends_with("B:hover_leave "));
  /* re-entering fires again */
  fx_move(&f, 20, 20);
  TEST_ASSERT(log_ends_with("A:hover_enter "));
  my_widget_unref(f.root);
}

static void test_nested_widgets(void) {
  fx_t f;
  fx_init(&f);
  g_log[0] = '\0';
  /* move inside A but on no sub-child: hover = A; then to the root's
   * empty area: hover climbs to the root (hit_test returns root) */
  fx_move(&f, 20, 20);
  TEST_ASSERT(f.d.hovered == f.btn_a);
  fx_move(&f, 150, 150); /* root area, no child */
  TEST_ASSERT(!f.btn_a->hovered);
  TEST_ASSERT(f.d.hovered == f.root);
  TEST_ASSERT(f.root->hovered);
  my_widget_unref(f.root);
}

/** @brief Paint A and return whether the hover bg color op is present. */
static bool painted_with(fx_t* f, const char* needle) {
  rec_vg_t rec;
  rec_vg_init(&rec);
  my_widget_paint(f->btn_a, (my_vgcanvas_t*)&rec);
  return rec_has(&rec, needle);
}

static void test_hover_style_slot(void) {
  fx_t f;
  my_value_t v;
  fx_init(&f);
  /* local override: hover bg = #112233 */
  my_value_init(&v, NULL);
  my_value_set_uint32(&v, 0x112233FFu);
  my_widget_style_set(f.btn_a, MY_STATE_HOVER, "bg_color", &v);
  my_value_reset(&v);
  TEST_ASSERT(!painted_with(&f, "set_fill #112233")); /* normal */
  fx_move(&f, 20, 20);
  TEST_ASSERT(painted_with(&f, "set_fill #112233")); /* hover slot live */
  fx_move(&f, 500, 400);
  TEST_ASSERT(!painted_with(&f, "set_fill #112233")); /* back to normal */
  my_widget_unref(f.root);
}

static void test_pressed_beats_hover(void) {
  fx_t f;
  my_event_t e;
  rec_vg_t rec;
  fx_init(&f);
  /* hover A, then press it: pressed color wins over hover */
  fx_move(&f, 20, 20);
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 20;
  e.u.pointer.y = 20;
  e.u.pointer.button = 1;
  my_event_dispatch(&f.d, &e);
  TEST_ASSERT(f.btn_a->hovered);
  TEST_ASSERT(((my_button_t*)f.btn_a)->pressed);
  rec_vg_init(&rec);
  my_widget_paint(f.btn_a, (my_vgcanvas_t*)&rec);
  /* built-in fallbacks (no theme here): pressed #9696a0, hover #dcdce6 */
  TEST_ASSERT(rec_has(&rec, "set_fill #9696a0"));
  TEST_ASSERT(!rec_has(&rec, "set_fill #dcdce6"));
  /* drag off A while grabbed: hover stays on the grabbed widget, the
   * button stays pressed */
  fx_move(&f, 220, 20);
  TEST_ASSERT(f.btn_a->hovered);
  TEST_ASSERT(!f.btn_b->hovered);
  /* release over B: grab ends, hover re-hits to B */
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = 220;
  e.u.pointer.y = 20;
  my_event_dispatch(&f.d, &e);
  TEST_ASSERT(!((my_button_t*)f.btn_a)->pressed);
  TEST_ASSERT(!f.btn_a->hovered);
  TEST_ASSERT(f.btn_b->hovered);
  my_widget_unref(f.root);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_enter_leave_sequence);
  MYTEST_RUN(test_nested_widgets);
  MYTEST_RUN(test_hover_style_slot);
  MYTEST_RUN(test_pressed_beats_hover);
MYTEST_MAIN_END()
