/**
 * @file dxx_qx_test.c
 * @brief qxlive stats + chart tests (M15): line chart grid/polyline,
 * series switch, bar chart colors, stat button interaction, edge cases.
 */
#include "dxx_data.h"
#include "dxx_theme.h"
#include "myc/my_str.h"
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_window_manager.h"
#include "views/qx_chart.h"
#include "views/views.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

static my_window_t* make_win(my_pal_t** pal, my_window_manager_t** wm,
                             my_pal_main_loop_t** loop) {
  my_window_t* win;
  *pal = my_pal_dummy_create(NULL);
  *loop = my_pal_main_loop_create(*pal);
  *wm = my_window_manager_create(NULL, *pal, *loop);
  win = my_window_create(NULL, *pal, 1320, 900, "dxx");
  my_window_set_theme(win, dxx_theme_create(NULL), true);
  my_window_manager_open(*wm, win);
  my_widget_unref(my_window_widget(win));
  return win;
}

static void paint_chart(my_widget_t* chart, rec_vg_t* rec) {
  rec_vg_init(rec);
  my_widget_paint(chart, (my_vgcanvas_t*)rec);
}

/* ---------------- chart widget ---------------- */

static void test_line_chart_grid_and_polyline(void) {
  my_widget_t* c = dxx_chart_create(NULL, DXX_CHART_LINE);
  rec_vg_t rec;
  my_widget_set_rect(c, &(my_rect_t){0, 0, 500, 300});
  dxx_chart_set_series(c, DXX_SERIES[0].name, DXX_SERIES[0].points,
                       DXX_SERIES[0].count, DXX_SERIES[0].ymin,
                       DXX_SERIES[0].ymax);
  paint_chart(c, &rec);
  TEST_ASSERT(rec_has(&rec, "情绪指标"));  /* title */
  TEST_ASSERT(rec_has(&rec, "09:30"));     /* x ticks */
  TEST_ASSERT(rec_has(&rec, "15:00"));
  TEST_ASSERT(rec_has(&rec, "draw_text 2 19 75")); /* ymax tick at top */
  TEST_ASSERT_EQ_INT(rec_count(&rec, "set_fill #eeeeee"), 1); /* grid color set once... */
  /* 5 grid lines are 1px fill_rects across the plot */
  {
    int i, grids = 0;
    for (i = 0; i < rec.n_ops; i++) {
      if (strncmp(rec.ops[i], "fill_rect 36 ", 13) == 0) {
        grids++;
      }
    }
    TEST_ASSERT_EQ_INT(grids, 5);
  }
  TEST_ASSERT(rec_has(&rec, "set_stroke #e64c62")); /* polyline color */
  TEST_ASSERT(rec_has(&rec, "move_to"));
  TEST_ASSERT(rec_has(&rec, "line_to"));
  TEST_ASSERT(rec_has(&rec, "stroke"));
  TEST_ASSERT_EQ_STR(dxx_chart_get_series_name(c), "情绪指标");
  my_widget_unref(c);
}

static void test_line_chart_edge_cases(void) {
  my_widget_t* c = dxx_chart_create(NULL, DXX_CHART_LINE);
  rec_vg_t rec;
  float one = 42.0f;
  my_widget_set_rect(c, &(my_rect_t){0, 0, 500, 300});
  /* empty: no polyline stroke */
  dxx_chart_set_series(c, "empty", NULL, 0, 0, 100);
  paint_chart(c, &rec);
  TEST_ASSERT(!rec_has(&rec, "line_to"));
  /* single point: move_to only, no line_to */
  dxx_chart_set_series(c, "one", &one, 1, 0, 100);
  paint_chart(c, &rec);
  TEST_ASSERT(rec_has(&rec, "move_to"));
  TEST_ASSERT(!rec_has(&rec, "line_to"));
  /* degenerate range: no stroke at all */
  dxx_chart_set_series(c, "flat", &one, 1, 50, 50);
  paint_chart(c, &rec);
  TEST_ASSERT(!rec_has(&rec, "set_stroke #e64c62"));
  my_widget_unref(c);
}

static void test_bar_chart_red_green(void) {
  my_widget_t* c = dxx_chart_create(NULL, DXX_CHART_BAR);
  rec_vg_t rec;
  my_widget_set_rect(c, &(my_rect_t){0, 0, 180, 275});
  dxx_chart_set_series(c, "涨幅分布", DXX_DIST, DXX_DIST_COUNT, 0, 0);
  paint_chart(c, &rec);
  TEST_ASSERT(rec_has(&rec, "set_fill #ff0000")); /* up bars */
  TEST_ASSERT(rec_has(&rec, "set_fill #008000")); /* down bars */
  TEST_ASSERT(rec_has(&rec, "涨幅分布"));
  my_widget_unref(c);
}

/* ---------------- stats grid + switching ---------------- */

static void test_stats_switch_series(void) {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win = make_win(&pal, &wm, &loop);
  my_widget_t* card = dxx_build_emotion_card(my_window_widget(win), 0, 0,
                                             750, 1276);
  /* children: 12 stat buttons, bar chart, line chart, feed */
  my_widget_t* line;
  my_widget_t* dt_btn;
  my_widget_t* vol_btn;
  my_event_t e;
  int32_t cx, cy;
  TEST_ASSERT_EQ_INT((int)my_widget_child_count(card), 15);
  line = my_widget_get_child(card, 13);
  TEST_ASSERT_EQ_STR(dxx_chart_get_series_name(line), "情绪指标");
  /* click 跌停家数 (index 2) -> series switches + active moves */
  dt_btn = my_widget_get_child(card, 2);
  cx = dt_btn->rect.w / 2;
  cy = dt_btn->rect.h / 2;
  my_widget_local_to_global(dt_btn, &cx, &cy);
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  my_window_on_pal_event(win, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  my_window_on_pal_event(win, &e);
  TEST_ASSERT_EQ_STR(dxx_chart_get_series_name(line), "跌停家数");
  /* active highlight: 跌停家数 bg darkened (0x5CB85C -> 0x4D9D4D) */
  {
    rec_vg_t rec;
    rec_vg_init(&rec);
    my_widget_paint(dt_btn, (my_vgcanvas_t*)&rec);
    TEST_ASSERT(rec_has(&rec, "set_fill #4e9c4e"));
    /* previous active (情绪指标) back to plain #F0AD4E */
    rec_vg_init(&rec);
    my_widget_paint(my_widget_get_child(card, 0), (my_vgcanvas_t*)&rec);
    TEST_ASSERT(rec_has(&rec, "set_fill #f0ad4e"));
  }
  /* 量能 (index 11, no series): click does not change the chart */
  vol_btn = my_widget_get_child(card, 11);
  cx = vol_btn->rect.w / 2;
  cy = vol_btn->rect.h / 2;
  my_widget_local_to_global(vol_btn, &cx, &cy);
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  my_window_on_pal_event(win, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  my_window_on_pal_event(win, &e);
  TEST_ASSERT_EQ_STR(dxx_chart_get_series_name(line), "跌停家数");
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
}

static void test_stats_data_snapshot(void) {
  TEST_ASSERT_EQ_INT(DXX_STAT_COUNT, 12);
  TEST_ASSERT(my_str_eq(DXX_STATS[0].label, "情绪指标"));
  TEST_ASSERT(my_str_eq(DXX_STATS[0].value, "62"));
  TEST_ASSERT(my_str_eq(DXX_STATS[1].value, "72"));
  TEST_ASSERT(my_str_eq(DXX_STATS[2].value, "3"));
  TEST_ASSERT(DXX_STATS[11].value == NULL); /* 量能无数值 */
  TEST_ASSERT_EQ_INT(DXX_SERIES[0].count, 61);
  TEST_ASSERT_EQ_INT(DXX_SERIES[1].count, 61);
  TEST_ASSERT_EQ_INT(DXX_SERIES[2].count, 61);
  TEST_ASSERT_EQ_INT(DXX_DIST_COUNT, 10);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_line_chart_grid_and_polyline);
  MYTEST_RUN(test_line_chart_edge_cases);
  MYTEST_RUN(test_bar_chart_red_green);
  MYTEST_RUN(test_stats_switch_series);
  MYTEST_RUN(test_stats_data_snapshot);
MYTEST_MAIN_END()
