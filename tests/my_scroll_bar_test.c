/**
 * @file my_scroll_bar_test.c
 * @brief scroll_bar widget + list_view variable row heights + bar linkage.
 */
#include "myui/widgets/my_list_view.h"
#include "myui/widgets/my_scroll_bar.h"

#include <string.h>

#include "mytest.h"

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

/* ---------------- scroll bar basics ---------------- */

static void test_thumb_drag_and_page(void) {
  my_widget_t* bar = my_scroll_bar_create(NULL);
  int changed = 0;
  my_event_t e;

  my_widget_set_rect(bar, &(my_rect_t){0, 0, 12, 200});
  my_scroll_bar_set_page_size(bar, 0.5f); /* thumb = 100px */
  my_widget_on(bar, "changed", on_hit, &changed);

  /* drag the thumb (starts at y=0) down to the middle */
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 6, 5);
  TEST_ASSERT_EQ_INT(bar->vtable->on_event(bar, &e), MY_RET_OK);
  e = pointer_ev(MY_EVENT_POINTER_MOVE, 6, 55);
  bar->vtable->on_event(bar, &e);
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.4f &&
              my_scroll_bar_get_value(bar) < 0.6f);
  TEST_ASSERT(changed > 0);
  e = pointer_ev(MY_EVENT_POINTER_UP, 6, 55);
  bar->vtable->on_event(bar, &e);

  /* track click below thumb: page down */
  my_scroll_bar_set_value(bar, 0.2f);
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 6, 190);
  bar->vtable->on_event(bar, &e);
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.2f);

  my_widget_unref(bar);
}

static void test_value_clamps_and_min_thumb(void) {
  my_widget_t* bar = my_scroll_bar_create(NULL);
  my_widget_set_rect(bar, &(my_rect_t){0, 0, 10, 40});
  my_scroll_bar_set_page_size(bar, 0.01f); /* < min thumb 16px */
  my_scroll_bar_set_value(bar, 2.0f);
  TEST_ASSERT(my_scroll_bar_get_value(bar) == 1.0f);
  my_scroll_bar_set_value(bar, -1.0f);
  TEST_ASSERT(my_scroll_bar_get_value(bar) == 0.0f);
  my_widget_unref(bar);
}

/* ---------------- variable row heights ---------------- */

typedef struct var_adapter_t {
  my_list_adapter_t base;
  size_t count;
} var_adapter_t;

static size_t var_count(my_list_adapter_t* a) {
  return ((var_adapter_t*)a)->count;
}

static my_widget_t* var_create(my_list_adapter_t* a) {
  (void)a;
  return my_widget_create(NULL, "row");
}

static void var_bind(my_list_adapter_t* a, my_widget_t* row, size_t index) {
  (void)a;
  (void)row;
  (void)index;
}

static int32_t var_height(my_list_adapter_t* a, size_t index) {
  (void)a;
  return (int32_t)(20 + (index % 3) * 20); /* 20, 40, 60 repeating */
}

static const my_list_adapter_vtable_t VAR_VTABLE = {var_count, var_create,
                                                    var_bind, var_height};

static void test_variable_height_visible_range(void) {
  var_adapter_t a;
  my_widget_t* lv;
  my_widget_t* r0;
  a.base.vtable = &VAR_VTABLE;
  a.count = 1000;

  lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 100});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);

  /* rows 0(20),1(40),2(60): first=0, 100px viewport + buffer -> rows 0..2+ */
  r0 = my_widget_get_child(lv, 0);
  TEST_ASSERT_NOT_NULL(r0);
  TEST_ASSERT_EQ_INT(r0->rect.h, 20);
  TEST_ASSERT_EQ_INT(my_widget_get_child(lv, 1)->rect.h, 40);
  TEST_ASSERT(my_widget_child_count(lv) <= 6); /* not all 1000 */

  /* scroll 60px (rows 0+1): first visible row becomes 2 */
  my_list_view_set_scroll_offset(lv, 60);
  TEST_ASSERT_EQ_INT(my_widget_get_child(lv, 0)->rect.h, 60);

  /* total content estimate grows as psum fills */
  my_list_view_set_scroll_offset(lv, 1000000);
  TEST_ASSERT(my_list_view_get_scroll_offset(lv) > 0);
  TEST_ASSERT(my_list_view_get_scroll_offset(lv) < 1000000);

  my_widget_unref(lv);
}

static void test_variable_bench_1k(void) {
  var_adapter_t a;
  my_widget_t* lv;
  int i;
  a.base.vtable = &VAR_VTABLE;
  a.count = 1000;
  lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 480});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);

  for (i = 0; i < 200; i++) {
    my_list_view_set_scroll_offset(lv, i * 50);
  }
  /* 200 scrolls over 1000 variable rows: generous bound */
  TEST_ASSERT(my_list_view_rows_created_total(lv) <= 200);

  my_widget_unref(lv);
}

/* ---------------- bar <-> list linkage ---------------- */

static void test_bar_list_linkage(void) {
  var_adapter_t a;
  my_widget_t* lv;
  my_widget_t* bar;
  my_event_t e;
  a.base.vtable = &VAR_VTABLE;
  a.count = 100;

  lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 100});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);
  bar = my_scroll_bar_create(NULL);
  my_widget_set_rect(bar, &(my_rect_t){200, 0, 12, 100});
  my_list_view_set_scroll_bar(lv, bar);

  /* page_size reflects viewport/content */
  TEST_ASSERT(my_scroll_bar_get_page_size(bar) > 0.0f &&
              my_scroll_bar_get_page_size(bar) < 1.0f);

  /* scroll the list -> bar.value follows */
  my_list_view_set_scroll_offset(lv, 500);
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.0f);

  /* drag the bar -> list scroll_offset follows. thumb at top: */
  my_scroll_bar_set_page_size(bar, 0.5f); /* thumb 50px at y=0 */
  my_scroll_bar_set_value(bar, 0.0f);
  e = pointer_ev(MY_EVENT_POINTER_DOWN, 206, 5); /* inside thumb */
  bar->vtable->on_event(bar, &e);
  e = pointer_ev(MY_EVENT_POINTER_MOVE, 206, 50);
  bar->vtable->on_event(bar, &e);
  TEST_ASSERT(my_list_view_get_scroll_offset(lv) > 0);

  my_widget_unref(lv);
  my_widget_unref(bar);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* bar = my_scroll_bar_create(dbg);
  my_widget_set_rect(bar, &(my_rect_t){0, 0, 10, 100});
  my_scroll_bar_set_page_size(bar, 0.3f);
  my_widget_unref(bar);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_thumb_drag_and_page);
  MYTEST_RUN(test_value_clamps_and_min_thumb);
  MYTEST_RUN(test_variable_height_visible_range);
  MYTEST_RUN(test_variable_bench_1k);
  MYTEST_RUN(test_bar_list_linkage);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
