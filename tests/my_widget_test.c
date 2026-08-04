/**
 * @file my_widget_test.c
 * @brief Unit tests for my_widget (tree, geometry, paint, invalidate).
 */
#include "myui/my_widget.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_widget_t* make(const char* name, int32_t x, int32_t y, int32_t w,
                         int32_t h) {
  my_widget_t* widget = my_widget_create(NULL, name);
  my_widget_set_rect(widget, &(my_rect_t){x, y, w, h});
  return widget;
}

/* root takes ownership; returns child for convenience */
static my_widget_t* add(my_widget_t* parent, my_widget_t* child) {
  my_widget_add_child(parent, child);
  my_widget_unref(child); /* tree holds its own ref now */
  return child;
}

static void test_tree_add_find_remove(void) {
  my_widget_t* root = make("root", 0, 0, 100, 100);
  my_widget_t* a = add(root, make("a", 0, 0, 10, 10));
  my_widget_t* b = add(root, make("b", 20, 0, 10, 10));
  add(a, make("a1", 1, 1, 5, 5));

  TEST_ASSERT_EQ_INT(my_widget_child_count(root), 2);
  TEST_ASSERT(my_widget_find_child(root, "a") == a);
  TEST_ASSERT(my_widget_find_child(root, "b") == b);
  TEST_ASSERT_NULL(my_widget_find_child(root, "a1")); /* direct only */
  TEST_ASSERT_NULL(my_widget_find_child(root, "nope"));
  TEST_ASSERT(my_widget_get_child(root, 0) == a);
  TEST_ASSERT(my_widget_get_child(root, 9) == NULL);
  TEST_ASSERT_EQ_INT(((my_object_t*)a)->ref_count, 1); /* tree's ref only */

  my_widget_remove_child(root, b);
  TEST_ASSERT_EQ_INT(my_widget_child_count(root), 1);
  TEST_ASSERT(my_widget_find_child(root, "b") == NULL);

  my_widget_unref(root);
}

static void test_coordinate_transform(void) {
  my_widget_t* root = make("root", 10, 10, 100, 100);
  my_widget_t* child = add(root, make("c", 20, 30, 50, 50));
  int32_t x = 5, y = 5;

  my_widget_local_to_global(child, &x, &y);
  TEST_ASSERT_EQ_INT(x, 35);
  TEST_ASSERT_EQ_INT(y, 45);

  my_widget_global_to_local(child, &x, &y);
  TEST_ASSERT_EQ_INT(x, 5);
  TEST_ASSERT_EQ_INT(y, 5);

  my_widget_unref(root);
}

static void test_hit_test_zorder_and_visibility(void) {
  my_widget_t* root = make("root", 0, 0, 100, 100);
  my_widget_t* bottom = add(root, make("bottom", 0, 0, 50, 50));
  my_widget_t* top = add(root, make("top", 10, 10, 50, 50));
  my_widget_t* inner = add(top, make("inner", 5, 5, 10, 10));

  TEST_ASSERT(my_widget_hit_test(root, 15, 15) == inner); /* deepest */
  TEST_ASSERT(my_widget_hit_test(root, 55, 55) == top);
  TEST_ASSERT(my_widget_hit_test(root, 5, 5) == bottom);
  TEST_ASSERT(my_widget_hit_test(root, 99, 99) == root);
  TEST_ASSERT_NULL(my_widget_hit_test(root, 150, 150));

  my_widget_set_visible(top, false);
  TEST_ASSERT(my_widget_hit_test(root, 15, 15) == bottom); /* skips hidden */

  my_widget_unref(root);
}

/* paint hooks recording which widgets got painted with what transform */
static void on_paint_fill(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){1, 1, 2, 2});
  (void)widget;
}

static const my_widget_vtable_t FILL_VTABLE = {on_paint_fill, NULL, NULL};

static void test_paint_recursion_transform_and_clip(void) {
  my_widget_t* root = make("root", 10, 20, 100, 100);
  my_widget_t* child = add(root, make("c", 5, 6, 40, 40));
  rec_vg_t rec;
  rec_vg_init(&rec);

  root->vtable = &FILL_VTABLE;
  child->vtable = &FILL_VTABLE;

  my_widget_paint(root, (my_vgcanvas_t*)&rec);

  /* root: translate(10,20) clip(0 0 100 100); child: translate(5 6) clip 40 */
  TEST_ASSERT(rec_has(&rec, "translate 10 20"));
  TEST_ASSERT(rec_has(&rec, "clip 0 0 100 100"));
  TEST_ASSERT(rec_has(&rec, "translate 5 6"));
  TEST_ASSERT(rec_has(&rec, "clip 0 0 40 40"));
  TEST_ASSERT_EQ_INT(rec_count(&rec, "fill_rect"), 2);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "save"), 2);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "restore"), 2);

  /* hidden child is skipped */
  rec_vg_init(&rec);
  my_widget_set_visible(child, false);
  my_widget_paint(root, (my_vgcanvas_t*)&rec);
  TEST_ASSERT_EQ_INT(rec_count(&rec, "fill_rect"), 1);

  my_widget_unref(root);
}

static void test_invalidate_bubbles_to_dirty_sink(void) {
  my_dirty_rects_t sink;
  my_widget_t* root = make("root", 10, 10, 100, 100);
  my_widget_t* child = add(root, make("c", 20, 20, 30, 30));
  const my_rect_t* r;

  my_dirty_rects_init(&sink);
  root->dirty_sink = &sink;

  my_widget_invalidate(child, &(my_rect_t){5, 5, 10, 10});
  TEST_ASSERT(child->dirty);
  TEST_ASSERT(root->dirty);
  TEST_ASSERT_EQ_INT(my_dirty_rects_count(&sink), 1);
  r = my_dirty_rects_get(&sink, 0);
  /* local (5,5) -> global: root(10,10) + child(20,20) + (5,5) */
  TEST_ASSERT_EQ_INT(r->x, 35);
  TEST_ASSERT_EQ_INT(r->y, 35);
  TEST_ASSERT_EQ_INT(r->w, 10);
  TEST_ASSERT_EQ_INT(r->h, 10);

  /* NULL rect = whole widget */
  my_dirty_rects_clear(&sink);
  my_widget_invalidate(child, NULL);
  r = my_dirty_rects_get(&sink, 0);
  TEST_ASSERT_EQ_INT(r->x, 30);
  TEST_ASSERT_EQ_INT(r->y, 30);
  TEST_ASSERT_EQ_INT(r->w, 30);
  TEST_ASSERT_EQ_INT(r->h, 30);

  my_widget_unref(root);
}

static void on_count(void* ctx, const char* event, void* data) {
  int* n = (int*)ctx;
  (void)event;
  (void)data;
  (*n)++;
}

static void test_widget_on_off(void) {
  my_widget_t* w = make("w", 0, 0, 10, 10);
  int n = 0;
  uint32_t id = my_widget_on(w, "click", on_count, &n);
  TEST_ASSERT(id > 0);

  my_emitter_emit(w->emitter, "click", NULL);
  TEST_ASSERT_EQ_INT(n, 1);

  TEST_ASSERT_EQ_INT(my_widget_off(w, id), MY_RET_OK);
  my_emitter_emit(w->emitter, "click", NULL);
  TEST_ASSERT_EQ_INT(n, 1);

  my_widget_unref(w);
}

static void test_null_params(void) {
  my_widget_t* w = make("w", 0, 0, 10, 10);
  TEST_ASSERT_EQ_INT(my_widget_add_child(NULL, w), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_widget_add_child(w, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_widget_remove_child(w, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_EQ_INT(my_widget_set_rect(w, NULL), MY_RET_INVALID_PARAMS);
  TEST_ASSERT_NULL(my_widget_find_child(NULL, "x"));
  TEST_ASSERT_EQ_INT(my_widget_child_count(NULL), 0);
  TEST_ASSERT_NULL(my_widget_hit_test(NULL, 0, 0));
  TEST_ASSERT_EQ_INT(my_widget_on(NULL, "x", on_count, NULL), 0);
  my_widget_paint(NULL, NULL); /* must be safe */
  my_widget_unref(NULL);       /* must be safe */
  my_widget_unref(w);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* root = my_widget_create(dbg, "root");
  my_widget_t* a = my_widget_create(dbg, "a");
  my_widget_t* b = my_widget_create(dbg, "b");
  int n = 0;

  my_widget_set_rect(root, &(my_rect_t){0, 0, 100, 100});
  my_widget_add_child(root, a);
  my_widget_unref(a);
  my_widget_add_child(root, b);
  my_widget_unref(b);
  my_widget_add_child(a, my_widget_create(dbg, "a1")); /* leak-check nested */
  my_widget_unref(my_widget_get_child(a, 0));

  my_widget_on(root, "click", on_count, &n);
  my_widget_invalidate(root, NULL);

  my_widget_remove_child(root, b);
  my_widget_unref(root);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_tree_add_find_remove);
  MYTEST_RUN(test_coordinate_transform);
  MYTEST_RUN(test_hit_test_zorder_and_visibility);
  MYTEST_RUN(test_paint_recursion_transform_and_clip);
  MYTEST_RUN(test_invalidate_bubbles_to_dirty_sink);
  MYTEST_RUN(test_widget_on_off);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
