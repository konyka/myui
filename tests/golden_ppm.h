/**
 * @file golden_ppm.h
 * @brief Golden-image helpers: dump an my_lcd_mem framebuffer as PPM (P6)
 * and compare against reference files.
 *
 * Deterministic byte format: "P6\n<w> <h>\n255\n" + w*h*3 RGB bytes.
 * Pixel unpacking mirrors the documented native formats; RGB565 is read
 * as host-endian uint16 (golden files are host-generated, fine for CI on
 * the same class of machines).
 */
#ifndef GOLDEN_PPM_H
#define GOLDEN_PPM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "myr/my_lcd_mem.h"

/** @brief Unpack one native pixel to RGB triplets. */
static inline void golden_unpack_pixel(my_pixel_format_t format, const uint8_t* p,
                                       uint8_t* r, uint8_t* g, uint8_t* b) {
  switch (format) {
    case MY_PIXEL_FORMAT_RGB565: {
      uint16_t v;
      memcpy(&v, p, 2);
      *r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
      *g = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
      *b = (uint8_t)((v & 0x1F) * 255 / 31);
      break;
    }
    case MY_PIXEL_FORMAT_RGB888:
      *r = p[0];
      *g = p[1];
      *b = p[2];
      break;
    case MY_PIXEL_FORMAT_ARGB8888:
      *r = p[1];
      *g = p[2];
      *b = p[3];
      break;
    case MY_PIXEL_FORMAT_BGRA8888:
      *r = p[2];
      *g = p[1];
      *b = p[0];
      break;
    case MY_PIXEL_FORMAT_MONO:
    default:
      *r = *g = *b = (*p != 0) ? 255 : 0;
      break;
  }
}

/** @brief Read pixel (x,y) of a mem lcd as RGB, MONO handled per bit. */
static inline void golden_read_rgb(my_lcd_t* lcd, uint32_t x, uint32_t y,
                                   uint8_t* r, uint8_t* g, uint8_t* b) {
  my_pixel_format_t format = my_lcd_get_format(lcd);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
  if (format == MY_PIXEL_FORMAT_MONO) {
    uint8_t byte = buf[(size_t)y * stride + x / 8];
    uint8_t bit = (uint8_t)(byte & (0x80u >> (x % 8)));
    *r = *g = *b = bit != 0 ? 255 : 0;
  } else {
    uint32_t bpp = my_pixel_format_bpp(format) / 8;
    golden_unpack_pixel(format, buf + (size_t)y * stride + (size_t)x * bpp, r, g,
                        b);
  }
}

/** @brief Render the framebuffer as PPM bytes into out (needs w*h*3+32). */
static inline size_t golden_ppm_render(my_lcd_t* lcd, uint8_t* out) {
  uint32_t w = my_lcd_get_width(lcd);
  uint32_t h = my_lcd_get_height(lcd);
  int header_len = sprintf((char*)out, "P6\n%u %u\n255\n", (unsigned)w, (unsigned)h);
  uint8_t* p = out + header_len;
  uint32_t x, y;
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      golden_read_rgb(lcd, x, y, p, p + 1, p + 2);
      p += 3;
    }
  }
  return (size_t)header_len + (size_t)w * h * 3;
}

/** @brief Write the framebuffer as a PPM file. Returns true on success. */
static inline bool golden_ppm_write(const char* path, my_lcd_t* lcd) {
  uint32_t w = my_lcd_get_width(lcd);
  uint32_t h = my_lcd_get_height(lcd);
  size_t cap = (size_t)w * h * 3 + 32;
  uint8_t* data = (uint8_t*)malloc(cap);
  size_t size;
  FILE* f;
  bool ok = false;
  if (data == NULL) {
    return false;
  }
  size = golden_ppm_render(lcd, data);
  f = fopen(path, "wb");
  if (f != NULL) {
    ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0) {
      ok = false;
    }
  }
  free(data);
  return ok;
}

/** @brief Read a whole file; NULL on failure. Caller frees. */
static inline uint8_t* golden_read_file(const char* path, size_t* out_size) {
  FILE* f = fopen(path, "rb");
  uint8_t* data;
  long size;
  if (f == NULL) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
      fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  data = (uint8_t*)malloc((size_t)size > 0 ? (size_t)size : 1);
  if (data == NULL) {
    fclose(f);
    return NULL;
  }
  if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
    free(data);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *out_size = (size_t)size;
  return data;
}

#endif /* GOLDEN_PPM_H */
