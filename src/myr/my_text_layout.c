/**
 * @file my_text_layout.c
 * @brief Text layout implementation (M11a): decode -> fast path ->
 * Arabic shaping -> SheenBidi UBA reorder; LRU-cached masters.
 */
#include "myr/my_text_layout.h"

#include <string.h>

#include "myc/my_str.h" /* my_strdup */
#include "myr/my_font.h" /* my_utf8_next */

#if defined(MYUI_BIDI)
#include <SheenBidi/SBAlgorithm.h>
#include <SheenBidi/SBCodepointSequence.h>
#include <SheenBidi/SBLine.h>
#include <SheenBidi/SBParagraph.h>
#include <SheenBidi/SBRun.h>

#include "myr/my_arabic_shape.h"
#endif

/* ---------------- utf-8 helpers ---------------- */

static uint32_t* tl_decode(const my_allocator_t* alloc, const char* text,
                           size_t* out_len) {
  size_t cap = strlen(text) + 1, n = 0;
  uint32_t* cps = (uint32_t*)my_mem_alloc(alloc, cap * sizeof(uint32_t));
  const char* p = text;
  if (cps == NULL) {
    return NULL;
  }
  while (*p != '\0') {
    cps[n++] = my_utf8_next(&p);
  }
  *out_len = n;
  return cps;
}

static size_t tl_utf8_encode(uint32_t cp, char out[4]) {
  if (cp < 0x80u) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800u) {
    out[0] = (char)(0xC0u | (cp >> 6));
    out[1] = (char)(0x80u | (cp & 0x3Fu));
    return 2;
  }
  if (cp < 0x10000u) {
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
  }
  out[0] = (char)(0xF0u | (cp >> 18));
  out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
  out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
  out[3] = (char)(0x80u | (cp & 0x3Fu));
  return 4;
}

static char* tl_encode_all(const my_allocator_t* alloc, const uint32_t* cps,
                           size_t len) {
  char* s = (char*)my_mem_alloc(alloc, len * 4 + 1);
  size_t i, n = 0;
  if (s == NULL) {
    return NULL;
  }
  for (i = 0; i < len; i++) {
    n += tl_utf8_encode(cps[i], s + n);
  }
  s[n] = '\0';
  return s;
}

static bool tl_cp_needs_bidi(uint32_t cp) {
  return (cp >= 0x0590u && cp <= 0x08FFu) || /* Hebrew, Arabic, Syriac... */
         (cp >= 0xFB1Du && cp <= 0xFEFCu) || /* presentation forms */
         cp == 0x061Cu ||                    /* Arabic letter mark */
         cp == 0x200Eu || cp == 0x200Fu ||   /* LRM/RLM */
         (cp >= 0x202Au && cp <= 0x202Eu) || /* embeddings/overrides */
         (cp >= 0x2066u && cp <= 0x2069u);   /* isolates */
}

bool my_text_layout_may_need_bidi(const char* text) {
  const char* p = text;
  if (text == NULL) {
    return false;
  }
  while (*p != '\0') {
    if (tl_cp_needs_bidi(my_utf8_next(&p))) {
      return true;
    }
  }
  return false;
}

/* ---------------- layout master (cache payload) ---------------- */

typedef struct tl_master_t {
  char* text;  /**< key (owned copy) */
  uint32_t* cps;
  uint32_t* map;
  char* utf8;
  size_t len;
  bool has_rtl;
  uint64_t tick;
} tl_master_t;

static void tl_master_free(tl_master_t* m) {
  my_mem_free(NULL, m->text);
  my_mem_free(NULL, m->cps);
  my_mem_free(NULL, m->map);
  my_mem_free(NULL, m->utf8);
  memset(m, 0, sizeof(*m));
}

/** @brief Compute the master: decode, shape (BIDI), reorder (BIDI). */
static bool tl_master_compute(tl_master_t* m, const char* text) {
  size_t len = 0, i;
  bool may;
  memset(m, 0, sizeof(*m));
  m->text = my_strdup(NULL, text);
  m->cps = tl_decode(NULL, text, &len);
  if (m->text == NULL || m->cps == NULL) {
    tl_master_free(m);
    return false;
  }
  m->len = len;
  m->map = (uint32_t*)my_mem_alloc(NULL, (len > 0 ? len : 1) * sizeof(uint32_t));
  if (m->map == NULL) {
    tl_master_free(m);
    return false;
  }
  may = false;
  for (i = 0; i < len; i++) {
    if (tl_cp_needs_bidi(m->cps[i])) {
      may = true;
      break;
    }
  }
#if defined(MYUI_BIDI)
  if (may && len > 0) {
    /* Arabic joining first (context = logical neighbours), then UBA */
    my_arabic_shape(m->cps, len);
    {
      SBCodepointSequence seq = {SBStringEncodingUTF32, m->cps, len};
      SBAlgorithmRef alg = SBAlgorithmCreate(&seq);
      SBParagraphRef para = NULL;
      SBLineRef line = NULL;
      const SBRun* runs = NULL;
      size_t run_count = 0, vi = 0, ri;
      bool has_rtl = false;
      if (alg != NULL) {
        para = SBAlgorithmCreateParagraph(alg, 0, len, SBLevelDefaultLTR);
      }
      if (para != NULL) {
        line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));
      }
      if (line != NULL) {
        runs = SBLineGetRunsPtr(line);
        run_count = SBLineGetRunCount(line);
        has_rtl = SBParagraphGetBaseLevel(para) != 0;
        /* runs are already in VISUAL order; odd level = RTL -> reverse */
        {
          uint32_t* tmp = (uint32_t*)my_mem_alloc(NULL, len * sizeof(uint32_t));
          if (tmp == NULL) {
            SBLineRelease(line);
            SBParagraphRelease(para);
            SBAlgorithmRelease(alg);
            tl_master_free(m);
            return false;
          }
          memcpy(tmp, m->cps, len * sizeof(uint32_t));
          for (ri = 0; ri < run_count; ri++) {
            SBUInteger k;
            bool rtl = (runs[ri].level & 1u) != 0;
            has_rtl = has_rtl || rtl;
            for (k = 0; k < runs[ri].length; k++) {
              SBUInteger logical =
                  runs[ri].offset + (rtl ? runs[ri].length - 1u - k : k);
              m->cps[vi] = tmp[logical];
              m->map[vi] = (uint32_t)logical;
              vi++;
            }
          }
          my_mem_free(NULL, tmp);
        }
        SBLineRelease(line);
      }
      if (para != NULL) {
        SBParagraphRelease(para);
      }
      if (alg != NULL) {
        SBAlgorithmRelease(alg);
      }
      if (vi != len) { /* SheenBidi failure: fall back to identity */
        may = false;
      } else {
        m->has_rtl = has_rtl;
      }
    }
  }
#else
  (void)may;
#endif
  if (!may || m->len == 0) {
    /* identity: visual == logical */
    for (i = 0; i < len; i++) {
      m->map[i] = (uint32_t)i;
    }
    m->has_rtl = false;
  }
  m->utf8 = tl_encode_all(NULL, m->cps, len);
  if (m->utf8 == NULL) {
    tl_master_free(m);
    return false;
  }
  return true;
}

/* ---------------- LRU cache ---------------- */

#define TL_CACHE_CAP 64u

static tl_master_t g_cache[TL_CACHE_CAP];
static uint64_t g_tick;

static my_text_layout_t* tl_copy(const my_allocator_t* alloc,
                                 const tl_master_t* m) {
  my_text_layout_t* l =
      (my_text_layout_t*)my_mem_calloc(alloc, 1, sizeof(my_text_layout_t));
  if (l == NULL) {
    return NULL;
  }
  l->allocator = alloc;
  l->len = m->len;
  l->has_rtl = m->has_rtl;
  if (m->len > 0) {
    l->visual_cps = (uint32_t*)my_mem_alloc(alloc, m->len * sizeof(uint32_t));
    l->visual_to_logical =
        (uint32_t*)my_mem_alloc(alloc, m->len * sizeof(uint32_t));
    if (l->visual_cps == NULL || l->visual_to_logical == NULL) {
      my_text_layout_destroy(l);
      return NULL;
    }
    memcpy(l->visual_cps, m->cps, m->len * sizeof(uint32_t));
    memcpy(l->visual_to_logical, m->map, m->len * sizeof(uint32_t));
  }
  l->visual_utf8 = my_strdup(alloc, m->utf8 != NULL ? m->utf8 : "");
  if (l->visual_utf8 == NULL) {
    my_text_layout_destroy(l);
    return NULL;
  }
  return l;
}

my_text_layout_t* my_text_layout_process(const my_allocator_t* allocator,
                                         const char* text) {
  size_t i, slot = 0;
  uint64_t oldest;
  if (text == NULL) {
    return NULL;
  }
  g_tick++;
  for (i = 0; i < TL_CACHE_CAP; i++) {
    if (g_cache[i].text != NULL && strcmp(g_cache[i].text, text) == 0) {
      g_cache[i].tick = g_tick;
      return tl_copy(allocator, &g_cache[i]);
    }
  }
  /* miss: compute into the LRU slot (empty slot or oldest entry) */
  oldest = UINT64_MAX;
  for (i = 0; i < TL_CACHE_CAP; i++) {
    if (g_cache[i].text == NULL) {
      slot = i;
      break;
    }
    if (g_cache[i].tick < oldest) {
      oldest = g_cache[i].tick;
      slot = i;
    }
  }
  tl_master_free(&g_cache[slot]);
  if (!tl_master_compute(&g_cache[slot], text)) {
    return NULL;
  }
  g_cache[slot].tick = g_tick;
  return tl_copy(allocator, &g_cache[slot]);
}

void my_text_layout_destroy(my_text_layout_t* layout) {
  if (layout != NULL) {
    const my_allocator_t* alloc = layout->allocator;
    my_mem_free(alloc, layout->visual_cps);
    my_mem_free(alloc, layout->visual_to_logical);
    my_mem_free(alloc, layout->visual_utf8);
    my_mem_free(alloc, layout);
  }
}

void my_text_layout_cache_flush(void) {
  size_t i;
  for (i = 0; i < TL_CACHE_CAP; i++) {
    tl_master_free(&g_cache[i]);
  }
}

size_t my_text_layout_cache_size(void) {
  size_t i, n = 0;
  for (i = 0; i < TL_CACHE_CAP; i++) {
    if (g_cache[i].text != NULL) {
      n++;
    }
  }
  return n;
}
