/**
 * @file my_image_test.c
 * @brief Unit tests for the image loader + image widget (M8b).
 *
 * The test PNG is generated programmatically with stb_image_write (no
 * binary assets committed).
 */
#include <stdio.h>
#include <string.h>

#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"
#include "myui/widgets/my_image.h"

#include "mytest.h"

#ifdef MYUI_IMAGE_STB
#include "stb/stb_image_write.h"
#endif

#define TEST_PNG "/tmp/myui_test_img.png"
#define TEST_PNG_B "/tmp/myui_test_img_b.png"

#ifdef MYUI_IMAGE_STB
static int g_have_png = 0;
#endif

static void make_test_pngs(void) {
#ifdef MYUI_IMAGE_STB
  uint8_t px[16 * 16 * 4];
  int x, y;
  /* 16x16 gradient with a transparent corner */
  for (y = 0; y < 16; y++) {
    for (x = 0; x < 16; x++) {
      uint8_t* p = px + (y * 16 + x) * 4;
      p[0] = (uint8_t)(x * 16);      /* R ramps right */
      p[1] = (uint8_t)(y * 16);      /* G ramps down */
      p[2] = 200;
      p[3] = (x < 4 && y < 4) ? 0 : 255; /* transparent 4x4 corner */
    }
  }
  g_have_png = stbi_write_png(TEST_PNG, 16, 16, 4, px, 16 * 4) != 0;
  /* second file for cache tests */
  px[3] = 255;
  stbi_write_png(TEST_PNG_B, 16, 16, 4, px, 16 * 4);
#endif
}

static void test_loader_decode(void) {
#ifdef MYUI_IMAGE_STB
  my_image_loader_t* loader;
  my_image_data_t* data;
  if (!g_have_png) {
    fprintf(stdout, "SKIP: png generation failed\n");
    return;
  }
  loader = my_image_loader_stb_create(NULL);
  TEST_ASSERT_NOT_NULL(loader);
  data = my_image_loader_load(loader, TEST_PNG);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_EQ_INT(data->w, 16);
  TEST_ASSERT_EQ_INT(data->h, 16);
  TEST_ASSERT_EQ_INT(data->pixels[2], 200);       /* B channel constant */
  TEST_ASSERT(data->pixels[0] < 4);               /* R=0 at x=0 */
  TEST_ASSERT_EQ_INT(data->pixels[3], 0);         /* transparent corner */
  TEST_ASSERT(my_image_loader_load(loader, "/nonexistent.png") == NULL);
  my_image_loader_free_data(loader, data);
  my_image_loader_destroy(loader);
#endif
}

static void test_widget_paint_and_scale(void) {
#ifdef MYUI_IMAGE_STB
  my_lcd_t* lcd = my_lcd_mem_create(NULL, 64, 64, MY_PIXEL_FORMAT_BGRA8888);
  my_vgcanvas_t* vg;
  my_widget_t* img;
  uint8_t* buf;
  if (!g_have_png) {
    fprintf(stdout, "SKIP: png generation failed\n");
    my_lcd_destroy(lcd);
    return;
  }
  img = my_image_create(NULL);
  my_image_set_image(img, TEST_PNG);
  my_widget_set_rect(img, &(my_rect_t){0, 0, 64, 64});

  /* FILL: stretches 16x16 over 64x64; bottom-right pixel = image's (15,15) */
  my_image_set_scale_mode(img, MY_IMAGE_SCALE_FILL);
  {
    my_widget_t* root = my_widget_create(NULL, "root");
    my_widget_set_rect(root, &(my_rect_t){0, 0, 64, 64});
    my_widget_add_child(root, img);
    my_widget_unref(img);
    vg = my_vgcanvas_soft_create(NULL, lcd);
    my_widget_paint(root, vg);
    my_widget_unref(root);
  }
  buf = my_lcd_mem_get_buffer(lcd);
  {
    const uint8_t* p = buf + (63 * 64 + 63) * 4;
    TEST_ASSERT_EQ_INT(p[0], 200); /* B */
    TEST_ASSERT(p[2] > 230);       /* R at x=15 -> 240 */
  }
  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
#endif
}

static void test_cache_hit_and_evict(void) {
#ifdef MYUI_IMAGE_STB
  size_t hits0, misses0, hits1, misses1;
  my_widget_t* img;
  if (!g_have_png) {
    fprintf(stdout, "SKIP: png generation failed\n");
    return;
  }
  img = my_image_create(NULL);
  my_image_set_image(img, TEST_PNG);
  my_image_set_scale_mode(img, MY_IMAGE_SCALE_FILL);
  my_widget_set_rect(img, &(my_rect_t){0, 0, 8, 8});
  {
    my_lcd_t* lcd = my_lcd_mem_create(NULL, 8, 8, MY_PIXEL_FORMAT_BGRA8888);
    my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
    my_widget_t* root = my_widget_create(NULL, "root");
    my_widget_set_rect(root, &(my_rect_t){0, 0, 8, 8});
    my_widget_add_child(root, img);
    my_widget_unref(img);

    my_image_cache_stats(&hits0, &misses0);
    my_widget_paint(root, vg); /* hit or miss depending on cache state */
    my_widget_paint(root, vg); /* same path: always a hit */
    my_image_cache_stats(&hits1, &misses1);
    TEST_ASSERT(hits1 >= hits0 + 1);

    /* a fresh path: another miss */
    my_image_set_image(img, TEST_PNG_B);
    my_widget_paint(root, vg);
    my_image_cache_stats(&hits1, &misses1);
    TEST_ASSERT(misses1 >= misses0 + 1);

    my_widget_unref(root);
    my_vgcanvas_destroy(vg);
    my_lcd_destroy(lcd);
  }
#endif
}

static void test_no_leak_with_debug_allocator(void) {
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_widget_t* img = my_image_create(dbg);
  my_image_set_image(img, TEST_PNG);
  my_image_set_scale_mode(img, MY_IMAGE_SCALE_CENTER);
  my_widget_unref(img);
  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  make_test_pngs();
  MYTEST_RUN(test_loader_decode);
  MYTEST_RUN(test_widget_paint_and_scale);
  MYTEST_RUN(test_cache_hit_and_evict);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
