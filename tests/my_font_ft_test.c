/**
 * @file my_font_ft_test.c
 * @brief FreeType backend tests (M16): metrics/glyph/bearings, LRU cache
 * counters, leaks, and the hinting sharpness difference vs the stb
 * backend (fraction of mid-coverage pixels).
 */
#include "myr/my_font_ft.h"

#include <stdio.h>

#include "mytest.h"

#ifdef MYUI_FONT_FREETYPE

static const char* find_ttf(void) {
  static const char* candidates[] = {
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf",
      NULL};
  int i;
  for (i = 0; candidates[i] != NULL; i++) {
    FILE* f = fopen(candidates[i], "rb");
    if (f != NULL) {
      fclose(f);
      return candidates[i];
    }
  }
  return NULL;
}

static void test_ft_metrics_and_glyph(void) {
  const char* path = find_ttf();
  my_font_t* f;
  my_glyph_t g;
  int32_t w = 0, h = 0;
  if (path == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  f = my_font_ft_create(NULL, path, 0, 0);
  TEST_ASSERT(f != NULL);
  TEST_ASSERT(my_font_measure(f, "Hello", 16, &w, &h) == MY_RET_OK);
  TEST_ASSERT(w > 20 && w < 100);
  TEST_ASSERT(h > 0);
  TEST_ASSERT(my_font_ascent(f, 16) > 8);
  TEST_ASSERT(my_font_line_height(f, 16) >= my_font_ascent(f, 16));
  TEST_ASSERT_EQ_INT(my_font_get_glyph(f, 'A', 16, &g), MY_RET_OK);
  TEST_ASSERT(g.bitmap != NULL);
  TEST_ASSERT(g.w > 5 && g.h > 8);
  TEST_ASSERT(g.bearing_y > 5);   /* above the baseline */
  TEST_ASSERT(g.advance > 5);
  my_font_destroy(f);
}

static void test_ft_cache_counters(void) {
  const char* path = find_ttf();
  my_font_t* f;
  my_glyph_t g;
  size_t hits0, miss0;
  if (path == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  f = my_font_ft_create(NULL, path, 0, 4); /* tiny cache: force eviction */
  TEST_ASSERT(f != NULL);
  my_font_get_glyph(f, 'A', 16, &g);
  miss0 = my_font_ft_cache_misses(f);
  hits0 = my_font_ft_cache_hits(f);
  TEST_ASSERT_EQ_INT((int)miss0, 1);
  TEST_ASSERT_EQ_INT((int)hits0, 0);
  my_font_get_glyph(f, 'A', 16, &g); /* hit */
  TEST_ASSERT_EQ_INT((int)my_font_ft_cache_hits(f), 1);
  /* force eviction with 5 distinct glyphs in a 4-entry cache */
  my_font_get_glyph(f, 'B', 16, &g);
  my_font_get_glyph(f, 'C', 16, &g);
  my_font_get_glyph(f, 'D', 16, &g);
  my_font_get_glyph(f, 'E', 16, &g);
  my_font_get_glyph(f, 'A', 16, &g); /* A was evicted: miss again */
  TEST_ASSERT(my_font_ft_cache_misses(f) > 1);
  my_font_destroy(f);
}

/** @brief Fraction of pixels with mid coverage (1..254) in a glyph. */
static double mid_ratio(const my_glyph_t* g) {
  size_t i, n = (size_t)g->w * (size_t)g->h;
  size_t mid = 0;
  if (n == 0 || g->bitmap == NULL) {
    return 0.0;
  }
  for (i = 0; i < n; i++) {
    if (g->bitmap[i] > 0 && g->bitmap[i] < 255) {
      mid++;
    }
  }
  return (double)mid / (double)n;
}

static void test_ft_hinting_sharper_than_stb(void) {
  const char* path = find_ttf();
  my_font_t* ft;
  my_font_t* stb;
  my_glyph_t gf, gs;
  double rf = 1.0, rs = 0.0;
  if (path == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  ft = my_font_ft_create(NULL, path, 0, 0);
  stb = my_font_stb_create(NULL, path, 0);
  if (ft == NULL || stb == NULL) {
    fprintf(stdout, "SKIP: ft or stb backend unavailable\n");
    my_font_destroy(ft);
    my_font_destroy(stb);
    return;
  }
  /* hinted ft rendering snaps edges to pixels: fewer mid-coverage
   * pixels than unhinted stb at a small size */
  my_font_get_glyph(ft, 'H', 13, &gf);
  my_font_get_glyph(stb, 'H', 13, &gs);
  rf = mid_ratio(&gf);
  rs = mid_ratio(&gs);
  fprintf(stdout, "hinting mid-coverage ratio: ft=%.3f stb=%.3f\n", rf, rs);
  TEST_ASSERT(rf < rs * 0.95); /* ft visibly sharper */
  my_font_destroy(ft);
  my_font_destroy(stb);
}

static void test_ft_no_leak(void) {
  const char* path = find_ttf();
  my_allocator_t* dbg;
  my_font_t* f;
  my_glyph_t g;
  if (path == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  dbg = my_allocator_debug_create(NULL);
  f = my_font_ft_create(dbg, path, 0, 8);
  my_font_get_glyph(f, 'A', 16, &g);
  my_font_get_glyph(f, 0x4E2D, 16, &g);
  my_font_destroy(f);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

static void test_ft_bad_path(void) {
  TEST_ASSERT(my_font_ft_create(NULL, "/nonexistent/none.ttf", 0, 0) == NULL);
}

#else

static void test_ft_off_skips(void) {
  fprintf(stdout, "SKIP: MYUI_FONT_FREETYPE off\n");
}

#endif /* MYUI_FONT_FREETYPE */

MYTEST_MAIN_BEGIN()
#ifdef MYUI_FONT_FREETYPE
  MYTEST_RUN(test_ft_metrics_and_glyph);
  MYTEST_RUN(test_ft_cache_counters);
  MYTEST_RUN(test_ft_hinting_sharper_than_stb);
  MYTEST_RUN(test_ft_no_leak);
  MYTEST_RUN(test_ft_bad_path);
#else
  MYTEST_RUN(test_ft_off_skips);
#endif
MYTEST_MAIN_END()
