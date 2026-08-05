/**
 * @file my_clipboard_test.c
 * @brief Clipboard (dummy roundtrip) + edit Ctrl+C/X/V + cursor blink.
 */
#include "myui/my_window_manager.h"
#include "myui/widgets/my_edit.h"

#include "mypal/dummy/my_pal_dummy.h"

#include "mytest.h"

typedef struct clip_app_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_widget_t* edit;
} clip_app_t;

static void clip_app_init(clip_app_t* a) {
  a->pal = my_pal_dummy_create(NULL);
  a->loop = my_pal_main_loop_create(a->pal);
  a->wm = my_window_manager_create(NULL, a->pal, a->loop);
  a->win = my_window_create(NULL, a->pal, 300, 100, "clip");
  a->edit = my_edit_create(NULL);
  my_widget_set_rect(a->edit, &(my_rect_t){0, 0, 200, 28});
  my_widget_add_child(my_window_widget(a->win), a->edit);
  my_widget_unref(a->edit);
  my_window_manager_open(a->wm, a->win);
  my_widget_unref(my_window_widget(a->win));
}

static void clip_app_destroy(clip_app_t* a) {
  my_window_manager_destroy(a->wm);
  my_pal_main_loop_destroy(a->loop);
  my_pal_destroy(a->pal);
}

static void key(my_widget_t* edit, uint32_t k, uint8_t mods) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = k;
  e.u.key.modifiers = mods;
  edit->vtable->on_event(edit, &e);
}

static void type_str(my_widget_t* edit, const char* s) {
  ((my_edit_t*)edit)->focused = true;
  while (*s != '\0') {
    key(edit, (uint8_t)*s, 0);
    s++;
  }
}

static void test_dummy_clipboard_roundtrip(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  char buf[64];
  TEST_ASSERT_EQ_INT(my_pal_clipboard_get_text(pal, buf, sizeof(buf)),
                     MY_RET_NOT_FOUND);
  TEST_ASSERT_EQ_INT(my_pal_clipboard_set_text(pal, "hello \xE4\xB8\xAD"),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_pal_clipboard_get_text(pal, buf, sizeof(buf)),
                     MY_RET_OK);
  TEST_ASSERT_EQ_STR(buf, "hello \xE4\xB8\xAD"); /* UTF-8 preserved */
  my_pal_destroy(pal);
}

static void test_copy_cut_paste(void) {
  clip_app_t a;
  char buf[64];
  clip_app_init(&a);

  type_str(a.edit, "hello");
  key(a.edit, 'a', MY_KEYMOD_CTRL); /* select all */
  key(a.edit, 'c', MY_KEYMOD_CTRL); /* copy */
  my_pal_clipboard_get_text(a.pal, buf, sizeof(buf));
  TEST_ASSERT_EQ_STR(buf, "hello");
  TEST_ASSERT_EQ_STR(my_edit_get_text(a.edit), "hello"); /* copy keeps text */

  key(a.edit, 'a', MY_KEYMOD_CTRL);
  key(a.edit, 'x', MY_KEYMOD_CTRL); /* cut */
  TEST_ASSERT_EQ_STR(my_edit_get_text(a.edit), "");
  my_pal_clipboard_get_text(a.pal, buf, sizeof(buf));
  TEST_ASSERT_EQ_STR(buf, "hello");

  key(a.edit, 'v', MY_KEYMOD_CTRL); /* paste */
  TEST_ASSERT_EQ_STR(my_edit_get_text(a.edit), "hello");
  key(a.edit, 'v', MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(a.edit), "hellohello");

  clip_app_destroy(&a);
}

static void test_paste_strips_newlines_and_max_len(void) {
  clip_app_t a;
  clip_app_init(&a);

  my_pal_clipboard_set_text(a.pal, "ab\ncd");
  type_str(a.edit, "");
  key(a.edit, 'v', MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(a.edit), "abcd");

  my_edit_set_text(a.edit, "");
  my_edit_set_max_len(a.edit, 3);
  my_pal_clipboard_set_text(a.pal, "uvwxyz");
  key(a.edit, 'v', MY_KEYMOD_CTRL);
  TEST_ASSERT_EQ_STR(my_edit_get_text(a.edit), "uvw"); /* truncated at 3 */

  clip_app_destroy(&a);
}

static void test_cursor_blink_timer(void) {
  clip_app_t a;
  my_edit_t* e;
  clip_app_init(&a);
  e = (my_edit_t*)a.edit;

  /* focus: blink timer starts */
  my_emitter_emit(a.edit->emitter, "focus", NULL);
  TEST_ASSERT(e->blink_timer_id > 0);
  TEST_ASSERT(e->cursor_visible);

  my_pal_dummy_set_now_ms(a.pal, 600);
  my_pal_main_loop_run(a.loop); /* first 500ms tick fires */
  TEST_ASSERT(!e->cursor_visible);

  my_pal_dummy_set_now_ms(a.pal, 1200);
  my_pal_main_loop_run(a.loop);
  TEST_ASSERT(e->cursor_visible);

  /* blur: timer stops */
  my_emitter_emit(a.edit->emitter, "blur", NULL);
  TEST_ASSERT_EQ_INT(e->blink_timer_id, 0);
  my_pal_dummy_set_now_ms(a.pal, 5000);
  my_pal_main_loop_run(a.loop);
  TEST_ASSERT(e->cursor_visible); /* unchanged */

  clip_app_destroy(&a);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  clip_app_t a;
  a.pal = my_pal_dummy_create(dbg);
  a.loop = my_pal_main_loop_create(a.pal);
  a.wm = my_window_manager_create(dbg, a.pal, a.loop);
  a.win = my_window_create(dbg, a.pal, 300, 100, "clip");
  a.edit = my_edit_create(dbg);
  my_widget_set_rect(a.edit, &(my_rect_t){0, 0, 200, 28});
  my_widget_add_child(my_window_widget(a.win), a.edit);
  my_widget_unref(a.edit);
  my_window_manager_open(a.wm, a.win);
  my_widget_unref(my_window_widget(a.win));

  my_pal_clipboard_set_text(a.pal, "data");
  type_str(a.edit, "ab");
  key(a.edit, 'a', MY_KEYMOD_CTRL);
  key(a.edit, 'c', MY_KEYMOD_CTRL);
  key(a.edit, 'v', MY_KEYMOD_CTRL);
  my_emitter_emit(a.edit->emitter, "focus", NULL);
  my_pal_dummy_set_now_ms(a.pal, 600);
  my_pal_main_loop_run(a.loop);

  clip_app_destroy(&a);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_dummy_clipboard_roundtrip);
  MYTEST_RUN(test_copy_cut_paste);
  MYTEST_RUN(test_paste_strips_newlines_and_max_len);
  MYTEST_RUN(test_cursor_blink_timer);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
