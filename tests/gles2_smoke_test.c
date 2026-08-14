/**
 * @file gles2_smoke_test.c
 * @brief Real-context GLES2 smoke test: EGL surfaceless (Mesa) pbuffer
 * context, render with the gles2 backend, glReadPixels verification.
 * Skips (exit 0) when no usable EGL context can be created, so headless
 * CI still passes. Registered only when EGL + GLES2 are available.
 */
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "myr/my_vgcanvas_gles2.h"

#include "mytest.h"

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLSurface g_surf = EGL_NO_SURFACE;

static int egl_setup(int samples) {
  EGLint major, minor, n;
  EGLConfig cfg;
  EGLint cfg_plain[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                        EGL_NONE};
  EGLint cfg_msaa[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                       EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                       EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4, EGL_NONE};
  EGLint surf_attrs[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
  EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

  /* try surfaceless platform first (works without any window system) */
  g_dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                EGL_DEFAULT_DISPLAY, NULL);
  if (g_dpy == EGL_NO_DISPLAY) {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  }
  if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, &major, &minor)) {
    return -1;
  }
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    return -1;
  }
  if (samples > 0) {
    if (!eglChooseConfig(g_dpy, cfg_msaa, &cfg, 1, &n) || n < 1) {
      return -2; /* no MSAA pbuffer config: caller skips */
    }
  } else if (!eglChooseConfig(g_dpy, cfg_plain, &cfg, 1, &n) || n < 1) {
    return -1;
  }
  g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (g_ctx == EGL_NO_CONTEXT) {
    return -1;
  }
  g_surf = eglCreatePbufferSurface(g_dpy, cfg, surf_attrs);
  if (g_surf == EGL_NO_SURFACE) {
    /* surfaceless make-current */
    if (!eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) {
      return -1;
    }
    return 0;
  }
  if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
    return -1;
  }
  return 0;
}

static void egl_teardown(void) {
  if (g_dpy != EGL_NO_DISPLAY) {
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_surf != EGL_NO_SURFACE) {
      eglDestroySurface(g_dpy, g_surf);
    }
    if (g_ctx != EGL_NO_CONTEXT) {
      eglDestroyContext(g_dpy, g_ctx);
    }
    eglTerminate(g_dpy);
  }
}

static void test_gles2_real_render(void) {
  my_vgcanvas_t* vg;
  GLubyte px[4];

  vg = my_vgcanvas_gles2_create(NULL, 64, 64);
  TEST_ASSERT_NOT_NULL(vg);
  if (vg == NULL) {
    return;
  }

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){8, 8, 48, 48});
  my_vgcanvas_end_frame(vg);
  glFinish();

  glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  TEST_ASSERT(px[0] > 200); /* red inside the rect */

  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  TEST_ASSERT(px[0] < 60); /* dark outside */

  /* translucent red over the red-filled area -> blended result */
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgba(255, 0, 0, 128));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){8, 8, 48, 48});
  my_vgcanvas_end_frame(vg);
  glFinish();
  glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  TEST_ASSERT(px[0] > 200 && px[1] > 100 && px[1] < 160); /* ~(255,128,128) */

  /* text via the gles2 backend (bitmap font, alpha texture quads) */
  {
    my_font_t* font = my_font_bitmap_create(NULL);
    int lit = 0, x, y;
    GLubyte row[16 * 4];
    TEST_ASSERT_NOT_NULL(font);
    my_vgcanvas_set_font(vg, font, 8);
    my_vgcanvas_set_fill_color(vg, my_color_rgb(0, 255, 0));
    TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "A", 10, 8), MY_RET_OK);
    glFinish();
    for (y = 8; y < 16 && lit == 0; y++) {
      glReadPixels(10, 64 - 1 - y, 8, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
      for (x = 0; x < 8; x++) {
        if (row[x * 4 + 1] > 100) {
          lit = 1;
        }
      }
    }
    TEST_ASSERT(lit > 0); /* some green glyph pixels rendered */
    my_font_destroy(font);
  }

  /* draw_image: 2x2 four-quadrant image scaled up 16x */
  {
    static const uint8_t quad_img[2 * 2 * 4] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 0, 255};
    my_vgcanvas_begin_frame(vg, NULL);
    TEST_ASSERT_EQ_INT(my_vgcanvas_draw_image(vg, quad_img, 2, 2,
                                              &(my_rectf_t){0, 0, 32, 32},
                                              NULL),
                       MY_RET_OK);
    my_vgcanvas_end_frame(vg);
    glFinish();
    glReadPixels(8, 64 - 1 - 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] > 200 && px[1] < 60); /* top-left: red */
    glReadPixels(24, 64 - 1 - 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[1] > 200 && px[0] < 60); /* top-right: green */
    glReadPixels(8, 64 - 1 - 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[2] > 200); /* bottom-left: blue */
    glReadPixels(24, 64 - 1 - 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] > 200 && px[1] > 200 && px[2] < 60); /* yellow */
  }

  /* round line caps (M10d): pixel beyond the endpoint is lit only with
   * ROUND cap (line (16,32)->(48,32), lw 8, cap radius 4) */
  {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    my_vgcanvas_begin_frame(vg, NULL);
    my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 255, 255));
    my_vgcanvas_set_line_width(vg, 8.0f);
    my_vgcanvas_set_line_cap(vg, MY_LINE_CAP_BUTT);
    my_vgcanvas_begin_path(vg);
    my_vgcanvas_move_to(vg, 16, 32);
    my_vgcanvas_line_to(vg, 48, 32);
    my_vgcanvas_stroke(vg);
    my_vgcanvas_end_frame(vg);
    glFinish();
    glReadPixels(50, 64 - 1 - 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] < 60); /* BUTT: nothing beyond x=48 */

    my_vgcanvas_begin_frame(vg, NULL);
    my_vgcanvas_set_line_cap(vg, MY_LINE_CAP_ROUND);
    my_vgcanvas_begin_path(vg);
    my_vgcanvas_move_to(vg, 16, 32);
    my_vgcanvas_line_to(vg, 48, 32);
    my_vgcanvas_stroke(vg);
    my_vgcanvas_end_frame(vg);
    glFinish();
    glReadPixels(50, 64 - 1 - 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] > 200); /* ROUND: cap extends to x=52 */
  }

  /* cubic bezier (M19a): S-curve stroke — endpoints lit, midpoint off
   * the straight chord lit, far corner dark */
  {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    my_vgcanvas_begin_frame(vg, NULL);
    my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 64, 64));
    my_vgcanvas_set_line_width(vg, 4.0f);
    my_vgcanvas_begin_path(vg);
    my_vgcanvas_move_to(vg, 8, 48);
    my_vgcanvas_curve_to(vg, 24, 0, 40, 64, 56, 16); /* S */
    my_vgcanvas_stroke(vg);
    my_vgcanvas_end_frame(vg);
    glFinish();
    glReadPixels(8, 64 - 1 - 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] > 50); /* start point lit (cap edge, partial) */
    glReadPixels(56, 64 - 1 - 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] > 50); /* end point lit */
    /* off-chord: curve passes (20, 29.5) at t=0.25 while the straight
     * chord sits at y~40 there */
    glReadPixels(20, 64 - 1 - 29, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] > 100);
    glReadPixels(20, 64 - 1 - 41, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] < 60); /* the chord position itself is dark */
    glReadPixels(4, 64 - 1 - 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    TEST_ASSERT(px[0] < 60); /* far corner dark */
    TEST_ASSERT(glGetError() == GL_NO_ERROR);
  }

  /* RTL text through the gles2 backend (M11a): Arabic word shaped +
   * reversed via text_layout; skip when the Noto font is absent */
  {
    my_font_t* ar = my_font_stb_create(
        NULL, "/usr/share/fonts/google-noto-vf/NotoNaskhArabic[wght].ttf", 0);
    if (ar == NULL) {
      fprintf(stdout, "SKIP: no Noto Arabic font\n");
    } else {
      int lit = 0, x, y;
      GLubyte rowpx[64 * 4];
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      my_vgcanvas_begin_frame(vg, NULL);
      my_vgcanvas_set_font(vg, ar, 24);
      my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
      /* محمد */
      TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(
                             vg, "\xD9\x85\xD8\xAD\xD9\x85\xD8\xAF", 4, 4),
                         MY_RET_OK);
      my_vgcanvas_end_frame(vg);
      glFinish();
      for (y = 4; y < 40 && lit == 0; y++) {
        glReadPixels(4, 64 - 1 - y, 60, 1, GL_RGBA, GL_UNSIGNED_BYTE, rowpx);
        for (x = 0; x < 60; x++) {
          if (rowpx[x * 4] > 100) {
            lit = 1;
          }
        }
      }
      TEST_ASSERT(lit > 0); /* shaped glyphs rendered */
      TEST_ASSERT(glGetError() == GL_NO_ERROR);
      my_font_destroy(ar);
    }
  }

  my_vgcanvas_destroy(vg);
}

static void test_gles2_msaa_edge(void) {
  my_vgcanvas_t* vg;
  int x, y, intermediate = 0;
  GLubyte px[4];

  vg = my_vgcanvas_gles2_create(NULL, 64, 64);
  TEST_ASSERT_NOT_NULL(vg);
  if (vg == NULL) {
    return;
  }
  my_vgcanvas_gles2_set_antialias(vg, true);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_stroke_color(vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_set_line_width(vg, 2.0f);
  /* diagonal stroke: quad edges cross pixels at subpixel positions
   * (fill would emit pixel-snapped spans -- useless for MSAA testing) */
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 4.5f, 60.0f);
  my_vgcanvas_line_to(vg, 60.0f, 4.5f);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_end_frame(vg);
  glFinish();
  TEST_ASSERT(glGetError() == GL_NO_ERROR);
  /* scan the diagonal band x+y ~ 64 for partially covered pixels */
  for (y = 4; y < 60; y++) {
    for (x = 4; x < 60; x++) {
      if (x + y >= 60 && x + y <= 68) {
        glReadPixels(x, 64 - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (px[0] > 10 && px[0] < 245) {
          intermediate = 1;
        }
      }
    }
  }
  TEST_ASSERT(intermediate); /* MSAA: edge pixels have coverage values */
  my_vgcanvas_destroy(vg);
}

MYTEST_MAIN_BEGIN()
  {
    int rc = egl_setup(0);
    if (rc != 0) {
      fprintf(stdout, "SKIP: no usable EGL context\n");
    } else {
      MYTEST_RUN(test_gles2_real_render);
      egl_teardown();
    }
  }
  {
    int rc = egl_setup(4);
    if (rc != 0) {
      fprintf(stdout, "SKIP: no MSAA pbuffer config\n");
    } else {
      MYTEST_RUN(test_gles2_msaa_edge);
      egl_teardown();
    }
  }
MYTEST_MAIN_END()
