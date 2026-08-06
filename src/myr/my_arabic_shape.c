/**
 * @file my_arabic_shape.c
 * @brief Arabic letter joining (shaping) implementation (M11a).
 */
#include "myr/my_arabic_shape.h"

#include <string.h>

#include "myc/my_mem.h"

static int join_cmp(uint32_t cp, const my_arabic_join_entry_t* e) {
  return cp < e->cp ? -1 : cp > e->cp ? 1 : 0;
}

my_arabic_join_t my_arabic_join_class(uint32_t cp) {
  size_t lo = 0, hi = sizeof(MY_ARABIC_JOINS) / sizeof(MY_ARABIC_JOINS[0]);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = join_cmp(cp, &MY_ARABIC_JOINS[mid]);
    if (c == 0) {
      return (my_arabic_join_t)MY_ARABIC_JOINS[mid].join;
    }
    if (c < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return MY_ARABIC_JOIN_NONE;
}

static const my_arabic_form_t* form_find(uint32_t base) {
  size_t lo = 0, hi = sizeof(MY_ARABIC_FORMS) / sizeof(MY_ARABIC_FORMS[0]);
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const my_arabic_form_t* e = &MY_ARABIC_FORMS[mid];
    if (base == e->base) {
      return e;
    }
    if (base < e->base) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return NULL;
}

static bool joins_forward(my_arabic_join_t j) {
  return j == MY_ARABIC_JOIN_DUAL || j == MY_ARABIC_JOIN_LEFT ||
         j == MY_ARABIC_JOIN_CAUSING;
}

static bool joins_backward(my_arabic_join_t j) {
  return j == MY_ARABIC_JOIN_DUAL || j == MY_ARABIC_JOIN_RIGHT ||
         j == MY_ARABIC_JOIN_CAUSING;
}

uint32_t my_arabic_form_for(uint32_t base, bool join_prev, bool join_next) {
  const my_arabic_form_t* e = form_find(base);
  uint32_t form = 0;
  if (e == NULL) {
    return base;
  }
  if (join_prev && join_next) {
    form = e->medial;
  } else if (join_prev) {
    form = e->final_;
  } else if (join_next) {
    form = e->initial;
  } else {
    form = e->isolated;
  }
  return form != 0 ? form : base; /* e.g. right-joining chars have no
                                     initial/medial forms */
}

size_t my_arabic_shape(uint32_t* cps, size_t len) {
  uint32_t* orig;
  size_t i;
  if (cps == NULL) {
    return 0;
  }
  /* joining decisions need the ORIGINAL logical neighbours: replaced
   * presentation forms have no joining class, so work from a copy */
  orig = (uint32_t*)my_mem_alloc(NULL, (len > 0 ? len : 1) * sizeof(uint32_t));
  if (orig == NULL) {
    return len; /* OOM: leave the text unshaped rather than mis-shaped */
  }
  memcpy(orig, cps, len * sizeof(uint32_t));
  for (i = 0; i < len; i++) {
    const my_arabic_form_t* e = form_find(orig[i]);
    my_arabic_join_t cur;
    bool prev_ok = false, next_ok = false;
    size_t j;
    if (e == NULL) {
      continue; /* not a shapable Arabic letter */
    }
    cur = my_arabic_join_class(orig[i]);
    /* nearest non-transparent char before/after i */
    for (j = i; j-- > 0;) {
      my_arabic_join_t jc = my_arabic_join_class(orig[j]);
      if (jc == MY_ARABIC_JOIN_TRANSPARENT) {
        continue;
      }
      prev_ok = joins_forward(jc) && joins_backward(cur);
      break;
    }
    for (j = i + 1; j < len; j++) {
      my_arabic_join_t jc = my_arabic_join_class(orig[j]);
      if (jc == MY_ARABIC_JOIN_TRANSPARENT) {
        continue;
      }
      next_ok = joins_backward(jc) && joins_forward(cur);
      break;
    }
    cps[i] = my_arabic_form_for(orig[i], prev_ok, next_ok);
  }
  my_mem_free(NULL, orig);
  return len;
}
