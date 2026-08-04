/**
 * @file bench_render.c
 * @brief Render benchmark: full-frame repaint of N buttons, 100 frames.
 *
 * Prints timings; loose upper bound guards against gross regressions.
 * Registered in ctest with label "bench".
 */
#include <stdio.h>
#include <time.h>

#include "mypal/dummy/my_pal_dummy.h"
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

  my_widget_unref(my_window_widget(win));
  my_pal_destroy(pal);

  /* loose regression guard: must never take 200ms for one frame */
  if (total / BENCH_FRAMES > 200.0) {
    fprintf(stderr, "bench_render: too slow\n");
    return 1;
  }
  return 0;
}
