/**
 * @file golden_gen.c
 * @brief Regenerate golden reference images (NOT a ctest test).
 *
 * Usage: my_golden_gen [output-dir]   (default: tests/golden)
 * Run after an INTENTIONAL rendering change, then review the PPM files
 * before committing them.
 */
#include <stdio.h>

#include "golden_ppm.h"
#include "golden_scenes.h"

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : "tests/golden";
  size_t i;
  int failures = 0;

  for (i = 0; i < GOLDEN_SCENE_COUNT; i++) {
    const golden_scene_t* scene = &GOLDEN_SCENES[i];
    char path[512];
    my_lcd_t* lcd = my_lcd_mem_create(NULL, scene->w, scene->h, scene->format);
    my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
    if (lcd == NULL || vg == NULL) {
      fprintf(stderr, "gen: failed to create surface for %s\n", scene->name);
      failures++;
      my_vgcanvas_destroy(vg);
      my_lcd_destroy(lcd);
      continue;
    }
    scene->render(vg);
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, scene->name);
    if (!golden_ppm_write(path, lcd)) {
      fprintf(stderr, "gen: failed to write %s\n", path);
      failures++;
    } else {
      printf("gen: wrote %s\n", path);
    }
    my_vgcanvas_destroy(vg);
    my_lcd_destroy(lcd);
  }
  return failures;
}
