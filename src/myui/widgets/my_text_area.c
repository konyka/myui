/**
 * @file my_text_area.c
 * @brief Multi-line text editing widget.
 */
#include "myui/widgets/my_text_area.h"

#include <string.h>

#include "myc/my_str.h"
#include "myui/my_window.h"

#define TA_PAD_X 4
#define TA_PAD_Y 3
#define TA_CELL_W 8 /* fallback cell width without a font */

/* ---------------- line offset cache ---------------- */

static size_t ta_line_count(const my_text_area_t* ta) {
  return my_darray_size(ta->line_offsets);
}

static size_t ta_line_start(const my_text_area_t* ta, size_t row) {
  return (size_t)my_darray_get(ta->line_offsets, row);
}

static void ta_offsets_push(my_text_area_t* ta, size_t offset) {
  my_darray_push(ta->line_offsets, (void*)offset);
}

/** @brief Rebuild line offsets from `row` to the end of the buffer. */
static void ta_rebuild_from(my_text_area_t* ta, size_t row) {
  size_t pos, line;
  if (row == 0) {
    my_darray_clear(ta->line_offsets);
    ta_offsets_push(ta, 0);
    row = 0;
  }
  line = row;
  pos = ta_line_start(ta, line);
  /* drop stale entries beyond row */
  while (ta_line_count(ta) > row + 1) {
    my_darray_remove_at(ta->line_offsets, ta_line_count(ta) - 1);
  }
  while (pos < ta->text_len) {
    if (ta->text[pos] == '\n') {
      ta_offsets_push(ta, pos + 1);
      line++;
    }
    pos++;
  }
}

static size_t ta_offset_of(const my_text_area_t* ta, size_t row, size_t col) {
  size_t start, end, off, c = 0;
  if (row >= ta_line_count(ta)) {
    return ta->text_len;
  }
  start = ta_line_start(ta, row);
  end = row + 1 < ta_line_count(ta) ? ta_line_start(ta, row + 1)
                                    : ta->text_len;
  off = start;
  while (off < end && c < col && ta->text[off] != '\n') {
    off += my_str_utf8_char_len(ta->text + off);
    c++;
  }
  return off;
}

static void ta_pos_of(const my_text_area_t* ta, size_t offset, size_t* row,
                      size_t* col) {
  size_t r = 0, start, c = 0, off;
  /* binary search the line */
  size_t lo = 0, hi = ta_line_count(ta);
  while (lo + 1 < hi) {
    size_t mid = (lo + hi) / 2;
    if (ta_line_start(ta, mid) <= offset) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  r = lo;
  start = ta_line_start(ta, r);
  off = start;
  while (off < offset && ta->text[off] != '\n') {
    off += my_str_utf8_char_len(ta->text + off);
    c++;
  }
  *row = r;
  *col = c;
}

static size_t ta_line_cp_len(const my_text_area_t* ta, size_t row) {
  size_t start, end, len = 0;
  if (row >= ta_line_count(ta)) {
    return 0;
  }
  start = ta_line_start(ta, row);
  end = row + 1 < ta_line_count(ta) ? ta_line_start(ta, row + 1)
                                    : ta->text_len;
  while (start < end && ta->text[start] != '\n') {
    start += my_str_utf8_char_len(ta->text + start);
    len++;
  }
  return len;
}

/* ---------------- buffer ops ---------------- */

static void ta_cursor_to_offset(my_text_area_t* ta, size_t offset) {
  ta_pos_of(ta, offset, &ta->cursor_row, &ta->cursor_col);
  ta->anchor_row = ta->cursor_row;
  ta->anchor_col = ta->cursor_col;
  ta->goal_col = ta->cursor_col;
}

static my_ret_t ta_reserve(my_text_area_t* ta, size_t extra) {
  /* simple: realloc to exact need (capacity doubling is a TODO; edits
   * are user-typing-rate so this is not hot) */
  size_t need = ta->text_len + extra + 1;
  char* p = (char*)my_mem_realloc(ta->allocator, ta->text, need);
  if (p == NULL) {
    return MY_RET_OOM;
  }
  ta->text = p;
  return MY_RET_OK;
}

static size_t ta_total_cps(const my_text_area_t* ta) {
  size_t n = 0, off = 0;
  while (off < ta->text_len) {
    off += my_str_utf8_char_len(ta->text + off);
    n++;
  }
  return n;
}

static bool ta_sel(const my_text_area_t* ta, size_t* r0, size_t* c0,
                   size_t* r1, size_t* c1) {
  bool fwd;
  if (ta->cursor_row == ta->anchor_row && ta->cursor_col == ta->anchor_col) {
    return false;
  }
  fwd = ta->cursor_row < ta->anchor_row ||
        (ta->cursor_row == ta->anchor_row && ta->cursor_col < ta->anchor_col);
  if (fwd) {
    *r0 = ta->cursor_row;
    *c0 = ta->cursor_col;
    *r1 = ta->anchor_row;
    *c1 = ta->anchor_col;
  } else {
    *r0 = ta->anchor_row;
    *c0 = ta->anchor_col;
    *r1 = ta->cursor_row;
    *c1 = ta->cursor_col;
  }
  return true;
}

static void emit_changed(my_text_area_t* ta) {
  my_emitter_emit(((my_widget_t*)ta)->emitter, "changed",
                  ta->text != NULL ? ta->text : "");
}

static void ta_insert_bytes(my_text_area_t* ta, size_t offset,
                            const char* bytes, size_t n) {
  if (ta_reserve(ta, n) != MY_RET_OK) {
    return;
  }
  memmove(ta->text + offset + n, ta->text + offset, ta->text_len - offset + 1);
  memcpy(ta->text + offset, bytes, n);
  ta->text_len += n;
  {
    size_t row, col;
    ta_pos_of(ta, offset, &row, &col);
    ta_rebuild_from(ta, row);
  }
}

static void ta_delete_bytes(my_text_area_t* ta, size_t start, size_t end) {
  if (start >= end || end > ta->text_len) {
    return;
  }
  memmove(ta->text + start, ta->text + end, ta->text_len - end + 1);
  ta->text_len -= end - start;
  {
    size_t row, col;
    ta_pos_of(ta, start, &row, &col);
    ta_rebuild_from(ta, row);
  }
}

static void user_insert(my_text_area_t* ta, const char* bytes, size_t n,
                        size_t cp_count) {
  size_t r0, c0, r1, c1, start;
  if (ta->readonly) {
    return;
  }
  if (ta_sel(ta, &r0, &c0, &r1, &c1)) {
    size_t s0 = ta_offset_of(ta, r0, c0);
    size_t s1 = ta_offset_of(ta, r1, c1);
    ta_delete_bytes(ta, s0, s1);
    ta_cursor_to_offset(ta, s0);
    emit_changed(ta);
  }
  if (ta->max_len > 0 && ta_total_cps(ta) + cp_count > ta->max_len) {
    return;
  }
  start = ta_offset_of(ta, ta->cursor_row, ta->cursor_col);
  ta_insert_bytes(ta, start, bytes, n);
  ta_cursor_to_offset(ta, start + n);
  emit_changed(ta);
  my_widget_invalidate((my_widget_t*)ta, NULL);
}

static void user_delete_range(my_text_area_t* ta, size_t start, size_t end) {
  if (ta->readonly) {
    return;
  }
  ta_delete_bytes(ta, start, end);
  ta_cursor_to_offset(ta, start);
  emit_changed(ta);
  my_widget_invalidate((my_widget_t*)ta, NULL);
}

/* ---------------- scrolling ---------------- */

static void ta_ensure_visible(my_text_area_t* ta) {
  my_widget_t* w = (my_widget_t*)ta;
  int32_t line_h = ta->font != NULL
                       ? my_font_line_height(ta->font, ta->font_size)
                       : ta->font_size;
  int32_t inner_h, inner_w, cy;
  if (line_h <= 0) {
    line_h = ta->font_size > 0 ? ta->font_size : 16;
  }
  inner_h = w->rect.h - 2 * TA_PAD_Y;
  inner_w = w->rect.w - 2 * TA_PAD_X;
  if (inner_h > 0) {
    int32_t top = (int32_t)ta->cursor_row * line_h;
    if (top - ta->scroll_y < 0) {
      ta->scroll_y = top;
    }
    if (top + line_h - ta->scroll_y > inner_h) {
      ta->scroll_y = top + line_h - inner_h;
    }
  }
  if (inner_w > 0) {
    cy = (int32_t)ta->cursor_col * TA_CELL_W;
    if (cy - ta->scroll_x < 0) {
      ta->scroll_x = cy;
    }
    if (cy - ta->scroll_x > inner_w) {
      ta->scroll_x = cy - inner_w;
    }
  }
}

/* ---------------- key handling ---------------- */

static void ta_move_to(my_text_area_t* ta, size_t row, size_t col,
                       bool select) {
  size_t lines = ta_line_count(ta);
  if (row >= lines) {
    row = lines > 0 ? lines - 1 : 0;
  }
  {
    size_t max_col = ta_line_cp_len(ta, row);
    if (col > max_col) {
      col = max_col;
    }
  }
  ta->cursor_row = row;
  ta->cursor_col = col;
  if (!select) {
    ta->anchor_row = row;
    ta->anchor_col = col;
    /* goal_col is intentionally NOT updated here: vertical moves keep it,
     * horizontal moves set it at the call site */
  }
  ta_ensure_visible(ta);
  my_widget_invalidate((my_widget_t*)ta, NULL);
}

static my_ret_t ta_on_key(my_text_area_t* ta, const my_event_t* event) {
  uint32_t key = event->u.key.key;
  uint8_t mods = event->u.key.modifiers;
  bool shift = (mods & MY_KEYMOD_SHIFT) != 0;
  bool ctrl = (mods & MY_KEYMOD_CTRL) != 0;

  if (ctrl && (key == 'a' || key == 'A')) {
    ta->anchor_row = 0;
    ta->anchor_col = 0;
    ta->cursor_row = ta_line_count(ta) - 1;
    ta->cursor_col = ta_line_cp_len(ta, ta->cursor_row);
    my_widget_invalidate((my_widget_t*)ta, NULL);
    return MY_RET_OK;
  }
  if (ctrl && (key == 'c' || key == 'C' || key == 'x' || key == 'X')) {
    my_pal_t* pal = my_window_pal_of_widget((my_widget_t*)ta);
    size_t r0, c0, r1, c1;
    if (pal != NULL && ta_sel(ta, &r0, &c0, &r1, &c1)) {
      size_t s0 = ta_offset_of(ta, r0, c0);
      size_t s1 = ta_offset_of(ta, r1, c1);
      char* buf = (char*)my_mem_alloc(ta->allocator, s1 - s0 + 1);
      if (buf != NULL) {
        memcpy(buf, ta->text + s0, s1 - s0);
        buf[s1 - s0] = '\0';
        my_pal_clipboard_set_text(pal, buf);
        my_mem_free(ta->allocator, buf);
      }
      if (key == 'x' || key == 'X') {
        user_delete_range(ta, s0, s1);
      }
    }
    return MY_RET_OK;
  }
  if (ctrl && (key == 'v' || key == 'V')) {
    my_pal_t* pal = my_window_pal_of_widget((my_widget_t*)ta);
    if (pal != NULL && !ta->readonly) {
      char buf[4096];
      if (my_pal_clipboard_get_text(pal, buf, sizeof(buf)) == MY_RET_OK) {
        const char* q = buf;
        while (*q != '\0') {
          size_t n = my_str_utf8_char_len(q);
          user_insert(ta, q, n, 1); /* newlines preserved */
          q += n;
        }
      }
    }
    return MY_RET_OK;
  }

  switch (key) {
    case MY_KEY_LEFT:
      if (ta->cursor_col > 0) {
        ta_move_to(ta, ta->cursor_row, ta->cursor_col - 1, shift);
      } else if (ta->cursor_row > 0) {
        ta_move_to(ta, ta->cursor_row - 1,
                   ta_line_cp_len(ta, ta->cursor_row - 1), shift);
      }
      if (!shift) {
        ta->goal_col = ta->cursor_col;
      }
      return MY_RET_OK;
    case MY_KEY_RIGHT:
      if (ta->cursor_col < ta_line_cp_len(ta, ta->cursor_row)) {
        ta_move_to(ta, ta->cursor_row, ta->cursor_col + 1, shift);
      } else if (ta->cursor_row + 1 < ta_line_count(ta)) {
        ta_move_to(ta, ta->cursor_row + 1, 0, shift);
      }
      if (!shift) {
        ta->goal_col = ta->cursor_col;
      }
      return MY_RET_OK;
    case MY_KEY_UP:
      if (ta->cursor_row > 0) {
        ta_move_to(ta, ta->cursor_row - 1, ta->goal_col, shift);
      }
      return MY_RET_OK;
    case MY_KEY_DOWN:
      if (ta->cursor_row + 1 < ta_line_count(ta)) {
        ta_move_to(ta, ta->cursor_row + 1, ta->goal_col, shift);
      }
      return MY_RET_OK;
    case MY_KEY_HOME:
      if (ctrl) {
        ta_move_to(ta, 0, 0, shift);
      } else {
        ta_move_to(ta, ta->cursor_row, 0, shift);
      }
      if (!shift) {
        ta->goal_col = ta->cursor_col;
      }
      return MY_RET_OK;
    case MY_KEY_END:
      if (ctrl) {
        ta_move_to(ta, ta_line_count(ta) - 1,
                   ta_line_cp_len(ta, ta_line_count(ta) - 1), shift);
      } else {
        ta_move_to(ta, ta->cursor_row,
                   ta_line_cp_len(ta, ta->cursor_row), shift);
      }
      if (!shift) {
        ta->goal_col = ta->cursor_col;
      }
      return MY_RET_OK;
    case MY_KEY_BACKSPACE: {
      size_t r0, c0, r1, c1;
      if (ta_sel(ta, &r0, &c0, &r1, &c1)) {
        user_delete_range(ta, ta_offset_of(ta, r0, c0),
                          ta_offset_of(ta, r1, c1));
      } else {
        size_t off = ta_offset_of(ta, ta->cursor_row, ta->cursor_col);
        if (off > 0) {
          /* previous codepoint boundary (skip continuation bytes) */
          size_t prev = off - 1;
          while (prev > 0 && (ta->text[prev] & 0xC0) == 0x80) {
            prev--;
          }
          user_delete_range(ta, prev, off);
        }
      }
      return MY_RET_OK;
    }
    case MY_KEY_DELETE: {
      size_t r0, c0, r1, c1;
      if (ta_sel(ta, &r0, &c0, &r1, &c1)) {
        user_delete_range(ta, ta_offset_of(ta, r0, c0),
                          ta_offset_of(ta, r1, c1));
      } else {
        size_t off = ta_offset_of(ta, ta->cursor_row, ta->cursor_col);
        if (off < ta->text_len) {
          user_delete_range(ta, off, off + my_str_utf8_char_len(ta->text + off));
        }
      }
      return MY_RET_OK;
    }
    case MY_KEY_RETURN:
      user_insert(ta, "\n", 1, 1);
      return MY_RET_OK;
    default:
      break;
  }
  if (key >= 32 && key <= 126 && !ctrl) {
    char ch = (char)key;
    user_insert(ta, &ch, 1, 1);
    return MY_RET_OK;
  }
  return MY_RET_FAIL;
}

/* ---------------- events ---------------- */

static my_ret_t ta_on_event(my_widget_t* widget, const my_event_t* event) {
  my_text_area_t* ta = (my_text_area_t*)widget;
  switch (event->type) {
    case MY_EVENT_POINTER_DOWN: {
      int32_t lx = event->u.pointer.x, ly = event->u.pointer.y;
      int32_t line_h = ta->font_size > 0 ? ta->font_size : 16;
      size_t row, col;
      my_widget_global_to_local(widget, &lx, &ly);
      row = (size_t)((ly - TA_PAD_Y + ta->scroll_y) / line_h);
      col = (size_t)((lx - TA_PAD_X + ta->scroll_x + TA_CELL_W / 2) /
                     TA_CELL_W);
      ta_move_to(ta, row, col, false);
      ta->goal_col = ta->cursor_col;
      return MY_RET_OK;
    }
    case MY_EVENT_KEY_DOWN:
      if (!ta->focused) {
        return MY_RET_FAIL;
      }
      return ta_on_key(ta, event);
    default:
      return MY_RET_FAIL;
  }
}

/* ---------------- paint ---------------- */

static void ta_on_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_text_area_t* ta = (my_text_area_t*)widget;
  uint32_t bg = my_widget_style_get_color(
      widget, widget->enable ? MY_STATE_NORMAL : MY_STATE_DISABLED, "bg_color",
      0xFFFFFFFFu);
  uint32_t border = my_widget_style_get_color(
      widget, ta->focused ? MY_STATE_HOVER : MY_STATE_NORMAL, "border_color",
      0x9E9E9EFFu);
  uint32_t fg = my_widget_style_get_color(widget, MY_STATE_NORMAL, "fg_color",
                                          0x212121FFu);
  int32_t line_h = ta->font != NULL
                       ? my_font_line_height(ta->font, ta->font_size)
                       : ta->font_size;
  size_t first_row, last_row, row;
  size_t sel_r0 = 0, sel_c0 = 0, sel_r1 = 0, sel_c1 = 0;
  bool has_sel;

  if (line_h <= 0) {
    line_h = ta->font_size > 0 ? ta->font_size : 16;
  }
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(bg));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
  my_vgcanvas_set_stroke_color(vg, my_color_from_rgba32(border));
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                            (float)widget->rect.h});

  my_vgcanvas_save(vg);
  my_vgcanvas_clip_rect(vg, &(my_rectf_t){TA_PAD_X, TA_PAD_Y,
                                          (float)(widget->rect.w - 2 * TA_PAD_X),
                                          (float)(widget->rect.h -
                                                  2 * TA_PAD_Y)});

  has_sel = ta_sel(ta, &sel_r0, &sel_c0, &sel_r1, &sel_c1);
  first_row = (size_t)(ta->scroll_y / line_h);
  last_row = first_row + (size_t)(widget->rect.h / line_h) + 1;
  if (last_row >= ta_line_count(ta)) {
    last_row = ta_line_count(ta) > 0 ? ta_line_count(ta) - 1 : 0;
  }

  if ((ta->text == NULL || ta->text_len == 0) && ta->hint != NULL &&
      !ta->focused) {
    my_vgcanvas_set_fill_color(vg, my_color_rgb(150, 150, 150));
    my_vgcanvas_draw_text(vg, ta->hint, TA_PAD_X, TA_PAD_Y);
  }

  for (row = first_row; row <= last_row && row < ta_line_count(ta); row++) {
    size_t start = ta_line_start(ta, row);
    size_t end = row + 1 < ta_line_count(ta) ? ta_line_start(ta, row + 1)
                                             : ta->text_len;
    size_t len = end - start;
    int32_t ty = TA_PAD_Y + (int32_t)row * line_h - ta->scroll_y;
    if (len > 0 && ta->text[start + len - 1] == '\n') {
      len--;
    }
    /* selection highlight for this row */
    if (has_sel && row >= sel_r0 && row <= sel_r1) {
      size_t c0 = row == sel_r0 ? sel_c0 : 0;
      size_t c1 = row == sel_r1 ? sel_c1 : ta_line_cp_len(ta, row) +
                                               (row + 1 < ta_line_count(ta)
                                                    ? 1
                                                    : 0);
      if (c1 > c0) {
        my_vgcanvas_set_fill_color(vg, my_color_rgb(130, 170, 230));
        my_vgcanvas_fill_rect(vg,
                              &(my_rectf_t){(float)(TA_PAD_X + (int32_t)c0 *
                                                        TA_CELL_W -
                                                    ta->scroll_x),
                                            (float)ty, (float)(c1 - c0) * TA_CELL_W,
                                            (float)line_h});
      }
    }
    if (len > 0) {
      char* line = (char*)my_mem_alloc(ta->allocator, len + 1);
      if (line != NULL) {
        memcpy(line, ta->text + start, len);
        line[len] = '\0';
        my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(fg));
        my_vgcanvas_draw_text(vg, line,
                              (float)(TA_PAD_X - ta->scroll_x), (float)ty);
        my_mem_free(ta->allocator, line);
      }
    }
  }

  /* cursor */
  if (ta->focused && ta->cursor_visible) {
    int32_t cx = TA_PAD_X + (int32_t)ta->cursor_col * TA_CELL_W - ta->scroll_x;
    int32_t cy = TA_PAD_Y + (int32_t)ta->cursor_row * line_h - ta->scroll_y;
    my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(fg));
    my_vgcanvas_fill_rect(vg, &(my_rectf_t){(float)cx, (float)cy, 1,
                                            (float)line_h});
  }
  my_vgcanvas_restore(vg);
}

static const my_widget_vtable_t s_ta_vtable = {ta_on_paint, ta_on_event, NULL};

/* ---------------- focus / blink / lifecycle ---------------- */

static my_ret_t ta_blink_tick(void* ctx) {
  my_text_area_t* ta = (my_text_area_t*)ctx;
  ta->cursor_visible = !ta->cursor_visible;
  my_widget_invalidate((my_widget_t*)ta, NULL);
  return MY_RET_OK;
}

static void ta_on_focus(void* ctx, const char* event, void* data) {
  my_text_area_t* ta = (my_text_area_t*)ctx;
  my_pal_main_loop_t* loop;
  (void)event;
  (void)data;
  ta->focused = true;
  ta->cursor_visible = true;
  loop = my_window_loop_of_widget((my_widget_t*)ta);
  if (loop != NULL && ta->blink_timer_id == 0) {
    ta->blink_timer_id =
        my_pal_main_loop_add_timer(loop, ta_blink_tick, ta, 500);
    ta->blink_loop = ta->blink_timer_id > 0 ? loop : NULL;
  }
  my_widget_invalidate((my_widget_t*)ta, NULL);
}

static void ta_on_blur(void* ctx, const char* event, void* data) {
  my_text_area_t* ta = (my_text_area_t*)ctx;
  (void)event;
  (void)data;
  ta->focused = false;
  ta->cursor_visible = true;
  if (ta->blink_timer_id > 0 && ta->blink_loop != NULL) {
    my_pal_main_loop_remove_timer(ta->blink_loop, ta->blink_timer_id);
    ta->blink_timer_id = 0;
    ta->blink_loop = NULL;
  }
  my_widget_invalidate((my_widget_t*)ta, NULL);
}

static void ta_destroy_chain(my_object_t* obj) {
  my_text_area_t* ta = (my_text_area_t*)obj;
  if (ta->blink_timer_id > 0 && ta->blink_loop != NULL) {
    my_pal_main_loop_remove_timer(ta->blink_loop, ta->blink_timer_id);
  }
  my_darray_destroy(ta->line_offsets);
  my_mem_free(ta->allocator, ta->text);
  my_mem_free(ta->allocator, ta->hint);
  my_widget_destroy((my_widget_t*)ta);
  my_object_destroy(obj);
}

my_widget_t* my_text_area_create(const my_allocator_t* allocator) {
  my_text_area_t* ta =
      (my_text_area_t*)my_mem_calloc(allocator, 1, sizeof(my_text_area_t));
  if (ta == NULL) {
    return NULL;
  }
  if (my_widget_init((my_widget_t*)ta, allocator, &s_ta_vtable,
                     "text_area") != MY_RET_OK) {
    my_mem_free(allocator, ta);
    return NULL;
  }
  ((my_object_t*)ta)->destroy = ta_destroy_chain;
  ta->allocator = allocator;
  ta->font_size = 16;
  ta->cursor_visible = true;
  ta->line_offsets = my_darray_create(allocator, 0);
  if (ta->line_offsets == NULL) {
    my_object_unref((my_object_t*)ta);
    return NULL;
  }
  ta_offsets_push(ta, 0);
  ta->text = (char*)my_mem_calloc(allocator, 1, 1);
  if (ta->text == NULL) {
    my_object_unref((my_object_t*)ta);
    return NULL;
  }
  ((my_widget_t*)ta)->focusable = true;
  ((my_widget_t*)ta)->widget_type = "text_area";
  my_widget_on((my_widget_t*)ta, "focus", ta_on_focus, ta);
  my_widget_on((my_widget_t*)ta, "blur", ta_on_blur, ta);
  return (my_widget_t*)ta;
}

my_ret_t my_text_area_set_text(my_widget_t* area, const char* text) {
  my_text_area_t* ta = (my_text_area_t*)area;
  size_t len;
  if (area == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  len = text != NULL ? strlen(text) : 0;
  {
    char* p = (char*)my_mem_realloc(ta->allocator, ta->text, len + 1);
    if (p == NULL) {
      return MY_RET_OOM;
    }
    ta->text = p;
  }
  if (len > 0) {
    memcpy(ta->text, text, len);
  }
  ta->text[len] = '\0';
  ta->text_len = len;
  ta_rebuild_from(ta, 0);
  ta_cursor_to_offset(ta, len);
  my_widget_invalidate(area, NULL);
  return MY_RET_OK;
}

const char* my_text_area_get_text(my_widget_t* area) {
  my_text_area_t* ta = (my_text_area_t*)area;
  return area == NULL || ta->text == NULL ? "" : ta->text;
}

my_ret_t my_text_area_set_hint(my_widget_t* area, const char* hint) {
  my_text_area_t* ta = (my_text_area_t*)area;
  char* copy;
  if (area == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  copy = my_strdup(ta->allocator, hint);
  if (hint != NULL && copy == NULL) {
    return MY_RET_OOM;
  }
  my_mem_free(ta->allocator, ta->hint);
  ta->hint = copy;
  my_widget_invalidate(area, NULL);
  return MY_RET_OK;
}

my_ret_t my_text_area_set_readonly(my_widget_t* area, bool readonly) {
  if (area == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  ((my_text_area_t*)area)->readonly = readonly;
  return MY_RET_OK;
}

my_ret_t my_text_area_set_max_len(my_widget_t* area, size_t max_codepoints) {
  if (area == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  ((my_text_area_t*)area)->max_len = max_codepoints;
  return MY_RET_OK;
}

void my_text_area_set_font(my_widget_t* area, my_font_t* font, int32_t size) {
  my_text_area_t* ta = (my_text_area_t*)area;
  if (area != NULL) {
    if (font != NULL) {
      ta->font = font;
    }
    if (size > 0) {
      ta->font_size = size;
    }
  }
}

size_t my_text_area_line_count(my_widget_t* area) {
  return area != NULL ? ta_line_count((my_text_area_t*)area) : 0;
}
