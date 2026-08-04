/**
 * @file my_vgcanvas_gles2.h
 * @brief GLES2 vgcanvas backend (implements the frozen vtable from M1).
 *
 * The caller owns the GL context (PAL window / EGL): create this backend
 * after a context is current, with the framebuffer size. Geometry is
 * triangulated on the CPU (rects -> 2 triangles, rounded corners -> fans,
 * path fill -> even-odd scanline spans batched as triangles, strokes ->
 * segment quads) and submitted as small batches with a single pos+color
 * program. No anti-aliasing, draw_text NOT_SUPPORTED (same as soft).
 */
#ifndef MY_VGCANVAS_GLES2_H
#define MY_VGCANVAS_GLES2_H

#include "myc/my_mem.h"
#include "myr/my_gl.h"
#include "myr/my_vgcanvas.h"

/**
 * @brief Create the backend with an explicit GL table (mock or
 * my_gl_real_default()). NULL allocator = default.
 */
my_vgcanvas_t* my_vgcanvas_gles2_create_with_gl(const my_allocator_t* allocator,
                                                int32_t width, int32_t height,
                                                const my_gl_t* gl);

/**
 * @brief Create the backend on the current real GLES2 context.
 * Returns NULL when built without GLES2 support or shader setup fails.
 */
my_vgcanvas_t* my_vgcanvas_gles2_create(const my_allocator_t* allocator,
                                        int32_t width, int32_t height);

/** @brief Notify a framebuffer resize (updates viewport + scissor math). */
my_ret_t my_vgcanvas_gles2_resize(my_vgcanvas_t* vg, int32_t width,
                                  int32_t height);

#endif /* MY_VGCANVAS_GLES2_H */
