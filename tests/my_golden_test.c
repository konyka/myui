/**
 * @file my_golden_test.c
 * @brief Golden-image tests: render each scene and byte-compare with the
 * reference PPM under tests/golden/.
 *
 * If a reference is missing or mismatches after an intentional rendering
 * change, regenerate with:
 *   ./build-c99/tests/my_golden_gen <repo-root>/tests/golden
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "golden_ppm.h"
#include "golden_scenes.h"
#include "mytest.h"

#ifndef MY_GOLDEN_DIR
#define MY_GOLDEN_DIR "golden"
#endif

static void check_scene(const golden_scene_t* scene) {
  char path[512];
  my_lcd_t* lcd = my_lcd_mem_create(NULL, scene->w, scene->h, scene->format);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  size_t cap = (size_t)scene->w * scene->h * 3 + 32;
  uint8_t* actual = (uint8_t*)malloc(cap);
  uint8_t* expected = NULL;
  size_t actual_size = 0, expected_size = 0;

  TEST_ASSERT_NOT_NULL(lcd);
  TEST_ASSERT_NOT_NULL(vg);
  TEST_ASSERT_NOT_NULL(actual);
  if (lcd == NULL || vg == NULL || actual == NULL) {
    free(actual);
    my_vgcanvas_destroy(vg);
    my_lcd_destroy(lcd);
    return;
  }

  scene->render(vg);
  actual_size = golden_ppm_render(lcd, actual);

  snprintf(path, sizeof(path), "%s/%s.ppm", MY_GOLDEN_DIR, scene->name);
  expected = golden_read_file(path, &expected_size);
  TEST_ASSERT_NOT_NULL(expected);
  if (expected != NULL) {
    if (expected_size != actual_size || memcmp(expected, actual, actual_size) != 0) {
      fprintf(stderr, "golden mismatch: %s (expected %zu bytes, actual %zu)\n",
              path, expected_size, actual_size);
      mytest_report(__FILE__, __LINE__, "golden image byte-compare");
    }
  }

  free(expected);
  free(actual);
  my_vgcanvas_destroy(vg);
  my_lcd_destroy(lcd);
}

static void test_golden_scenes(void) {
  size_t i;
  for (i = 0; i < GOLDEN_SCENE_COUNT; i++) {
    check_scene(&GOLDEN_SCENES[i]);
  }
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_golden_scenes);
MYTEST_MAIN_END()
