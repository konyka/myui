/**
 * @file my_text_layout.h
 * @brief Text layout: logical-order UTF-8 -> shaped + visually reordered
 * codepoints (M11a).
 *
 * Pipeline: UTF-8 decode -> pure-LTR fast path (no RTL/Arabic codepoint:
 * identity, SheenBidi untouched) -> Arabic joining (my_arabic_shape,
 * presentation forms) -> SheenBidi paragraph direction + UBA reorder.
 * The result is a visual-order codepoint array plus a visual->logical
 * index map and a re-encoded visual UTF-8 string (for font-vtable
 * measure, which is order-insensitive but shaping-sensitive).
 *
 * Results are cached in a process-global LRU (64 entries, key = text;
 * the layout is font-independent). my_text_layout_process returns a
 * CALLER-OWNED copy (destroy with my_text_layout_destroy).
 *
 * Built with MYUI_BIDI=OFF: shaping/reorder compile out, process always
 * returns the identity layout (still cached).
 *
 * Boundaries: single paragraph (draw_text strings); mirroring (UBA rule
 * L4) not applied (TODO); editing widgets' RTL cursor mapping is a TODO.
 */
#ifndef MY_TEXT_LAYOUT_H
#define MY_TEXT_LAYOUT_H

#include "myc/my_mem.h"
#include "myc/my_types.h"

/** @brief One laid-out string (caller-owned copy). */
typedef struct my_text_layout_t {
  const my_allocator_t* allocator;
  uint32_t* visual_cps;        /**< len items: shaped + reordered */
  uint32_t* visual_to_logical; /**< len items: visual i -> logical index */
  char* visual_utf8;           /**< visual_cps re-encoded as UTF-8 */
  size_t len;
  bool has_rtl;                /**< any RTL-level run (or RTL base) */
} my_text_layout_t;

/**
 * @brief Lay out a logical-order UTF-8 string. NULL text -> NULL. The
 * returned layout is owned by the caller (a copy of the cached master).
 */
my_text_layout_t* my_text_layout_process(const my_allocator_t* allocator,
                                         const char* text);

/** @brief Destroy a layout returned by my_text_layout_process. */
void my_text_layout_destroy(my_text_layout_t* layout);

/**
 * @brief Cheap pre-scan: true when the text contains any codepoint that
 * could need shaping or reordering (Arabic/Hebrew blocks, presentation
 * forms, bidi controls). Backends use it to keep a zero-overhead legacy
 * path for plain LTR text.
 */
bool my_text_layout_may_need_bidi(const char* text);

/** @brief Drop all cached layouts (tests / shutdown). */
void my_text_layout_cache_flush(void);

/** @brief Occupied cache slots (tests). */
size_t my_text_layout_cache_size(void);

#endif /* MY_TEXT_LAYOUT_H */
