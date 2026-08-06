/**
 * @file my_list_view_test.c
 * @brief Unit tests for the virtualized list_view (mock adapter).
 */
#include "myui/widgets/my_list_view.h"
#include "myui/widgets/my_scroll_bar.h"

#include <stdio.h>
#include <string.h>

#include "mytest.h"

/* ---------------- mock adapter ---------------- */

typedef struct mock_adapter_t {
  my_list_adapter_t base;
  size_t count;
  int create_calls;
  int bind_calls;
  size_t last_bound_index;
  char last_bound_text[32];
} mock_adapter_t;

static my_widget_t* mock_create_row(my_list_adapter_t* adapter) {
  mock_adapter_t* m = (mock_adapter_t*)adapter;
  m->create_calls++;
  return my_widget_create(NULL, "row");
}

static void mock_bind_row(my_list_adapter_t* adapter, my_widget_t* row,
                          size_t index) {
  mock_adapter_t* m = (mock_adapter_t*)adapter;
  m->bind_calls++;
  m->last_bound_index = index;
  snprintf(m->last_bound_text, sizeof(m->last_bound_text), "row-%zu", index);
  my_widget_set_name(row, m->last_bound_text);
}

static size_t mock_get_count(my_list_adapter_t* adapter) {
  return ((mock_adapter_t*)adapter)->count;
}

static const my_list_adapter_vtable_t MOCK_ADAPTER_VTABLE = {
    mock_get_count, mock_create_row, mock_bind_row, NULL};

static void mock_adapter_init(mock_adapter_t* m, size_t count) {
  memset(m, 0, sizeof(*m));
  m->base.vtable = &MOCK_ADAPTER_VTABLE;
  m->count = count;
}

static my_widget_t* make_lv(mock_adapter_t* adapter, int32_t w, int32_t h,
                            int32_t row_h) {
  my_widget_t* lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, w, h});
  my_list_view_set_row_height(lv, row_h);
  my_list_view_set_adapter(lv, (my_list_adapter_t*)adapter);
  return lv;
}

/* ---------------- tests ---------------- */

static void test_virtualization_row_count(void) {
  mock_adapter_t a;
  my_widget_t* lv;
  mock_adapter_init(&a, 10000);
  lv = make_lv(&a, 200, 240, 24); /* 10 rows visible + buffer */

  /* 10000 data rows, but only ~12 row widgets built */
  TEST_ASSERT(my_list_view_rows_created_total(lv) <= 13);
  TEST_ASSERT_EQ_INT(my_widget_child_count(lv), 12); /* 240/24+2 = 12 */
  TEST_ASSERT_EQ_INT(a.create_calls, (int)my_list_view_rows_created_total(lv));

  my_widget_unref(lv);
}

static void test_scroll_reuses_pool_rows(void) {
  mock_adapter_t a;
  my_widget_t* lv;
  my_list_view_t* l;
  my_widget_t* first_row;
  size_t created_before;
  mock_adapter_init(&a, 10000);
  lv = make_lv(&a, 200, 240, 24);
  l = (my_list_view_t*)lv;
  first_row = my_widget_get_child(lv, 0);
  created_before = my_list_view_rows_created_total(lv);

  /* scroll one row down: top row recycled, one new row bound (reused) */
  my_list_view_set_scroll_offset(lv, 24);
  TEST_ASSERT_EQ_INT(my_widget_child_count(lv), 12);
  /* rows were rebound: last visible index = 12 */
  TEST_ASSERT_EQ_INT(a.last_bound_index, 12);
  /* no NEW row widgets were created beyond the pool reuse */
  TEST_ASSERT(my_list_view_rows_created_total(lv) <= created_before + 1);
  /* pool cycled: old top row went back and a reused row reappeared */
  TEST_ASSERT(my_darray_size(l->pool) >= 1 ||
              first_row == my_widget_get_child(lv, 0));

  my_widget_unref(lv);
}

static void test_scroll_clamps(void) {
  mock_adapter_t a;
  my_widget_t* lv;
  mock_adapter_init(&a, 100);
  lv = make_lv(&a, 200, 240, 24);

  my_list_view_set_scroll_offset(lv, -50);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 0);

  my_list_view_set_scroll_offset(lv, 999999);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 100 * 24 - 240);

  my_widget_unref(lv);
}

static void test_wheel_scrolls(void) {
  mock_adapter_t a;
  my_widget_t* lv;
  my_event_t e;
  mock_adapter_init(&a, 100);
  lv = make_lv(&a, 200, 240, 24);

  e = my_event_init(MY_EVENT_POINTER_WHEEL);
  e.u.pointer.delta = -1; /* scroll down 3 rows = 72px */
  TEST_ASSERT_EQ_INT(lv->vtable->on_event(lv, &e), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 72);

  e.u.pointer.delta = 1;
  lv->vtable->on_event(lv, &e);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 0);

  my_widget_unref(lv);
}

static void test_drag_scroll(void) {
  mock_adapter_t a;
  my_widget_t* lv;
  my_event_t e;
  mock_adapter_init(&a, 100);
  lv = make_lv(&a, 200, 240, 24);

  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.y = 100;
  lv->vtable->on_event(lv, &e);
  e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.y = 40; /* dragged up 60px: content scrolls down 60 */
  lv->vtable->on_event(lv, &e);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 60);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.y = 40;
  lv->vtable->on_event(lv, &e);

  my_widget_unref(lv);
}

/* ---------------- variable row heights + invalidation (M10d) ---------------- */

typedef struct var_adapter_t {
  my_list_adapter_t base;
  size_t count;
  int32_t* heights;     /**< per-row heights (mutable by the test) */
  size_t* height_calls; /**< per-row row_height() call counts */
} var_adapter_t;

static size_t var_get_count(my_list_adapter_t* adapter) {
  return ((var_adapter_t*)adapter)->count;
}

static my_widget_t* var_create_row(my_list_adapter_t* adapter) {
  (void)adapter;
  return my_widget_create(NULL, "row");
}

static void var_bind_row(my_list_adapter_t* adapter, my_widget_t* row,
                         size_t index) {
  (void)adapter;
  (void)row;
  (void)index;
}

static int32_t var_row_height(my_list_adapter_t* adapter, size_t index) {
  var_adapter_t* a = (var_adapter_t*)adapter;
  a->height_calls[index]++;
  return a->heights[index];
}

static const my_list_adapter_vtable_t VAR_ADAPTER_VTABLE = {
    var_get_count, var_create_row, var_bind_row, var_row_height};

static void var_adapter_init(var_adapter_t* a, size_t count, int32_t* heights,
                             size_t* height_calls) {
  memset(a, 0, sizeof(*a));
  memset(height_calls, 0, count * sizeof(size_t));
  a->base.vtable = &VAR_ADAPTER_VTABLE;
  a->count = count;
  a->heights = heights;
  a->height_calls = height_calls;
}

static void test_invalidate_row_heights_rebuilds_visible(void) {
  int32_t heights[100];
  size_t calls[100];
  var_adapter_t a;
  my_widget_t* lv;
  size_t i;
  for (i = 0; i < 100; i++) {
    heights[i] = 20;
  }
  var_adapter_init(&a, 100, heights, calls);
  lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 100});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);
  TEST_ASSERT(my_widget_child_count(lv) > 0);
  TEST_ASSERT_EQ_INT(my_widget_get_child(lv, 0)->rect.h, 20);

  /* every row grows 20 -> 40: full invalidation rebuilds visible rows */
  for (i = 0; i < 100; i++) {
    heights[i] = 40;
  }
  my_list_view_invalidate_row_heights(lv);
  TEST_ASSERT(my_widget_child_count(lv) > 0);
  TEST_ASSERT_EQ_INT(my_widget_get_child(lv, 0)->rect.h, 40);

  my_widget_unref(lv);
}

static void test_invalidate_row_height_truncates_tail(void) {
  int32_t heights[100];
  size_t calls[100];
  var_adapter_t a;
  my_list_view_t* l;
  my_widget_t* lv;
  size_t i, calls0;
  for (i = 0; i < 100; i++) {
    heights[i] = 20;
  }
  var_adapter_init(&a, 100, heights, calls);
  lv = my_list_view_create(NULL);
  l = (my_list_view_t*)lv;
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 100});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);

  /* measure a good chunk of the prefix-sum cache (scroll to row ~20) */
  my_list_view_set_scroll_offset(lv, 400);
  TEST_ASSERT(my_darray_size(l->psum) >= 20);
  calls0 = calls[0];
  TEST_ASSERT(calls0 > 0);

  /* row 5 grows: index invalidation keeps rows < 5 measured */
  heights[5] = 60;
  my_list_view_invalidate_row_height(lv, 5);
  TEST_ASSERT_EQ_INT(calls[0], (int)calls0); /* prefix not re-measured */
  TEST_ASSERT(my_darray_size(l->psum) >= 6);
  TEST_ASSERT_EQ_INT((int64_t)(size_t)my_darray_get(l->psum, 5), 100);
  TEST_ASSERT_EQ_INT((int64_t)(size_t)my_darray_get(l->psum, 6), 160);

  my_widget_unref(lv);
}

static void test_invalidate_clamps_scroll_after_shrink(void) {
  int32_t heights[100];
  size_t calls[100];
  var_adapter_t a;
  my_widget_t* lv;
  my_widget_t* bar;
  size_t i;
  for (i = 0; i < 100; i++) {
    heights[i] = 20;
  }
  var_adapter_init(&a, 100, heights, calls);
  lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 100});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);
  bar = my_scroll_bar_create(NULL);
  my_list_view_set_scroll_bar(lv, bar);

  /* scroll to the very bottom (content 2000, max offset 1900) */
  my_list_view_set_scroll_offset(lv, 10000);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 1900);

  /* all rows shrink 20 -> 5: content 500, max offset 400 -> clamp back */
  for (i = 0; i < 100; i++) {
    heights[i] = 5;
  }
  my_list_view_invalidate_row_heights(lv);
  TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 400);
  /* scroll bar re-synced to the clamped position (bottom = 1.0) */
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.99f);

  my_widget_unref(bar);
  my_widget_unref(lv);
}

static void test_empty_list(void) {
  mock_adapter_t a;
  my_widget_t* lv;
  mock_adapter_init(&a, 0);
  lv = make_lv(&a, 200, 240, 24);
  TEST_ASSERT_EQ_INT(my_widget_child_count(lv), 0);
  TEST_ASSERT_EQ_INT(my_list_view_rows_created_total(lv), 0);
  my_list_view_refresh(lv);
  my_widget_unref(lv);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  mock_adapter_t a;
  my_widget_t* lv = my_list_view_create(dbg);
  my_list_view_t* l = (my_list_view_t*)lv;
  my_event_t e;

  my_mem_free(dbg, l->active); /* swap pools to dbg for full coverage */
  l->active = my_darray_create(dbg, 0);
  my_mem_free(dbg, l->pool);
  l->pool = my_darray_create(dbg, 0);

  mock_adapter_init(&a, 1000);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 240});
  my_list_view_set_row_height(lv, 24);
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);

  e = my_event_init(MY_EVENT_POINTER_WHEEL);
  e.u.pointer.delta = -1;
  lv->vtable->on_event(lv, &e);
  my_list_view_set_scroll_offset(lv, 0);

  my_widget_unref(lv);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_virtualization_row_count);
  MYTEST_RUN(test_scroll_reuses_pool_rows);
  MYTEST_RUN(test_scroll_clamps);
  MYTEST_RUN(test_wheel_scrolls);
  MYTEST_RUN(test_drag_scroll);
  MYTEST_RUN(test_empty_list);
  MYTEST_RUN(test_invalidate_row_heights_rebuilds_visible);
  MYTEST_RUN(test_invalidate_row_height_truncates_tail);
  MYTEST_RUN(test_invalidate_clamps_scroll_after_shrink);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
