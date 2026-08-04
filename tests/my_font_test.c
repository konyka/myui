/**
 * @file my_font_test.c
 * @brief Unit tests for my_font (bitmap font + stb backend + cache).
 */
#include <stdio.h>

#include "myr/my_font.h"

#include "mytest.h"

static void test_utf8_decode(void) {
  const char* p = "a\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80!";
  TEST_ASSERT_EQ_INT(my_utf8_next(&p), 'a');
  TEST_ASSERT_EQ_INT(my_utf8_next(&p), 0xE9);
  TEST_ASSERT_EQ_INT(my_utf8_next(&p), 0x4E2D);
  TEST_ASSERT_EQ_INT(my_utf8_next(&p), 0x1F600);
  TEST_ASSERT_EQ_INT(my_utf8_next(&p), '!');
  {
    const char* bad = "\xFF\x80";
    TEST_ASSERT_EQ_INT(my_utf8_next(&bad), 0xFFFD);
  }
}

static void test_bitmap_font_metrics(void) {
  my_font_t* f = my_font_bitmap_create(NULL);
  int32_t w = 0, h = 0;
  my_glyph_t g;

  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_EQ_INT(my_font_measure(f, "hello", 8, &w, &h), MY_RET_OK);
  TEST_ASSERT_EQ_INT(w, 40); /* monospace 8px cells */
  TEST_ASSERT_EQ_INT(h, 8);
  my_font_measure(f, "ab", 16, &w, &h);
  TEST_ASSERT_EQ_INT(w, 32);

  TEST_ASSERT_EQ_INT(my_font_get_glyph(f, 'A', 8, &g), MY_RET_OK);
  TEST_ASSERT_NOT_NULL(g.bitmap);
  TEST_ASSERT_EQ_INT(g.w, 8);
  TEST_ASSERT(g.advance == 8);
  /* 'A' has some pixels set */
  {
    int i, sum = 0;
    for (i = 0; i < 8; i++) {
      sum += g.bitmap[i];
    }
    TEST_ASSERT(sum > 0);
  }
  TEST_ASSERT_EQ_INT(my_font_ascent(f, 8), 8);
  TEST_ASSERT_EQ_INT(my_font_line_height(f, 8), 8);

  my_font_destroy(f);
}

/* ---------------- stb backend ---------------- */

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

static void test_stb_metrics_and_glyph(void) {
  const char* path = find_ttf();
  my_font_t* f;
  int32_t w = 0, h = 0;
  my_glyph_t g;

  if (path == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  f = my_font_stb_create(NULL, path, 0);
  if (f == NULL) {
    fprintf(stdout, "SKIP: stb font unavailable (OFF or bad file)\n");
    return;
  }

  TEST_ASSERT_EQ_INT(my_font_measure(f, "hello", 16, &w, &h), MY_RET_OK);
  TEST_ASSERT(w > 20 && w < 100);
  TEST_ASSERT(h >= 16 && h < 32);

  TEST_ASSERT_EQ_INT(my_font_get_glyph(f, 'A', 16, &g), MY_RET_OK);
  TEST_ASSERT_NOT_NULL(g.bitmap);
  TEST_ASSERT(g.w > 0 && g.h > 0);
  TEST_ASSERT(g.advance > 0);

  my_font_destroy(f);
}

static void test_stb_cache_hit_and_lru(void) {
  const char* path = find_ttf();
  my_font_t* f;
  my_glyph_t g;
  size_t hits0, miss0;

  if (path == NULL) {
    fprintf(stdout, "SKIP: no TTF found\n");
    return;
  }
  f = my_font_stb_create(NULL, path, 2); /* tiny cache: 2 entries */
  if (f == NULL) {
    fprintf(stdout, "SKIP: stb font unavailable (OFF or bad file)\n");
    return;
  }

  my_font_get_glyph(f, 'a', 12, &g); /* miss: a */
  my_font_get_glyph(f, 'b', 12, &g); /* miss: b */
  hits0 = my_font_stb_cache_hits(f);
  miss0 = my_font_stb_cache_misses(f);
  my_font_get_glyph(f, 'a', 12, &g); /* HIT: a */
  TEST_ASSERT_EQ_INT(my_font_stb_cache_hits(f), hits0 + 1);

  /* cache is {a, b}, a touched most recently: c evicts b */
  my_font_get_glyph(f, 'c', 12, &g); /* miss: c (evicts b) */
  TEST_ASSERT_EQ_INT(my_font_stb_cache_misses(f), miss0 + 1);

  /* a still cached, b was evicted: a hits, b misses */
  my_font_get_glyph(f, 'a', 12, &g);
  TEST_ASSERT_EQ_INT(my_font_stb_cache_hits(f), hits0 + 2);
  my_font_get_glyph(f, 'b', 12, &g);
  TEST_ASSERT_EQ_INT(my_font_stb_cache_misses(f), miss0 + 2);

  my_font_destroy(f);
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_font_t* bmp = my_font_bitmap_create(dbg);
  const char* path = find_ttf();
  my_glyph_t g;

  my_font_get_glyph(bmp, 'x', 8, &g);
  my_font_destroy(bmp);

  if (path != NULL) {
    my_font_t* stb = my_font_stb_create(dbg, path, 4);
    if (stb != NULL) {
      my_font_get_glyph(stb, 'q', 20, &g);
      my_font_get_glyph(stb, 'w', 20, &g);
      my_font_get_glyph(stb, 'q', 20, &g);
      my_font_destroy(stb);
    }
  }

  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_utf8_decode);
  MYTEST_RUN(test_bitmap_font_metrics);
  MYTEST_RUN(test_stb_metrics_and_glyph);
  MYTEST_RUN(test_stb_cache_hit_and_lru);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
