/**
 * @file my_controls_test.c
 * @brief Unit tests for checkbox / slider / progress_bar (M7d).
 */
#include "myui/widgets/my_checkbox.h"
#include "myui/widgets/my_progress_bar.h"
#include "myui/widgets/my_slider.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_event_t pointer_ev(my_event_type_t type, int32_t x, int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  e.u.pointer.button = 1;
  return e;
}

static void on_hit(void* ctx, const char* event, void* data) {
  int* n = (int*)ctx;
  (void)event;
  (void)data;
  (*n)++;
}

/* ---------------- checkbox ---------------- */

static void test_checkbox_toggle_and_mixed(void) {
  my_widget_t* cb = my_checkbox_create(NULL, "enable");
  my_checkbox_t* c = (my_checkbox_t*)cb;
  int changed = 0;
  my_event_t e;

  my_widget_set_rect(cb, &(my_rect_t){10, 10, 100, 24});
  my_widget_on(cb, "changed", on_hit, &changed);
  TEST_ASSERT(!my_checkbox_get_checked(cb));

  e = pointer_ev(MY_EVENT_POINTER_DOWN, 15, 20);
  TEST_ASSERT_EQ_INT(cb->vtable->on_event(cb, &e), MY_RET_OK);
  e = pointer_ev(MY_EVENT_POINTER_UP, 15, 20);
  cb->vtable->on_event(cb, &e);
  TEST_ASSERT(my_checkbox_get_checked(cb));
  TEST_ASSERT_EQ_INT(changed, 1);

  my_checkbox_set_mixed(cb, true);
  TEST_ASSERT(c->mixed);
  /* toggle from mixed: goes to unchecked (was checked) and clears mixed */
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 15, 20);
  cb->vtable->on_event(cb, &e);
  e = pointer_ev(MY_EVENT_POINTER_UP, 15, 20);
  cb->vtable->on_event(cb, &e);
  TEST_ASSERT(!c->mixed);
  TEST_ASSERT(!my_checkbox_get_checked(cb));

  my_widget_unref(cb);
}

static void test_checkbox_paint(void) {
  my_widget_t* cb = my_checkbox_create(NULL, "ok");
  rec_vg_t rec;
  rec_vg_init(&rec);

  my_widget_set_rect(cb, &(my_rect_t){0, 0, 120, 24});
  my_checkbox_set_checked(cb, true);
  my_widget_paint(cb, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "rounded_rect")); /* box */
  TEST_ASSERT(rec_has(&rec, "stroke"));       /* check mark path */

  my_widget_unref(cb);
}

/* ---------------- slider ---------------- */

static void test_slider_drag_and_clamp(void) {
  my_widget_t* sl = my_slider_create(NULL);
  my_event_t e;
  int changed = 0;

  my_widget_set_rect(sl, &(my_rect_t){0, 0, 116, 24}); /* inner 100px */
  my_widget_on(sl, "changed", on_hit, &changed);

  /* drag to the middle of the inner range */
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 58, 12);
  sl->vtable->on_event(sl, &e);
  TEST_ASSERT(my_slider_get_value(sl) > 40.0f &&
              my_slider_get_value(sl) < 60.0f);
  TEST_ASSERT(changed > 0);

  /* drag far right beyond the widget: clamps to max */
  e = pointer_ev(MY_EVENT_POINTER_MOVE, 500, 12);
  sl->vtable->on_event(sl, &e);
  TEST_ASSERT(my_slider_get_value(sl) == 100.0f);

  e = pointer_ev(MY_EVENT_POINTER_MOVE, -50, 12);
  sl->vtable->on_event(sl, &e);
  TEST_ASSERT(my_slider_get_value(sl) == 0.0f);

  e = pointer_ev(MY_EVENT_POINTER_UP, 0, 12);
  sl->vtable->on_event(sl, &e);

  /* move without grab: ignored */
  e = pointer_ev(MY_EVENT_POINTER_MOVE, 58, 12);
  TEST_ASSERT_EQ_INT(sl->vtable->on_event(sl, &e), MY_RET_FAIL);

  my_widget_unref(sl);
}

static void test_slider_range_and_step(void) {
  my_widget_t* sl = my_slider_create(NULL);
  my_event_t e;

  my_widget_set_rect(sl, &(my_rect_t){0, 0, 116, 24});
  my_slider_set_range(sl, 10, 20);
  my_slider_set_step(sl, 2.5f);

  e = pointer_ev(MY_EVENT_POINTER_DOWN, 58, 12); /* ~15 -> snaps to 15.0 */
  sl->vtable->on_event(sl, &e);
  TEST_ASSERT(my_slider_get_value(sl) >= 10.0f);
  {
    float v = my_slider_get_value(sl);
    float steps = (v - 10.0f) / 2.5f;
    TEST_ASSERT(steps - (float)(int)(steps + 0.5f) < 0.01f &&
                steps - (float)(int)(steps + 0.5f) > -0.01f);
  }

  my_slider_set_value(sl, 99.0f); /* clamps to max on set */
  TEST_ASSERT(my_slider_get_value(sl) == 20.0f);
  TEST_ASSERT_EQ_INT(my_slider_set_range(sl, 5, 5), MY_RET_INVALID_PARAMS);

  my_widget_unref(sl);
}

/* ---------------- progress bar ---------------- */

static void test_progress_clamp_and_paint(void) {
  my_widget_t* pb = my_progress_bar_create(NULL);
  rec_vg_t rec;

  my_widget_set_rect(pb, &(my_rect_t){0, 0, 100, 10});
  my_progress_bar_set_value(pb, 150.0f);
  TEST_ASSERT(my_progress_bar_get_value(pb) == 100.0f);
  my_progress_bar_set_value(pb, -5.0f);
  TEST_ASSERT(my_progress_bar_get_value(pb) == 0.0f);

  rec_vg_init(&rec);
  my_progress_bar_set_value(pb, 50.0f);
  my_widget_paint(pb, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "rounded_rect"), 2); /* bg + fill */
  TEST_ASSERT(rec_has(&rec, "clip 0 0 50 10"));           /* fill clipped */

  rec_vg_init(&rec);
  my_progress_bar_set_value(pb, 0.0f);
  my_widget_paint(pb, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "rounded_rect"), 1); /* bg only */

  my_widget_unref(pb);
}

/* ---------------- leak check ---------------- */

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* cb = my_checkbox_create(dbg, "x");
  my_widget_t* sl = my_slider_create(dbg);
  my_widget_t* pb = my_progress_bar_create(dbg);
  int n = 0;
  my_event_t e;

  my_widget_set_rect(sl, &(my_rect_t){0, 0, 116, 24});
  my_widget_on(cb, "changed", on_hit, &n);
  my_widget_on(sl, "changed", on_hit, &n);
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 15, 10);
  cb->vtable->on_event(cb, &e);
  e = pointer_ev(MY_EVENT_POINTER_UP, 15, 10);
  cb->vtable->on_event(cb, &e);
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 58, 12);
  sl->vtable->on_event(sl, &e);
  my_progress_bar_set_value(pb, 33.0f);

  my_widget_unref(cb);
  my_widget_unref(sl);
  my_widget_unref(pb);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_checkbox_toggle_and_mixed);
  MYTEST_RUN(test_checkbox_paint);
  MYTEST_RUN(test_slider_drag_and_clamp);
  MYTEST_RUN(test_slider_range_and_step);
  MYTEST_RUN(test_progress_clamp_and_paint);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
