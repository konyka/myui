/**
 * @file my_layout.h
 * @brief Layouters: arrange a widget's children.
 *
 * A layouter is attached with my_widget_set_layouter() and runs on
 * my_widget_relayout(). Children declare their sizing via layout_params
 * (my_widget_set_layout_params), syntax per axis:
 *   "w:100"   fixed pixels
 *   "w:50%"   percent of the parent's content size
 *   "w:1f"    flex: share of the remaining space, weighted
 * Missing axis = MY_LAYOUT_AUTO (main axis: keep current size; cross
 * axis: fill the parent's content size).
 * Grid layout: TODO (M3b+).
 */
#ifndef MY_LAYOUT_H
#define MY_LAYOUT_H

#include "myui/my_widget.h"

/**
 * @brief Parse a layout-params string ("w:50% h:1f"). NULL/empty str =
 * both axes AUTO. Returns MY_RET_INVALID_PARAMS on garbage.
 */
my_ret_t my_layout_params_parse(const char* str, my_layout_params_t* out);

/** @brief Set a child's layout params from a string (see syntax above). */
my_ret_t my_widget_set_layout_params(my_widget_t* widget, const char* params);

/** @brief Layouter interface (single-function vtable + destroy). */
typedef struct my_layouter_t {
  /** @brief Arrange parent's direct children (set their rects). */
  void (*layout)(struct my_layouter_t* self, my_widget_t* parent);
  void (*destroy)(struct my_layouter_t* self);
} my_layouter_t;

/**
 * @brief The default layouter: does nothing (absolute positioning).
 * Shared singleton, do NOT destroy.
 */
my_layouter_t* my_layouter_default(void);

/** @brief Linear layouter (NULL allocator = default). */
my_layouter_t* my_layouter_linear_create(const my_allocator_t* allocator,
                                         bool horizontal, int32_t spacing);

/** @brief Attach a layouter (takes ownership; NULL resets to absolute). */
my_ret_t my_widget_set_layouter(my_widget_t* widget, my_layouter_t* layouter);

/** @brief Run this widget's layouter, then recurse into children. */
void my_widget_relayout(my_widget_t* widget);

#endif /* MY_LAYOUT_H */
