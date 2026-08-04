/**
 * @file main.c
 * @brief demo_hello: myui M2 integration demo.
 *
 * Creates a window via the compile-time PAL port, draws a scene with the
 * software vgcanvas backend, and runs the main loop (quit on window
 * close or Escape). Under the dummy port (headless) it renders one frame
 * and exits; set MYUI_DEMO_DUMP_PPM=<path> to dump that frame as PPM.
 */
#include <stdio.h>
#include <stdlib.h>

#include "mypal/my_pal.h"
#include "myr/my_lcd_mem.h"
#include "myr/my_vgcanvas_soft.h"
#include "myui/my_window.h"

#ifndef MYUI_VERSION
#define MYUI_VERSION "0.1.0"
#endif

/** @brief Prefer a system TTF (stb backend), fall back to the 8x8 font. */
static my_font_t* create_default_font(void) {
  static const char* candidates[] = {
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf", NULL};
  my_font_t* font = NULL;
  int i;
  for (i = 0; candidates[i] != NULL && font == NULL; i++) {
    font = my_font_stb_create(NULL, candidates[i], 0);
  }
  if (font == NULL) {
    font = my_font_bitmap_create(NULL);
  }
  return font;
}

typedef struct app_t {
  my_pal_t* pal;
  my_pal_window_t* window;
  my_pal_main_loop_t* loop;
} app_t;

static void draw_scene(my_pal_window_t* window) {
  my_lcd_t* lcd = my_pal_window_get_lcd(window);
  my_vgcanvas_t* vg = my_vgcanvas_soft_create(NULL, lcd);
  static my_font_t* font = NULL;
  int32_t w = 0, h = 0;
  if (vg == NULL) {
    return;
  }
  if (font == NULL) {
    font = create_default_font();
  }
  my_vgcanvas_set_font(vg, font, 20);
  my_pal_window_get_size(window, &w, &h);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(16, 16, 64));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)w, (float)h});

  my_vgcanvas_set_fill_color(vg, my_color_rgb(220, 40, 40));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){40, 40, 200, 120});

  my_vgcanvas_set_stroke_color(vg, my_color_rgb(40, 220, 40));
  my_vgcanvas_set_line_width(vg, 4);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){280, 40, 200, 120});

  my_vgcanvas_set_fill_color(vg, my_color_rgb(240, 220, 40));
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){40, 220, 240, 160}, 24);

  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 500, 220);
  my_vgcanvas_line_to(vg, 620, 380);
  my_vgcanvas_line_to(vg, 380, 380);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);

  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_draw_text(vg, "myui: text works!", 40, 420);
  my_vgcanvas_end_frame(vg);

  my_vgcanvas_destroy(vg);
}

static my_ret_t on_event(void* ctx, my_pal_window_t* window,
                         const my_event_t* event) {
  app_t* app = (app_t*)ctx;
  switch (event->type) {
    case MY_EVENT_PAINT:
    case MY_EVENT_RESIZE:
      if (window != NULL) {
        draw_scene(window);
      }
      break;
    case MY_EVENT_QUIT:
      my_pal_main_loop_quit(app->loop);
      break;
    case MY_EVENT_KEY_DOWN:
      if (event->u.key.key == MY_KEY_ESCAPE) {
        my_pal_main_loop_quit(app->loop);
      }
      break;
    default:
      break;
  }
  return MY_RET_OK;
}

#ifdef MYUI_PAL_DUMMY
/** @brief Headless convenience: dump the dummy window's frame as PPM. */
static void dump_ppm(my_pal_window_t* window, const char* path) {
  my_lcd_t* lcd = my_pal_window_get_lcd(window);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  uint32_t w = my_lcd_get_width(lcd);
  uint32_t h = my_lcd_get_height(lcd);
  uint32_t x, y;
  FILE* f;
  if (buf == NULL) {
    return;
  }
  f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "demo_hello: cannot write %s\n", path);
    return;
  }
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
      fputc(p[2], f); /* R */
      fputc(p[1], f); /* G */
      fputc(p[0], f); /* B */
    }
  }
  fclose(f);
  printf("demo_hello: dumped %s\n", path);
}
#endif

int main(void) {
  app_t app;

  printf("myui demo_hello, myui version %s\n", MYUI_VERSION);

  app.pal = my_pal_create(NULL);
  if (app.pal == NULL) {
    fprintf(stderr, "demo_hello: failed to create platform\n");
    return 1;
  }
  app.window = my_pal_window_create(app.pal, 800, 480, "myui demo_hello");
  app.loop = my_pal_main_loop_create(app.pal);
  if (app.window == NULL || app.loop == NULL) {
    fprintf(stderr, "demo_hello: failed to create window/loop\n");
    my_pal_main_loop_destroy(app.loop);
    my_pal_window_destroy(app.window);
    my_pal_destroy(app.pal);
    return 1;
  }

  my_pal_set_event_handler(app.pal, on_event, &app);
  my_pal_window_show(app.window);
  draw_scene(app.window);

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      dump_ppm(app.window, dump);
    }
  }
#endif

  my_pal_main_loop_run(app.loop);

  my_pal_main_loop_destroy(app.loop);
  my_pal_window_destroy(app.window);
  my_pal_destroy(app.pal);
  return 0;
}
