/**
 * @file my_vggeometry_test.c
 * @brief Unit tests for the shared CPU geometry (M25b): triangulation
 * rules identical to what the gles2 backend emitted inline before the
 * extraction.
 */
#include "myr/my_vggeometry.h"

#include <math.h>

#include "mytest.h"

static void test_rect_triangles(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_rect(&g, 0, 0, 10, 10);
  TEST_ASSERT_EQ_INT(g.vert_count, 12); /* 2 triangles = 6 xy pairs */
  TEST_ASSERT(g.verts[0] == 0.0f && g.verts[1] == 0.0f);
  TEST_ASSERT(g.verts[2] == 10.0f && g.verts[3] == 0.0f);
  TEST_ASSERT(g.verts[4] == 10.0f && g.verts[5] == 10.0f);
  TEST_ASSERT(g.verts[10] == 0.0f && g.verts[11] == 10.0f);
  /* degenerate rect emits nothing */
  my_vggeometry_begin_verts(&g);
  my_vggeometry_rect(&g, 5, 5, 5, 9);
  TEST_ASSERT_EQ_INT(g.vert_count, 0);
  my_vggeometry_destroy(&g);
}

static void test_transform_applied_at_push(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  my_vggeometry_set_transform(&g, 5.0f, 5.0f, 2.0f);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_rect(&g, 0, 0, 10, 10);
  TEST_ASSERT(g.verts[0] == 10.0f && g.verts[1] == 10.0f); /* (0+5)*2 */
  TEST_ASSERT(g.verts[4] == 30.0f && g.verts[5] == 30.0f); /* (10+5)*2 */
  my_vggeometry_destroy(&g);
}

static void test_fill_rounded_rect_counts(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  /* r > 0.5: 3 plain rects + 4 corner fans of 8 segments */
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill_rounded_rect(&g, 0, 0, 20, 20, 4.0f);
  TEST_ASSERT_EQ_INT(g.vert_count, 3 * 12 + 4 * 8 * 3 * 2);
  /* radius clamped to half the smaller side; r <= 0.5 = plain rect */
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill_rounded_rect(&g, 0, 0, 8, 6, 100.0f);
  /* r clamps to 3 (=h/2): the two side rects become degenerate (y0+r ==
   * y1-r) and drop out, leaving the middle rect + 4 corner fans */
  TEST_ASSERT_EQ_INT(g.vert_count, 1 * 12 + 4 * 8 * 3 * 2);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill_rounded_rect(&g, 0, 0, 8, 6, 0.4f);
  TEST_ASSERT_EQ_INT(g.vert_count, 12);
  my_vggeometry_destroy(&g);
}

static void test_stroke_rect_counts(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_stroke_rect(&g, 0, 0, 10, 10, 2.0f);
  TEST_ASSERT_EQ_INT(g.vert_count, 4 * 12); /* 4 edge rects */
  my_vggeometry_destroy(&g);
}

static void test_fill_triangle_spans(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  /* clip-driven even-odd scanline: a 10x10 triangle -> one span/row */
  my_vggeometry_begin_path(&g);
  TEST_ASSERT_EQ_INT(my_vggeometry_move_to(&g, 0, 0), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_vggeometry_line_to(&g, 10, 0), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_vggeometry_line_to(&g, 5, 10), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_vggeometry_close_path(&g), MY_RET_OK);
  my_vggeometry_begin_verts(&g);
  TEST_ASSERT_EQ_INT(my_vggeometry_fill(&g, &(my_rect_t){0, 0, 20, 20}),
                     MY_RET_OK);
  /* rows 0..8 emit one span each; row 9's span collapses to zero width
   * (the triangle tip: ceil maps both crossings to x=5) and drops out */
  TEST_ASSERT_EQ_INT(g.vert_count, 9 * 12);
  /* clip smaller than the shape: fewer rows */
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill(&g, &(my_rect_t){0, 0, 20, 4});
  TEST_ASSERT_EQ_INT(g.vert_count, 4 * 12);
  my_vggeometry_destroy(&g);
}

static void test_fill_concave_even_odd(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  /* 10x10 square with a centered 4x4 hole (reverse-wound sub-contour is
   * irrelevant: the rule is even-odd) -> rows 0..2: 1 span... actually
   * two side spans in the hole rows, one full span elsewhere */
  my_vggeometry_begin_path(&g);
  my_vggeometry_move_to(&g, 0, 0);
  my_vggeometry_line_to(&g, 10, 0);
  my_vggeometry_line_to(&g, 10, 10);
  my_vggeometry_line_to(&g, 0, 10);
  my_vggeometry_close_path(&g);
  my_vggeometry_move_to(&g, 3, 3);
  my_vggeometry_line_to(&g, 3, 7);
  my_vggeometry_line_to(&g, 7, 7);
  my_vggeometry_line_to(&g, 7, 3);
  my_vggeometry_close_path(&g);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill(&g, &(my_rect_t){0, 0, 20, 20});
  /* rows 0,1,2,7,8,9: 1 span each (6); rows 3..6: 2 spans each (8 spans)
   * -> 14 spans * 12 floats */
  TEST_ASSERT_EQ_INT(g.vert_count, 14 * 12);
  my_vggeometry_destroy(&g);
}

static void test_stroke_open_line(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  my_vggeometry_begin_path(&g);
  my_vggeometry_move_to(&g, 0, 0);
  my_vggeometry_line_to(&g, 10, 0);
  my_vggeometry_begin_verts(&g);
  /* BUTT: one segment quad = 6 vertices */
  my_vggeometry_stroke(&g, 2.0f, MY_LINE_CAP_BUTT, MY_LINE_JOIN_MITER);
  TEST_ASSERT_EQ_INT(g.vert_count, 12);
  /* ROUND caps add 2 semicircle fans of 8 triangles each */
  my_vggeometry_begin_verts(&g);
  my_vggeometry_stroke(&g, 2.0f, MY_LINE_CAP_ROUND, MY_LINE_JOIN_MITER);
  TEST_ASSERT_EQ_INT(g.vert_count, 12 + 2 * 8 * 3 * 2);
  my_vggeometry_destroy(&g);
}

static void test_curve_to_subdivides(void) {
  my_vggeometry_t g;
  my_vggeometry_init(&g, NULL);
  my_vggeometry_begin_path(&g);
  /* canvas convention: curve without a current point fails */
  TEST_ASSERT_EQ_INT(my_vggeometry_curve_to(&g, 1, 0, 2, 0, 3, 3),
                     MY_RET_FAIL);
  my_vggeometry_move_to(&g, 0, 0);
  TEST_ASSERT_EQ_INT(my_vggeometry_curve_to(&g, 10, 0, 20, 10, 20, 20),
                     MY_RET_OK);
  TEST_ASSERT(g.point_count > 2); /* adaptive subdivision added points */
  my_vggeometry_destroy(&g);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_vggeometry_t g;
  my_vggeometry_init(&g, dbg);
  my_vggeometry_begin_path(&g);
  my_vggeometry_move_to(&g, 0, 0);
  my_vggeometry_curve_to(&g, 10, 0, 20, 10, 20, 20);
  my_vggeometry_close_path(&g);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill(&g, &(my_rect_t){0, 0, 30, 30});
  my_vggeometry_begin_verts(&g);
  my_vggeometry_stroke(&g, 2.0f, MY_LINE_CAP_ROUND, MY_LINE_JOIN_ROUND);
  my_vggeometry_begin_verts(&g);
  my_vggeometry_fill_rounded_rect(&g, 0, 0, 50, 50, 8.0f);
  my_vggeometry_destroy(&g);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_rect_triangles);
  MYTEST_RUN(test_transform_applied_at_push);
  MYTEST_RUN(test_fill_rounded_rect_counts);
  MYTEST_RUN(test_stroke_rect_counts);
  MYTEST_RUN(test_fill_triangle_spans);
  MYTEST_RUN(test_fill_concave_even_odd);
  MYTEST_RUN(test_stroke_open_line);
  MYTEST_RUN(test_curve_to_subdivides);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
