/**
 * @file my_edit_test.c
 * @brief Unit tests for the edit widget: full keyboard state machine,
 * selection, UTF-8, click-to-locate, scroll, focus integration.
 */
#include "myui/widgets/my_edit.h"

#include "myui/my_event_dispatch.h"

#include <string.h>

#include "mytest.h"

static my_event_t key_ev(uint32_t key, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = key;
  e.u.key.modifiers = mods;
  return e;
}

static void on_hit(void* ctx, const char* e, void* d) {
  int* n = (int*)ctx;
  (void)e;
  (void)d;
  (*n)++;
}

static void type_keys(my_widget_t* edit, const char* keys) {
  my_edit_t* e = (my_edit_t*)edit;
  const char* p;
  e->focused = true; /* keys only accepted when focused */
  for (p = keys; *p != '\0'; p++) {
    my_event_t ev = key_ev((uint8_t)*p, 0);
    edit->vtable->on_event(edit, &ev);
  }
}

static void test_insert_and_cursor(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_edit_t* e = (my_edit_t*)edit;
  my_event_t ev;

  type_keys(edit, "abc");
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "abc");
  TEST_ASSERT_EQ_INT(e->cursor, 3);

  ev = key_ev(MY_KEY_LEFT, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(e->cursor, 2);
  type_keys(edit, "X");
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "abXc");

  ev = key_ev(MY_KEY_HOME, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(e->cursor, 0);
  ev = key_ev(MY_KEY_END, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(e->cursor, 4);

  my_widget_unref(edit);
}

static void test_backspace_and_delete(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_event_t ev;

  type_keys(edit, "ab");
  ev = key_ev(MY_KEY_BACKSPACE, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "a");

  ev = key_ev(MY_KEY_HOME, 0);
  edit->vtable->on_event(edit, &ev);
  ev = key_ev(MY_KEY_DELETE, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "");

  /* backspace at start is a no-op */
  ev = key_ev(MY_KEY_BACKSPACE, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "");

  my_widget_unref(edit);
}

static void test_selection_semantics(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_event_t ev;
  size_t a = 0, b = 0;

  type_keys(edit, "hello");

  /* Ctrl+A selects all */
  ev = key_ev('a', MY_KEYMOD_CTRL);
  edit->vtable->on_event(edit, &ev);
  my_edit_get_selection(edit, &a, &b);
  TEST_ASSERT_EQ_INT(a, 0);
  TEST_ASSERT_EQ_INT(b, 5);

  /* typing replaces the selection */
  type_keys(edit, "H");
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "H");

  /* Shift+Left extends selection, backspace deletes it */
  type_keys(edit, "i");
  ev = key_ev(MY_KEY_LEFT, MY_KEYMOD_SHIFT);
  edit->vtable->on_event(edit, &ev);
  my_edit_get_selection(edit, &a, &b);
  TEST_ASSERT_EQ_INT(b - a, 1);
  ev = key_ev(MY_KEY_BACKSPACE, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "H");

  my_widget_unref(edit);
}

static void test_max_len_and_readonly_and_password(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_edit_t* e = (my_edit_t*)edit;

  my_edit_set_max_len(edit, 3);
  type_keys(edit, "abcde");
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "abc");

  my_edit_set_readonly(edit, true);
  type_keys(edit, "X");
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "abc");
  my_edit_set_readonly(edit, false);

  my_edit_set_password(edit, true);
  TEST_ASSERT_NOT_NULL(e->masked);
  TEST_ASSERT_EQ_STR(e->masked, "***");
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "abc"); /* real text kept */

  my_widget_unref(edit);
}

static void test_utf8_codepoint_ops(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_edit_t* e = (my_edit_t*)edit;
  my_event_t ev;

  my_edit_set_text(edit, "\xE4\xBD\xA0\xE5\xA5\xBD"); /* 你好 (6 bytes) */
  e->focused = true; /* keys only handled when focused */
  TEST_ASSERT_EQ_INT(e->cursor, 6);

  ev = key_ev(MY_KEY_LEFT, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(e->cursor, 3); /* one codepoint back */

  ev = key_ev(MY_KEY_BACKSPACE, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "\xE5\xA5\xBD"); /* 好 */
  TEST_ASSERT_EQ_INT(e->cursor, 0);

  my_widget_unref(edit);
}

static void test_click_locate_with_bitmap_font(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_edit_t* e = (my_edit_t*)edit;
  my_font_t* font = my_font_bitmap_create(NULL);
  my_event_t ev;

  my_edit_set_font(edit, font, 8);
  my_edit_set_text(edit, "abcd");
  my_widget_set_rect(edit, &(my_rect_t){10, 10, 200, 24});

  /* click between b and c: global x = 10 + 4(pad) + 19px (< midpoint 20) */
  ev = my_event_init(MY_EVENT_POINTER_DOWN);
  ev.u.pointer.x = 33;
  ev.u.pointer.y = 20;
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(e->cursor, 2);

  /* click far left */
  ev.u.pointer.x = 11;
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(e->cursor, 0);

  my_widget_unref(edit);
  my_font_destroy(font);
}

static void test_scroll_keeps_cursor_visible(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_edit_t* e = (my_edit_t*)edit;
  my_font_t* font = my_font_bitmap_create(NULL);

  my_edit_set_font(edit, font, 8);
  my_widget_set_rect(edit, &(my_rect_t){0, 0, 48, 24}); /* inner 40px = 5 cells */
  type_keys(edit, "abcdefghij"); /* 10 cells = 80px */
  TEST_ASSERT(e->scroll_x > 0); /* scrolled to keep the cursor visible */
  TEST_ASSERT(e->scroll_x <= 80 - 40);

  my_widget_unref(edit);
  my_font_destroy(font);
}

static void test_keys_ignored_when_not_focused(void) {
  my_widget_t* edit = my_edit_create(NULL);
  my_event_t ev;
  type_keys(edit, ""); /* sets focused=true in helper; reset below */
  ((my_edit_t*)edit)->focused = false;
  ev = key_ev('x', 0);
  TEST_ASSERT_EQ_INT(edit->vtable->on_event(edit, &ev), MY_RET_FAIL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "");
  my_widget_unref(edit);
}

static void test_activate_and_changed_events(void) {
  my_widget_t* edit = my_edit_create(NULL);
  int changed = 0, activated = 0;
  my_event_t ev;

  my_widget_on(edit, "changed", on_hit, &changed);
  my_widget_on(edit, "activate", on_hit, &activated);

  type_keys(edit, "ab");
  TEST_ASSERT(changed >= 2);

  ev = key_ev(MY_KEY_RETURN, 0);
  edit->vtable->on_event(edit, &ev);
  TEST_ASSERT_EQ_INT(activated, 1);

  /* set_text does NOT fire changed (avoid binding loops) */
  changed = 0;
  my_edit_set_text(edit, "z");
  TEST_ASSERT_EQ_INT(changed, 0);

  my_widget_unref(edit);
}

static void test_focus_via_dispatcher(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  my_widget_t* edit = my_edit_create(NULL);
  my_widget_t* other = my_widget_create(NULL, "other");
  my_edit_t* e = (my_edit_t*)edit;
  my_event_t ev;
  my_event_dispatcher_t d;

  my_widget_set_rect(root, &(my_rect_t){0, 0, 200, 100});
  my_widget_set_rect(edit, &(my_rect_t){0, 0, 100, 24});
  my_widget_set_rect(other, &(my_rect_t){0, 40, 100, 24});
  my_widget_add_child(root, edit);
  my_widget_add_child(root, other);
  my_widget_unref(edit);
  my_widget_unref(other);

  my_event_dispatcher_init(&d, root);

  /* click the edit: gains focus */
  ev = my_event_init(MY_EVENT_POINTER_DOWN);
  ev.u.pointer.x = 50;
  ev.u.pointer.y = 10;
  my_event_dispatch(&d, &ev);
  TEST_ASSERT(e->focused);
  TEST_ASSERT(d.focused == edit);

  /* click elsewhere: blurs, keys stop reaching the edit */
  ev.u.pointer.y = 50;
  my_event_dispatch(&d, &ev);
  TEST_ASSERT(!e->focused);
  TEST_ASSERT_NULL(d.focused);
  {
    my_event_t k = key_ev('q', 0);
    edit->vtable->on_event(edit, &k);
  }
  TEST_ASSERT_EQ_STR(my_edit_get_text(edit), "");

  my_widget_unref(root);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* edit = my_edit_create(dbg);
  int n = 0;

  my_edit_set_hint(edit, "type here");
  my_edit_set_password(edit, true);
  my_edit_set_max_len(edit, 5);
  my_widget_on(edit, "changed", on_hit, &n);
  my_widget_set_rect(edit, &(my_rect_t){0, 0, 100, 24});
  type_keys(edit, "hello!");
  my_edit_set_text(edit, "reset");
  my_widget_invalidate(edit, NULL);

  my_widget_unref(edit);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_insert_and_cursor);
  MYTEST_RUN(test_backspace_and_delete);
  MYTEST_RUN(test_selection_semantics);
  MYTEST_RUN(test_max_len_and_readonly_and_password);
  MYTEST_RUN(test_utf8_codepoint_ops);
  MYTEST_RUN(test_click_locate_with_bitmap_font);
  MYTEST_RUN(test_scroll_keeps_cursor_visible);
  MYTEST_RUN(test_keys_ignored_when_not_focused);
  MYTEST_RUN(test_activate_and_changed_events);
  MYTEST_RUN(test_focus_via_dispatcher);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
