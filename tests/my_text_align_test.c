/**
 * @file my_text_align_test.c
 * @brief Horizontal alignment tests (M11d): label aligns, text_area
 * LEFT/CENTER/RIGHT base x, JUSTIFY word stretching (bitmap font, 8px
 * cells), last-line-not-stretched, RTL right align, XML/MVVM plumbing.
 */
#include "mypal/dummy/my_pal_dummy.h"
#include "myui/my_ui_loader.h"
#include "myui/my_window.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_text_area.h"

#include <stdlib.h>
#include <string.h>

#include "mytest.h"
#include "rec_vgcanvas.h"

static void paint_at(my_widget_t* w, int32_t px_w, int32_t px_h,
                     rec_vg_t* rec) {
  static my_font_t* bmp = NULL;
  if (bmp == NULL) {
    bmp = my_font_bitmap_create(NULL);
  }
  my_widget_set_rect(w, &(my_rect_t){0, 0, px_w, px_h});
  rec_vg_init(rec);
  my_vgcanvas_set_font((my_vgcanvas_t*)rec, bmp, 8);
  my_widget_paint(w, (my_vgcanvas_t*)rec);
}

/** @brief x of the first draw_text op containing needle (-1 when none). */
static float text_op_x(rec_vg_t* rec, const char* needle) {
  int i;
  for (i = 0; i < rec->n_ops; i++) {
    if (strncmp(rec->ops[i], "draw_text ", 10) == 0 &&
        strstr(rec->ops[i], needle) != NULL) {
      return (float)atof(rec->ops[i] + 10);
    }
  }
  return -1.0f;
}

static my_widget_t* make_label_t(const char* text, my_text_align_t align) {
  my_widget_t* l = my_label_create(NULL, text);
  my_label_set_align(l, align);
  return l;
}

static void test_label_aligns(void) {
  rec_vg_t rec;
  my_widget_t* l;
  /* label style font_size 16: bitmap font "ab" = 2x16 = 32px in a 116px
   * label */
  l = make_label_t("ab", MY_TEXT_ALIGN_LEFT);
  paint_at(l, 116, 24, &rec);
  TEST_ASSERT(text_op_x(&rec, "ab") == 0.0f);
  my_widget_unref(l);

  l = make_label_t("ab", MY_TEXT_ALIGN_CENTER);
  paint_at(l, 116, 24, &rec);
  TEST_ASSERT(text_op_x(&rec, "ab") == 42.0f); /* (116-32)/2 */
  my_widget_unref(l);

  l = make_label_t("ab", MY_TEXT_ALIGN_RIGHT);
  paint_at(l, 116, 24, &rec);
  TEST_ASSERT(text_op_x(&rec, "ab") == 84.0f); /* 116-32 */
  my_widget_unref(l);

  l = make_label_t("ab", MY_TEXT_ALIGN_JUSTIFY); /* single line = LEFT */
  paint_at(l, 116, 24, &rec);
  TEST_ASSERT(text_op_x(&rec, "ab") == 0.0f);
  my_widget_unref(l);
}

static my_widget_t* make_ta(const char* text, int32_t w_px, bool wrap) {
  my_widget_t* w = my_text_area_create(NULL);
  my_font_t* f = my_font_bitmap_create(NULL);
  my_text_area_set_font(w, f, 8);
  my_widget_set_rect(w, &(my_rect_t){0, 0, w_px, 200});
  my_text_area_set_wrap(w, wrap);
  my_text_area_set_text(w, text);
  return w;
}

static void test_text_area_center_right(void) {
  rec_vg_t rec;
  my_widget_t* ta;
  /* w=64: inner = 64-8 = 56; "ab" = 16px */
  ta = make_ta("ab", 64, false);
  my_text_area_set_align(ta, MY_TEXT_ALIGN_CENTER);
  paint_at(ta, 64, 200, &rec);
  /* base = 4 + (56-16)/2 = 24 */
  TEST_ASSERT(text_op_x(&rec, "ab") == 24.0f);
  my_widget_unref(ta);

  ta = make_ta("ab", 64, false);
  my_text_area_set_align(ta, MY_TEXT_ALIGN_RIGHT);
  paint_at(ta, 64, 200, &rec);
  /* base = 4 + (56-16) = 44 */
  TEST_ASSERT(text_op_x(&rec, "ab") == 44.0f);
  my_widget_unref(ta);
}

static void test_justify_stretches_word_gaps(void) {
  rec_vg_t rec;
  my_widget_t* ta = make_ta("aaa bbb ccc", 40, true); /* 4 cells/line */
  float xa, xb, xc;
  my_text_area_set_align(ta, MY_TEXT_ALIGN_JUSTIFY);
  paint_at(ta, 40, 200, &rec);

  /* inner = 32. line "aaa " (4 cells = 32px) fills exactly -> no extra
   * stretch on v0; v1 "bbb " same; v2 "ccc" is the last segment -> LEFT.
   * So: "aaa" at x=4, "bbb" at x=4 (next line), "ccc" at x=4. */
  xa = text_op_x(&rec, "aaa");
  xb = text_op_x(&rec, "bbb");
  xc = text_op_x(&rec, "ccc");
  TEST_ASSERT(xa == 4.0f && xb == 4.0f && xc == 4.0f);
  my_widget_unref(ta);

  /* with slack: w=88 -> inner 80 (10 cells). v0 = "aaa bbb " (8 cells
   * = 64px): 1 separating space, extra = 80-64 = 16 -> "bbb" at
   * 4+24+8+16 = 52. v1 "ccc": last segment -> LEFT x=4. */
  ta = make_ta("aaa bbb ccc", 88, true);
  my_text_area_set_align(ta, MY_TEXT_ALIGN_JUSTIFY);
  paint_at(ta, 88, 200, &rec);
  xa = text_op_x(&rec, "aaa");
  xb = text_op_x(&rec, "bbb");
  xc = text_op_x(&rec, "ccc");
  TEST_ASSERT(xa == 4.0f);
  TEST_ASSERT(xb == 4.0f + 24.0f + 8.0f + 16.0f); /* word+space+extra */
  TEST_ASSERT(xc == 4.0f);                        /* last line not stretched */
  my_widget_unref(ta);
}

static void test_justify_off_without_wrap(void) {
  rec_vg_t rec;
  my_widget_t* ta = make_ta("a b", 72, false); /* no wrap */
  my_text_area_set_align(ta, MY_TEXT_ALIGN_JUSTIFY);
  paint_at(ta, 72, 200, &rec);
  TEST_ASSERT(text_op_x(&rec, "a b") == 4.0f); /* plain LEFT */
  my_widget_unref(ta);
}

static void test_rtl_right_align(void) {
  rec_vg_t rec;
  my_widget_t* ta;
  /* arabic word محمد (4 cps, shaped); RIGHT: block right edge at inner */
  ta = make_ta("\xD9\x85\xD8\xAD\xD9\x85\xD8\xAF", 64, false);
  my_text_area_set_align(ta, MY_TEXT_ALIGN_RIGHT);
  paint_at(ta, 64, 200, &rec);
  /* bitmap font fallback measures non-latin cps at cell width: shaped
   * forms are >= 0x80 multi-byte but the font-less path is not used
   * (bitmap font set): measure = 4 cells = 32px -> base = 4+(56-32)=28 */
  TEST_ASSERT(text_op_x(&rec, "\xD9\x85\xD8\xAD\xD9\x85\xD8\xAF") == 28.0f);
  my_widget_unref(ta);
}

static void test_xml_and_mvvm_align(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_widget_t* root = my_ui_load_str(
      NULL, pal,
      "<window w=\"200\" h=\"100\">"
      "<label text=\"t\" align=\"right\"/>"
      "<text_area align=\"center\" wrap=\"true\"/>"
      "</window>",
      NULL);
  my_widget_t* l = my_widget_get_child(root, 0);
  my_widget_t* ta = my_widget_get_child(root, 1);
  TEST_ASSERT_NOT_NULL(root);
  TEST_ASSERT_EQ_INT(((my_label_t*)l)->align, MY_TEXT_ALIGN_RIGHT);
  TEST_ASSERT_EQ_INT(((my_text_area_t*)ta)->align, MY_TEXT_ALIGN_CENTER);
  TEST_ASSERT(((my_text_area_t*)ta)->wrap);
  my_widget_unref(root);
  my_pal_destroy(pal);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_label_aligns);
  MYTEST_RUN(test_text_area_center_right);
  MYTEST_RUN(test_justify_stretches_word_gaps);
  MYTEST_RUN(test_justify_off_without_wrap);
  MYTEST_RUN(test_rtl_right_align);
  MYTEST_RUN(test_xml_and_mvvm_align);
MYTEST_MAIN_END()
