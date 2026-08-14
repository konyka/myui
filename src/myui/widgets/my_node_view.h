/**
 * @file my_node_view.h
 * @brief Node editor canvas (M19b): owns the node widgets and the link
 * model; draws bg -> bezier links -> nodes; drag-from-socket preview
 * connect, click-select + Del delete, drag-empty-space pan.
 *
 * Link semantics (Blender): an input slot accepts ONE link — connecting
 * replaces the previous one; dragging out of a connected input picks
 * the link up (disconnect + preview). Links change -> "changed" event
 * on the view. All coordinates are canvas (view-local); panning moves
 * node rects so hit testing stays exact.
 *
 * Theming (CSS): `node_view { background-color }` (canvas),
 * `node_link { color }` (link, `color` aliased to fg_color),
 * `node_link.selected`, `node_link.preview`.
 */
#ifndef MY_NODE_VIEW_H
#define MY_NODE_VIEW_H

#include "myui/widgets/my_node.h"

/** @brief Create the canvas widget (focusable for Del). */
my_widget_t* my_node_view_create(const my_allocator_t* allocator);

/** @brief Add a node at (x, y) with size w x h (canvas coords).
 * @return the node widget (tree-owned after this call). */
my_widget_t* my_node_view_add_node(my_widget_t* view, const char* id,
                                   const char* title, const char* category,
                                   int32_t x, int32_t y, int32_t w,
                                   int32_t h);

/** @brief Connect out(out_node, out_slot) -> in(in_node, in_slot); a
 * link already feeding that input is replaced (Blender semantics).
 * Emits "changed". */
my_ret_t my_node_view_connect(my_widget_t* view, my_widget_t* out_node,
                              size_t out_slot, my_widget_t* in_node,
                              size_t in_slot);

/** @brief Remove the link feeding in(in_node, in_slot) (NOT_FOUND when
 * none). Emits "changed". */
my_ret_t my_node_view_disconnect_in(my_widget_t* view, my_widget_t* in_node,
                                    size_t in_slot);

size_t my_node_view_link_count(const my_widget_t* view);

/** @brief Read link i's endpoints (any out may be NULL individually). */
bool my_node_view_get_link(const my_widget_t* view, size_t index,
                           my_widget_t** out_node, size_t* out_slot,
                           my_widget_t** in_node, size_t* in_slot);

/** @brief Link index whose bezier passes within 4px of (x, y) (canvas
 * coords), -1 = none. Later links win on overlap. */
int32_t my_node_view_find_link_at(my_widget_t* view, int32_t x, int32_t y);

/** @brief Currently selected link index (-1 = none). */
int32_t my_node_view_get_selected(const my_widget_t* view);

/** @brief Pan the canvas by (dx, dy) (moves node rects). */
void my_node_view_pan_by(my_widget_t* view, int32_t dx, int32_t dy);

#endif /* MY_NODE_VIEW_H */
