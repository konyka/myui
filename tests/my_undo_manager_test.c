/**
 * @file my_undo_manager_test.c
 * @brief Window-level shared undo manager tests (M11b): cross-widget
 * undo order, focus routing, shared/private switch, blur batching,
 * per-widget clear, leaks.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_undo_manager.h"
#include "myui/my_window.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_text_area.h"

#include <string.h>

#include "mytest.h"

static void key(my_widget_t* w, uint32_t k, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = k;
  e.u.key.modifiers = mods;
  w->vtable->on_event(w, &e);
}

static void type_str(my_widget_t* w, const char* s) {
  while (*s != '\0') {
    key(w, (uint8_t)*s, 0);
    s++;
  }
}

/* two edits in one window sharing a manager */
typedef struct fixture_t {
  my_pal_t* pal;
  my_window_t* win;
  my_undo_manager_t* mgr;
  my_widget_t* e1;
  my_widget_t* e2;
} fixture_t;

static void fixture_init(fixture_t* f) {
  memset(f, 0, sizeof(*f));
  f->pal = my_pal_dummy_create(NULL);
  f->win = my_window_create(NULL, f->pal, 400, 200, "t");
  f->mgr = my_undo_manager_create(NULL, 0);
  my_window_set_undo_manager(f->win, f->mgr);
  f->e1 = my_edit_create(NULL);
  f->e2 = my_edit_create(NULL);
  my_widget_set_rect(f->e1, &(my_rect_t){0, 0, 200, 30});
  my_widget_set_rect(f->e2, &(my_rect_t){0, 40, 200, 30});
  my_widget_add_child(my_window_widget(f->win), f->e1);
  my_widget_add_child(my_window_widget(f->win), f->e2);
  my_widget_unref(f->e1);
  my_widget_unref(f->e2);
  my_edit_set_undo_shared(f->e1, f->mgr);
  my_edit_set_undo_shared(f->e2, f->mgr);
}

static void fixture_destroy(fixture_t* f) {
  my_widget_unref(my_window_widget(f->win)); /* destroys e1/e2 (unregister) */
  my_undo_manager_destroy(f->mgr);
  my_pal_destroy(f->pal);
}

static void test_cross_widget_undo_order(void) {
  fixture_t f;
  fixture_init(&f);

  /* type in e1, then in e2 (focus switches batch the streams) */
  ((my_edit_t*)f.e1)->focused = true;
  type_str(f.e1, "ab");
  ((my_edit_t*)f.e1)->focused = false;
  ((my_edit_t*)f.e2)->focused = true;
  type_str(f.e2, "cd");
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "ab");
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e2), "cd");

  /* undo #1 reverts e2 ("cd" was one batch) */
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e2), "");
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "ab");
  /* undo #2 reverts e1 -- cross-widget chronological order */
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "");
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_NOT_FOUND);

  /* redo replays in the same routed order */
  TEST_ASSERT_EQ_INT(my_undo_manager_redo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "ab");
  TEST_ASSERT_EQ_INT(my_undo_manager_redo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e2), "cd");

  fixture_destroy(&f);
}

static void test_undo_focuses_owner(void) {
  fixture_t f;
  fixture_init(&f);
  ((my_edit_t*)f.e1)->focused = true;
  type_str(f.e1, "x");
  ((my_edit_t*)f.e1)->focused = false;
  ((my_edit_t*)f.e2)->focused = true;
  type_str(f.e2, "y");

  /* focus currently "on e2" per the manual flag; use the dispatcher to
   * make it real, then undo: focus must jump to the entry's owner */
  my_event_dispatcher_set_focus(&f.win->dispatcher, f.e2);
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK); /* reverts y */
  TEST_ASSERT(f.win->dispatcher.focused == f.e2);
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK); /* reverts x */
  TEST_ASSERT(f.win->dispatcher.focused == f.e1);

  fixture_destroy(&f);
}

static void test_shared_private_switch(void) {
  fixture_t f;
  fixture_init(&f);
  my_event_dispatcher_set_focus(&f.win->dispatcher, f.e1);
  ((my_edit_t*)f.e1)->focused = true;
  type_str(f.e1, "shared");
  /* back to private: the shared history of e1 is discarded (documented) */
  my_edit_set_undo_shared(f.e1, NULL);
  TEST_ASSERT(!my_undo_manager_can_undo(f.mgr));

  type_str(f.e1, "+priv"); /* private stack now */
  key(f.e1, 'z', MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "shared");
  key(f.e1, 'z', MY_KEYMOD_CTRL); /* private stack empty: no-op */
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "shared");

  fixture_destroy(&f);
}

static void test_blur_breaks_batch_in_shared_mode(void) {
  fixture_t f;
  fixture_init(&f);
  my_event_dispatcher_set_focus(&f.win->dispatcher, f.e1);
  ((my_edit_t*)f.e1)->focused = true;
  type_str(f.e1, "ab");
  /* blur e1 for real (focus e2): the shared batch closes */
  my_event_dispatcher_set_focus(&f.win->dispatcher, f.e2);
  ((my_edit_t*)f.e1)->focused = true; /* keep key acceptance simple */
  type_str(f.e1, "cd");

  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "abcd");
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "ab"); /* second batch only */
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e1), "");

  fixture_destroy(&f);
}

static void test_set_text_clears_only_owner_entries(void) {
  fixture_t f;
  fixture_init(&f);
  ((my_edit_t*)f.e1)->focused = true;
  type_str(f.e1, "aa");
  ((my_edit_t*)f.e1)->focused = false;
  ((my_edit_t*)f.e2)->focused = true;
  type_str(f.e2, "bb");

  my_edit_set_text(f.e1, "reset"); /* drops e1's entries only */
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(f.e2), ""); /* e2 still undoable */
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(f.mgr), MY_RET_NOT_FOUND);

  fixture_destroy(&f);
}

static void test_text_area_shared_mode(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = my_window_create(NULL, pal, 400, 200, "t");
  my_undo_manager_t* mgr = my_undo_manager_create(NULL, 0);
  my_widget_t* ta = my_text_area_create(NULL);
  my_widget_t* e = my_edit_create(NULL);
  my_widget_add_child(my_window_widget(win), ta);
  my_widget_add_child(my_window_widget(win), e);
  my_widget_unref(ta);
  my_widget_unref(e);
  my_text_area_set_undo_shared(ta, mgr);
  my_edit_set_undo_shared(e, mgr);

  ((my_text_area_t*)ta)->focused = true;
  type_str(ta, "l1");
  ((my_text_area_t*)ta)->focused = false;
  ((my_edit_t*)e)->focused = true;
  type_str(e, "e1");

  /* undo reverts the edit first, then the text_area */
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_edit_get_text(e), "");
  TEST_ASSERT_EQ_STR(my_text_area_get_text(ta), "l1");
  TEST_ASSERT_EQ_INT(my_undo_manager_undo(mgr), MY_RET_OK);
  TEST_ASSERT_EQ_STR(my_text_area_get_text(ta), "");

  my_widget_unref(my_window_widget(win));
  my_undo_manager_destroy(mgr);
  my_pal_destroy(pal);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_undo_manager_t* mgr = my_undo_manager_create(dbg, 0);
  my_widget_t* e = my_edit_create(dbg);
  my_edit_set_undo_shared(e, mgr);
  ((my_edit_t*)e)->focused = true;
  type_str(e, "abc");
  my_undo_manager_undo(mgr);
  my_undo_manager_redo(mgr);
  my_widget_unref(e);
  my_undo_manager_destroy(mgr);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_cross_widget_undo_order);
  MYTEST_RUN(test_undo_focuses_owner);
  MYTEST_RUN(test_shared_private_switch);
  MYTEST_RUN(test_blur_breaks_batch_in_shared_mode);
  MYTEST_RUN(test_set_text_clears_only_owner_entries);
  MYTEST_RUN(test_text_area_shared_mode);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
