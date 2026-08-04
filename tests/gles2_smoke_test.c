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

static int egl_setup(void) {
  EGLint major, minor, n;
  EGLConfig cfg;
  EGLint cfg_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                        EGL_NONE};
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
  if (!eglChooseConfig(g_dpy, cfg_attrs, &cfg, 1, &n) || n < 1) {
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

  my_vgcanvas_destroy(vg);
}

MYTEST_MAIN_BEGIN()
  if (egl_setup() != 0) {
    fprintf(stdout, "SKIP: no usable EGL context\n");
  } else {
    MYTEST_RUN(test_gles2_real_render);
    egl_teardown();
  }
MYTEST_MAIN_END()
