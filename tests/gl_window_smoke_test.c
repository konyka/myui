/**
 * @file gl_window_smoke_test.c
 * @brief Real-window GLES smoke test (M10c): PAL window GL mount
 * (my_pal_window_gl_enable), render with the gles2 vgcanvas backend,
 * glReadPixels verification before swap, then a 300ms main-loop stay.
 * Skips (exit 0) when no display/compositor or no GL mount is available.
 * Registered only when the resolved port was built with EGL support.
 */
#include <stdio.h>

#include <GLES2/gl2.h>

#include "mypal/my_pal.h"
#include "myr/my_vgcanvas_gles2.h"

#include "mytest.h"

static my_pal_main_loop_t* g_loop;
static int g_frames;

static my_ret_t on_quit_timer(void* ctx) {
  my_pal_main_loop_t* loop = (my_pal_main_loop_t*)ctx;
  my_pal_main_loop_quit(loop);
  return MY_RET_FAIL; /* one-shot */
}

static void render_once(my_pal_gl_t* gl, my_vgcanvas_t* vg) {
  GLubyte px[4];
  TEST_ASSERT_EQ_INT(my_pal_gl_make_current(gl), MY_RET_OK);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(32, 32, 32));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, 240, 160});
  my_vgcanvas_set_fill_color(vg, my_color_rgb(220, 40, 40));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){40, 40, 80, 60});
  my_vgcanvas_end_frame(vg);
  glFinish();
  TEST_ASSERT(glGetError() == GL_NO_ERROR);
  /* back buffer is readable before the swap; rect y 40..100 (top-based)
   * maps to GL y 60..120 in a 160px window */
  glReadPixels(80, 70, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  TEST_ASSERT(px[0] > 180 && px[1] < 90); /* red inside the rect */
  glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  TEST_ASSERT(px[0] < 90); /* dark background outside */
  TEST_ASSERT_EQ_INT(my_pal_gl_swap_buffers(gl), MY_RET_OK);
  g_frames++;
}

/** @brief M11c: when the GL mount negotiated an MSAA surface, a diagonal
 * stroke must show partially covered (intermediate) edge pixels;
 * otherwise verify the documented no-AA fallback path. */
static void check_msaa(my_pal_gl_t* gl, my_vgcanvas_t* vg) {
  TEST_ASSERT_EQ_INT(my_pal_gl_make_current(gl), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_vgcanvas_gles2_set_antialias(vg, true), MY_RET_OK);
  if (!my_pal_gl_has_multisample(gl)) {
    fprintf(stdout, "gl window smoke: no MSAA config (fallback path)\n");
    TEST_ASSERT(glGetError() == GL_NO_ERROR);
    return;
  }
  fprintf(stdout, "gl window smoke: MSAA surface active\n");
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_set_line_width(vg, 2.0f);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 20.5f, 140.0f);
  my_vgcanvas_line_to(vg, 220.0f, 19.5f);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
  glFinish();
  TEST_ASSERT(glGetError() == GL_NO_ERROR);
  {
    int x, y, intermediate = 0;
    GLubyte px[4];
    /* the stroke band: x+y ~ 160 (top-based) -> GL y = 160-y */
    for (x = 30; x < 210 && !intermediate; x++) {
      for (y = 20; y < 140; y++) {
        if (x + y >= 158 && x + y <= 164) {
          glReadPixels(x, 160 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
          if (px[0] > 10 && px[0] < 245) {
            intermediate = 1;
            break;
          }
        }
      }
    }
    TEST_ASSERT(intermediate); /* MSAA edge coverage visible */
  }
}

static my_ret_t on_event(void* ctx, my_pal_window_t* win,
                         const my_event_t* e) {
  my_pal_gl_t* gl = (my_pal_gl_t*)ctx;
  (void)win;
  if (e->type == MY_EVENT_PAINT) {
    g_frames++; /* compositor asked for a frame; content already swapped */
  }
  if (e->type == MY_EVENT_QUIT) {
    my_pal_main_loop_quit(g_loop);
  }
  (void)gl;
  return MY_RET_OK;
}

static void test_gl_window_smoke(void) {
  my_pal_t* pal;
  my_pal_window_t* win;
  my_pal_gl_t* gl;
  my_vgcanvas_t* vg;
  int32_t w = 0, h = 0;

  pal = my_pal_create(NULL);
  if (pal == NULL) {
    fprintf(stdout, "SKIP: no display/compositor\n");
    return;
  }
  win = my_pal_window_create(pal, 240, 160, "gl_smoke");
  g_loop = my_pal_main_loop_create(pal);
  TEST_ASSERT_NOT_NULL(win);
  TEST_ASSERT_NOT_NULL(g_loop);
  if (win == NULL || g_loop == NULL) {
    my_pal_main_loop_destroy(g_loop);
    my_pal_window_destroy(win);
    my_pal_destroy(pal);
    return;
  }

  gl = my_pal_window_gl_enable(win);
  if (gl == NULL) {
    fprintf(stdout, "SKIP: port has no GL mount\n");
    my_pal_main_loop_destroy(g_loop);
    my_pal_window_destroy(win);
    my_pal_destroy(pal);
    return;
  }
  TEST_ASSERT_EQ_INT(my_pal_gl_make_current(gl), MY_RET_OK);
  TEST_ASSERT_EQ_INT(my_pal_gl_get_size(gl, &w, &h), MY_RET_OK);
  TEST_ASSERT(w > 0 && h > 0);

  vg = my_vgcanvas_gles2_create(NULL, w, h);
  TEST_ASSERT_NOT_NULL(vg);
  if (vg == NULL) {
    my_pal_gl_destroy(gl);
    my_pal_main_loop_destroy(g_loop);
    my_pal_window_destroy(win);
    my_pal_destroy(pal);
    return;
  }

  my_pal_set_event_handler(pal, on_event, gl);
  my_pal_window_show(win);
  render_once(gl, vg);
  check_msaa(gl, vg);

  /* stay alive ~300ms (real window on screen), then quit */
  my_pal_main_loop_add_timer(g_loop, on_quit_timer, g_loop, 300);
  TEST_ASSERT_EQ_INT(my_pal_main_loop_run(g_loop), MY_RET_OK);
  TEST_ASSERT(g_frames >= 1);

  my_vgcanvas_destroy(vg);
  my_pal_gl_destroy(gl);
  my_pal_main_loop_destroy(g_loop);
  my_pal_window_destroy(win);
  my_pal_destroy(pal);
  fprintf(stdout, "gl window smoke: ran, %d frames\n", g_frames);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_gl_window_smoke);
MYTEST_MAIN_END()
