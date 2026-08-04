/**
 * @file my_gl.h
 * @brief Minimal GL function table used by the GLES2 vgcanvas backend.
 *
 * All GL calls of the backend go through this table, so tests can inject
 * a recording mock and ports can supply the real implementation
 * (my_gl_real_default() when built with GLES2). The table is a
 * simplified surface, not a 1:1 GL mapping: create_program takes shader
 * sources, draw_arrays_triangles takes interleaved vec2 positions and
 * draws with the currently set uniforms.
 */
#ifndef MY_GL_H
#define MY_GL_H

#include "myc/my_types.h"

/** @brief GL function table (simplified surface). */
typedef struct my_gl_t {
  void (*viewport)(void* ctx, int32_t w, int32_t h);
  void (*enable_scissor)(void* ctx, bool on);
  void (*scissor)(void* ctx, int32_t x, int32_t y, int32_t w, int32_t h);
  void (*clear_color)(void* ctx, float r, float g, float b, float a);
  void (*clear)(void* ctx);
  /** @brief Compile+link a program; 0 on failure. */
  uint32_t (*create_program)(void* ctx, const char* vs_src, const char* fs_src);
  void (*delete_program)(void* ctx, uint32_t program);
  void (*use_program)(void* ctx, uint32_t program);
  /** @brief "u_resolution": pixel size of the render target. */
  void (*uniform2f)(void* ctx, uint32_t program, const char* name, float a,
                    float b);
  /** @brief "u_color": current draw color. */
  void (*uniform4f)(void* ctx, uint32_t program, const char* name, float r,
                    float g, float b, float a);
  /** @brief Draw GL_TRIANGLES with vec2 xy positions (count = vertices). */
  void (*draw_arrays_triangles)(void* ctx, uint32_t program, const float* xy,
                                int32_t count);
  void* ctx;
} my_gl_t;

/**
 * @brief The real GLES2 implementation of the table (uses the CURRENT GL
 * context; only available when built with MYUI_HAS_GLES2, otherwise
 * returns NULL).
 */
const my_gl_t* my_gl_real_default(void);

#endif /* MY_GL_H */
