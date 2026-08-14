/**
 * @file my_bezier_test.c
 * @brief Cubic bezier subdivision tests (M19a): straight line collapses,
 * curvature drives segment counts, endpoints exact, depth cap, OOM/
 * param edges, rec_vgcanvas curve_to, soft stroke pixel equivalence.
 */
#include "myr/my_bezier.h"
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

#include <math.h>

#include "mytest.h"
#include "rec_vgcanvas.h"

typedef struct collect_t {
  float xs[512];
  float ys[512];
  int n;
} collect_t;

static my_ret_t collect(void* ctx, float x, float y) {
  collect_t* c = (collect_t*)ctx;
  if (c->n < 512) {
    c->xs[c->n] = x;
    c->ys[c->n] = y;
    c->n++;
  }
  return MY_RET_OK;
}

static void test_straight_collapses(void) {
  /* collinear control points = a straight line: one segment */
  collect_t c = {{0}, {0}, 0};
  int32_t seg = -1;
  TEST_ASSERT(my_bezier_cubic_to_lines(0, 0, 50, 0, 100, 0, 150, 0, 0.25f,
                                       16, collect, &c, &seg) == MY_RET_OK);
  TEST_ASSERT_EQ_INT(seg, 1);
  TEST_ASSERT_EQ_INT(c.n, 1);
  TEST_ASSERT(c.xs[0] == 150.0f && c.ys[0] == 0.0f); /* exact endpoint */
}

static void test_s_curve_subdivides(void) {
  /* S-curve: many segments; endpoints exact */
  collect_t c = {{0}, {0}, 0};
  int32_t seg = 0;
  TEST_ASSERT(my_bezier_cubic_to_lines(0, 0, 60, -80, 60, 80, 120, 0, 0.25f,
                                       16, collect, &c, &seg) == MY_RET_OK);
  TEST_ASSERT(seg > 8); /* clearly more than a straight line */
  TEST_ASSERT(c.xs[c.n - 1] == 120.0f && c.ys[c.n - 1] == 0.0f);
  /* midpoint of the parameter range is near the curve's middle */
  {
    float mx = c.xs[c.n / 2];
    TEST_ASSERT(mx > 40.0f && mx < 80.0f);
  }
}

static void test_curvature_drives_segment_count(void) {
  /* same chord length, flat vs sharp S: sharp needs more segments */
  collect_t c1 = {{0}, {0}, 0};
  collect_t c2 = {{0}, {0}, 0};
  int32_t s1 = 0, s2 = 0;
  my_bezier_cubic_to_lines(0, 0, 50, -4, 50, 4, 100, 0, 0.25f, 16, collect,
                           &c1, &s1);
  my_bezier_cubic_to_lines(0, 0, 50, -60, 50, 60, 100, 0, 0.25f, 16, collect,
                           &c2, &s2);
  TEST_ASSERT(s2 > s1);
}

static void test_depth_cap_and_params(void) {
  /* extreme curvature with a shallow cap: no explosion */
  collect_t c = {{0}, {0}, 0};
  int32_t seg = 0;
  TEST_ASSERT(my_bezier_cubic_to_lines(0, 0, 0, 500, 200, -500, 200, 0,
                                       0.01f, 4, collect, &c,
                                       &seg) == MY_RET_OK);
  TEST_ASSERT(seg <= 16); /* 2^4 leaves */
  /* bad params */
  TEST_ASSERT(my_bezier_cubic_to_lines(0, 0, 1, 1, 2, 2, 3, 3, 0.0f, 16,
                                       collect, &c, NULL) ==
              MY_RET_INVALID_PARAMS);
  TEST_ASSERT(my_bezier_cubic_to_lines(0, 0, 1, 1, 2, 2, 3, 3, 0.25f, 16,
                                       NULL, NULL, NULL) ==
              MY_RET_INVALID_PARAMS);
}

/** @brief A closed-region pixel read from an lcd buffer (BGRA). */
static uint8_t lcd_red(my_lcd_t* lcd, int x, int y) {
  return my_lcd_mem_get_buffer(lcd)[(size_t)y * my_lcd_mem_get_stride(lcd) +
                                    (size_t)x * 4 + 2];
}

static void test_soft_straight_bezier_matches_line(void) {
  /* stroke a collinear bezier vs the same straight line: identical
   * pixels */
  my_lcd_t* lcd1 = my_lcd_mem_create(NULL, 100, 40, MY_PIXEL_FORMAT_BGRA8888);
  my_lcd_t* lcd2 = my_lcd_mem_create(NULL, 100, 40, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg1 = my_vgcanvas_soft_create(NULL, lcd1);
  my_vgcanvas_t* vg2 = my_vgcanvas_soft_create(NULL, lcd2);
  int x, y;
  my_vgcanvas_begin_frame(vg1, NULL);
  my_vgcanvas_set_stroke_color(vg1, my_color_rgb(255, 0, 0));
  my_vgcanvas_set_line_width(vg1, 2);
  my_vgcanvas_begin_path(vg1);
  my_vgcanvas_move_to(vg1, 10, 20);
  my_vgcanvas_curve_to(vg1, 30, 20, 60, 20, 90, 20); /* collinear */
  my_vgcanvas_stroke(vg1);
  my_vgcanvas_end_frame(vg1);
  my_vgcanvas_begin_frame(vg2, NULL);
  my_vgcanvas_set_stroke_color(vg2, my_color_rgb(255, 0, 0));
  my_vgcanvas_set_line_width(vg2, 2);
  my_vgcanvas_begin_path(vg2);
  my_vgcanvas_move_to(vg2, 10, 20);
  my_vgcanvas_line_to(vg2, 90, 20);
  my_vgcanvas_stroke(vg2);
  my_vgcanvas_end_frame(vg2);
  for (y = 0; y < 40; y++) {
    for (x = 0; x < 100; x++) {
      TEST_ASSERT_EQ_INT(lcd_red(lcd1, x, y), lcd_red(lcd2, x, y));
    }
  }
  my_vgcanvas_destroy(vg1);
  my_vgcanvas_destroy(vg2);
  my_lcd_destroy(lcd1);
  my_lcd_destroy(lcd2);
}

static void test_soft_s_curve_endpoints_and_mid(void) {
  /* S curve (0,20)->(100,20), controls (25,-30)/(75,70): stroke hits
   * the endpoints, the top hump and the bottom hump, but NOT the
   * center of the bounding box corners */
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 120, 100, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_set_line_width(vg, 3);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 10, 50);
  my_vgcanvas_curve_to(vg, 35, -20, 75, 120, 100, 50);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
  /* endpoint pixels sit on the round-cap boundary: partial coverage
   * (same as plain line strokes — the equality test above pins that);
   * the pixels just inside are fully covered */
  TEST_ASSERT(lcd_red(lcd, 10, 50) > 100);   /* start (cap edge) */
  TEST_ASSERT(lcd_red(lcd, 11, 50) == 255);  /* just inside */
  TEST_ASSERT(lcd_red(lcd, 100, 50) > 100);  /* end (cap edge) */
  TEST_ASSERT(lcd_red(lcd, 99, 50) == 255);  /* just inside */
  TEST_ASSERT(lcd_red(lcd, 55, 50) > 200);   /* midpoint (curve center) */
  TEST_ASSERT(lcd_red(lcd, 28, 30) > 200);   /* top hump (computed) */
  TEST_ASSERT(lcd_red(lcd, 82, 70) > 200);   /* bottom hump (computed) */
  TEST_ASSERT(lcd_red(lcd, 5, 5) == 0);      /* empty corner */
  TEST_ASSERT(lcd_red(lcd, 115, 95) == 0);   /* empty corner */
  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_rec_mock_curve_to(void) {
  rec_vg_t rec;
  rec_vg_init(&rec);
  my_vgcanvas_begin_path((my_vgcanvas_t*)&rec);
  my_vgcanvas_move_to((my_vgcanvas_t*)&rec, 0, 0);
  my_vgcanvas_curve_to((my_vgcanvas_t*)&rec, 1, 2, 3, 4, 5, 6);
  TEST_ASSERT(rec_has(&rec, "curve_to 1 2 3 4 5 6"));
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_straight_collapses);
  MYTEST_RUN(test_s_curve_subdivides);
  MYTEST_RUN(test_curvature_drives_segment_count);
  MYTEST_RUN(test_depth_cap_and_params);
  MYTEST_RUN(test_soft_straight_bezier_matches_line);
  MYTEST_RUN(test_soft_s_curve_endpoints_and_mid);
  MYTEST_RUN(test_rec_mock_curve_to);
MYTEST_MAIN_END()
