/**
 * @file my_vgcanvas_gles2_test.c
 * @brief Unit tests for the GLES2 backend with a recording mock GL.
 */
#include "myr/my_vgcanvas_gles2.h"

#include <string.h>

#include "mytest.h"

/* ---------------- recording mock GL ---------------- */

typedef struct mock_gl_t {
  my_gl_t gl;
  int create_program_calls;
  int delete_program_calls;
  int use_program_calls;
  int viewport_calls;
  int scissor_calls;
  int32_t scissor[4];
  int draw_calls;
  int create_texture_calls;
  int create_texture_rgba_calls;
  int delete_texture_calls;
  int textured_draw_calls;
  int32_t last_draw_count;
  float last_uniform_color[4];
  float first_xy[6]; /* first 3 vertices of the LAST draw call */
} mock_gl_t;

static void mock_viewport(void* ctx, int32_t w, int32_t h) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)w;
  (void)h;
  m->viewport_calls++;
}

static void mock_enable_scissor(void* ctx, bool on) {
  (void)ctx;
  (void)on;
}

static void mock_scissor(void* ctx, int32_t x, int32_t y, int32_t w, int32_t h) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  m->scissor_calls++;
  m->scissor[0] = x;
  m->scissor[1] = y;
  m->scissor[2] = w;
  m->scissor[3] = h;
}

static void mock_clear_color(void* ctx, float r, float g, float b, float a) {
  (void)ctx;
  (void)r;
  (void)g;
  (void)b;
  (void)a;
}

static void mock_clear(void* ctx) {
  (void)ctx;
}

static uint32_t mock_create_program(void* ctx, const char* vs, const char* fs) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)vs;
  (void)fs;
  m->create_program_calls++;
  return 1;
}

static void mock_delete_program(void* ctx, uint32_t p) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)p;
  m->delete_program_calls++;
}

static void mock_use_program(void* ctx, uint32_t p) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)p;
  m->use_program_calls++;
}

static void mock_uniform2f(void* ctx, uint32_t p, const char* name, float a,
                           float b) {
  (void)ctx;
  (void)p;
  (void)name;
  (void)a;
  (void)b;
}

static void mock_uniform4f(void* ctx, uint32_t p, const char* name, float r,
                           float g, float b, float a) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)p;
  (void)name;
  m->last_uniform_color[0] = r;
  m->last_uniform_color[1] = g;
  m->last_uniform_color[2] = b;
  m->last_uniform_color[3] = a;
}

static void mock_draw_arrays(void* ctx, uint32_t p, const float* xy,
                             int32_t count) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  int i;
  (void)p;
  m->draw_calls++;
  m->last_draw_count = count;
  for (i = 0; i < 6 && i < count * 2; i++) {
    m->first_xy[i] = xy[i];
  }
}

static uint32_t mock_create_texture(void* ctx, const uint8_t* alpha, int32_t w,
                                    int32_t h) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)alpha;
  (void)w;
  (void)h;
  m->create_texture_calls++;
  return (uint32_t)m->create_texture_calls;
}

static uint32_t mock_create_texture_rgba(void* ctx, const uint8_t* rgba,
                                         int32_t w, int32_t h) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)rgba;
  (void)w;
  (void)h;
  m->create_texture_rgba_calls++;
  return 100u + (uint32_t)m->create_texture_rgba_calls;
}

static void mock_delete_texture(void* ctx, uint32_t tex) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  (void)tex;
  m->delete_texture_calls++;
}

static void mock_draw_textured(void* ctx, uint32_t p, uint32_t tex,
                               const float* xyuv, int32_t count) {
  mock_gl_t* m = (mock_gl_t*)ctx;
  int i;
  (void)p;
  (void)tex;
  m->textured_draw_calls++;
  m->last_draw_count = count;
  for (i = 0; i < 6 && i < count * 4; i++) {
    m->first_xy[i] = xyuv[i];
  }
}

static void mock_gl_init(mock_gl_t* m) {
  memset(m, 0, sizeof(*m));
  m->gl.viewport = mock_viewport;
  m->gl.enable_scissor = mock_enable_scissor;
  m->gl.scissor = mock_scissor;
  m->gl.clear_color = mock_clear_color;
  m->gl.clear = mock_clear;
  m->gl.create_program = mock_create_program;
  m->gl.delete_program = mock_delete_program;
  m->gl.use_program = mock_use_program;
  m->gl.uniform2f = mock_uniform2f;
  m->gl.uniform4f = mock_uniform4f;
  m->gl.draw_arrays_triangles = mock_draw_arrays;
  m->gl.create_texture = mock_create_texture;
  m->gl.create_texture_rgba = mock_create_texture_rgba;
  m->gl.delete_texture = mock_delete_texture;
  m->gl.draw_textured_quads = mock_draw_textured;
  m->gl.ctx = m;
}

/* ---------------- tests ---------------- */

static void test_create_and_fill_rect(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);

  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);
  TEST_ASSERT_NOT_NULL(vg);
  TEST_ASSERT_EQ_INT(gl.create_program_calls, 1);
  TEST_ASSERT_EQ_INT(gl.use_program_calls, 1);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){10, 20, 30, 40});
  my_vgcanvas_end_frame(vg);

  TEST_ASSERT_EQ_INT(gl.viewport_calls, 1);
  TEST_ASSERT_EQ_INT(gl.draw_calls, 1);
  TEST_ASSERT_EQ_INT(gl.last_draw_count, 6); /* 2 triangles */
  TEST_ASSERT(gl.first_xy[0] == 10.0f && gl.first_xy[1] == 20.0f);
  TEST_ASSERT(gl.last_uniform_color[0] == 1.0f);
  TEST_ASSERT(gl.last_uniform_color[1] == 0.0f);

  my_vgcanvas_destroy(vg);
  TEST_ASSERT_EQ_INT(gl.delete_program_calls, 1);
}

static void test_translate_applies(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_translate(vg, 5, 7);
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){10, 20, 30, 40});
  TEST_ASSERT(gl.first_xy[0] == 15.0f && gl.first_xy[1] == 27.0f);

  my_vgcanvas_destroy(vg);
}

static void test_clip_flips_y_for_scissor(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){10, 10, 20, 30});
  /* GL scissor origin is bottom-left: y = 80 - 10 - 30 = 40 */
  TEST_ASSERT_EQ_INT(gl.scissor[0], 10);
  TEST_ASSERT_EQ_INT(gl.scissor[1], 40);
  TEST_ASSERT_EQ_INT(gl.scissor[2], 20);
  TEST_ASSERT_EQ_INT(gl.scissor[3], 30);

  my_vgcanvas_destroy(vg);
}

static void test_stroke_rect_vertex_count(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_line_width(vg, 2);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){10, 10, 40, 40});
  TEST_ASSERT_EQ_INT(gl.last_draw_count, 24); /* 4 rects x 2 tris x 3 verts */

  my_vgcanvas_destroy(vg);
}

static void test_path_triangle_even_odd_spans(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 50, 10);
  my_vgcanvas_line_to(vg, 90, 70);
  my_vgcanvas_line_to(vg, 10, 70);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);

  /* scanline fill: one rect (6 verts) per covered scanline, y=10..69 */
  TEST_ASSERT(gl.draw_calls == 1);
  TEST_ASSERT(gl.last_draw_count > 0 && gl.last_draw_count % 6 == 0);

  my_vgcanvas_destroy(vg);
}

static void test_stroke_polyline_segments(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);

  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 10, 10);
  my_vgcanvas_line_to(vg, 50, 10);
  my_vgcanvas_line_to(vg, 50, 50);
  my_vgcanvas_stroke(vg);
  TEST_ASSERT_EQ_INT(gl.last_draw_count, 12); /* 2 segments x 6 verts */

  my_vgcanvas_destroy(vg);
}

static void test_draw_text_not_supported(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "x", 0, 0),
                     MY_RET_NOT_SUPPORTED);
  my_vgcanvas_destroy(vg);
}

static void test_draw_image_mock(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  static uint8_t img[4 * 4 * 4]; /* 4x4 */
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);
  memset(img, 200, sizeof(img));

  my_vgcanvas_begin_frame(vg, NULL);
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_image(vg, img, 4, 4,
                                            &(my_rectf_t){10, 20, 8, 8}, NULL),
                     MY_RET_OK);
  TEST_ASSERT_EQ_INT(gl.create_texture_rgba_calls, 1);
  TEST_ASSERT_EQ_INT(gl.textured_draw_calls, 1);
  TEST_ASSERT_EQ_INT(gl.last_draw_count, 6);
  TEST_ASSERT(gl.first_xy[0] == 10.0f && gl.first_xy[1] == 20.0f);

  /* second draw of the same bitmap: texture cache hit */
  my_vgcanvas_draw_image(vg, img, 4, 4, &(my_rectf_t){0, 0, 4, 4}, NULL);
  TEST_ASSERT_EQ_INT(gl.create_texture_rgba_calls, 1);
  TEST_ASSERT_EQ_INT(gl.textured_draw_calls, 2);

  /* destroy frees cached textures */
  my_vgcanvas_destroy(vg);
  TEST_ASSERT_EQ_INT(gl.delete_texture_calls, 1);
}

static void test_draw_text_with_font(void) {
  mock_gl_t gl;
  my_vgcanvas_t* vg;
  my_font_t* font;
  mock_gl_init(&gl);
  vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);
  font = my_font_bitmap_create(NULL);

  my_vgcanvas_set_font(vg, font, 8);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_set_fill_color(vg, my_color_rgb(255, 255, 255));
  TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(vg, "Hi", 10, 20), MY_RET_OK);

  /* one texture per unique glyph + one quad draw per glyph */
  TEST_ASSERT_EQ_INT(gl.create_texture_calls, 2);
  TEST_ASSERT_EQ_INT(gl.textured_draw_calls, 2);
  TEST_ASSERT_EQ_INT(gl.last_draw_count, 6);
  /* text program created lazily */
  TEST_ASSERT_EQ_INT(gl.create_program_calls, 2);

  /* measure goes through the current font */
  {
    int32_t w = 0, h = 0;
    TEST_ASSERT_EQ_INT(my_vgcanvas_measure_text(vg, "Hi", &w, &h), MY_RET_OK);
    TEST_ASSERT_EQ_INT(w, 16); /* 2 monospace 8px cells */
  }

  /* destroy releases cached textures and both programs */
  my_vgcanvas_destroy(vg);
  TEST_ASSERT_EQ_INT(gl.delete_texture_calls, 2);
  TEST_ASSERT_EQ_INT(gl.delete_program_calls, 2);
  my_font_destroy(font);
}

static void test_null_params(void) {
  mock_gl_t gl;
  mock_gl_init(&gl);
  TEST_ASSERT_NULL(my_vgcanvas_gles2_create_with_gl(NULL, 0, 80, &gl.gl));
  TEST_ASSERT_NULL(my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, NULL));
  {
    my_vgcanvas_t* vg = my_vgcanvas_gles2_create_with_gl(NULL, 100, 80, &gl.gl);
    TEST_ASSERT_EQ_INT(my_vgcanvas_fill_rect(vg, NULL), MY_RET_INVALID_PARAMS);
    TEST_ASSERT_EQ_INT(my_vgcanvas_restore(vg), MY_RET_FAIL);
    TEST_ASSERT_EQ_INT(my_vgcanvas_gles2_resize(vg, 200, 100), MY_RET_OK);
    TEST_ASSERT_EQ_INT(my_vgcanvas_gles2_resize(vg, 0, 100),
                       MY_RET_INVALID_PARAMS);
    my_vgcanvas_destroy(vg);
  }
}

static void test_no_leak_with_debug_allocator(void) {
  mock_gl_t gl;
  my_allocator_t* dbg = my_allocator_debug_create(NULL);
  my_vgcanvas_t* vg;
  mock_gl_init(&gl);

  vg = my_vgcanvas_gles2_create_with_gl(dbg, 100, 80, &gl.gl);
  my_vgcanvas_begin_frame(vg, NULL);
  my_vgcanvas_save(vg);
  my_vgcanvas_translate(vg, 3, 3);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, 10, 10);
  my_vgcanvas_line_to(vg, 40, 10);
  my_vgcanvas_line_to(vg, 25, 40);
  my_vgcanvas_close_path(vg);
  my_vgcanvas_fill(vg);
  my_vgcanvas_stroke(vg);
  my_vgcanvas_restore(vg);
  my_vgcanvas_fill_rounded_rect(vg, &(my_rectf_t){0, 0, 20, 20}, 5);
  my_vgcanvas_end_frame(vg);
  my_vgcanvas_destroy(vg);

  TEST_ASSERT_EQ_INT(my_allocator_debug_leak_count(dbg), 0);
  my_allocator_debug_destroy(dbg);
}

MYTEST_MAIN_BEGIN()
  MYTEST_RUN(test_create_and_fill_rect);
  MYTEST_RUN(test_translate_applies);
  MYTEST_RUN(test_clip_flips_y_for_scissor);
  MYTEST_RUN(test_stroke_rect_vertex_count);
  MYTEST_RUN(test_path_triangle_even_odd_spans);
  MYTEST_RUN(test_stroke_polyline_segments);
  MYTEST_RUN(test_draw_text_not_supported);
  MYTEST_RUN(test_draw_image_mock);
  MYTEST_RUN(test_draw_text_with_font);
  MYTEST_RUN(test_null_params);
  MYTEST_RUN(test_no_leak_with_debug_allocator);
MYTEST_MAIN_END()
