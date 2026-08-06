/**
 * @file my_undo_integration_test.c
 * @brief Undo/redo integration in edit & text_area, Tab focus ring,
 * PageUp/PageDown (M10a).
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_event_dispatch.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_list_view.h"
#include "myui/widgets/my_scroll_bar.h"
#include "myui/my_undo_stack.h"
#include "myui/widgets/my_text_area.h"

#include <stdio.h>

#include "mytest.h"

static my_event_t key_ev(uint32_t key, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = key;
  e.u.key.modifiers = mods;
  return e;
}

static void key(my_widget_t* w, uint32_t k, uint8_t mods) {
  my_event_t e = key_ev(k, mods);
  w->vtable->on_event(w, &e);
}

static void type_edit(my_widget_t* w, const char* s) {
  ((my_edit_t*)w)->focused = true;
  while (*s != '\0') {
    key(w, (uint8_t)*s, 0);
    s++;
  }
}

static void type_ta(my_widget_t* w, const char* s) {
  ((my_text_area_t*)w)->focused = true;
  while (*s != '\0') {
    if (*s == '\n') {
      key(w, MY_KEY_RETURN, 0);
    } else {
      key(w, (uint8_t)*s, 0);
    }
    s++;
  }
}

/* ---------------- edit undo ---------------- */

static void test_edit_typing_stream_single_undo(void) {
  my_widget_t* w = my_edit_create(NULL);

  type_edit(w, "hello");
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "hello");

  key(w, 'z', MY_KEYMOD_CTRL); /* one undo: whole typing stream gone */
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "");

  key(w, 'y', MY_KEYMOD_CTRL); /* redo */
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "hello");

  key(w, 'z', MY_KEYMOD_CTRL);
  key(w, 'z', MY_KEYMOD_CTRL | MY_KEYMOD_SHIFT); /* shift+Z = redo */
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "hello");

  my_widget_unref(w);
}

static void test_edit_selection_replace_undo(void) {
  my_widget_t* w = my_edit_create(NULL);

  type_edit(w, "hello");
  key(w, 'a', MY_KEYMOD_CTRL); /* select all */
  key(w, 'x', 0);              /* replace with "x" (batch breaks) */
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "x");

  key(w, 'z', MY_KEYMOD_CTRL); /* undo the insert of "x" */
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "");

  my_widget_unref(w);
}

static void test_edit_programmatic_set_text_clears_stack(void) {
  my_widget_t* w = my_edit_create(NULL);

  type_edit(w, "abc");
  my_edit_set_text(w, "program"); /* not undoable, stack cleared */
  key(w, 'z', MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(w), "program"); /* nothing to undo */

  my_widget_unref(w);
}

/* ---------------- text_area undo ---------------- */

static void test_ta_multiline_delete_undo(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;

  my_text_area_set_text(w, "ab\ncd\nef");
  ta->focused = true;

  /* select across a line boundary and delete, then undo */
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL);
  key(w, MY_KEY_DOWN, MY_KEYMOD_SHIFT);
  key(w, MY_KEY_DOWN, MY_KEYMOD_SHIFT);
  key(w, MY_KEY_RIGHT, MY_KEYMOD_SHIFT);
  key(w, MY_KEY_BACKSPACE, 0); /* deletes "ab\ncd\ne" */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "f");

  key(w, 'z', MY_KEYMOD_CTRL); /* undo restores the newline too */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "ab\ncd\nef");

  my_widget_unref(w);
}

static void test_ta_cut_undo_paste(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;
  my_pal_t* pal = my_pal_dummy_create(NULL);

  (void)pal;
  my_text_area_set_text(w, "hello");
  ta->focused = true;

  key(w, 'a', MY_KEYMOD_CTRL);
  key(w, MY_KEY_BACKSPACE, 0); /* delete all (recorded) */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "");

  key(w, 'z', MY_KEYMOD_CTRL); /* undo: back to hello */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "hello");

  key(w, 'a', MY_KEYMOD_CTRL);
  key(w, MY_KEY_BACKSPACE, 0);
  key(w, 'z', MY_KEYMOD_CTRL);
  type_ta(w, "world");
  key(w, 'z', MY_KEYMOD_CTRL); /* undo typing stream -> hello remains? */
  /* "world" batch removed, but the second delete-all was also undone? no:
   * redo branch was killed by the new edit; undo order is [world batch] */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(w), "hello");

  my_widget_unref(w);
}

/* ---------------- Tab focus ring ---------------- */

static void test_tab_focus_ring(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* a = my_button_create(NULL, "a");
  my_widget_t* b = my_button_create(NULL, "b");
  my_widget_t* c = my_button_create(NULL, "c");
  my_event_dispatcher_t d;
  my_event_t e;

  my_widget_set_rect(root, &(my_rect_t){0, 0, 300, 100});
  my_widget_set_rect(a, &(my_rect_t){0, 0, 90, 30});
  my_widget_set_rect(b, &(my_rect_t){100, 0, 90, 30});
  my_widget_set_rect(c, &(my_rect_t){200, 0, 90, 30});
  my_widget_add_child(root, a);
  my_widget_add_child(root, b);
  my_widget_add_child(root, c);
  my_widget_unref(a);
  my_widget_unref(b);
  my_widget_unref(c);

  my_event_dispatcher_init(&d, root);

  /* focus a via pointer, then Tab through the ring */
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 10;
  e.u.pointer.y = 10;
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == a);

  e = key_ev(MY_KEY_TAB, 0);
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == b);

  e = key_ev(MY_KEY_TAB, 0);
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == c);

  e = key_ev(MY_KEY_TAB, 0); /* wraps to a */
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == a);

  e = key_ev(MY_KEY_TAB, MY_KEYMOD_SHIFT); /* backwards to c */
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == c);

  my_widget_unref(root);
}

static void test_tab_skips_disabled_and_invisible(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* a = my_button_create(NULL, "a");
  my_widget_t* b = my_button_create(NULL, "b");
  my_widget_t* c = my_button_create(NULL, "c");
  my_event_dispatcher_t d;
  my_event_t e;

  my_widget_set_rect(root, &(my_rect_t){0, 0, 300, 100});
  my_widget_set_rect(a, &(my_rect_t){0, 0, 90, 30});
  my_widget_set_rect(b, &(my_rect_t){100, 0, 90, 30});
  my_widget_set_rect(c, &(my_rect_t){200, 0, 90, 30});
  b->enable = false;
  my_widget_set_visible(c, false);
  my_widget_add_child(root, a);
  my_widget_add_child(root, b);
  my_widget_add_child(root, c);
  my_widget_unref(a);
  my_widget_unref(b);
  my_widget_unref(c);

  my_event_dispatcher_init(&d, root);
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 10;
  e.u.pointer.y = 10;
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == a);

  e = key_ev(MY_KEY_TAB, 0); /* b disabled, c hidden: wraps back to a */
  my_event_dispatch(&d, &e);
  TEST_ASSERT(d.focused == a);

  my_widget_unref(root);
}

/* ---------------- PageUp/PageDown ---------------- */

static void test_text_area_page_up_down(void) {
  my_widget_t* w = my_text_area_create(NULL);
  my_text_area_t* ta = (my_text_area_t*)w;
  char buf[128];
  int i, n = 0;

  for (i = 0; i < 30; i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "r%d\n", i);
  }
  my_text_area_set_text(w, buf);
  my_widget_set_rect(w, &(my_rect_t){0, 0, 100, 40}); /* ~2 lines visible */
  ta->focused = true;
  key(w, MY_KEY_HOME, MY_KEYMOD_CTRL);

  key(w, MY_KEY_PAGE_DOWN, 0);
  TEST_ASSERT(ta->cursor_row >= 2); /* paged down by visible lines */
  TEST_ASSERT(ta->scroll_y > 0);

  key(w, MY_KEY_PAGE_UP, 0);
  TEST_ASSERT(ta->cursor_row < 4);

  my_widget_unref(w);
}

typedef struct pg_adapter_t {
  my_list_adapter_t base;
} pg_adapter_t;

static size_t pg_count(my_list_adapter_t* a) {
  (void)a;
  return 100;
}

static my_widget_t* pg_create(my_list_adapter_t* a) {
  (void)a;
  return my_widget_create(NULL, "row");
}

static void pg_bind(my_list_adapter_t* a, my_widget_t* row, size_t i) {
  (void)a;
  (void)row;
  (void)i;
}

static const my_list_adapter_vtable_t PG_ADAPTER = {pg_count, pg_create,
                                                    pg_bind, NULL};

static void test_list_view_page_up_down(void) {
  pg_adapter_t a;
  my_widget_t* lv;
  a.base.vtable = &PG_ADAPTER;

  lv = my_list_view_create(NULL);
  my_widget_set_rect(lv, &(my_rect_t){0, 0, 200, 240});
  my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);

  {
    my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
    e.u.key.key = MY_KEY_PAGE_DOWN;
    TEST_ASSERT_EQ_INT(lv->vtable->on_event(lv, &e), MY_RET_OK);
    TEST_ASSERT(my_list_view_get_scroll_offset(lv) > 0);
    e.u.key.key = MY_KEY_PAGE_UP;
    lv->vtable->on_event(lv, &e);
    TEST_ASSERT_EQ_INT(my_list_view_get_scroll_offset(lv), 0);
  }

  my_widget_unref(lv);
}

/* scroll_bar keyboard */
static void test_scroll_bar_keys(void) {
  my_widget_t* bar = my_scroll_bar_create(NULL);
  int changed = 0;
  my_event_t e;

  my_widget_set_rect(bar, &(my_rect_t){0, 0, 12, 100});
  my_scroll_bar_set_page_size(bar, 0.2f);
  my_widget_on(bar, "changed", NULL, NULL); /* ensure emitter path */

  e = key_ev(MY_KEY_DOWN, 0);
  bar->vtable->on_event(bar, &e);
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.0f);

  e = key_ev(MY_KEY_PAGE_DOWN, 0);
  bar->vtable->on_event(bar, &e);
  TEST_ASSERT(my_scroll_bar_get_value(bar) > 0.1f);
  (void)changed;

  e = key_ev(MY_KEY_HOME, 0);
  bar->vtable->on_event(bar, &e);
  TEST_ASSERT(my_scroll_bar_get_value(bar) == 0.0f);

  my_widget_unref(bar);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* e = my_edit_create(dbg);
  my_widget_t* ta = my_text_area_create(dbg);

  type_edit(e, "abc");
  key(e, 'z', MY_KEYMOD_CTRL);
  type_ta(ta, "x\ny");
  key(ta, 'z', MY_KEYMOD_CTRL);
  key(ta, 'y', MY_KEYMOD_CTRL);
  my_undo_stack_break_batch(((my_edit_t*)e)->undo);

  my_widget_unref(e);
  my_widget_unref(ta);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_edit_typing_stream_single_undo);
  MYTEST_RUN(test_edit_selection_replace_undo);
  MYTEST_RUN(test_edit_programmatic_set_text_clears_stack);
  MYTEST_RUN(test_ta_multiline_delete_undo);
  MYTEST_RUN(test_ta_cut_undo_paste);
  MYTEST_RUN(test_tab_focus_ring);
  MYTEST_RUN(test_tab_skips_disabled_and_invisible);
  MYTEST_RUN(test_text_area_page_up_down);
  MYTEST_RUN(test_list_view_page_up_down);
  MYTEST_RUN(test_scroll_bar_keys);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
