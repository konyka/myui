/**
 * @file my_font.c
 * @brief UTF-8 decoding + built-in 8x8 bitmap font.
 */
#include "myr/my_font.h"

#include <string.h>

#include "myr/my_font_bitmap_data.h"

/* ---------------- UTF-8 ---------------- */

uint32_t my_utf8_next(const char** s) {
  const unsigned char* p = (const unsigned char*)*s;
  uint32_t cp;
  size_t n;
  if (*p < 0x80) {
    cp = *p;
    n = 1;
  } else if ((*p & 0xE0) == 0xC0) {
    cp = *p & 0x1F;
    n = 2;
  } else if ((*p & 0xF0) == 0xE0) {
    cp = *p & 0x0F;
    n = 3;
  } else if ((*p & 0xF8) == 0xF0) {
    cp = *p & 0x07;
    n = 4;
  } else {
    *s += 1; /* invalid lead byte: replacement char, advance 1 */
    return 0xFFFD;
  }
  {
    size_t i;
    for (i = 1; i < n; i++) {
      if ((p[i] & 0xC0) != 0x80) {
        *s += 1;
        return 0xFFFD;
      }
      cp = (cp << 6) | (uint32_t)(p[i] & 0x3F);
    }
  }
  *s += n;
  return cp;
}

/* ---------------- bitmap font ---------------- */

typedef struct my_font_bitmap_t {
  my_font_t base;
  const my_allocator_t* allocator;
} my_font_bitmap_t;

static my_ret_t bmp_measure(my_font_t* font, const char* text, int32_t size,
                            int32_t* w, int32_t* h) {
  const char* p = text;
  int32_t width = 0;
  (void)font;
  if (text == NULL || size <= 0) {
    return MY_RET_INVALID_PARAMS;
  }
  while (*p != '\0') {
    my_utf8_next(&p);
    width += size; /* monospace: one cell per codepoint */
  }
  if (w != NULL) {
    *w = width;
  }
  if (h != NULL) {
    *h = size;
  }
  return MY_RET_OK;
}

static my_ret_t bmp_get_glyph(my_font_t* font, uint32_t codepoint, int32_t size,
                              my_glyph_t* glyph) {
  static const uint8_t blank[8] = {0};
  (void)font;
  if (glyph == NULL || size <= 0) {
    return MY_RET_INVALID_PARAMS;
  }
  if (codepoint < 32 || codepoint > 126) {
    /* non-ASCII: hollow box fallback glyph */
    static const uint8_t box[8] = {0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0};
    glyph->bitmap = box;
    glyph->w = 8;
    glyph->h = 8;
  } else if (codepoint == 32) {
    glyph->bitmap = blank;
    glyph->w = 8;
    glyph->h = 8;
  } else {
    glyph->bitmap = MY_FONT_BITMAP_DATA[codepoint - 32];
    glyph->w = 8;
    glyph->h = 8;
  }
  glyph->bearing_x = 0;
  glyph->bearing_y = (8 * size) / 8; /* baseline at bottom of the cell */
  glyph->advance = size;
  return MY_RET_OK;
}

static int32_t bmp_ascent(my_font_t* font, int32_t size) {
  (void)font;
  return size;
}

static int32_t bmp_descent(my_font_t* font, int32_t size) {
  (void)font;
  (void)size;
  return 0;
}

static int32_t bmp_line_height(my_font_t* font, int32_t size) {
  (void)font;
  return size;
}

static void bmp_destroy(my_font_t* font) {
  my_font_bitmap_t* f = (my_font_bitmap_t*)font;
  if (f != NULL) {
    my_mem_free(f->allocator, f);
  }
}

static const my_font_vtable_t s_bitmap_vtable = {bmp_measure, bmp_get_glyph,
                                                 bmp_ascent, bmp_descent,
                                                 bmp_line_height, bmp_destroy};

my_font_t* my_font_bitmap_create(const my_allocator_t* allocator) {
  my_font_bitmap_t* f =
      (my_font_bitmap_t*)my_mem_calloc(allocator, 1, sizeof(my_font_bitmap_t));
  if (f == NULL) {
    return NULL;
  }
  f->base.vtable = &s_bitmap_vtable;
  f->allocator = allocator;
  return (my_font_t*)f;
}
