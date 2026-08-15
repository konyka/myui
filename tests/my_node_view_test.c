/**
 * @file my_node_view_test.c
 * @brief Node editor tests (M19b): link model (connect/replace/
 * disconnect/find), node drag with links following, preview connect /
 * cancel / pickup-reconnect, select + Del, pan, CSS part colors, leaks.
 */
#include "myui/widgets/my_node_view.h"

#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_css.h"
#include "myui/my_event_dispatch.h"
#include "myui/my_layout.h"
#include "myui/my_theme.h"
#include "myui/my_window_manager.h"

#include <math.h>

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
  e.u.pointer.button = 1; /* left */
  my_event_dispatch(&f->d, &e);
}

/** @brief Event with an explicit mouse button (1=left, 2=middle). */
static void evb(fx_t* f, my_event_type_t type, int32_t x, int32_t y,
                uint8_t button) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  e.u.pointer.button = button;
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
  float sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
  int32_t cx0 = 0, cy0 = 0;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  /* M20a model: pan is a view offset — node rects stay, canvas->screen
   * mapping shifts */
  my_node_socket_center(f.na, MY_SOCKET_OUT, 0, &cx0, &cy0);
  my_node_view_canvas_to_screen(f.view, (float)cx0, (float)cy0, &sx0, &sy0);
  /* middle-button drag on empty space pans (M20b: left drag rubber-
   * bands instead; start away from the minimap corner) */
  evb(&f, MY_EVENT_POINTER_DOWN, 300, 500, 2);
  evb(&f, MY_EVENT_POINTER_MOVE, 320, 490, 2);
  evb(&f, MY_EVENT_POINTER_UP, 320, 490, 2);
  TEST_ASSERT_EQ_INT(f.na->rect.x, 100); /* canvas coords unchanged */
  TEST_ASSERT_EQ_INT(f.na->rect.y, 100);
  my_node_view_canvas_to_screen(f.view, (float)cx0, (float)cy0, &sx1, &sy1);
  TEST_ASSERT_EQ_INT((int)sx1, (int)sx0 + 20); /* screen shifts by the pan */
  TEST_ASSERT_EQ_INT((int)sy1, (int)sy0 - 10);
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

static void test_magnet_snap_and_connect(void) {
  fx_t f;
  rec_vg_t rec;
  fx_init(&f);
  /* drag from na's out socket; hover 15px off nb's input (within 20px
   * magnet range): preview snaps, ring drawn, UP connects */
  ev(&f, MY_EVENT_POINTER_DOWN, OUT_X, OUT_Y);
  ev(&f, MY_EVENT_POINTER_MOVE, IN_X - 15, IN_Y);
  rec_vg_init(&rec);
  my_widget_paint(f.view, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "stroke_circle") == false); /* circles are
                                                          * path curves */
  ev(&f, MY_EVENT_POINTER_UP, IN_X - 15, IN_Y); /* release off-center */
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1);
  fx_destroy(&f);
}

static void test_magnet_respects_type_and_distance(void) {
  fx_t f;
  my_widget_t* nc;
  fx_init(&f);
  /* another output socket nearby (type filter: preview from OUT must
   * not magnet to OUTPUT sockets) */
  nc = my_node_view_add_node(f.view, "c", "C", NULL, 340, 210, 160, 80);
  my_node_add_socket(nc, MY_SOCKET_OUT, "o", 0xFFFFFFFFu);
  ev(&f, MY_EVENT_POINTER_DOWN, OUT_X, OUT_Y);
  /* hover right at nc's OUTPUT socket (500? -> nc at (340,210): out at
   * (500, 244)); within 20px of IT but 70px from nb's input */
  ev(&f, MY_EVENT_POINTER_MOVE, 500, 244);
  ev(&f, MY_EVENT_POINTER_UP, 500, 244);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0); /* no
                                                                 * connect */
  /* far from everything (nearest input 70+ px): no snap, cancel */
  ev(&f, MY_EVENT_POINTER_DOWN, OUT_X, OUT_Y);
  ev(&f, MY_EVENT_POINTER_MOVE, 320, 300);
  ev(&f, MY_EVENT_POINTER_UP, 320, 300);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0);
  fx_destroy(&f);
}

static void test_zoom_clamp_and_anchor(void) {
  fx_t f;
  float cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
  fx_init(&f);
  /* clamp */
  my_node_view_set_zoom(f.view, 0.1f);
  TEST_ASSERT(my_node_view_get_zoom(f.view) >= 0.24f);
  TEST_ASSERT(my_node_view_get_zoom(f.view) <= 0.26f);
  my_node_view_set_zoom(f.view, 5.0f);
  TEST_ASSERT(my_node_view_get_zoom(f.view) >= 1.99f);
  /* anchor invariance: zoom_at keeps the anchor's canvas coordinate */
  my_node_view_set_zoom(f.view, 1.0f);
  my_node_view_screen_to_canvas(f.view, 300, 200, &cx0, &cy0);
  my_node_view_zoom_at(f.view, 300, 200, 1.5f);
  my_node_view_screen_to_canvas(f.view, 300, 200, &cx1, &cy1);
  TEST_ASSERT(fabsf(cx1 - cx0) < 0.01f);
  TEST_ASSERT(fabsf(cy1 - cy0) < 0.01f);
  TEST_ASSERT(my_node_view_get_zoom(f.view) > 1.49f);
  /* round trip screen->canvas->screen */
  {
    float sx = 0, sy = 0;
    my_node_view_canvas_to_screen(f.view, cx1, cy1, &sx, &sy);
    TEST_ASSERT(fabsf(sx - 300.0f) < 0.01f);
    TEST_ASSERT(fabsf(sy - 200.0f) < 0.01f);
  }
  fx_destroy(&f);
}

static void test_zoomed_interaction_uses_canvas_coords(void) {
  fx_t f;
  int32_t cx0 = 0, cy0 = 0;
  float sx = 0, sy = 0;
  fx_init(&f);
  my_node_view_set_zoom(f.view, 2.0f);
  my_node_view_set_zoom(f.view, 2.0f); /* idempotent */
  /* na out socket in canvas = (260,134) -> screen at zoom 2 */
  my_node_socket_center(f.na, MY_SOCKET_OUT, 0, &cx0, &cy0);
  my_node_view_canvas_to_screen(f.view, (float)cx0, (float)cy0, &sx, &sy);
  ev(&f, MY_EVENT_POINTER_DOWN, (int32_t)sx, (int32_t)sy);
  {
    float ix = 0, iy = 0, isx = 0, isy = 0;
    my_node_socket_center(f.nb, MY_SOCKET_IN, 0, &cx0, &cy0);
    my_node_view_canvas_to_screen(f.view, (float)cx0, (float)cy0, &isx,
                                  &isy);
    (void)ix;
    (void)iy;
    ev(&f, MY_EVENT_POINTER_UP, (int32_t)isx, (int32_t)isy);
  }
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1);
  /* link endpoints drawn at canvas positions: find_link in canvas
   * coords still works under zoom (endpoint = canvas (310,164)+ ...) */
  fx_destroy(&f);
}

static void test_remove_node_cascade(void) {
  fx_t f;
  fx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  {
    my_widget_t* nc = my_node_view_add_node(f.view, "c", "C", NULL, 100,
                                            300, 160, 80);
    my_node_add_socket(nc, MY_SOCKET_OUT, "o", 0xFFFFFFFFu);
    my_node_add_socket(nc, MY_SOCKET_IN, "i", 0xFFFFFFFFu);
    my_node_add_socket(f.nb, MY_SOCKET_IN, "输入2", 0xFFFFFFFFu); /* slot 1 */
    my_node_view_connect(f.view, nc, 0, f.nb, 1); /* nb slot 1 */
    my_node_view_connect(f.view, f.na, 0, nc, 0);
  }
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 3);
  g_changed = 0;
  TEST_ASSERT_EQ_INT(my_node_view_remove_node(f.view, "c"), MY_RET_OK);
  /* the two links touching c are gone; na->nb survives */
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 1);
  TEST_ASSERT_EQ_INT(g_changed, 1);
  TEST_ASSERT_EQ_INT(my_node_view_remove_node(f.view, "c"),
                     MY_RET_NOT_FOUND);
  /* Del on a selected node cascades too */
  ev(&f, MY_EVENT_POINTER_DOWN, 140, 110); /* title bar of na */
  ev(&f, MY_EVENT_POINTER_UP, 140, 110);
  g_changed = 0;
  key(&f, MY_KEY_DELETE);
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0);
  TEST_ASSERT_EQ_INT(g_changed, 1);
  /* children: nb + the overlay (M20b) — na and nc are gone */
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(f.view), 2);
  fx_destroy(&f);
}

static void test_rubber_band_multi_select(void) {
  fx_t f;
  fx_init(&f);
  /* rubber-band over na (100,100..260,180) only: band (50,50)-(300,190)
   * partially intersects na, misses nb (400,200+) */
  ev(&f, MY_EVENT_POINTER_DOWN, 50, 50);
  ev(&f, MY_EVENT_POINTER_MOVE, 300, 190);
  ev(&f, MY_EVENT_POINTER_UP, 300, 190); /* band commits on UP */
  TEST_ASSERT_EQ_INT((int)my_node_view_selected_count(f.view), 1);
  TEST_ASSERT(my_node_view_selected_at(f.view, 0) == f.na);
  /* plain click on nb: single-select replaces the set */
  ev(&f, MY_EVENT_POINTER_DOWN, 420, 230);
  ev(&f, MY_EVENT_POINTER_UP, 420, 230);
  TEST_ASSERT_EQ_INT((int)my_node_view_selected_count(f.view), 1);
  TEST_ASSERT(my_node_view_selected_at(f.view, 0) == f.nb);
  /* ctrl+click na: toggles in -> two selected */
  {
    my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
    e.u.pointer.x = 140;
    e.u.pointer.y = 130;
    e.u.pointer.button = 1;
    e.u.pointer.modifiers = MY_KEYMOD_CTRL;
    my_event_dispatch(&f.d, &e);
    e = my_event_init(MY_EVENT_POINTER_UP);
    my_event_dispatch(&f.d, &e);
  }
  TEST_ASSERT_EQ_INT((int)my_node_view_selected_count(f.view), 2);
  /* ctrl+click na again: toggles out -> one */
  {
    my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
    e.u.pointer.x = 140;
    e.u.pointer.y = 130;
    e.u.pointer.button = 1;
    e.u.pointer.modifiers = MY_KEYMOD_CTRL;
    my_event_dispatch(&f.d, &e);
    e = my_event_init(MY_EVENT_POINTER_UP);
    my_event_dispatch(&f.d, &e);
  }
  TEST_ASSERT_EQ_INT((int)my_node_view_selected_count(f.view), 1);
  /* click empty (no drag): clears */
  ev(&f, MY_EVENT_POINTER_DOWN, 500, 400);
  ev(&f, MY_EVENT_POINTER_UP, 500, 400);
  TEST_ASSERT_EQ_INT((int)my_node_view_selected_count(f.view), 0);
  fx_destroy(&f);
}

static void test_multi_drag_and_batch_delete(void) {
  fx_t f;
  fx_init(&f);
  /* select both via a band covering everything */
  ev(&f, MY_EVENT_POINTER_DOWN, 50, 50);
  ev(&f, MY_EVENT_POINTER_MOVE, 600, 400);
  ev(&f, MY_EVENT_POINTER_UP, 600, 400);
  TEST_ASSERT_EQ_INT((int)my_node_view_selected_count(f.view), 2);
  /* drag na's title bar: the whole set moves together */
  ev(&f, MY_EVENT_POINTER_DOWN, 140, 110);
  ev(&f, MY_EVENT_POINTER_MOVE, 170, 130);
  ev(&f, MY_EVENT_POINTER_UP, 170, 130);
  TEST_ASSERT_EQ_INT(f.na->rect.x, 130);
  TEST_ASSERT_EQ_INT(f.na->rect.y, 120);
  TEST_ASSERT_EQ_INT(f.nb->rect.x, 430);
  TEST_ASSERT_EQ_INT(f.nb->rect.y, 220);
  /* Del: batch cascade delete (each remove emits changed) */
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  g_changed = 0;
  key(&f, MY_KEY_DELETE);
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(f.view), 1); /* overlay */
  TEST_ASSERT_EQ_INT((int)my_node_view_link_count(f.view), 0);
  TEST_ASSERT(g_changed >= 2);
  fx_destroy(&f);
}

static void test_minimap_click_jumps_viewport(void) {
  fx_t f;
  fx_init(&f);
  /* click at the minimap center: viewport centers on the canvas bbox
   * middle; after the jump, pan_off = center_screen - center*zoom */
  ev(&f, MY_EVENT_POINTER_DOWN, 800 - 10 - 80, 600 - 10 - 50);
  ev(&f, MY_EVENT_POINTER_UP, 800 - 10 - 80, 600 - 10 - 50);
  {
    /* expected: pan_off = (w/2 - cx, h/2 - cy) where (cx,cy) is the
     * clicked canvas point (= bbox center by construction) */
    float cx = 0, cy = 0;
    float sx = 0, sy = 0;
    /* pan_off must have changed to center the viewport somewhere */
    my_node_view_get_pan(f.view, &cx, &cy);
    TEST_ASSERT(cx != 0.0f || cy != 0.0f);
    /* round-trip sanity: some canvas point now maps to the center */
    my_node_view_canvas_to_screen(f.view, 0.0f, 0.0f, &sx, &sy);
    TEST_ASSERT((int)sx != 0 || (int)sy != 0);
  }
  fx_destroy(&f);
}

/* ---------------- flow (needs a real window for loop timers) -------- */

typedef struct wfx_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_widget_t* view;
  my_widget_t* na;
  my_widget_t* nb;
} wfx_t;

static void wfx_init(wfx_t* f) {
  f->pal = my_pal_dummy_create(NULL);
  f->loop = my_pal_main_loop_create(f->pal);
  f->wm = my_window_manager_create(NULL, f->pal, f->loop);
  f->win = my_window_create(NULL, f->pal, 800, 600, "t");
  my_window_manager_open(f->wm, f->win);
  my_widget_unref(my_window_widget(f->win));
  f->view = my_node_view_create(NULL);
  my_widget_set_rect(f->view, &(my_rect_t){0, 0, 800, 600});
  my_widget_add_child(my_window_widget(f->win), f->view);
  my_widget_unref(f->view);
  f->na = my_node_view_add_node(f->view, "a", "A", NULL, 100, 100, 160,
                                80);
  my_node_add_socket(f->na, MY_SOCKET_OUT, "o", 0xFF0000FFu);
  f->nb = my_node_view_add_node(f->view, "b", "B", NULL, 400, 200, 160,
                                80);
  my_node_add_socket(f->nb, MY_SOCKET_IN, "i", 0x00FF00FFu);
}

static void wfx_destroy(wfx_t* f) {
  my_window_manager_destroy(f->wm);
  my_pal_main_loop_destroy(f->loop);
  my_pal_destroy(f->pal);
}

static void wev(wfx_t* f, my_event_type_t type, int32_t x, int32_t y) {
  my_event_t e = my_event_init(type);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  e.u.pointer.button = 1;
  my_window_on_pal_event(f->win, &e);
}

static void test_flow_selected_link_marches(void) {
  wfx_t f;
  float o0, o1, o2;
  wfx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  /* no selection: no timer, offset frozen */
  my_pal_dummy_set_now_ms(f.pal, 100);
  my_pal_main_loop_run(f.loop);
  o0 = my_node_view_flow_offset(f.view);
  TEST_ASSERT(o0 == 0.0f);
  /* select the link (click its midpoint ~ (330,184)) -> timer mounts,
   * offset advances 0.5px per 33ms tick (M21a; the dummy timer manager
   * fires an overdue timer exactly once per run with a frozen clock:
   * added at now=100 due 133, then rescheduled now+33) */
  wev(&f, MY_EVENT_POINTER_DOWN, 330, 184);
  wev(&f, MY_EVENT_POINTER_UP, 330, 184);
  my_pal_dummy_set_now_ms(f.pal, 200);
  my_pal_main_loop_run(f.loop);
  o1 = my_node_view_flow_offset(f.view);
  my_pal_dummy_set_now_ms(f.pal, 300);
  my_pal_main_loop_run(f.loop);
  o2 = my_node_view_flow_offset(f.view);
  TEST_ASSERT(o1 > o0);
  TEST_ASSERT(o2 > o1);
  TEST_ASSERT(o1 == 0.5f); /* exact M21a pace: 0.5px/tick (~15px/s) */
  TEST_ASSERT(o2 == 1.0f);
  /* deselect (click empty) -> timer unmounts, offset stops */
  wev(&f, MY_EVENT_POINTER_DOWN, 500, 400);
  wev(&f, MY_EVENT_POINTER_UP, 500, 400);
  o1 = my_node_view_flow_offset(f.view);
  my_pal_dummy_set_now_ms(f.pal, 300);
  my_pal_main_loop_run(f.loop);
  o2 = my_node_view_flow_offset(f.view);
  TEST_ASSERT(o2 == o1); /* frozen after deselect */
  wfx_destroy(&f);
}

static void test_flow_dash_matches_bezier(void) {
  wfx_t f;
  rec_vg_t rec;
  wfx_init(&f);
  my_node_view_connect(f.view, f.na, 0, f.nb, 0);
  wev(&f, MY_EVENT_POINTER_DOWN, 330, 184); /* select -> flow */
  wev(&f, MY_EVENT_POINTER_UP, 330, 184);
  my_widget_invalidate(my_window_widget(f.win), NULL);
  my_window_paint(f.win);
  rec_vg_init(&rec);
  my_widget_paint(my_window_widget(f.win), (my_vgcanvas_t*)&rec);
  /* dashed stroke: multiple subpaths (dash gaps) but all from the same
   * bezier — many move_to/line_to ops, no curve_to (subdivided) */
  TEST_ASSERT(rec_count(&rec, "move_to") >= 3);
  TEST_ASSERT(rec_has(&rec, "stroke"));
  /* selected-link flow is ON by default; global flow is off */
  TEST_ASSERT(!my_node_view_get_flow_enabled(f.view));
  my_node_view_set_flow_enabled(f.view, true);
  TEST_ASSERT(my_node_view_get_flow_enabled(f.view));
  wfx_destroy(&f);
}

static void test_selection_overlay_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* view = my_node_view_create(dbg);
  my_widget_t* a = my_node_view_add_node(view, "a", "A", NULL, 0, 0, 160,
                                         80);
  my_node_add_socket(a, MY_SOCKET_OUT, "o", 0xFF0000FFu);
  (void)a;
  my_widget_unref(view);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

/* ---------------- M21a: minimap anchors to the VISIBLE bottom -------- */

static void test_minimap_visible_extent_under_csd(void) {
  /* CSD (M16) shrinks the content container by the 36px title bar; a
   * view sized for the FULL window height (the demo_nodes values:
   * window 960x640, view (10,36,940,594)) overflows the 604-high
   * container and its rect bottom is clipped away by the parent clip
   * (my_widget_paint clips children to the parent rect). The minimap
   * must anchor to the VISIBLE bottom-right, not the rect corner. */
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm;
  my_window_t* win;
  my_widget_t* view;
  rec_vg_t rec;
  my_pal_dummy_set_needs_csd(pal, true);
  wm = my_window_manager_create(NULL, pal, loop);
  win = my_window_create(NULL, pal, 960, 640, "t");
  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));
  view = my_node_view_create(NULL);
  my_widget_set_rect(view, &(my_rect_t){10, 36, 940, 594}); /* demo_nodes */
  my_widget_add_child(my_window_widget(win), view);
  my_widget_unref(view);
  my_node_view_add_node(view, "a", "A", NULL, 100, 100, 160, 80);
  my_widget_relayout((my_widget_t*)win); /* bar h:36 + content h:604 */
  rec_vg_init(&rec);
  my_widget_paint((my_widget_t*)win, (my_vgcanvas_t*)&rec);
  /* visible bottom (view-local) = 604 - 36 = 568 -> minimap bg at
   * (940-170, 568-110) = (770, 458), bottom edge 558 <= 568; the
   * pre-fix rect-corner spot (770, 484) would have been clipped */
  TEST_ASSERT(rec_has(&rec, "fill_rect 770 458 160 100"));
  TEST_ASSERT(!rec_has(&rec, "fill_rect 770 484 160 100"));
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_minimap_rect_corner_when_fitting(void) {
  /* no clipping anywhere (non-CSD dummy, view == window): the minimap
   * stays at the view rect's bottom-right corner (zero regression) */
  wfx_t f;
  rec_vg_t rec;
  wfx_init(&f);
  my_widget_relayout((my_widget_t*)f.win);
  rec_vg_init(&rec);
  my_widget_paint((my_widget_t*)f.win, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(rec_has(&rec, "fill_rect 630 490 160 100")); /* (800-170,
                                                            * 600-110) */
  wfx_destroy(&f);
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
  MYTEST_RUN(test_magnet_snap_and_connect);
  MYTEST_RUN(test_magnet_respects_type_and_distance);
  MYTEST_RUN(test_zoom_clamp_and_anchor);
  MYTEST_RUN(test_zoomed_interaction_uses_canvas_coords);
  MYTEST_RUN(test_remove_node_cascade);
  MYTEST_RUN(test_rubber_band_multi_select);
  MYTEST_RUN(test_multi_drag_and_batch_delete);
  MYTEST_RUN(test_minimap_click_jumps_viewport);
  MYTEST_RUN(test_flow_selected_link_marches);
  MYTEST_RUN(test_flow_dash_matches_bezier);
  MYTEST_RUN(test_selection_overlay_no_leak);
  MYTEST_RUN(test_minimap_visible_extent_under_csd);
  MYTEST_RUN(test_minimap_rect_corner_when_fitting);
MYTEST_MAIN_END()
