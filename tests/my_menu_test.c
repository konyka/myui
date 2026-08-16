/**
 * @file my_menu_test.c
 * @brief Popup menu tests (M13c): popup/select, outside-click dismiss,
 * edge flip, submenu cascade, keyboard nav, leaks.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_menu.h"

#include "mytest.h"

static int g_selected = -999;
static int g_selects;
static void on_select(void* ctx, int32_t id) {
  (void)ctx;
  g_selected = id;
  g_selects++;
}

typedef struct fx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
} fx_t;

static void fx_init(fx_t* f) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  f->win = my_window_create(NULL, f->pal, 400, 300, "main");
  my_window_manager_open(f->wm, f->win);
  my_widget_unref(my_window_widget(f->win));
  g_selected = -999;
  g_selects = 0;
}

static void fx_destroy(fx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static void click(fx_t* f, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
}

static void key(fx_t* f, int32_t k) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = k;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
}

static void move(fx_t* f, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_MOVE);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_pal_dummy_inject_event(f->pal, f->win->pal_window, &e);
}

static void advance(fx_t* f, uint32_t ms) {
  my_pal_dummy_set_now_ms(f->pal, ms);
  my_pal_main_loop_pump_n(f->loop, 10);
  /* pump_n only drains queued events; timers fire during run() when the
   * queue is empty. */
  my_pal_main_loop_run(f->loop);
}

/** @brief The open menu box = child 0 of the root's last child (overlay). */
static my_widget_t* menu_box(fx_t* f) {
  my_widget_t* root = my_window_widget(f->win);
  size_t n = my_widget_child_count(root);
  my_widget_t* ov;
  if (n == 0) {
    return NULL;
  }
  ov = my_widget_get_child(root, n - 1);
  if (my_widget_child_count(ov) == 0) {
    return NULL;
  }
  return my_widget_get_child(ov, 0);
}

static size_t root_child_count(fx_t* f) {
  return my_widget_child_count(my_window_widget(f->win));
}

static void test_popup_and_select(void) {
  fx_t f;
  my_menu_t* m;
  fx_init(&f);
  m = my_menu_create(NULL);
  my_menu_add_item(m, "Alpha", 1);
  my_menu_add_item(m, "Beta", 2);
  TEST_ASSERT(my_menu_popup(f.win, m, 10, 10, on_select, NULL) == MY_RET_OK);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 1);
  /* item 1 ("Beta"): box at (10,10), items h=24, pad=4 -> y 10+4+24+12 */
  click(&f, 30, 10 + 4 + 24 + 12);
  TEST_ASSERT_EQ_INT(g_selected, 2);
  TEST_ASSERT_EQ_INT(g_selects, 1);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 0); /* dismissed */
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_outside_click_dismisses(void) {
  fx_t f;
  my_menu_t* m;
  fx_init(&f);
  m = my_menu_create(NULL);
  my_menu_add_item(m, "Alpha", 1);
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  click(&f, 300, 250); /* far outside the box */
  TEST_ASSERT_EQ_INT(g_selects, 0);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 0);
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_edge_flip(void) {
  fx_t f;
  my_menu_t* m;
  my_widget_t* box;
  fx_init(&f);
  m = my_menu_create(NULL);
  my_menu_add_item(m, "Alpha", 1); /* bw = 80, bh = 24+8 = 32 */
  my_menu_popup(f.win, m, 390, 290, on_select, NULL);
  box = menu_box(&f);
  TEST_ASSERT(box != NULL);
  TEST_ASSERT(box->rect.x + box->rect.w <= 400);
  TEST_ASSERT(box->rect.y + box->rect.h <= 300);
  TEST_ASSERT(box->rect.x >= 0);
  TEST_ASSERT(box->rect.y >= 0);
  my_menu_dismiss(m);
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_submenu_cascade(void) {
  fx_t f;
  my_menu_t* m;
  my_menu_t* sub;
  my_widget_t* box;
  fx_init(&f);
  m = my_menu_create(NULL);
  sub = my_menu_add_submenu(m, "More");
  my_menu_add_item(sub, "Deep", 7);
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  box = menu_box(&f);
  TEST_ASSERT(box != NULL);
  /* click the "More" item (index 0): opens the cascade */
  click(&f, 20, 10 + 4 + 12);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 2); /* two overlays */
  /* submenu box x = parent box right edge; click its first item */
  {
    my_widget_t* root = my_window_widget(f.win);
    my_widget_t* ov2 = my_widget_get_child(root, root_child_count(&f) - 1);
    my_widget_t* box2 = my_widget_get_child(ov2, 0);
    int32_t cx = box2->rect.x + 10;
    int32_t cy = box2->rect.y + 4 + 12;
    TEST_ASSERT(box2->rect.x == box->rect.x + box->rect.w);
    click(&f, cx, cy);
  }
  TEST_ASSERT_EQ_INT(g_selected, 7);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 0); /* all dismissed */
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_keyboard_nav(void) {
  fx_t f;
  my_menu_t* m;
  fx_init(&f);
  m = my_menu_create(NULL);
  my_menu_add_item(m, "Alpha", 1);
  my_menu_add_item(m, "Beta", 2);
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  key(&f, MY_KEY_DOWN); /* active = 0 */
  key(&f, MY_KEY_DOWN); /* active = 1 */
  key(&f, MY_KEY_RETURN);
  TEST_ASSERT_EQ_INT(g_selected, 2);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 0);

  /* ESC dismisses without selecting */
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  key(&f, MY_KEY_DOWN);
  key(&f, MY_KEY_ESCAPE);
  TEST_ASSERT_EQ_INT(g_selects, 1); /* unchanged */
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 0);
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_menu_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_menu_t* m = my_menu_create(dbg);
  my_menu_t* sub = my_menu_add_submenu(m, "More");
  my_menu_add_item(m, "Alpha", 1);
  my_menu_add_item(sub, "Deep", 7);
  my_menu_destroy(m);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

static void test_max_depth_api(void) {
  my_menu_t* m = my_menu_create(NULL);
  TEST_ASSERT_EQ_INT(my_menu_max_depth(m), 3);
  my_menu_set_max_depth(m, 5);
  TEST_ASSERT_EQ_INT(my_menu_max_depth(m), 5);
  my_menu_set_max_depth(m, 1);
  TEST_ASSERT_EQ_INT(my_menu_max_depth(m), 1);
  my_menu_set_max_depth(m, 0); /* clamp to 1 */
  TEST_ASSERT_EQ_INT(my_menu_max_depth(m), 1);
  my_menu_set_max_depth(m, -3); /* clamp to 1 */
  TEST_ASSERT_EQ_INT(my_menu_max_depth(m), 1);
  my_menu_destroy(m);
}

static void test_deep_cascade(void) {
  my_menu_t* m = my_menu_create(NULL);
  my_menu_t* l2;
  my_menu_t* l3;
  my_menu_t* l4;
  my_menu_t* l5;
  my_menu_set_max_depth(m, 5);
  l2 = my_menu_add_submenu(m, "L2");
  TEST_ASSERT_NOT_NULL(l2);
  l3 = my_menu_add_submenu(l2, "L3");
  TEST_ASSERT_NOT_NULL(l3);
  l4 = my_menu_add_submenu(l3, "L4");
  TEST_ASSERT_NOT_NULL(l4);
  l5 = my_menu_add_submenu(l4, "L5");
  TEST_ASSERT_NOT_NULL(l5);
  TEST_ASSERT_NULL(my_menu_add_submenu(l5, "L6")); /* depth 6 > 5 */
  my_menu_destroy(m);
}

static void test_hover_opens_submenu(void) {
  fx_t f;
  my_menu_t* m;
  my_menu_t* sub;
  fx_init(&f);
  m = my_menu_create(NULL);
  sub = my_menu_add_submenu(m, "More");
  my_menu_add_item(sub, "Deep", 7);
  my_menu_add_item(m, "Alpha", 1);
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  /* hover over the "More" item at y=10+4+12 (index 0) */
  move(&f, 20, 10 + 4 + 12);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 1); /* timer pending, no sub yet */
  /* before timer fires, nothing opens */
  advance(&f, 50);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 1);
  /* after 120ms the submenu opens */
  advance(&f, 150);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 2);
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_hover_to_leaf_closes_submenu(void) {
  fx_t f;
  my_menu_t* m;
  my_menu_t* sub;
  fx_init(&f);
  m = my_menu_create(NULL);
  sub = my_menu_add_submenu(m, "More");
  my_menu_add_item(sub, "Deep", 7);
  my_menu_add_item(m, "Alpha", 1);
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  /* open submenu by hover */
  move(&f, 20, 10 + 4 + 12);
  advance(&f, 150);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 2);
  /* hover over the leaf item (index 1) */
  move(&f, 20, 10 + 4 + 12 + 24);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 1); /* sub closed immediately */
  my_menu_destroy(m);
  fx_destroy(&f);
}

static void test_esc_back_one_level(void) {
  fx_t f;
  my_menu_t* m;
  my_menu_t* sub;
  fx_init(&f);
  m = my_menu_create(NULL);
  sub = my_menu_add_submenu(m, "More");
  my_menu_add_item(sub, "Deep", 7);
  my_menu_popup(f.win, m, 10, 10, on_select, NULL);
  /* open submenu by hover */
  move(&f, 20, 10 + 4 + 12);
  advance(&f, 150);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 2);
  /* ESC closes only the submenu layer */
  key(&f, MY_KEY_ESCAPE);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 1);
  /* another ESC closes the root menu */
  key(&f, MY_KEY_ESCAPE);
  TEST_ASSERT_EQ_INT((int)root_child_count(&f), 0);
  my_menu_destroy(m);
  fx_destroy(&f);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_popup_and_select);
  MYTEST_RUN(test_outside_click_dismisses);
  MYTEST_RUN(test_edge_flip);
  MYTEST_RUN(test_submenu_cascade);
  MYTEST_RUN(test_keyboard_nav);
  MYTEST_RUN(test_menu_no_leak);
  MYTEST_RUN(test_max_depth_api);
  MYTEST_RUN(test_deep_cascade);
  MYTEST_RUN(test_hover_opens_submenu);
  MYTEST_RUN(test_hover_to_leaf_closes_submenu);
  MYTEST_RUN(test_esc_back_one_level);
MYTEST_MAIN_END()
