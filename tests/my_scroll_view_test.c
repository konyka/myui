/**
 * @file my_scroll_view_test.c
 * @brief Scroll view tests (M14a): offset clamping, wheel step, scroll_bar
 * two-way sync, content offset/clip, flow auto height, leaks.
 */
#include "myui/widgets/my_scroll_view.h"

#include "myui/my_layout.h"
#include "myui/widgets/my_scroll_bar.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_scroll_view_t* make_sv(int32_t w, int32_t h) {
  my_scroll_view_t* sv = my_scroll_view_create(NULL);
  my_widget_set_rect((my_widget_t*)sv, &(my_rect_t){0, 0, w, h});
  return sv;
}

static void wheel(my_scroll_view_t* sv, int32_t delta) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_WHEEL);
  e.u.pointer.delta = delta;
  ((my_widget_t*)sv)->vtable->on_event((my_widget_t*)sv, &e);
}

static void test_offset_clamped(void) {
  my_scroll_view_t* sv = make_sv(200, 100);
  my_widget_t* content = my_widget_create(NULL, "content");
  my_scroll_view_set_content(sv, content);
  my_widget_unref(content);
  my_scroll_view_set_content_height(sv, 500); /* max = 400 */
  my_scroll_view_set_offset(sv, 999);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 400);
  my_scroll_view_set_offset(sv, -5);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 0);
  my_scroll_view_set_offset(sv, 120);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 120);
  /* content positioned at y = -offset, full width, content height */
  TEST_ASSERT_EQ_INT(my_scroll_view_get_content(sv)->rect.y, -120);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_content(sv)->rect.w, 200);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_content(sv)->rect.h, 500);
  my_widget_unref((my_widget_t*)sv);
}

static void test_wheel_steps(void) {
  my_scroll_view_t* sv = make_sv(200, 100);
  my_widget_t* content = my_widget_create(NULL, "content");
  my_scroll_view_set_content(sv, content);
  my_widget_unref(content);
  my_scroll_view_set_content_height(sv, 1000); /* max = 900 */
  wheel(sv, -1); /* down: +72 */
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 72);
  wheel(sv, -1);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 144);
  wheel(sv, 3); /* up 216 -> clamps at 0? no: 144-216 <0 -> 0 */
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 0);
  wheel(sv, -100); /* far past the end -> max */
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 900);
  my_widget_unref((my_widget_t*)sv);
}

static void test_scroll_bar_two_way_sync(void) {
  my_scroll_view_t* sv = make_sv(200, 100);
  my_widget_t* content = my_widget_create(NULL, "content");
  my_widget_t* bar = my_scroll_bar_create(NULL);
  my_scroll_view_set_content(sv, content);
  my_widget_unref(content);
  my_widget_set_rect(bar, &(my_rect_t){200, 0, 14, 100});
  my_scroll_view_set_content_height(sv, 500); /* max = 400 */
  my_scroll_view_set_scroll_bar(sv, bar);
  /* sv -> bar: page_size and value pushed */
  TEST_ASSERT(my_scroll_bar_get_page_size(bar) > 0.19f &&
              my_scroll_bar_get_page_size(bar) < 0.21f); /* 100/500 */
  my_scroll_view_set_offset(sv, 200);
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.49f &&
              my_scroll_bar_get_value(bar) < 0.51f);
  /* bar -> sv: simulate a user drag (public set_value does not notify) */
  ((my_scroll_bar_t*)bar)->value = 1.0f;
  my_emitter_emit(bar->emitter, "changed", NULL);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 400);
  my_widget_unref(bar);
  my_widget_unref((my_widget_t*)sv);
}

static void test_content_offset_and_clip(void) {
  my_scroll_view_t* sv = make_sv(200, 100);
  my_widget_t* content = my_widget_create(NULL, "content");
  my_widget_t* child = my_widget_create(NULL, "leaf");
  rec_vg_t rec;
  my_widget_set_rect(child, &(my_rect_t){0, 0, 50, 600});
  my_widget_add_child(content, child);
  my_widget_unref(child);
  my_scroll_view_set_content(sv, content);
  my_widget_unref(content);
  my_scroll_view_set_content_height(sv, 600);
  my_scroll_view_set_offset(sv, 250);
  rec_vg_init(&rec);
  my_widget_paint((my_widget_t*)sv, (my_vgcanvas_t*)&rec);
  /* the view clips to its own rect before painting children */
  TEST_ASSERT(rec_has(&rec, "clip 0 0 200 100"));
  /* content is translated to y=-250 (sv at 0,0: two nested translates) */
  TEST_ASSERT(rec_has(&rec, "translate 0 -250"));
  my_widget_unref((my_widget_t*)sv);
}

static void test_flow_measure_auto_height(void) {
  my_scroll_view_t* sv = make_sv(100, 50);
  my_widget_t* content = my_widget_create(NULL, "content");
  my_widget_t* a = my_widget_create(NULL, "i");
  my_widget_t* b = my_widget_create(NULL, "i");
  /* flow: two 100-wide items wrap into two rows, 20 + 5 + 20 = 45 */
  my_widget_set_layouter(content, my_layouter_flow_create(NULL, 0, 5,
                                                        MY_FLOW_ALIGN_LEFT));
  my_widget_set_rect(content, &(my_rect_t){0, 0, 100, 0});
  my_widget_set_layout_params(a, "w:100 h:20");
  my_widget_set_layout_params(b, "w:100 h:20");
  my_widget_add_child(content, a);
  my_widget_add_child(content, b);
  my_widget_unref(a);
  my_widget_unref(b);
  my_scroll_view_set_content(sv, content);
  my_widget_unref(content);
  /* auto height 45 < view 50: no scroll range */
  TEST_ASSERT_EQ_INT(my_scroll_view_get_content(sv)->rect.h, 45);
  my_scroll_view_set_offset(sv, 10);
  TEST_ASSERT_EQ_INT(my_scroll_view_get_offset(sv), 0);
  my_widget_unref((my_widget_t*)sv);
}

static void test_scroll_view_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_scroll_view_t* sv = my_scroll_view_create(dbg);
  my_widget_t* content = my_widget_create(dbg, "content");
  my_widget_t* bar = my_scroll_bar_create(dbg);
  my_scroll_view_set_content(sv, content);
  my_widget_unref(content);
  my_scroll_view_set_content_height(sv, 500);
  my_scroll_view_set_scroll_bar(sv, bar);
  my_scroll_view_set_offset(sv, 100);
  my_widget_unref(bar);
  my_widget_unref((my_widget_t*)sv);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_offset_clamped);
  MYTEST_RUN(test_wheel_steps);
  MYTEST_RUN(test_scroll_bar_two_way_sync);
  MYTEST_RUN(test_content_offset_and_clip);
  MYTEST_RUN(test_flow_measure_auto_height);
  MYTEST_RUN(test_scroll_view_no_leak);
MYTEST_MAIN_END()
