/**
 * @file my_csd_test.c
 * @brief Client-side decoration tests (M16): CSD window structure
 * (bar + content container, my_window_widget semantics), layout/resize
 * geometry, begin_move routing, close button -> deferred wm close,
 * non-CSD baseline, leaks.
 */
#include "myc/my_str.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

typedef struct fx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
} fx_t;

static void fx_init(fx_t* f, bool csd) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  my_pal_dummy_set_needs_csd(f->pal, csd);
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static void test_non_csd_baseline(void) {
  fx_t f;
  my_window_t* win;
  fx_init(&f, false);
  win = my_window_create(NULL, f.pal, 400, 300, "plain");
  TEST_ASSERT(!win->csd);
  TEST_ASSERT(my_window_widget(win) == (my_widget_t*)win);
  TEST_ASSERT_EQ_INT((int)my_widget_child_count((my_widget_t*)win), 0);
  my_window_manager_open(f.wm, win);
  my_widget_unref(my_window_widget(win));
  fx_destroy(&f);
}

static void test_csd_structure_and_layout(void) {
  fx_t f;
  my_window_t* win;
  my_widget_t* root;
  my_widget_t* bar;
  my_widget_t* content;
  my_widget_t* app_child;
  fx_init(&f, true);
  win = my_window_create(NULL, f.pal, 400, 300, "我的窗口");
  TEST_ASSERT(win->csd);
  TEST_ASSERT(win->title != NULL);
  root = (my_widget_t*)win;
  /* root = vertical linear[bar h:36, content h:1f] */
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(root), 2);
  bar = my_widget_get_child(root, 0);
  content = my_widget_get_child(root, 1);
  TEST_ASSERT(my_str_eq(content->base.name, "csd_content"));
  TEST_ASSERT(my_window_widget(win) == content);
  /* app children land in the content container */
  app_child = my_widget_create(NULL, "app");
  my_widget_add_child(my_window_widget(win), app_child);
  my_widget_unref(app_child);
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(content), 1);
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(root), 2); /* not on root */
  /* layout: bar 400x36 at top, content takes the rest */
  my_widget_relayout(root);
  TEST_ASSERT_EQ_INT(bar->rect.y, 0);
  TEST_ASSERT_EQ_INT(bar->rect.h, 36);
  TEST_ASSERT_EQ_INT(bar->rect.w, 400);
  TEST_ASSERT_EQ_INT(content->rect.y, 36);
  TEST_ASSERT_EQ_INT(content->rect.h, 264);
  /* close button glued right inside the bar */
  {
    my_widget_t* btn = my_widget_get_child(bar, 0);
    TEST_ASSERT_EQ_INT(btn->rect.x, 400 - 32);
    TEST_ASSERT_EQ_INT(btn->rect.h, 36);
  }
  /* resize reflows */
  {
    my_event_t e = my_event_init(MY_EVENT_RESIZE);
    e.u.resize.w = 500;
    e.u.resize.h = 400;
    my_window_on_pal_event(win, &e);
    my_widget_relayout(root);
  }
  TEST_ASSERT_EQ_INT(bar->rect.w, 500);
  TEST_ASSERT_EQ_INT(content->rect.h, 400 - 36);
  TEST_ASSERT_EQ_INT(my_widget_get_child(bar, 0)->rect.x, 500 - 32);
  my_window_manager_open(f.wm, win);
  my_widget_unref(my_window_widget(win));
  fx_destroy(&f);
}

static void test_csd_bar_begins_move(void) {
  fx_t f;
  my_window_t* win;
  my_event_t e;
  fx_init(&f, true);
  win = my_window_create(NULL, f.pal, 400, 300, "t");
  my_window_manager_open(f.wm, win);
  my_widget_unref(my_window_widget(win));
  my_widget_relayout((my_widget_t*)win);
  /* DOWN on the bar body (not the close button) */
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 100;
  e.u.pointer.y = 18;
  my_pal_dummy_inject_event(f.pal, win->pal_window, &e);
  TEST_ASSERT_EQ_INT((int)my_pal_dummy_begin_move_count(win->pal_window), 1);
  /* DOWN on the close button must NOT begin a move (child eats it) */
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 400 - 16;
  e.u.pointer.y = 18;
  my_pal_dummy_inject_event(f.pal, win->pal_window, &e);
  TEST_ASSERT_EQ_INT((int)my_pal_dummy_begin_move_count(win->pal_window), 1);
  fx_destroy(&f);
}

static void test_csd_close_button_closes_window(void) {
  fx_t f;
  my_window_t* win;
  my_event_t e;
  fx_init(&f, true);
  win = my_window_create(NULL, f.pal, 400, 300, "t");
  my_window_manager_open(f.wm, win);
  my_widget_unref(my_window_widget(win));
  my_widget_relayout((my_widget_t*)win);
  TEST_ASSERT_EQ_INT(my_window_manager_count(f.wm), 1);
  /* click the close button: close is deferred to a 1ms timer */
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = 400 - 16;
  e.u.pointer.y = 18;
  my_pal_dummy_inject_event(f.pal, win->pal_window, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = 400 - 16;
  e.u.pointer.y = 18;
  my_pal_dummy_inject_event(f.pal, win->pal_window, &e);
  TEST_ASSERT_EQ_INT(my_window_manager_count(f.wm), 1); /* not yet */
  my_pal_dummy_set_now_ms(f.pal, 10);
  my_pal_main_loop_run(f.loop); /* fires the deferred close */
  TEST_ASSERT_EQ_INT(my_window_manager_count(f.wm), 0);
  TEST_ASSERT(f.wm->quit_requested);
  fx_destroy(&f);
}

static void test_csd_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win;
  my_pal_dummy_set_needs_csd(pal, true);
  win = my_window_create(dbg, pal, 400, 300, "csd");
  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win)); /* the universal pattern */
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

static void test_csd_bar_renders(void) {
  fx_t f;
  my_window_t* win;
  rec_vg_t rec;
  static my_font_t* bmp = NULL;
  fx_init(&f, true);
  win = my_window_create(NULL, f.pal, 400, 300, "短线侠");
  my_widget_relayout((my_widget_t*)win);
  if (bmp == NULL) {
    bmp = my_font_bitmap_create(NULL);
  }
  rec_vg_init(&rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)&rec, bmp, 13);
  my_widget_paint(my_widget_get_child((my_widget_t*)win, 0),
                  (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "set_fill #3c4043")); /* bar bg */
  TEST_ASSERT(rec_has(&rec, "短线侠"));           /* centered title */
  my_window_manager_open(f.wm, win);
  my_widget_unref(my_window_widget(win));
  fx_destroy(&f);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_non_csd_baseline);
  MYTEST_RUN(test_csd_structure_and_layout);
  MYTEST_RUN(test_csd_bar_renders);
  MYTEST_RUN(test_csd_bar_begins_move);
  MYTEST_RUN(test_csd_close_button_closes_window);
  MYTEST_RUN(test_csd_no_leak);
MYTEST_MAIN_END()
