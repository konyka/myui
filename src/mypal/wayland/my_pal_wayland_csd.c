/**
 * @file my_pal_wayland_csd.c
 * @brief Wayland CSD helpers (M16): rounded window corners with AA.
 */
#include "mypal/wayland/my_pal_wayland_csd.h"

#include <stddef.h>

/* 4x4 sub-sample coverage per edge pixel: fully-inside pixels keep their
 * value, fully-outside become (0,0,0,0), edge pixels get all four bytes
 * scaled by coverage — wl_shm is premultiplied-alpha, so scaling RGB with
 * the alpha keeps the data valid (hard binary punches looked jagged). */
#define CSD_SS 4
#define CSD_SS_N (CSD_SS * CSD_SS)

/** @brief Scale one pixel by coverage/16 (premultiplied-alpha safe). */
static void punch(uint8_t* buf, uint32_t stride, uint32_t x, uint32_t y,
                  int32_t coverage) {
  uint8_t* px = buf + (size_t)y * stride + (size_t)x * 4;
  if (coverage <= 0) {
    px[0] = 0;
    px[1] = 0;
    px[2] = 0;
    px[3] = 0;
  } else if (coverage < CSD_SS_N) {
    px[0] = (uint8_t)((int)px[0] * coverage / CSD_SS_N);
    px[1] = (uint8_t)((int)px[1] * coverage / CSD_SS_N);
    px[2] = (uint8_t)((int)px[2] * coverage / CSD_SS_N);
    px[3] = (uint8_t)((int)px[3] * coverage / CSD_SS_N);
  }
}

/** @brief One corner: the arc center in box coords is (r,r)/(0,r)/(r,0)/
 * (0,0) for TL/TR/BL/BR respectively. */
static void mask_corner(uint8_t* buf, uint32_t w, uint32_t h, uint32_t stride,
                        int32_t radius, int corner) {
  int32_t x, y;
  int32_t cx = (corner == 0 || corner == 2) ? radius : 0;
  int32_t cy = (corner == 0 || corner == 1) ? radius : 0;
  for (y = 0; y < radius; y++) {
    for (x = 0; x < radius; x++) {
      int32_t sx, sy, cov = 0;
      uint32_t px, py;
      /* 4x4 sub-sample the pixel against the arc; coordinates are in
       * eighths of a pixel: sub-sample center = 8x + 2sx + 1, arc center
       * = 8*cx, radius = 8r */
      int64_t r8 = 8 * (int64_t)radius;
      int64_t r82 = r8 * r8;
      for (sy = 0; sy < CSD_SS; sy++) {
        for (sx = 0; sx < CSD_SS; sx++) {
          int64_t dx = 8 * (int64_t)x + 2 * sx + 1 - 8 * (int64_t)cx;
          int64_t dy = 8 * (int64_t)y + 2 * sy + 1 - 8 * (int64_t)cy;
          if (dx * dx + dy * dy <= r82) {
            cov++;
          }
        }
      }
      if (cov == CSD_SS_N) {
        continue; /* fully inside: keep */
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
      punch(buf, stride, px, py, cov);
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
