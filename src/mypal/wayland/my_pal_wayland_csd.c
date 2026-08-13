/**
 * @file my_pal_wayland_csd.c
 * @brief Wayland CSD helpers (M16): rounded window corners.
 */
#include "mypal/wayland/my_pal_wayland_csd.h"

#include <stddef.h>

/** @brief Zero the alpha of one pixel (BGRA: alpha at byte 3). */
static void punch(uint8_t* buf, uint32_t stride, uint32_t x, uint32_t y) {
  buf[(size_t)y * stride + (size_t)x * 4 + 3] = 0;
}

/** @brief One corner: the arc center in box coords is (r,r)/(0,r)/(r,0)/
 * (0,0) for TL/TR/BL/BR respectively. */
static void mask_corner(uint8_t* buf, uint32_t w, uint32_t h, uint32_t stride,
                        int32_t radius, int corner) {
  int32_t x, y;
  int64_t r2 = (int64_t)4 * radius * radius;
  int32_t cx = (corner == 0 || corner == 2) ? radius : 0;
  int32_t cy = (corner == 0 || corner == 1) ? radius : 0;
  for (y = 0; y < radius; y++) {
    for (x = 0; x < radius; x++) {
      /* pixel-center distance to the arc center, doubled (integers) */
      int64_t dx = 2 * (int64_t)x + 1 - 2 * (int64_t)cx;
      int64_t dy = 2 * (int64_t)y + 1 - 2 * (int64_t)cy;
      uint32_t px, py;
      if (dx * dx + dy * dy <= r2) {
        continue; /* inside the arc: keep */
      }
      switch (corner) {
        case 0:
          px = (uint32_t)x;
          py = (uint32_t)y;
          break;
        case 1:
          px = w - (uint32_t)radius + (uint32_t)x;
          py = (uint32_t)y;
          break;
        case 2:
          px = (uint32_t)x;
          py = h - (uint32_t)radius + (uint32_t)y;
          break;
        default:
          px = w - (uint32_t)radius + (uint32_t)x;
          py = h - (uint32_t)radius + (uint32_t)y;
          break;
      }
      punch(buf, stride, px, py);
    }
  }
}

void myui_wl_corner_mask(uint8_t* buf, uint32_t w, uint32_t h,
                         uint32_t stride, int32_t radius) {
  int c;
  if (buf == NULL || radius <= 0 || (uint32_t)(2 * radius) > w ||
      (uint32_t)(2 * radius) > h) {
    return;
  }
  for (c = 0; c < 4; c++) {
    mask_corner(buf, w, h, stride, radius, c);
  }
}
