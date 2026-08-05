/**
 * @file golden_scenes.h
 * @brief Golden-image scenes, shared by my_golden_test (compare) and
 * my_golden_gen (regenerate references).
 *
 * One scene per pixel format. To regenerate the reference images after an
 * INTENTIONAL rendering change, run:
 *   ./build-c99/tests/my_golden_gen <repo-root>/tests/golden
 * then eyeball the PPM files (any image viewer) before committing.
 */
#ifndef GOLDEN_SCENES_H
#define GOLDEN_SCENES_H

#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"

typedef struct golden_scene_t {
  const char* name;
  uint32_t w;
  uint32_t h;
  my_pixel_format_t format;
  void (*render)(my_vgcanvas_t* vg);
} golden_scene_t;

static void golden_scene_shapes(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_set_antialias(vg, false);
  static const my_color_t NAVY = {16, 16, 64, 255};
  static const my_color_t RED = {255, 0, 0, 255};
  static const my_color_t GREEN = {0, 255, 0, 255};
  static const my_color_t YELLOW = {255, 255, 0, 255};
  static const my_color_t WHITE = {255, 255, 255, 255};

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, NAVY);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 64, 48});

  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){4, 4, 20, 12});

  my_vgcanvas_set_stroke_color(vg, GREEN);
  my_vgcanvas_set_line_width(vg, 2);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){28, 4, 20, 12});

  my_vgcanvas_set_fill_color(vg, YELLOW);
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){4, 20, 24, 16}, 4);

  my_vgcanvas_set_fill_color(vg, WHITE);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 50, 20);
  my_vgcanvas_line_to(vg, 60, 40);
  my_vgcanvas_line_to(vg, 40, 40);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);

  my_vgcanvas_set_stroke_color(vg, RED);
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 32, 44);
  my_vgcanvas_line_to(vg, 44, 24);
  my_vgcanvas_line_to(vg, 60, 44);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
}

static void golden_scene_clip(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_set_antialias(vg, false);
  static const my_color_t BLACK = {0, 0, 0, 255};
  static const my_color_t RED = {255, 0, 0, 255};
  static const my_color_t BLUE = {0, 0, 255, 255};

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, BLACK);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 32, 24});
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){8, 6, 16, 12});
  my_vgcanvas_set_fill_color(vg, RED);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 32, 24});
  my_vgcanvas_set_fill_color(vg, BLUE);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){16, 12, 8, 6});
  my_vgcanvas_end_frame(vg);
}

static void golden_scene_rounded(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_set_antialias(vg, false);
  static const my_color_t GRAY = {32, 32, 32, 255};
  static const my_color_t CYAN = {0, 255, 255, 255};
  static const my_color_t MAGENTA = {255, 0, 255, 255};

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, GRAY);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 32, 24});
  my_vgcanvas_set_fill_color(vg, CYAN);
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){4, 3, 24, 18}, 6);
  my_vgcanvas_set_stroke_color(vg, MAGENTA);
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){2, 1, 28, 22});
  my_vgcanvas_end_frame(vg);
}

static void golden_scene_concave(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_set_antialias(vg, false);
  static const my_color_t BLACK = {0, 0, 0, 255};
  static const my_color_t ORANGE = {255, 128, 0, 255};
  static const my_color_t WHITE = {255, 255, 255, 255};

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, BLACK);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 48, 36});

  /* L-shape (concave) */
  my_vgcanvas_set_fill_color(vg, ORANGE);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4, 4);
  my_vgcanvas_line_to(vg, 20, 4);
  my_vgcanvas_line_to(vg, 20, 18);
  my_vgcanvas_line_to(vg, 44, 18);
  my_vgcanvas_line_to(vg, 44, 32);
  my_vgcanvas_line_to(vg, 4, 32);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);

  /* even-odd hole: two nested rects in one path */
  my_vgcanvas_set_fill_color(vg, WHITE);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 24, 4);
  my_vgcanvas_line_to(vg, 44, 4);
  my_vgcanvas_line_to(vg, 44, 14);
  my_vgcanvas_line_to(vg, 24, 14);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_move_to(vg, 28, 6);
  my_vgcanvas_line_to(vg, 40, 6);
  my_vgcanvas_line_to(vg, 40, 12);
  my_vgcanvas_line_to(vg, 28, 12);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_end_frame(vg);
}

static void golden_scene_mono(my_vgcanvas_t* vg) {
  my_vgcanvas_soft_set_antialias(vg, false);
  static const my_color_t OFF = {0, 0, 0, 255};
  static const my_color_t ON = {255, 255, 255, 255};

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, OFF);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 64, 24});

  my_vgcanvas_set_fill_color(vg, ON);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){2, 2, 28, 20});
  my_vgcanvas_set_fill_color(vg, OFF);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){6, 6, 20, 12});

  my_vgcanvas_set_fill_color(vg, ON);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 48, 2);
  my_vgcanvas_line_to(vg, 62, 22);
  my_vgcanvas_line_to(vg, 34, 22);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_end_frame(vg);
}

static void golden_scene_aa(my_vgcanvas_t* vg) {
  static const my_color_t NAVY = {16, 16, 64, 255};
  static const my_color_t WHITE = {255, 255, 255, 255};
  static const my_color_t GREEN = {0, 255, 0, 255};
  static const my_color_t RED50 = {255, 0, 0, 128};

  my_vgcanvas_soft_set_antialias(vg, true); /* AA coverage visible */
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, NAVY);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 64, 48});

  my_vgcanvas_set_fill_color(vg, WHITE);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 50, 6);
  my_vgcanvas_line_to(vg, 60, 42);
  my_vgcanvas_line_to(vg, 8, 42);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);

  my_vgcanvas_set_fill_color(vg, GREEN);
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){4, 4, 20, 16}, 6);

  my_vgcanvas_set_fill_color(vg, RED50); /* translucent overlay */
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){24, 24, 30, 16});
  my_vgcanvas_end_frame(vg);
}

static void golden_scene_round_cap(my_vgcanvas_t* vg) {
  static const my_color_t NAVY = {16, 16, 64, 255};
  static const my_color_t YELLOW = {255, 220, 40, 255};
  my_vgcanvas_soft_set_antialias(vg, true);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, NAVY);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 64, 48});
  my_vgcanvas_set_stroke_color(vg, YELLOW);
  my_vgcanvas_set_line_width(vg, 5);
  my_vgcanvas_set_line_cap(vg, MY_LINE_CAP_ROUND);
  my_vgcanvas_set_line_join(vg, MY_LINE_JOIN_ROUND);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 10, 38);
  my_vgcanvas_line_to(vg, 24, 10);
  my_vgcanvas_line_to(vg, 40, 30);
  my_vgcanvas_line_to(vg, 56, 12);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
}

static const golden_scene_t GOLDEN_SCENES[] = {
    {"scene_shapes_rgb565", 64, 48, MY_PIXEL_FORMAT_RGB565, golden_scene_shapes},
    {"scene_clip_rgb888", 32, 24, MY_PIXEL_FORMAT_RGB888, golden_scene_clip},
    {"scene_rounded_argb8888", 32, 24, MY_PIXEL_FORMAT_ARGB8888,
     golden_scene_rounded},
    {"scene_concave_bgra8888", 48, 36, MY_PIXEL_FORMAT_BGRA8888,
     golden_scene_concave},
    {"scene_mono", 64, 24, MY_PIXEL_FORMAT_MONO, golden_scene_mono},
    {"scene_aa_bgra8888", 64, 48, MY_PIXEL_FORMAT_BGRA8888, golden_scene_aa},
    {"scene_round_cap_bgra8888", 64, 48, MY_PIXEL_FORMAT_BGRA8888,
     golden_scene_round_cap},
};

#define GOLDEN_SCENE_COUNT (sizeof(GOLDEN_SCENES) / sizeof(GOLDEN_SCENES[0]))

#endif /* GOLDEN_SCENES_H */
