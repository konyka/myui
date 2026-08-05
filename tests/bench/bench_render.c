/**
 * @file bench_render.c
 * @brief Render benchmark: full-frame repaint of N buttons, 100 frames.
 *
 * Prints timings; loose upper bound guards against gross regressions.
 * Registered in ctest with label "bench".
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mypal/dummy/my_pal_dummy.h"
#include "myr/my_vgcanvas_soft.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"

#define BENCH_BUTTONS 50
#define BENCH_FRAMES 100

static double now_ms(void) {
  return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

int main(void) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_window_t* win = my_window_create(NULL, pal, 800, 480, "bench");
  int i, f;
  double t0, t1, total;

  for (i = 0; i < BENCH_BUTTONS; i++) {
    my_widget_t* b = my_button_create(NULL, "bench");
    my_widget_set_rect(b, &(my_rect_t){(i % 10) * 78, (i / 10) * 90, 72, 80});
    my_widget_add_child(my_window_widget(win), b);
    my_widget_unref(b);
  }

  /* warm up */
  my_widget_invalidate(my_window_widget(win), NULL);
  my_window_paint(win);

  t0 = now_ms();
  for (f = 0; f < BENCH_FRAMES; f++) {
    my_widget_invalidate(my_window_widget(win), NULL);
    my_window_paint(win);
  }
  t1 = now_ms();
  total = t1 - t0;

  printf("bench_render: %d buttons x %d frames: total %.1f ms, avg %.3f ms/frame\n",
         BENCH_BUTTONS, BENCH_FRAMES, total, total / BENCH_FRAMES);

  /* translucent fill (blend path) + AA on/off comparison */
  {
    my_window_t* win2 = my_window_create(NULL, pal, 800, 480, "b2");
    my_vgcanvas_t* vg2 = my_vgcanvas_soft_create(
        NULL, my_pal_window_get_lcd(win2->pal_window));
    t0 = now_ms();
    for (f = 0; f < BENCH_FRAMES; f++) {
      my_vgcanvas_begin_frame(vg2, NULL);
      for (i = 0; i < 100; i++) {
        my_vgcanvas_set_fill_color(vg2, my_color_rgba(255, 0, 0, 128));
        my_vgcanvas_fill_rect(vg2, &(my_rectf_t){(float)(i % 10) * 78,
                                                 (float)(i / 10) * 44, 72, 40});
      }
      my_vgcanvas_end_frame(vg2);
    }
    t1 = now_ms();
    printf("bench_render: 100 translucent rects x %d frames: total %.1f ms, "
           "avg %.3f ms/frame\n",
           BENCH_FRAMES, t1 - t0, (t1 - t0) / BENCH_FRAMES);

    /* AA level comparison on path fills */
    {
      int pass;
      for (pass = 0; pass < 3; pass++) {
        my_vgcanvas_soft_set_antialias_level(vg2, 2 - pass); /* 2, 1, 0 */
        t0 = now_ms();
        for (f = 0; f < BENCH_FRAMES; f++) {
          my_vgcanvas_begin_frame(vg2, NULL);
          for (i = 0; i < 8; i++) {
            my_vgcanvas_set_fill_color(vg2, my_color_rgb(255, 255, 255));
            my_vgcanvas_begin_path(vg2);
            my_vgcanvas_move_to(vg2, 100.0f * i + 50, 40);
            my_vgcanvas_line_to(vg2, 100.0f * i + 90, 400);
            my_vgcanvas_line_to(vg2, 100.0f * i + 10, 400);
            my_vgcanvas_close_path(vg2);
            my_vgcanvas_fill(vg2);
          }
          my_vgcanvas_end_frame(vg2);
        }
        t1 = now_ms();
        printf("bench_render: 8 triangles x %d frames AA_level=%d: total %.1f ms, "
               "avg %.3f ms/frame\n",
               BENCH_FRAMES, 2 - pass, t1 - t0,
               (t1 - t0) / BENCH_FRAMES);
      }
    }
    /* image scaling: 480x270 -> 800x600, nearest vs bilinear */
    {
      static uint8_t big_img[270 * 480 * 4];
      int pass;
      memset(big_img, 128, sizeof(big_img));
      for (pass = 0; pass < 2; pass++) {
        my_vgcanvas_soft_set_scale_filter(
            vg2, pass == 0 ? MY_SCALE_FILTER_NEAREST : MY_SCALE_FILTER_BILINEAR);
        t0 = now_ms();
        for (f = 0; f < BENCH_FRAMES; f++) {
          my_vgcanvas_begin_frame(vg2, NULL);
          my_vgcanvas_draw_image(vg2, big_img, 480, 270,
                                 &(my_rectf_t){0, 0, 800, 600}, NULL);
          my_vgcanvas_end_frame(vg2);
        }
        t1 = now_ms();
        printf("bench_render: 480x270->800x600 %s x %d frames: total %.1f ms, "
               "avg %.3f ms/frame\n",
               pass == 0 ? "nearest " : "bilinear", BENCH_FRAMES, t1 - t0,
               (t1 - t0) / BENCH_FRAMES);
      }
    }
    my_vgcanvas_destroy(vg2);
    my_widget_unref(my_window_widget(win2));
  }

  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);

  /* loose regression guard: must never take 200ms for one frame */
  if (total / BENCH_FRAMES > 200.0) {
    fprintf(stderr, "bench_render: too slow\n");
    return 1;
  }
  return 0;
}
