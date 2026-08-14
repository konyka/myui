/**
 * @file my_node_view_test.c
 * @brief Node editor tests (M19b): link model (connect/replace/
 * disconnect/find), node drag with links following, preview connect /
 * cancel / pickup-reconnect, select + Del, pan, CSS part colors, leaks.
 */
#include "myui/widgets/my_node_view.h"

#include "myui/my_css.h"
#include "myui/my_event_dispatch.h"
#include "myui/my_layout.h"
#include "myui/my_theme.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

typedef struct fx_t {
  my_widget_t* root;
  my_event_dispatcher_t d;
  my_widget_t* view;
  my_widget_t* na; /* 输出节点 (100,100,160,80): out socket 0 */
  my_widget_t* nb; /* 输入节点 (400,200,160,80): in socket 0 */
} fx_t;

static int g_changed;
static void on_changed(void* ctx, const char* ev, void* data) {
  (void)ctx;
  (void)ev;
  (void)data;
  g_changed++;
}

static void fx_init(fx_t* f) {
  f->root = my_widget_create(NULL, "root");
  my_widget_set_rect(f->root, &(my_rect_t){0, 0, 800, 600});
  my_event_dispatcher_init(&f->d, f->root);
  f->view = my_node_view_create(NULL);
  my_widget_set_rect(f->view, &(my_rect_t){0, 0, 800, 600});
  my_widget_add_child(f->root, f->view);
  my_widget_unref(f->view);
  my_widget_on(f->view, "changed", on_changed, NULL);
  f->na = my_node_view_add_node(f->view, "a", "节点A", "shader", 100, 100,
                                160, 80);
  my_node_add_socket(f->na, MY_SOCKET_OUT, "输出", 0x60A060FFu);
  f->nb = my_node_view_add_node(f->view, "b", "节点B", "color", 400, 200,
                                160, 80);
  my_node_add_socket(f->nb, MY_SOCKET_IN, "输入", 0xA06060FFu);
  g_changed = 0;
}

static void fx_destroy(fx_t* f) {
  my_widget_unref(f->root);
}

static void ev(fx_t* f, my_event_type_t type, int32_t x, int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_event_dispatch(&f->d, &e);
}

static void key(fx_t* f, uint32_t k) {
  my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
  e.u.key.key = k;
  my_event_dispatch(&f->d, &e);
}

/* out socket of na = (260, 134); in socket of nb = (400, 234) */
#define OUT_X 260
#define OUT_Y 134
#define IN_X 400
#define IN_Y 234

static void test_model_connect_replace_disconnect(void) {
  fx_t f;
  my_widget_t* out_n = NULL;
  size_t out_s = 99;
  fx_init(&f);
  TEST_ASSERT_EQ_INT(my_node_view_connect(f.view, f.na, 0, f.nb, 0),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1);
  TEST_ASSERT_EQ_INT(g_changed, 1);
  TEST_ASSERT(my_node_view_get_link(f.view, 0, &out_n, &out_s, NULL, NULL));
  TEST_ASSERT(out_n == f.na && out_s == 0);
  /* replace: same input slot */
  {
    my_widget_t* nc = my_node_view_add_node(f.view, "c", "节点C", NULL, 100,
                                            300, 160, 80);
    my_node_add_socket(nc, MY_SOCKET_OUT, "输出2", 0x6060A0FFu);
    TEST_ASSERT_EQ_INT(my_node_view_connect(f.view, nc, 0, f.nb, 0),
                       MY_RET_OK);
    TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1); /* replaced */
    TEST_ASSERT(my_node_view_get_link(f.view, 0, &out_n, NULL, NULL, NULL));
    TEST_ASSERT(out_n == nc);
  }
  TEST_ASSERT_EQ_INT(g_changed, 2);
  TEST_ASSERT_EQ_INT(my_node_view_disconnect_in(f.view, f.nb, 0), MY_RET_OK);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0);
  TEST_ASSERT_EQ_INT(g_changed, 3);
  TEST_ASSERT_EQ_INT(my_node_view_disconnect_in(f.view, f.nb, 0),
                     MY_RET_NOT_FOUND);
  fx_destroy(&f);
}

static void test_drag_preview_connect(void) {
  fx_t f;
  fx_init(&f);
  /* drag from na's out socket to nb's in socket */
  ev(&f, MY_EVENT_POINTER_DOWN, OUT_X, OUT_Y);
  ev(&f, MY_EVENT_POINTER_MOVE, 330, 180);
  ev(&f, MY_EVENT_POINTER_UP, IN_X, IN_Y);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1);
  TEST_ASSERT_EQ_INT(g_changed, 1);
  /* drag to empty space: cancel */
  ev(&f, MY_EVENT_POINTER_DOWN, OUT_X, OUT_Y);
  ev(&f, MY_EVENT_POINTER_MOVE, 330, 180);
  ev(&f, MY_EVENT_POINTER_UP, 330, 180);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1); /* unchanged */
  fx_destroy(&f);
}

static void test_pickup_connected_input_reconnects(void) {
  fx_t f;
  my_widget_t* nc;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  /* a third node with its own input socket */
  nc = my_node_view_add_node(f.view, "c", "节点C", NULL, 400, 400, 160, 80);
  my_node_add_socket(nc, MY_SOCKET_IN, "输入", 0x60A060FFu);
  g_changed = 0;
  /* drag out of nb's connected input: disconnects immediately, preview
   * from na's out; drop on nc's input -> reconnect */
  ev(&f, MY_EVENT_POINTER_DOWN, IN_X, IN_Y);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0); /* picked up */
  TEST_ASSERT_EQ_INT(g_changed, 1);
  ev(&f, MY_EVENT_POINTER_UP, 400, 434); /* nc in socket (400, 424+10) */
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1);
  {
    my_widget_t* in_n = NULL;
    my_node_view_get_link(f.view, 0, NULL, NULL, &in_n, NULL);
    TEST_ASSERT(in_n == nc);
  }
  fx_destroy(&f);
}

static void test_select_and_delete_link(void) {
  fx_t f;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  /* the link midpoint is around (330, 184) — computed via find itself */
  {
    int32_t li = my_node_view_find_link_at(f.view, 330, 184);
    TEST_ASSERT(li >= 0);
    /* clicking the link selects it */
    ev(&f, MY_EVENT_POINTER_DOWN, 330, 184);
    ev(&f, MY_EVENT_POINTER_UP, 330, 184);
    TEST_ASSERT_EQ_INT(my_node_view_get_selected(f.view), 0);
  }
  /* Del removes it (view is focusable; focus via the click above) */
  g_changed = 0;
  key(&f, MY_KEY_DELETE);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0);
  TEST_ASSERT_EQ_INT(g_changed, 1);
  /* far point finds nothing */
  TEST_ASSERT_EQ_INT(my_node_view_find_link_at(f.view, 700, 500), -1);
  fx_destroy(&f);
}

static void test_node_drag_moves_link(void) {
  fx_t f;
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  my_node_socket_center(f.na, MY_SOCKET_OUT, 0, &x0, &y0);
  /* drag na's title bar (140, 110) by (+50, +30) */
  ev(&f, MY_EVENT_POINTER_DOWN, 140, 110);
  ev(&f, MY_EVENT_POINTER_MOVE, 190, 140);
  ev(&f, MY_EVENT_POINTER_UP, 190, 140);
  TEST_ASSERT_EQ_INT(f.na->rect.x, 150);
  TEST_ASSERT_EQ_INT(f.na->rect.y, 130);
  my_node_socket_center(f.na, MY_SOCKET_OUT, 0, &x1, &y1);
  TEST_ASSERT_EQ_INT(x1, x0 + 50); /* link endpoints follow */
  TEST_ASSERT_EQ_INT(y1, y0 + 30);
  /* find_link follows too: after the drag the bezier midpoint is
   * (355, 199) (computed from the moved endpoints) */
  TEST_ASSERT(my_node_view_find_link_at(f.view, 355, 199) >= 0);
  fx_destroy(&f);
}

static void test_pan_moves_nodes(void) {
  fx_t f;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  /* drag empty space (700, 500) by (+20, -10) */
  ev(&f, MY_EVENT_POINTER_DOWN, 700, 500);
  ev(&f, MY_EVENT_POINTER_MOVE, 720, 490);
  ev(&f, MY_EVENT_POINTER_UP, 720, 490);
  TEST_ASSERT_EQ_INT(f.na->rect.x, 120);
  TEST_ASSERT_EQ_INT(f.na->rect.y, 90);
  TEST_ASSERT_EQ_INT(f.nb->rect.x, 420);
  fx_destroy(&f);
}

static void test_css_part_colors(void) {
  fx_t f;
  my_theme_t* t = my_theme_create(NULL);
  rec_vg_t rec;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  my_theme_load_css(t,
                    "node_view { background-color: #101010 } "
                    "node { background-color: #202020 } "
                    "node .header { background-color: #663300 } "
                    "node_socket.output { background-color: #00FF00 } "
                    "node_link { color: #FF00FF } "
                    "node_link.preview { color: #00FFFF }");
  my_widget_apply_theme(f.view, t);
  my_widget_relayout(f.root);
  rec_vg_init(&rec);
  my_widget_paint(f.root, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "set_fill #101010")); /* canvas bg */
  TEST_ASSERT(rec_has(&rec, "set_fill #663300")); /* header via后代 */
  TEST_ASSERT(rec_has(&rec, "set_fill #00ff00")); /* out socket */
  TEST_ASSERT(rec_has(&rec, "set_stroke #ff00ff")); /* link color */
  my_theme_destroy(t);
  fx_destroy(&f);
}

static void test_node_view_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* view = my_node_view_create(dbg);
  my_widget_t* a = my_node_view_add_node(view, "a", "A", "shader", 0, 0,
                                         160, 80);
  my_widget_t* b = my_node_view_add_node(view, "b", "B", NULL, 300, 100,
                                         160, 80);
  my_node_add_socket(a, MY_SOCKET_OUT, "o", 0xFF0000FFu);
  my_node_add_socket(b, MY_SOCKET_IN, "i", 0x00FF00FFu);
  my_node_view_connect(view, a, 0, b, 0);
  my_node_view_disconnect_in(view, b, 0);
  my_widget_unref(view);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_model_connect_replace_disconnect);
  MYTEST_RUN(test_drag_preview_connect);
  MYTEST_RUN(test_pickup_connected_input_reconnects);
  MYTEST_RUN(test_select_and_delete_link);
  MYTEST_RUN(test_node_drag_moves_link);
  MYTEST_RUN(test_pan_moves_nodes);
  MYTEST_RUN(test_css_part_colors);
  MYTEST_RUN(test_node_view_no_leak);
MYTEST_MAIN_END()
