/**
 * @file my_pal_linux_fb.h
 * @brief Linux framebuffer PAL port (/dev/fb0 + evdev input).
 *
 * Opens the fb device, mmaps it, and wraps it as the window's lcd
 * (my_lcd_mem over the mapped buffer; fill/draw land on screen
 * directly). Input comes from an evdev device node. All syscalls go
 * through my_osal_t so tests can run against a scripted fake device.
 */
#ifndef MY_PAL_LINUX_FB_H
#define MY_PAL_LINUX_FB_H

#include "mypal/my_osal.h"
#include "mypal/my_pal.h"

/**
 * @brief Create the linux_fb platform.
 * @param osal syscall table (NULL = my_osal_default()).
 * @param fb_dev framebuffer node (NULL = "/dev/fb0").
 * @param input_dev evdev node (NULL = "/dev/input/event0").
 * @return the platform, or NULL when the devices cannot be opened.
 */
my_pal_t* my_pal_linux_fb_create(const my_allocator_t* allocator,
                                 const my_osal_t* osal, const char* fb_dev,
                                 const char* input_dev);

#endif /* MY_PAL_LINUX_FB_H */
