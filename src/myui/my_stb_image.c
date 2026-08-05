/**
 * @file my_stb_image.c
 * @brief stb_image / stb_image_write implementation translation unit.
 *
 * Third-party single-header libraries (public domain,
 * https://github.com/nothings/stb). Compiled in isolation so their
 * warnings can be relaxed WITHOUT lowering the project's own bar.
 */
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
