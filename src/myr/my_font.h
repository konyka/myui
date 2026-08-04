/**
 * @file my_font.h
 * @brief Font abstraction: metrics + glyph bitmaps (8bpp alpha).
 *
 * A font provides glyph rasterization for the vgcanvas backends.
 * Implementations: my_font_bitmap (built-in 8x8, zero-dependency
 * fallback) and my_font_stb (TrueType via stb_truetype, optional).
 * Text is UTF-8; decode with myc's my_str helpers.
 */
#ifndef MY_FONT_H
#define MY_FONT_H

#include "myc/my_error.h"
#include "myc/my_mem.h"

/** @brief One rasterized glyph (8bpp alpha coverage, row-major). */
typedef struct my_glyph_t {
  const uint8_t* bitmap; /**< w*h alpha bytes; NULL for blank (space) */
  int32_t w;             /**< bitmap width */
  int32_t h;             /**< bitmap height */
  int32_t bearing_x;     /**< left side bearing (pixels from pen x) */
  int32_t bearing_y;     /**< ascent offset: pixels above the baseline */
  int32_t advance;       /**< pen advance */
} my_glyph_t;

typedef struct my_font_t my_font_t;

/** @brief Font vtable. */
typedef struct my_font_vtable_t {
  /** @brief Metrics of a UTF-8 string at size (pixels). */
  my_ret_t (*measure)(my_font_t* font, const char* text, int32_t size,
                      int32_t* w, int32_t* h);
  /** @brief Rasterize one codepoint; blank glyph for missing/space. */
  my_ret_t (*get_glyph)(my_font_t* font, uint32_t codepoint, int32_t size,
                        my_glyph_t* glyph);
  int32_t (*ascent)(my_font_t* font, int32_t size);
  int32_t (*descent)(my_font_t* font, int32_t size); /**< negative or 0 */
  int32_t (*line_height)(my_font_t* font, int32_t size);
  void (*destroy)(my_font_t* font);
} my_font_vtable_t;

/** @brief Font base "class". */
struct my_font_t {
  const my_font_vtable_t* vtable;
};

static inline my_ret_t my_font_measure(my_font_t* font, const char* text,
                                       int32_t size, int32_t* w, int32_t* h) {
  return font->vtable->measure(font, text, size, w, h);
}

static inline my_ret_t my_font_get_glyph(my_font_t* font, uint32_t codepoint,
                                         int32_t size, my_glyph_t* glyph) {
  return font->vtable->get_glyph(font, codepoint, size, glyph);
}

static inline int32_t my_font_ascent(my_font_t* font, int32_t size) {
  return font->vtable->ascent(font, size);
}

static inline int32_t my_font_line_height(my_font_t* font, int32_t size) {
  return font->vtable->line_height(font, size);
}

static inline void my_font_destroy(my_font_t* font) {
  if (font != NULL) {
    font->vtable->destroy(font);
  }
}

/**
 * @brief Decode the first UTF-8 codepoint of s and advance the pointer.
 * Invalid bytes decode as 0xFFFD (advance 1). s must not be empty.
 */
uint32_t my_utf8_next(const char** s);

/* ---------------- built-in 8x8 bitmap font ---------------- */

/** @brief Built-in monospaced 8x8 font (ASCII 32..126), zero-dependency. */
my_font_t* my_font_bitmap_create(const my_allocator_t* allocator);

/* ---------------- stb_truetype backend ---------------- */

/**
 * @brief Load a TrueType font from a file path, with an LRU glyph cache
 * of cache_capacity entries (0 = default 256). NULL when the file
 * cannot be read/parsed, or when built without MYUI_FONT_STB.
 */
my_font_t* my_font_stb_create(const my_allocator_t* allocator, const char* path,
                              size_t cache_capacity);

/** @brief Test/diagnostics: glyph cache hit counter (0 without STB). */
size_t my_font_stb_cache_hits(my_font_t* font);

/** @brief Test/diagnostics: glyph cache miss counter (0 without STB). */
size_t my_font_stb_cache_misses(my_font_t* font);

#endif /* MY_FONT_H */
