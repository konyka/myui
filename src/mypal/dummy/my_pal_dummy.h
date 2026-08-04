/**
 * @file my_pal_dummy.h
 * @brief Dummy PAL port: headless, deterministic, used by all unit tests.
 *
 * Windows wrap my_lcd_mem (BGRA8888); the main loop is a manual queue
 * (my_pal_main_loop_pump_n) with an injectable clock so timer tests are
 * deterministic. run() processes until starved or quit.
 */
#ifndef MY_PAL_DUMMY_H
#define MY_PAL_DUMMY_H

#include "mypal/my_pal.h"

/** @brief Create the dummy platform. */
my_pal_t* my_pal_dummy_create(const my_allocator_t* allocator);

/** @brief Test hook: set the dummy platform's monotonic clock. */
void my_pal_dummy_set_now_ms(my_pal_t* pal, uint64_t now_ms);

/**
 * @brief Test hook (dummy loops only): dispatch up to n queued events,
 * FIFO. @return number of events dispatched (0 when queue is empty or
 * loop is not a dummy loop).
 */
uint32_t my_pal_main_loop_pump_n(my_pal_main_loop_t* loop, uint32_t n);

#endif /* MY_PAL_DUMMY_H */
