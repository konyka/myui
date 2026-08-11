/**
 * @file my_text_layout_test.c
 * @brief Text layout tests (M11a): fast path, Arabic shaping, UBA
 * reorder, LRU cache. SheenBidi-dependent cases are compiled only with
 * MYUI_BIDI (the whole suite is registered either way).
 */
#include "myr/my_arabic_shape.h"
#include "myr/my_text_layout.h"

#include <string.h>

#include "mytest.h"

/* U+0645 U+062D U+0645 U+062F = محمد (meem-hah-meem-dal) */
static const char MUHAMMAD[] = "\xD9\x85\xD8\xAD\xD9\x85\xD8\xAF";
/* U+05D0 U+05D1 U+05D2 = אבג (alef-bet-gimel) */
static const char ALEF_BET_GIMEL[] = "\xD7\x90\xD7\x91\xD7\x92";

static void test_fast_path_identity(void) {
  my_text_layout_t* l;
  size_t i;
  my_text_layout_cache_flush();
  l = my_text_layout_process(NULL, "hello 123");
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(l->len, 9);
  TEST_ASSERT(!l->has_rtl);
  for (i = 0; i < l->len; i++) {
    TEST_ASSERT_EQ_INT(l->visual_cps[i], (uint32_t)"hello 123"[i]);
    TEST_ASSERT_EQ_INT(l->visual_to_logical[i], (uint32_t)i);
  }
  TEST_ASSERT_EQ_STR(l->visual_utf8, "hello 123");
  my_text_layout_destroy(l);

  /* may_need_bidi pre-scan agrees */
  TEST_ASSERT(!my_text_layout_may_need_bidi("hello 123"));
  TEST_ASSERT(my_text_layout_may_need_bidi(MUHAMMAD));
  TEST_ASSERT(my_text_layout_may_need_bidi(ALEF_BET_GIMEL));
  TEST_ASSERT(!my_text_layout_may_need_bidi(NULL));
}

static void test_arabic_shaping_unit(void) {
  /* my_arabic_shape itself: محمد logical -> initial/medial/medial/final */
  uint32_t cps[4] = {0x0645u, 0x062Du, 0x0645u, 0x062Fu};
  my_arabic_shape(cps, 4);
  TEST_ASSERT_EQ_INT(cps[0], 0xFEE3u); /* meem initial */
  TEST_ASSERT_EQ_INT(cps[1], 0xFEA4u); /* hah medial */
  TEST_ASSERT_EQ_INT(cps[2], 0xFEE4u); /* meem medial */
  TEST_ASSERT_EQ_INT(cps[3], 0xFEAAu); /* dal final (right-joining) */

  /* سلام: seen initial, lam medial, alef final, meem isolated (alef
   * cannot join forward) */
  {
    uint32_t s[4] = {0x0633u, 0x0644u, 0x0627u, 0x0645u};
    my_arabic_shape(s, 4);
    TEST_ASSERT_EQ_INT(s[0], 0xFEB3u);
    TEST_ASSERT_EQ_INT(s[1], 0xFEE0u);
    TEST_ASSERT_EQ_INT(s[2], 0xFE8Eu);
    TEST_ASSERT_EQ_INT(s[3], 0xFEE1u);
  }

  /* join classes */
  TEST_ASSERT_EQ_INT(my_arabic_join_class(0x0645u), MY_ARABIC_JOIN_DUAL);
  TEST_ASSERT_EQ_INT(my_arabic_join_class(0x062Fu), MY_ARABIC_JOIN_RIGHT);
  TEST_ASSERT_EQ_INT(my_arabic_join_class(0x0640u),
                     MY_ARABIC_JOIN_CAUSING); /* tatweel */
  TEST_ASSERT_EQ_INT(my_arabic_join_class('a'), MY_ARABIC_JOIN_NONE);
}

#if defined(MYUI_BIDI)

static void test_arabic_layout_visual_order(void) {
  /* pure Arabic word: RTL paragraph -> shaped AND reversed visually */
  my_text_layout_t* l = my_text_layout_process(NULL, MUHAMMAD);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(l->len, 4);
  TEST_ASSERT(l->has_rtl);
  /* visual (left-to-right): dal-final, meem-medial, hah-medial,
   * meem-initial */
  TEST_ASSERT_EQ_INT(l->visual_cps[0], 0xFEAAu);
  TEST_ASSERT_EQ_INT(l->visual_cps[1], 0xFEE4u);
  TEST_ASSERT_EQ_INT(l->visual_cps[2], 0xFEA4u);
  TEST_ASSERT_EQ_INT(l->visual_cps[3], 0xFEE3u);
  TEST_ASSERT_EQ_INT(l->visual_to_logical[0], 3u);
  TEST_ASSERT_EQ_INT(l->visual_to_logical[3], 0u);
  my_text_layout_destroy(l);
}

static void test_hebrew_in_ltr_paragraph(void) {
  /* "abc אבג def": LTR paragraph, Hebrew run reversed in place */
  char text[32];
  my_text_layout_t* l;
  static const uint32_t expect[] = {'a', 'b', 'c', ' ',   0x05D2u, 0x05D1u,
                                    0x05D0u, ' ', 'd', 'e', 'f'};
  static const uint32_t expect_map[] = {0, 1, 2, 3, 6, 5, 4, 7, 8, 9, 10};
  size_t i;
  strcpy(text, "abc ");
  strcat(text, ALEF_BET_GIMEL);
  strcat(text, " def");
  l = my_text_layout_process(NULL, text);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(l->len, 11);
  TEST_ASSERT(l->has_rtl);
  for (i = 0; i < l->len; i++) {
    TEST_ASSERT_EQ_INT(l->visual_cps[i], expect[i]);
    TEST_ASSERT_EQ_INT(l->visual_to_logical[i], expect_map[i]);
  }
  my_text_layout_destroy(l);
}

static void test_rtl_paragraph_reversed(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, ALEF_BET_GIMEL);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(l->len, 3);
  TEST_ASSERT(l->has_rtl);
  TEST_ASSERT_EQ_INT(l->visual_cps[0], 0x05D2u); /* gimel first visually */
  TEST_ASSERT_EQ_INT(l->visual_cps[2], 0x05D0u);
  my_text_layout_destroy(l);
}

/* ---------------- boundary <-> visual mapping (M12a) ----------------
 * "abc אבג def": logical a0 b1 c2 sp3 א4 ב5 ג6 sp7 d8 e9 f10;
 * visual: a b c sp ג ב א sp d e f (8px cells, NULL font). */

static const char MIXED[] = "abc \xD7\x90\xD7\x91\xD7\x92 def";

static void test_boundary_visual_x(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, MIXED);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 0), 0);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 3), 24);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 4), 32);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 5), 48);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 6), 40);
  /* alias: boundary 7 (after gimel) shares boundary 4's visual spot at
   * the run's left end (documented single-cursor rule) */
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 7), 32);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 8), 64);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 11), 88);
  my_text_layout_destroy(l);
}

static void test_boundary_right_walk(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, MIXED);
  static const size_t expect[] = {1, 2, 3, 4, 6, 5, 8, 9, 10, 11};
  size_t b = 0, i;
  TEST_ASSERT_NOT_NULL(l);
  for (i = 0; i < 10; i++) {
    b = my_text_layout_boundary_right(l, b);
    TEST_ASSERT_EQ_INT(b, expect[i]);
  }
  TEST_ASSERT_EQ_INT(my_text_layout_boundary_right(l, 11), 11); /* at end */
  my_text_layout_destroy(l);
}

static void test_boundary_left_walk(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, MIXED);
  static const size_t expect[] = {10, 9, 8, 5, 6, 4, 3, 2, 1, 0};
  size_t b = 11, i;
  TEST_ASSERT_NOT_NULL(l);
  for (i = 0; i < 10; i++) {
    b = my_text_layout_boundary_left(l, b);
    TEST_ASSERT_EQ_INT(b, expect[i]);
  }
  TEST_ASSERT_EQ_INT(my_text_layout_boundary_left(l, 0), 0); /* at start */
  my_text_layout_destroy(l);
}

static void test_boundary_home_end_pure_rtl(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, ALEF_BET_GIMEL);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(my_text_layout_boundary_home(l), 3); /* visual start */
  TEST_ASSERT_EQ_INT(my_text_layout_boundary_end(l), 0);   /* visual end */
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 0), 24);
  TEST_ASSERT_EQ_INT(my_text_layout_visual_x(l, NULL, 8, 3), 0);
  my_text_layout_destroy(l);
}

static void test_logical_at_x_clicks(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, MIXED);
  TEST_ASSERT_NOT_NULL(l);
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 0), 0);
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 34), 4); /* ג L */
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 36), 6); /* ג R */
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 40), 6); /* ב L */
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 44), 5); /* ב mid->R */
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 46), 5); /* ב R */
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 60), 8); /* sp R */
  TEST_ASSERT_EQ_INT(my_text_layout_logical_at_x(l, NULL, 8, 500), 11);
  my_text_layout_destroy(l);
}

static void test_selection_visual_rects(void) {
  my_text_layout_t* l = my_text_layout_process(NULL, MIXED);
  my_rectf_t r[4];
  size_t n;
  TEST_ASSERT_NOT_NULL(l);

  n = my_text_layout_visual_rects(l, NULL, 8, 0, 11, r, 4);
  TEST_ASSERT_EQ_INT(n, 1);
  TEST_ASSERT(r[0].x == 0.0f && r[0].w == 88.0f);

  /* Hebrew run only [4,7): one rect over גבא (x 32, w 24) */
  n = my_text_layout_visual_rects(l, NULL, 8, 4, 7, r, 4);
  TEST_ASSERT_EQ_INT(n, 1);
  TEST_ASSERT(r[0].x == 32.0f && r[0].w == 24.0f);

  /* "c"+"א" across the run boundary [3,5): two segments */
  n = my_text_layout_visual_rects(l, NULL, 8, 3, 5, r, 4);
  TEST_ASSERT_EQ_INT(n, 2);
  TEST_ASSERT(r[0].x == 24.0f && r[0].w == 8.0f);
  TEST_ASSERT(r[1].x == 48.0f && r[1].w == 8.0f);
  my_text_layout_destroy(l);
}


#endif /* MYUI_BIDI */

static void test_cache_hit_and_eviction(void) {
  char buf[16];
  size_t i;
  my_text_layout_cache_flush();
  TEST_ASSERT_EQ_INT(my_text_layout_cache_size(), 0);

  /* hit: same text processed twice occupies one slot */
  my_text_layout_destroy(my_text_layout_process(NULL, "cache-a"));
  TEST_ASSERT_EQ_INT(my_text_layout_cache_size(), 1);
  my_text_layout_destroy(my_text_layout_process(NULL, "cache-a"));
  TEST_ASSERT_EQ_INT(my_text_layout_cache_size(), 1);

  /* fill to capacity (64) and overflow by one: size stays 64 (evicted) */
  for (i = 0; i < 64; i++) {
    snprintf(buf, sizeof(buf), "cache-%zu", i + 1);
    my_text_layout_destroy(my_text_layout_process(NULL, buf));
  }
  TEST_ASSERT_EQ_INT(my_text_layout_cache_size(), 64);
  my_text_layout_destroy(my_text_layout_process(NULL, "cache-overflow"));
  TEST_ASSERT_EQ_INT(my_text_layout_cache_size(), 64);

  /* the evicted oldest entry ("cache-a") recomputes correctly */
  {
    my_text_layout_t* l = my_text_layout_process(NULL, "cache-a");
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQ_INT(l->len, 7);
    TEST_ASSERT_EQ_STR(l->visual_utf8, "cache-a");
    my_text_layout_destroy(l);
  }
  my_text_layout_cache_flush();
  TEST_ASSERT_EQ_INT(my_text_layout_cache_size(), 0);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_text_layout_t* l;
  my_text_layout_cache_flush();
  l = my_text_layout_process(dbg, MUHAMMAD);
  TEST_ASSERT_NOT_NULL(l);
  my_text_layout_destroy(l);
  l = my_text_layout_process(dbg, "plain ltr");
  TEST_ASSERT_NOT_NULL(l);
  my_text_layout_destroy(l);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
  my_text_layout_cache_flush();
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_fast_path_identity);
  MYTEST_RUN(test_arabic_shaping_unit);
#if defined(MYUI_BIDI)
  MYTEST_RUN(test_arabic_layout_visual_order);
  MYTEST_RUN(test_hebrew_in_ltr_paragraph);
  MYTEST_RUN(test_rtl_paragraph_reversed);
  MYTEST_RUN(test_boundary_visual_x);
  MYTEST_RUN(test_boundary_right_walk);
  MYTEST_RUN(test_boundary_left_walk);
  MYTEST_RUN(test_boundary_home_end_pure_rtl);
  MYTEST_RUN(test_logical_at_x_clicks);
  MYTEST_RUN(test_selection_visual_rects);
#endif
  MYTEST_RUN(test_cache_hit_and_eviction);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
