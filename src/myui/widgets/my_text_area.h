/**
 * @file my_text_area.h
 * @brief Multi-line text editing widget.
 *
 * Model: one UTF-8 byte buffer + a line-start offset cache (darray of
 * byte offsets). Edits rebuild offsets only from the edited line onward
 * (O(bytes after edit)); cursor movement is O(1) amortized on huge
 * documents. Cursor is (row, col) in codepoints; vertical moves keep a
 * goal column. Shift+arrows select, Ctrl+A selects all, Ctrl+C/X/V via
 * the PAL clipboard (newlines preserved). Scrolls to keep the cursor
 * visible. NOT done: word wrap, line numbers, undo/redo (TODO).
 * Emits "changed" (data = full text). No "activate" (Enter splits lines).
 */
#ifndef MY_TEXT_AREA_H
#define MY_TEXT_AREA_H

#include "mypal/my_pal.h"
#include "myui/my_widget.h"

/** @brief Multi-line text area (IS-A widget). */
typedef struct my_text_area_t {
  my_widget_t base;
  const my_allocator_t* allocator;
  char* text;               /**< owned UTF-8 buffer */
  size_t text_len;          /**< bytes in use */
  my_darray_t* line_offsets;/**< size_t per line start (line 0 = 0) */
  size_t cursor_row;
  size_t cursor_col;        /**< codepoints */
  size_t anchor_row;        /**< selection anchor (== cursor = no sel) */
  size_t anchor_col;
  size_t goal_col;          /**< target col for vertical moves */
  int32_t scroll_x;
  int32_t scroll_y;
  struct my_undo_stack_t* undo; /**< user-edit history (M10a) */
  bool applying_history;          /**< suppresses recording during undo/redo */
  size_t max_len;           /**< codepoints cap, 0 = unlimited */
  bool readonly;
  char* hint;               /**< owned, shown when empty and unfocused */
  my_widget_t* scroll_bar;     /**< weak; linked scroll_bar (M9c) */
  bool focused;
  bool cursor_visible;
  uint32_t blink_timer_id;
  my_pal_main_loop_t* blink_loop; /**< weak while active */
  my_font_t* font;          /**< borrowed */
  int32_t font_size;
} my_text_area_t;

my_widget_t* my_text_area_create(const my_allocator_t* allocator);

/** @brief Replace the whole text (cursor to end, no "changed" emit). */
my_ret_t my_text_area_set_text(my_widget_t* area, const char* text);
const char* my_text_area_get_text(my_widget_t* area);
my_ret_t my_text_area_set_hint(my_widget_t* area, const char* hint);
my_ret_t my_text_area_set_readonly(my_widget_t* area, bool readonly);
my_ret_t my_text_area_set_max_len(my_widget_t* area, size_t max_codepoints);
/** @brief Font for layout/measuring (borrowed). */
void my_text_area_set_font(my_widget_t* area, my_font_t* font, int32_t size);

/** @brief Line count (from the offset cache). */
size_t my_text_area_line_count(my_widget_t* area);

/** @brief Link a scroll_bar (weak): synced with scroll_y/content height. */
my_ret_t my_text_area_set_scroll_bar(my_widget_t* area, my_widget_t* bar);

#endif /* MY_TEXT_AREA_H */
