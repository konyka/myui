/**
 * @file vulkan_smoke_test.c
 * @brief Vulkan backend smoke test (M25b): offscreen (surface-less)
 * render target, rect fill / alpha blend / text / four-quadrant image,
 * host readback pixel assertions. Skips (exit 0) when no Vulkan ICD is
 * available, so machines without a driver still pass.
 */
#include <stdio.h>

#include "myr/my_vgcanvas_vulkan.h"

#include "mytest.h"

#define W 64
#define H 64

static my_vgcanvas_t* g_vg;
static uint8_t g_px[W * H * 4];

/** @brief Read back and return the pixel at (x, y) (top-left origin). */
static const uint8_t* px(int x, int y) {
  my_vgcanvas_vulkan_readback(g_vg, g_px, W, H);
  return &g_px[(y * W + x) * 4];
}

static void test_vulkan_offscreen_render(void) {
  /* rect fill */
  my_vgcanvas_begin_frame(g_vg, NULL);
  my_vgcanvas_set_fill_color(g_vg, my_color_rgb(255, 0, 0));
  my_vgcanvas_fill_rect(g_vg, &(my_rectf_t){8, 8, 48, 48});
  my_vgcanvas_end_frame(g_vg);
  TEST_ASSERT(px(32, 32)[0] > 200); /* red inside */
  TEST_ASSERT(px(32, 32)[1] < 60);
  TEST_ASSERT(px(2, 2)[0] < 60); /* dark outside (clear is black) */

  /* alpha blend over white: ~(255,128,128) */
  my_vgcanvas_begin_frame(g_vg, NULL);
  my_vgcanvas_set_fill_color(g_vg, my_color_rgb(255, 255, 255));
  my_vgcanvas_fill_rect(g_vg, &(my_rectf_t){0, 0, W, H});
  my_vgcanvas_set_fill_color(g_vg, my_color_rgba(255, 0, 0, 128));
  my_vgcanvas_fill_rect(g_vg, &(my_rectf_t){8, 8, 48, 48});
  my_vgcanvas_end_frame(g_vg);
  TEST_ASSERT(px(32, 32)[0] > 200 && px(32, 32)[1] > 100 &&
              px(32, 32)[1] < 160);

  /* text via the backend (bitmap font, R8 glyph texture quads) */
  {
    my_font_t* font = my_font_bitmap_create(NULL);
    int lit = 0, x, y;
    TEST_ASSERT_NOT_NULL(font);
    my_vgcanvas_set_font(g_vg, font, 8);
    my_vgcanvas_begin_frame(g_vg, NULL);
    my_vgcanvas_set_fill_color(g_vg, my_color_rgb(0, 255, 0));
    TEST_ASSERT_EQ_INT(my_vgcanvas_draw_text(g_vg, "A", 10, 8), MY_RET_OK);
    my_vgcanvas_end_frame(g_vg);
    my_vgcanvas_vulkan_readback(g_vg, g_px, W, H);
    for (y = 8; y < 16 && !lit; y++) {
      for (x = 10; x < 26 && !lit; x++) {
        if (g_px[(y * W + x) * 4 + 1] > 100) {
          lit = 1;
        }
      }
    }
    TEST_ASSERT(lit); /* some green glyph pixels rendered */
    my_font_destroy(font);
  }

  /* draw_image: 2x2 four-quadrant image scaled up 16x */
  {
    static const uint8_t quad_img[2 * 2 * 4] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 0, 255};
    my_vgcanvas_begin_frame(g_vg, NULL);
    TEST_ASSERT_EQ_INT(my_vgcanvas_draw_image(g_vg, quad_img, 2, 2,
                                              &(my_rectf_t){0, 0, 32, 32},
                                              NULL),
                       MY_RET_OK);
    my_vgcanvas_end_frame(g_vg);
    TEST_ASSERT(px(8, 8)[0] > 200 && px(8, 8)[1] < 60);   /* TL: red */
    TEST_ASSERT(px(24, 8)[1] > 200 && px(24, 8)[0] < 60); /* TR: green */
    TEST_ASSERT(px(8, 24)[2] > 200);                      /* BL: blue */
    TEST_ASSERT(px(24, 24)[0] > 200 && px(24, 24)[1] > 200 &&
                px(24, 24)[2] < 60); /* BR: yellow */
  }
}

MYTEST_MAIN_BEGIN()
  g_vg = my_vgcanvas_vulkan_create_offscreen(NULL, W, H);
  if (g_vg == NULL) {
    fprintf(stdout, "SKIP: no usable Vulkan device\n");
  } else {
    MYTEST_RUN(test_vulkan_offscreen_render);
    my_vgcanvas_destroy(g_vg);
  }
MYTEST_MAIN_END()
