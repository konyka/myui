/**
 * @file bench_gpu.c
 * @brief GPU backend comparison benchmark (M25c): the same scene as
 * bench_render's "50 buttons full-frame repaint" driven through the
 * soft, gles2, desktop OpenGL and Vulkan backends, 100 frames each,
 * average wall-clock ms/frame (begin_frame -> paint -> end_frame +
 * backend sync: glFinish for GL, the offscreen fence wait for Vulkan).
 *
 * Offscreen everywhere: gles2/opengl via EGL surfaceless/pbuffer
 * contexts (same setup as the smoke tests), Vulkan via the offscreen
 * image mode. A backend that cannot be initialized prints
 * "backend X: skipped" and the run continues (exit 0).
 *
 * Registered in ctest with label "bench".
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime under -std=c99 */
#include <stdio.h>
#include <time.h>

#include "mypal/dummy/my_pal_dummy.h"
#include "myr/my_gl_desktop.h"
#include "myr/my_vgcanvas_gles2.h"
#include "myr/my_vgcanvas_soft.h"
#include "myr/my_vgcanvas_vulkan.h"
#include "myui/widgets/my_button.h"

#ifdef BENCH_GPU_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#endif

#define BENCH_BUTTONS 50
#define BENCH_FRAMES 100
#define BENCH_W 800
#define BENCH_H 480

/** @brief Wall clock (GPU sync points may sleep the CPU, so clock()
 * would underreport; bench_render uses clock() because soft is pure
 * CPU work). */
static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/** @brief The shared scene: 50 buttons on a plain container (no theme,
 * same style fallbacks as bench_render's window). */
static my_widget_t* make_scene(void) {
  my_widget_t* root = my_widget_create(NULL, "bench_root");
  int i;
  my_widget_set_rect(root, &(my_rect_t){0, 0, BENCH_W, BENCH_H});
  for (i = 0; i < BENCH_BUTTONS; i++) {
    my_widget_t* b = my_button_create(NULL, "bench");
    my_widget_set_rect(b, &(my_rect_t){(i % 10) * 78, (i / 10) * 90, 72, 80});
    my_widget_add_child(root, b);
    my_widget_unref(b);
  }
  return root;
}

/**
 * @brief Run BENCH_FRAMES frames of the scene on `vg`; returns the
 * average ms/frame. `sync` is an optional backend sync hook (glFinish).
 */
static double bench_frames(my_widget_t* scene, my_vgcanvas_t* vg,
                           void (*sync)(void)) {
  double t0, t1;
  int f;
  /* warm up */
  my_vgcanvas_begin_frame(vg, NULL);
  my_widget_paint(scene, vg);
  my_vgcanvas_end_frame(vg);
  if (sync != NULL) {
    sync();
  }
  t0 = now_ms();
  for (f = 0; f < BENCH_FRAMES; f++) {
    my_vgcanvas_begin_frame(vg, NULL);
    my_widget_paint(scene, vg);
    my_vgcanvas_end_frame(vg);
    if (sync != NULL) {
      sync();
    }
  }
  t1 = now_ms();
  return (t1 - t0) / BENCH_FRAMES;
}

/* ---------------- soft (reference path, same as bench_render) ---------------- */

static void bench_soft(my_widget_t* scene) {
  my_pal_t* pal = my_pal_dummy_create(NULL);
  my_pal_window_t* pw = my_pal_window_create(pal, BENCH_W, BENCH_H, "bench");
  my_vgcanvas_t* vg;
  double avg;
  if (pw == NULL) {
    printf("backend soft: skipped (no dummy window)\n");
    my_pal_destroy(pal);
    return;
  }
  vg = my_vgcanvas_soft_create(NULL, my_pal_window_get_lcd(pw));
  if (vg == NULL) {
    printf("backend soft: skipped (no soft vgcanvas)\n");
  } else {
    avg = bench_frames(scene, vg, NULL);
    printf("backend soft:   avg %.3f ms/frame (%d buttons x %d frames)\n",
           avg, BENCH_BUTTONS, BENCH_FRAMES);
    my_vgcanvas_destroy(vg);
  }
  my_pal_window_destroy(pw);
  my_pal_destroy(pal);
}

/* ---------------- EGL offscreen contexts (gles2 + desktop GL) ---------------- */

#ifdef BENCH_GPU_EGL

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

typedef struct egl_ctx_t {
  EGLDisplay dpy;
  EGLContext ctx;
  EGLSurface surf;
} egl_ctx_t;

/** @brief api: MY_PAL_GL_API_GLES2 / MY_PAL_GL_API_OPENGL. */
static int egl_setup(egl_ctx_t* e, int api) {
  EGLint major, minor, n;
  EGLConfig cfg;
  EGLint renderable = api == MY_PAL_GL_API_OPENGL ? EGL_OPENGL_BIT
                                                  : EGL_OPENGL_ES2_BIT;
  EGLint cfg_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                        EGL_RENDERABLE_TYPE, renderable,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                        EGL_NONE};
  EGLint surf_attrs[] = {EGL_WIDTH, BENCH_W, EGL_HEIGHT, BENCH_H, EGL_NONE};
  EGLint ctx_es_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLint ctx_gl_attrs[] = {EGL_NONE};

  e->dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                 EGL_DEFAULT_DISPLAY, NULL);
  if (e->dpy == EGL_NO_DISPLAY) {
    e->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  }
  if (e->dpy == EGL_NO_DISPLAY || !eglInitialize(e->dpy, &major, &minor)) {
    return -1;
  }
  if (!eglBindAPI(api == MY_PAL_GL_API_OPENGL ? EGL_OPENGL_API
                                              : EGL_OPENGL_ES_API)) {
    return -1;
  }
  if (!eglChooseConfig(e->dpy, cfg_attrs, &cfg, 1, &n) || n < 1) {
    return -1;
  }
  e->ctx = eglCreateContext(e->dpy, cfg, EGL_NO_CONTEXT,
                            api == MY_PAL_GL_API_OPENGL ? ctx_gl_attrs
                                                        : ctx_es_attrs);
  if (e->ctx == EGL_NO_CONTEXT) {
    return -1;
  }
  e->surf = eglCreatePbufferSurface(e->dpy, cfg, surf_attrs);
  if (e->surf == EGL_NO_SURFACE) {
    if (!eglMakeCurrent(e->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, e->ctx)) {
      return -1;
    }
    return 0;
  }
  if (!eglMakeCurrent(e->dpy, e->surf, e->surf, e->ctx)) {
    return -1;
  }
  return 0;
}

static void egl_teardown(egl_ctx_t* e) {
  if (e->dpy != EGL_NO_DISPLAY) {
    eglMakeCurrent(e->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (e->surf != EGL_NO_SURFACE) {
      eglDestroySurface(e->dpy, e->surf);
    }
    if (e->ctx != EGL_NO_CONTEXT) {
      eglDestroyContext(e->dpy, e->ctx);
    }
    eglTerminate(e->dpy);
  }
}

static void gl_sync(void) {
  glFinish();
}

static void bench_gles2(my_widget_t* scene) {
  egl_ctx_t e = {EGL_NO_DISPLAY, EGL_NO_CONTEXT, EGL_NO_SURFACE};
  my_vgcanvas_t* vg;
  double avg;
  if (egl_setup(&e, MY_PAL_GL_API_GLES2) != 0) {
    printf("backend gles2: skipped (no ES2 EGL context)\n");
    return;
  }
  vg = my_vgcanvas_gles2_create(NULL, BENCH_W, BENCH_H);
  if (vg == NULL) {
    printf("backend gles2: skipped (backend init failed)\n");
  } else {
    avg = bench_frames(scene, vg, gl_sync);
    printf("backend gles2:  avg %.3f ms/frame (%d buttons x %d frames)\n",
           avg, BENCH_BUTTONS, BENCH_FRAMES);
    my_vgcanvas_destroy(vg);
  }
  egl_teardown(&e);
}

static void bench_opengl(my_widget_t* scene) {
  egl_ctx_t e = {EGL_NO_DISPLAY, EGL_NO_CONTEXT, EGL_NO_SURFACE};
  my_vgcanvas_t* vg;
  double avg;
  if (egl_setup(&e, MY_PAL_GL_API_OPENGL) != 0) {
    printf("backend opengl: skipped (no desktop-GL EGL context)\n");
    return;
  }
  vg = my_vgcanvas_gles2_create_with_gl(NULL, BENCH_W, BENCH_H,
                                        my_gl_desktop_default());
  if (vg == NULL) {
    printf("backend opengl: skipped (backend init failed)\n");
  } else {
    avg = bench_frames(scene, vg, gl_sync);
    printf("backend opengl: avg %.3f ms/frame (%d buttons x %d frames)\n",
           avg, BENCH_BUTTONS, BENCH_FRAMES);
    my_vgcanvas_destroy(vg);
  }
  egl_teardown(&e);
}

#else /* !BENCH_GPU_EGL */

static void bench_gles2(my_widget_t* scene) {
  (void)scene;
  printf("backend gles2: skipped (built without EGL)\n");
}

static void bench_opengl(my_widget_t* scene) {
  (void)scene;
  printf("backend opengl: skipped (built without EGL)\n");
}

#endif /* BENCH_GPU_EGL */

/* ---------------- vulkan (offscreen image mode) ---------------- */

static void bench_vulkan(my_widget_t* scene) {
  my_vgcanvas_t* vg = my_vgcanvas_vulkan_create_offscreen(NULL, BENCH_W,
                                                          BENCH_H);
  double avg;
  if (vg == NULL) {
    printf("backend vulkan: skipped (no usable Vulkan device)\n");
    return;
  }
  /* offscreen end_frame already submits + waits the frame fence: that is
   * the synchronization point (equivalent to glFinish) */
  avg = bench_frames(scene, vg, NULL);
  printf("backend vulkan: avg %.3f ms/frame (%d buttons x %d frames)\n",
         avg, BENCH_BUTTONS, BENCH_FRAMES);
  my_vgcanvas_destroy(vg);
}

int main(void) {
  my_widget_t* scene = make_scene();
  printf("bench_gpu: %d buttons x %d frames, wall-clock avg per backend\n",
         BENCH_BUTTONS, BENCH_FRAMES);
  bench_soft(scene);
  bench_gles2(scene);
  bench_opengl(scene);
  bench_vulkan(scene);
  my_widget_unref(scene);
  return 0;
}
