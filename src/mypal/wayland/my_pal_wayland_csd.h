/**
 * @file my_pal_wayland_csd.h
 * @brief Wayland CSD helpers (M16): rounded window corners.
 */
#ifndef MY_PAL_WAYLAND_CSD_H
#define MY_PAL_WAYLAND_CSD_H

#include <stdint.h>

/**
 * @brief Punch rounded corners into a BGRA8888 buffer: pixels outside
 * the corner arc get alpha=0 (all four corners, radius in pixels,
 * integer math on pixel centers). mutter gives plain xdg-shell clients
 * no SSD, so our CSD windows round their own corners GNOME-style.
 */
void myui_wl_corner_mask(uint8_t* buf, uint32_t w, uint32_t h,
                         uint32_t stride, int32_t radius);

#endif /* MY_PAL_WAYLAND_CSD_H */
