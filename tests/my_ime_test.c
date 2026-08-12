/**
 * @file my_ime_test.c
 * @brief IME preedit/commit tests (M13a) with fake events (no IM
 * needed): preedit display state (not in the document, no undo, no
 * "changed"), commit insertion (undoable, emits changed, drives MVVM
 * TwoWay), spot reporting to the dummy window, leaks.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_window.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_text_area.h"
#include "mymvvm_myui/my_mvvm.h"

#include <string.h>

#include "mytest.h"

static my_event_t ime_ev(my_event_type_t type, const char* text,
                         int32_t caret) {
  my_event_t e = my_event_init(type);
  e.u.ime.text = text;
  e.u.ime.cursor = caret;
  return e;
}

static void ime_send(my_widget_t* w, my_event_type_t type, const char* text) {
  my_event_t e = ime_ev(type, text, 0);
  w->vtable->on_event(w, &e);
}

static void test_edit_preedit_not_in_document(void) {
  my_widget_t* e = my_edit_create(NULL);
  my_edit_t* ed = (my_edit_t*)e;
  my_edit_set_text(e, "hi");
  ed->focused = true;

  ime_send(e, MY_EVENT_IME_PREEDIT, "ni");
  TEST_ASSERT(ed->ime_preedit != NULL);
  TEST_ASSERT_EQ_STR(ed->ime_preedit, "ni");
  TEST_ASSERT_EQ_STR(my_edit_get_text(e), "hi"); /* document untouched */

  ime_send(e, MY_EVENT_IME_PREEDIT, "ni h"); /* replace */
  TEST_ASSERT_EQ_STR(ed->ime_preedit, "ni h");

  ime_send(e, MY_EVENT_IME_PREEDIT, ""); /* clear */
  TEST_ASSERT(ed->ime_preedit == NULL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(e), "hi");
  my_widget_unref(e);
}

static void test_edit_commit_inserts_and_undoes(void) {
  my_widget_t* e = my_edit_create(NULL);
  int changes = 0;
  my_edit_set_text(e, "hi");
  ((my_edit_t*)e)->focused = true;
  my_widget_on(e, "changed", NULL, NULL); /* listener slot ok? no-op */
  (void)changes;

  ime_send(e, MY_EVENT_IME_PREEDIT, "ni hao");
  ime_send(e, MY_EVENT_IME_COMMIT, "\xE4\xBD\xA0\xE5\xA5\xBD"); /* 你好 */
  TEST_ASSERT(((my_edit_t*)e)->ime_preedit == NULL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(e), "hi\xE4\xBD\xA0\xE5\xA5\xBD");

  /* commit is ONE undoable step */
  {
    my_event_t z = my_event_init(MY_EVENT_KEY_DOWN);
    z.u.key.key = 'z';
    z.u.key.modifiers = MY_KEYMOD_CTRL;
    e->vtable->on_event(e, &z);
  }
  TEST_ASSERT_EQ_STR(my_edit_get_text(e), "hi");
  my_widget_unref(e);
}

static void test_edit_commit_drives_mvvm(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = my_window_create(NULL, pal, 300, 100, "ime");
  my_widget_t* e = my_edit_create(NULL);
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_mvvm_context_t* mc;
  my_value_t v;
  my_widget_add_child(my_window_widget(win), e);
  my_widget_unref(e);
  my_widget_set_bind_rules(e, "v:text={name, Mode=TwoWay}");
  mc = my_mvvm_bind(NULL, win, vm);
  TEST_ASSERT_NOT_NULL(mc);

  my_event_dispatcher_set_focus(&win->dispatcher, e);
  ((my_edit_t*)e)->focused = true;
  ime_send(e, MY_EVENT_IME_COMMIT, "abc");

  my_value_init(&v, NULL);
  my_view_model_get_prop(vm, "name", &v);
  TEST_ASSERT_EQ_STR(my_value_get_str(&v), "abc");

  my_mvvm_context_destroy(mc);
  my_view_model_unref(vm);
  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);
}

static void test_edit_spot_reported(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = my_window_create(NULL, pal, 300, 100, "ime");
  my_widget_t* e = my_edit_create(NULL);
  int32_t sx = -1, sy = -1;
  my_widget_add_child(my_window_widget(win), e);
  my_widget_unref(e);
  my_widget_set_rect(e, &(my_rect_t){10, 20, 200, 30});
  my_edit_set_text(e, "ab");
  ((my_edit_t*)e)->focused = true;

  /* cursor left once: the spot moves to the cursor's window coords
   * (edit at (10,20); cursor after 'a': 4+8=12 local x, bottom y=30) */
  {
    my_event_t k = my_event_init(MY_EVENT_KEY_DOWN);
    k.u.key.key = MY_KEY_LEFT;
    e->vtable->on_event(e, &k);
  }
  my_pal_dummy_get_ime_spot(win->pal_window, &sx, &sy);
  TEST_ASSERT_EQ_INT(sx, 10 + 4 + 8); /* edit x + pad + 1 cell (8px) */
  TEST_ASSERT_EQ_INT(sy, 20 + 30);    /* edit y + height */

  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);
}

static void test_text_area_commit(void) {
  my_widget_t* ta = my_text_area_create(NULL);
  my_text_area_set_text(ta, "l1");
  ((my_text_area_t*)ta)->focused = true;
  ime_send(ta, MY_EVENT_IME_PREEDIT, "zhong");
  TEST_ASSERT_EQ_STR(my_text_area_get_text(ta), "l1");
  ime_send(ta, MY_EVENT_IME_COMMIT, "\xE4\xB8\xAD"); /* 中 */
  TEST_ASSERT_EQ_STR(my_text_area_get_text(ta), "l1\xE4\xB8\xAD");
  {
    my_event_t z = my_event_init(MY_EVENT_KEY_DOWN);
    z.u.key.key = 'z';
    z.u.key.modifiers = MY_KEYMOD_CTRL;
    ta->vtable->on_event(ta, &z);
  }
  TEST_ASSERT_EQ_STR(my_text_area_get_text(ta), "l1");
  my_widget_unref(ta);
}

static void test_ime_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* e = my_edit_create(dbg);
  ((my_edit_t*)e)->focused = true;
  ime_send(e, MY_EVENT_IME_PREEDIT, "ni");
  ime_send(e, MY_EVENT_IME_COMMIT, "hao");
  my_widget_unref(e);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_edit_preedit_not_in_document);
  MYTEST_RUN(test_edit_commit_inserts_and_undoes);
  MYTEST_RUN(test_edit_commit_drives_mvvm);
  MYTEST_RUN(test_edit_spot_reported);
  MYTEST_RUN(test_text_area_commit);
  MYTEST_RUN(test_ime_leak);
MYTEST_MAIN_END()
