/**
 * @file my_stb_truetype.c
 * @brief stb_truetype implementation translation unit.
 *
 * Third-party single-header library (public domain,
 * https://github.com/nothings/stb). Compiled in isolation so its
 * warnings can be relaxed WITHOUT lowering the project's own bar
 * (see src/myr/CMakeLists.txt: this file drops -Werror/-pedantic).
 */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
