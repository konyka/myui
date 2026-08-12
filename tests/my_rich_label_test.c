/**
 * @file my_rich_label_test.c
 * @brief Rich label tests (M14a): segment order/widths, fake-bold double
 * draw, clipping cutoff, empty segments, leaks. Bitmap font = 8px cells.
 */
#include "myui/widgets/my_rich_label.h"

#include "mytest.h"
#include "rec_vgcanvas.h"

/** @brief Paint the label into a recording vgcanvas (bitmap font, 8). */
static void paint_rec(my_widget_t* label, rec_vg_t* rec) {
  static my_font_t* bmp = NULL;
  if (bmp == NULL) {
    bmp = my_font_bitmap_create(NULL);
  }
  rec_vg_init(rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)rec, bmp, 8);
  my_widget_paint(label, (my_vgcanvas_t*)rec);
}

static void test_segments_in_order_and_widths(void) {
  my_widget_t* l = my_rich_label_create(NULL);
  rec_vg_t rec;
  my_widget_set_rect(l, &(my_rect_t){0, 0, 200, 24});
  my_rich_label_add_segment(l, "AB", 0xFF0000FFu, false); /* 16px */
  my_rich_label_add_segment(l, "CD", 0x00FF00FFu, false); /* 16px */
  paint_rec(l, &rec);
  /* y: (24-8)/2 = 8; x: 0 then 16 */
  TEST_ASSERT(rec_has(&rec, "draw_text 0 8 AB"));
  TEST_ASSERT(rec_has(&rec, "draw_text 16 8 CD"));
  TEST_ASSERT_EQ_INT(my_rich_label_content_width(l), 32);
  my_widget_unref(l);
}

static void test_fake_bold_double_draw(void) {
  my_widget_t* l = my_rich_label_create(NULL);
  rec_vg_t rec;
  my_widget_set_rect(l, &(my_rect_t){0, 0, 200, 24});
  my_rich_label_add_segment(l, "XY", 0xFF0000FFu, true);
  paint_rec(l, &rec);
  /* fake bold: same text at x and x+1 */
  TEST_ASSERT(rec_has(&rec, "draw_text 0 8 XY"));
  TEST_ASSERT(rec_has(&rec, "draw_text 1 8 XY"));
  TEST_ASSERT_EQ_INT(my_rich_label_content_width(l), 17); /* +1 for bold */
  my_widget_unref(l);
}

static void test_clipped_tail_segments_skipped(void) {
  my_widget_t* l = my_rich_label_create(NULL);
  rec_vg_t rec;
  my_widget_set_rect(l, &(my_rect_t){0, 0, 20, 24}); /* narrow */
  my_rich_label_add_segment(l, "AB", 0xFF0000FFu, false); /* x=0  w=16 */
  my_rich_label_add_segment(l, "CD", 0x00FF00FFu, false); /* x=16 w=16 */
  my_rich_label_add_segment(l, "EF", 0x0000FFFFu, false); /* x=32: skip */
  paint_rec(l, &rec);
  TEST_ASSERT(rec_has(&rec, "draw_text 0 8 AB"));
  TEST_ASSERT(rec_has(&rec, "draw_text 16 8 CD")); /* starts inside */
  TEST_ASSERT(!rec_has(&rec, "EF"));               /* fully outside */
  my_widget_unref(l);
}

static void test_empty_and_clear(void) {
  my_widget_t* l = my_rich_label_create(NULL);
  rec_vg_t rec;
  my_widget_set_rect(l, &(my_rect_t){0, 0, 100, 24});
  my_rich_label_add_segment(l, "", 0xFF0000FFu, false); /* zero width */
  my_rich_label_add_segment(l, "A", 0xFF0000FFu, false);
  TEST_ASSERT_EQ_INT(my_rich_label_content_width(l), 8);
  paint_rec(l, &rec);
  TEST_ASSERT(rec_has(&rec, "draw_text 0 8 A"));
  my_rich_label_clear(l);
  TEST_ASSERT_EQ_INT(my_rich_label_content_width(l), 0);
  rec_vg_init(&rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)&rec, NULL, 8);
  my_widget_paint(l, (my_vgcanvas_t*)&rec);
  TEST_ASSERT(!rec_has(&rec, "draw_text"));
  my_widget_unref(l);
}

static void test_rich_label_no_leak(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* l = my_rich_label_create(dbg);
  my_rich_label_add_segment(l, "alpha", 0xFF0000FFu, false);
  my_rich_label_add_segment(l, "beta", 0x00FF00FFu, true);
  my_rich_label_clear(l);
  my_rich_label_add_segment(l, "gamma", 0x0000FFFFu, false);
  my_widget_unref(l);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_segments_in_order_and_widths);
  MYTEST_RUN(test_fake_bold_double_draw);
  MYTEST_RUN(test_clipped_tail_segments_skipped);
  MYTEST_RUN(test_empty_and_clear);
  MYTEST_RUN(test_rich_label_no_leak);
MYTEST_MAIN_END()
