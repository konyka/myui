/**
 * @file my_vgcanvas_soft.h
 * @brief Software rasterizer vgcanvas backend, draws onto an my_lcd_t.
 *
 * No anti-aliasing, no alpha blending (colors replace pixels). Path fill
 * uses the even-odd rule; strokes are Bresenham lines with a square
 * line_width brush. The backend records the device-space bounding box of
 * every draw call into a dirty-rect set for the frame (partial redraw).
 */
#ifndef MY_VGCANVAS_SOFT_H
#define MY_VGCANVAS_SOFT_H

#include "myc/my_mem.h"
#include "myr/my_dirty_rects.h"
#include "myr/my_lcd.h"
#include "myr/my_vgcanvas.h"

/**
 * @brief Create a software vgcanvas drawing on lcd (borrowed, NOT owned;
 * the caller destroys the lcd). NULL allocator = default.
 */
my_vgcanvas_t* my_vgcanvas_soft_create(const my_allocator_t* allocator,
                                       my_lcd_t* lcd);

/**
 * @brief Dirty rects accumulated during the current frame (between
 * begin_frame/end_frame). NULL if vg is not a soft backend.
 */
const my_dirty_rects_t* my_vgcanvas_soft_get_dirty_rects(my_vgcanvas_t* vg);

/** @brief Access the soft backend's anti-alias flag (default on). */
void my_vgcanvas_soft_set_antialias(my_vgcanvas_t* vg, bool enabled);

/**
 * @brief AA level: 0 = off, 1 = x-only (4x subsampling), 2 = x4 * y2
 * (default 2). set_antialias(false) == level 0, true == level 2.
 */
void my_vgcanvas_soft_set_antialias_level(my_vgcanvas_t* vg, int level);

#endif /* MY_VGCANVAS_SOFT_H */
