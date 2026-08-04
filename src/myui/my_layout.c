/**
 * @file my_layout.c
 * @brief Layout params parser + default and linear layouters.
 */
#include "myui/my_layout.h"

#include <stdlib.h>
#include <string.h>

#include "myc/my_str.h"

/* ---------------- params parser ---------------- */

static my_ret_t parse_axis_value(const char* spec, my_layout_mode_t* mode,
                                 float* value) {
  char* end = NULL;
  float v = strtof(spec, &end);
  if (end == spec) {
    return MY_RET_INVALID_PARAMS;
  }
  if (*end == '\0') {
    *mode = MY_LAYOUT_PX;
  } else if (strcmp(end, "%") == 0) {
    *mode = MY_LAYOUT_PERCENT;
  } else if (strcmp(end, "f") == 0) {
    *mode = MY_LAYOUT_FLEX;
  } else {
    return MY_RET_INVALID_PARAMS;
  }
  *value = v;
  return MY_RET_OK;
}

my_ret_t my_layout_params_parse(const char* str, my_layout_params_t* out) {
  my_layout_params_t p;
  const char* cur;
  if (out == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  memset(&p, 0, sizeof(p));
  if (str == NULL || *str == '\0') {
    *out = p;
    return MY_RET_OK;
  }
  cur = str;
  while (*cur != '\0') {
    my_layout_mode_t* mode;
    float* value;
    char token[32];
    size_t len;
    const char* colon;
    while (*cur == ' ') {
      cur++;
    }
    if (*cur == '\0') {
      break;
    }
    len = 0;
    while (cur[len] != '\0' && cur[len] != ' ') {
      len++;
    }
    if (len == 0 || len >= sizeof(token)) {
      return MY_RET_INVALID_PARAMS;
    }
    memcpy(token, cur, len);
    token[len] = '\0';
    cur += len;
    colon = strchr(token, ':');
    if (colon == NULL || colon == token || colon[1] == '\0') {
      return MY_RET_INVALID_PARAMS;
    }
    if (colon - token == 1 && token[0] == 'w') {
      mode = &p.w_mode;
      value = &p.w_value;
    } else if (colon - token == 1 && token[0] == 'h') {
      mode = &p.h_mode;
      value = &p.h_value;
    } else {
      return MY_RET_INVALID_PARAMS;
    }
    if (parse_axis_value(colon + 1, mode, value) != MY_RET_OK) {
      return MY_RET_INVALID_PARAMS;
    }
  }
  *out = p;
  return MY_RET_OK;
}

my_ret_t my_widget_set_layout_params(my_widget_t* widget, const char* params) {
  my_layout_params_t p;
  my_ret_t ret;
  if (widget == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  ret = my_layout_params_parse(params, &p);
  if (ret != MY_RET_OK) {
    return ret;
  }
  widget->layout_params = p;
  if (widget->parent != NULL) {
    widget->parent->need_layout = true;
  }
  return MY_RET_OK;
}

/* ---------------- default layouter ---------------- */

static void default_layout(my_layouter_t* self, my_widget_t* parent) {
  (void)self;
  (void)parent; /* absolute positioning: rects are the truth */
}

my_layouter_t* my_layouter_default(void) {
  static my_layouter_t s_default = {default_layout, NULL};
  return &s_default;
}

/* ---------------- linear layouter ---------------- */

typedef struct my_layouter_linear_t {
  my_layouter_t base;
  const my_allocator_t* allocator;
  bool horizontal;
  int32_t spacing;
} my_layouter_linear_t;

/** @brief Resolve one axis size from a mode/value pair. */
static int32_t axis_size(my_layout_mode_t mode, float value, bool is_main,
                         int32_t content_main, int32_t content_cross,
                         int32_t remaining, float flex_total,
                         int32_t fallback) {
  switch (mode) {
    case MY_LAYOUT_PX:
      return (int32_t)value;
    case MY_LAYOUT_PERCENT: {
      int32_t base = is_main ? content_main : content_cross;
      return (int32_t)(base * value / 100.0f);
    }
    case MY_LAYOUT_FLEX:
      if (is_main) {
        return flex_total > 0.0f ? (int32_t)(remaining * value / flex_total) : 0;
      }
      return content_cross; /* cross-axis flex = fill */
    case MY_LAYOUT_AUTO:
    default:
      /* main AUTO: keep current size; cross AUTO: fill the parent */
      return is_main ? fallback : content_cross;
  }
}

static void linear_layout(my_layouter_t* self, my_widget_t* parent) {
  my_layouter_linear_t* lin = (my_layouter_linear_t*)self;
  bool horz = lin->horizontal;
  int32_t content_main = horz ? parent->rect.w : parent->rect.h;
  int32_t content_cross = horz ? parent->rect.h : parent->rect.w;
  int32_t fixed_total = 0;
  int32_t visible_count = 0;
  float flex_total = 0.0f;
  int32_t remaining;
  int32_t cursor = 0;
  size_t i, n = my_widget_child_count(parent);

  /* pass 1: fixed sizes + flex weights */
  for (i = 0; i < n; i++) {
    my_widget_t* c = my_widget_get_child(parent, i);
    const my_layout_params_t* p;
    my_layout_mode_t mode;
    float value;
    if (!c->visible) {
      continue;
    }
    p = &c->layout_params;
    mode = horz ? p->w_mode : p->h_mode;
    value = horz ? p->w_value : p->h_value;
    visible_count++;
    if (mode == MY_LAYOUT_PX) {
      fixed_total += (int32_t)value;
    } else if (mode == MY_LAYOUT_PERCENT) {
      fixed_total += (int32_t)(content_main * value / 100.0f);
    } else if (mode == MY_LAYOUT_FLEX) {
      flex_total += value;
    } else {
      fixed_total += horz ? c->rect.w : c->rect.h; /* AUTO: current size */
    }
  }
  if (visible_count > 1) {
    fixed_total += lin->spacing * (visible_count - 1);
  }
  remaining = content_main - fixed_total;
  if (remaining < 0) {
    remaining = 0;
  }

  /* pass 2: assign rects (direct field write to avoid re-marking) */
  for (i = 0; i < n; i++) {
    my_widget_t* c = my_widget_get_child(parent, i);
    const my_layout_params_t* p;
    int32_t main_size, cross_size;
    if (!c->visible) {
      continue;
    }
    p = &c->layout_params;
    main_size = axis_size(horz ? p->w_mode : p->h_mode,
                          horz ? p->w_value : p->h_value, true, content_main,
                          content_cross, remaining, flex_total,
                          horz ? c->rect.w : c->rect.h);
    cross_size = axis_size(horz ? p->h_mode : p->w_mode,
                           horz ? p->h_value : p->w_value, false, content_main,
                           content_cross, remaining, flex_total,
                           horz ? c->rect.h : c->rect.w);
    if (horz) {
      c->rect.x = cursor;
      c->rect.w = main_size;
      c->rect.h = cross_size;
    } else {
      c->rect.y = cursor;
      c->rect.h = main_size;
      c->rect.w = cross_size;
    }
    cursor += main_size + lin->spacing;
    my_widget_invalidate(c, NULL);
  }
}

static void linear_destroy(my_layouter_t* self) {
  my_layouter_linear_t* lin = (my_layouter_linear_t*)self;
  my_mem_free(lin->allocator, lin);
}

my_layouter_t* my_layouter_linear_create(const my_allocator_t* allocator,
                                         bool horizontal, int32_t spacing) {
  my_layouter_linear_t* lin =
      (my_layouter_linear_t*)my_mem_calloc(allocator, 1, sizeof(my_layouter_linear_t));
  if (lin == NULL) {
    return NULL;
  }
  lin->base.layout = linear_layout;
  lin->base.destroy = linear_destroy;
  lin->allocator = allocator;
  lin->horizontal = horizontal;
  lin->spacing = spacing;
  return (my_layouter_t*)lin;
}

/* ---------------- attach / run ---------------- */

my_ret_t my_widget_set_layouter(my_widget_t* widget, my_layouter_t* layouter) {
  if (widget == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  if (widget->layouter != NULL && widget->layouter->destroy != NULL) {
    widget->layouter->destroy(widget->layouter);
  }
  widget->layouter = layouter;
  widget->need_layout = true;
  return MY_RET_OK;
}

void my_widget_relayout(my_widget_t* widget) {
  size_t i, n;
  if (widget == NULL) {
    return;
  }
  if (widget->layouter != NULL && widget->layouter->layout != NULL) {
    widget->layouter->layout(widget->layouter, widget);
  }
  if (widget->vtable != NULL && widget->vtable->on_layout != NULL) {
    widget->vtable->on_layout(widget);
  }
  widget->need_layout = false;
  n = my_widget_child_count(widget);
  for (i = 0; i < n; i++) {
    my_widget_relayout(my_widget_get_child(widget, i));
  }
}
