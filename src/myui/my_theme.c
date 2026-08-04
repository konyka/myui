/**
 * @file my_theme.c
 * @brief Theme: style sheet + text loader + widget style resolution.
 */
#include "myui/my_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "myc/my_str.h"
#include "myui/my_widget.h"

/* ---------------- entries ---------------- */

my_theme_t* my_theme_create(const my_allocator_t* allocator) {
  my_theme_t* theme = (my_theme_t*)my_mem_calloc(allocator, 1, sizeof(my_theme_t));
  if (theme == NULL) {
    return NULL;
  }
  theme->allocator = allocator;
  theme->entries = my_darray_create(allocator, 0);
  if (theme->entries == NULL) {
    my_mem_free(allocator, theme);
    return NULL;
  }
  return theme;
}

void my_theme_destroy(my_theme_t* theme) {
  size_t i, n;
  if (theme == NULL) {
    return;
  }
  n = my_darray_size(theme->entries);
  for (i = 0; i < n; i++) {
    my_theme_entry_t* e = (my_theme_entry_t*)my_darray_get(theme->entries, i);
    my_style_reset(&e->style);
    my_mem_free(theme->allocator, e);
  }
  my_darray_destroy(theme->entries);
  my_mem_free(theme->allocator, theme);
}

static my_theme_entry_t* theme_find_entry(my_theme_t* theme, const char* type,
                                          const char* name, bool create) {
  size_t i, n = my_darray_size(theme->entries);
  const char* nm = name != NULL ? name : "";
  for (i = 0; i < n; i++) {
    my_theme_entry_t* e = (my_theme_entry_t*)my_darray_get(theme->entries, i);
    if (my_str_eq(e->widget_type, type) && my_str_eq(e->name, nm)) {
      return e;
    }
  }
  if (!create) {
    return NULL;
  }
  {
    my_theme_entry_t* e =
        (my_theme_entry_t*)my_mem_calloc(theme->allocator, 1, sizeof(my_theme_entry_t));
    if (e == NULL) {
      return NULL;
    }
    strncpy(e->widget_type, type, MY_THEME_TYPE_LEN - 1);
    strncpy(e->name, nm, MY_THEME_NAME_LEN - 1);
    my_style_init(&e->style, theme->allocator);
    if (my_darray_push(theme->entries, e) != MY_RET_OK) {
      my_mem_free(theme->allocator, e);
      return NULL;
    }
    return e;
  }
}

my_ret_t my_theme_set(my_theme_t* theme, const char* widget_type, const char* name,
                      my_widget_state_t state, const char* key,
                      const my_value_t* value) {
  my_theme_entry_t* e;
  if (theme == NULL || widget_type == NULL || key == NULL || value == NULL ||
      strlen(widget_type) >= MY_THEME_TYPE_LEN ||
      (name != NULL && strlen(name) >= MY_THEME_NAME_LEN)) {
    return MY_RET_INVALID_PARAMS;
  }
  e = theme_find_entry(theme, widget_type, name, true);
  if (e == NULL) {
    return MY_RET_OOM;
  }
  return my_style_set(&e->style, state, key, value);
}

my_ret_t my_theme_set_color(my_theme_t* theme, const char* widget_type,
                            const char* name, my_widget_state_t state,
                            const char* key, uint32_t rgba) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_uint32(&v, rgba);
  return my_theme_set(theme, widget_type, name, state, key, &v);
}

my_ret_t my_theme_set_int(my_theme_t* theme, const char* widget_type,
                          const char* name, my_widget_state_t state,
                          const char* key, int32_t value) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_int32(&v, value);
  return my_theme_set(theme, widget_type, name, state, key, &v);
}

const my_value_t* my_theme_get(const my_theme_t* theme, const char* widget_type,
                               const char* name, my_widget_state_t state,
                               const char* key) {
  const my_theme_entry_t* e;
  const my_value_t* v = NULL;
  if (theme == NULL || widget_type == NULL || key == NULL) {
    return NULL;
  }
  if (name != NULL && *name != '\0') {
    e = theme_find_entry((my_theme_t*)theme, widget_type, name, false);
    if (e != NULL) {
      v = my_style_get(&e->style, state, key);
      if (v != NULL) {
        return v;
      }
    }
  }
  e = theme_find_entry((my_theme_t*)theme, widget_type, "", false);
  if (e != NULL) {
    v = my_style_get(&e->style, state, key);
  }
  return v;
}

/* ---------------- default theme ---------------- */

my_theme_t* my_theme_default_create(const my_allocator_t* allocator) {
  my_theme_t* t = my_theme_create(allocator);
  if (t == NULL) {
    return NULL;
  }
  my_theme_set_color(t, "window", NULL, MY_STATE_NORMAL, "bg_color", 0xF5F5F5FF);

  my_theme_set_color(t, "button", NULL, MY_STATE_NORMAL, "bg_color", 0xE0E0E0FF);
  my_theme_set_color(t, "button", NULL, MY_STATE_HOVER, "bg_color", 0xEEEEEEFF);
  my_theme_set_color(t, "button", NULL, MY_STATE_PRESSED, "bg_color", 0xBDBDBDFF);
  my_theme_set_color(t, "button", NULL, MY_STATE_DISABLED, "bg_color", 0xCFCFCFFF);
  my_theme_set_color(t, "button", NULL, MY_STATE_NORMAL, "border_color", 0x9E9E9EFF);
  my_theme_set_color(t, "button", NULL, MY_STATE_NORMAL, "fg_color", 0x212121FF);
  my_theme_set_int(t, "button", NULL, MY_STATE_NORMAL, "round_radius", 4);

  my_theme_set_color(t, "label", NULL, MY_STATE_NORMAL, "bg_color", 0xF5F5F5FF);
  my_theme_set_color(t, "label", NULL, MY_STATE_NORMAL, "fg_color", 0x212121FF);
  return t;
}

/* ---------------- text loader ---------------- */

static my_ret_t parse_state(const char* s, size_t len, my_widget_state_t* out) {
  static const char* NAMES[] = {"normal", "hover", "pressed", "disabled"};
  size_t i;
  for (i = 0; i < 4; i++) {
    if (strlen(NAMES[i]) == len && strncmp(s, NAMES[i], len) == 0) {
      *out = (my_widget_state_t)i;
      return MY_RET_OK;
    }
  }
  return MY_RET_INVALID_PARAMS;
}

static my_ret_t parse_value(const char* s, my_value_t* out) {
  char* end = NULL;
  if (*s == '#') {
    unsigned long v = strtoul(s + 1, &end, 16);
    size_t digits = (size_t)(end - (s + 1));
    uint32_t rgba;
    if (end == s + 1 || *end != '\0' || (digits != 6 && digits != 8)) {
      return MY_RET_INVALID_PARAMS;
    }
    if (digits == 6) {
      rgba = ((uint32_t)v << 8) | 0xFFu; /* #RRGGBB -> opaque */
    } else {
      rgba = (uint32_t)v; /* #RRGGBBAA */
    }
    my_value_set_uint32(out, rgba);
    return MY_RET_OK;
  }
  {
    double d = strtod(s, &end);
    if (end == s || *end != '\0') {
      /* not numeric: store as string */
      return my_value_set_str(out, s);
    }
    if (strchr(s, '.') != NULL) {
      my_value_set_double(out, d);
    } else {
      my_value_set_int32(out, (int32_t)d);
    }
    return MY_RET_OK;
  }
}

static my_ret_t theme_load_line(my_theme_t* theme, const char* line, size_t len) {
  char buf[128];
  char* dot1;
  char* dot_last;
  char* eq;
  char* bracket;
  char type[MY_THEME_TYPE_LEN];
  char name[MY_THEME_NAME_LEN];
  const char* key;
  my_widget_state_t state = MY_STATE_NORMAL;
  bool all_states = false;
  my_value_t v;
  my_ret_t ret;

  if (len == 0 || len >= sizeof(buf)) {
    return len == 0 ? MY_RET_OK : MY_RET_INVALID_PARAMS;
  }
  memcpy(buf, line, len);
  buf[len] = '\0';

  eq = strchr(buf, '=');
  dot1 = strchr(buf, '.');
  dot_last = strrchr(buf, '.');
  if (eq == NULL || dot_last == NULL || dot_last > eq) {
    return MY_RET_INVALID_PARAMS;
  }
  *eq = '\0';
  key = dot_last + 1;
  *dot_last = '\0';
  if (*key == '\0') {
    return MY_RET_INVALID_PARAMS;
  }

  /* selector part before the last dot may still contain ".state" */
  bracket = strchr(buf, '[');
  if (bracket != NULL) {
    char* close = strchr(bracket, ']');
    if (close == NULL || close == bracket + 1 || close - bracket - 1 >= MY_THEME_NAME_LEN) {
      return MY_RET_INVALID_PARAMS;
    }
    memcpy(name, bracket + 1, (size_t)(close - bracket - 1));
    name[close - bracket - 1] = '\0';
    *bracket = '\0';
  } else {
    name[0] = '\0';
  }

  /* buf is now "type" or "type.state" (dot1 points into buf if present) */
  if (dot1 != NULL && dot1 < dot_last) {
    *dot1 = '\0';
    if (parse_state(dot1 + 1, strlen(dot1 + 1), &state) != MY_RET_OK) {
      return MY_RET_INVALID_PARAMS;
    }
  } else {
    all_states = true;
  }
  if (strlen(buf) >= MY_THEME_TYPE_LEN || *buf == '\0') {
    return MY_RET_INVALID_PARAMS;
  }
  strncpy(type, buf, MY_THEME_TYPE_LEN - 1);
  type[MY_THEME_TYPE_LEN - 1] = '\0';

  my_value_init(&v, theme->allocator);
  ret = parse_value(eq + 1, &v);
  if (ret == MY_RET_OK) {
    if (all_states) {
      int i;
      for (i = 0; i < (int)MY_STATE_COUNT && ret == MY_RET_OK; i++) {
        ret = my_theme_set(theme, type, name, (my_widget_state_t)i, key, &v);
      }
    } else {
      ret = my_theme_set(theme, type, name, state, key, &v);
    }
  }
  my_value_reset(&v);
  return ret;
}

my_ret_t my_theme_load_str(my_theme_t* theme, const char* str) {
  const char* cur;
  if (theme == NULL || str == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  cur = str;
  while (*cur != '\0') {
    const char* eol = strchr(cur, '\n');
    size_t len = eol != NULL ? (size_t)(eol - cur) : strlen(cur);
    /* skip blank lines and ';' comments */
    while (len > 0 && (*cur == ' ' || *cur == '\t' || *cur == '\r')) {
      cur++;
      len--;
    }
    if (len > 0 && *cur != ';') {
      if (theme_load_line(theme, cur, len) != MY_RET_OK) {
        return MY_RET_INVALID_PARAMS;
      }
    }
    if (eol == NULL) {
      break;
    }
    cur = eol + 1;
  }
  return MY_RET_OK;
}

/* ---------------- widget style resolution (declared in my_widget.h) ---- */

my_ret_t my_widget_style_set(my_widget_t* widget, my_widget_state_t state,
                             const char* key, const my_value_t* value) {
  if (widget == NULL || key == NULL || value == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  if (widget->local_style == NULL) {
    widget->local_style =
        (my_style_t*)my_mem_calloc(((my_object_t*)widget)->allocator, 1,
                                   sizeof(my_style_t));
    if (widget->local_style == NULL) {
      return MY_RET_OOM;
    }
    my_style_init(widget->local_style, ((my_object_t*)widget)->allocator);
  }
  return my_style_set(widget->local_style, state, key, value);
}

const my_value_t* my_widget_style_get(my_widget_t* widget,
                                      my_widget_state_t state, const char* key) {
  my_widget_t* w;
  const my_value_t* v;
  if (widget == NULL || key == NULL) {
    return NULL;
  }
  if (widget->local_style != NULL) {
    v = my_style_get(widget->local_style, state, key);
    if (v != NULL) {
      return v;
    }
  }
  /* climb to the nearest themed ancestor */
  w = widget;
  while (w != NULL) {
    if (w->theme != NULL) {
      return my_theme_get(w->theme, widget->widget_type,
                          ((my_object_t*)widget)->name, state, key);
    }
    w = w->parent;
  }
  return NULL;
}

uint32_t my_widget_style_get_color(my_widget_t* widget, my_widget_state_t state,
                                   const char* key, uint32_t fallback) {
  const my_value_t* v = my_widget_style_get(widget, state, key);
  return v != NULL && v->type == MY_VALUE_UINT32 ? my_value_get_uint32(v)
                                                 : fallback;
}

int32_t my_widget_style_get_int(my_widget_t* widget, my_widget_state_t state,
                                const char* key, int32_t fallback) {
  const my_value_t* v = my_widget_style_get(widget, state, key);
  if (v == NULL) {
    return fallback;
  }
  if (v->type == MY_VALUE_INT32) {
    return my_value_get_int32(v);
  }
  if (v->type == MY_VALUE_DOUBLE) {
    return (int32_t)my_value_get_double(v);
  }
  return fallback;
}

static void invalidate_tree(my_widget_t* widget) {
  size_t i, n;
  my_widget_invalidate(widget, NULL);
  n = my_widget_child_count(widget);
  for (i = 0; i < n; i++) {
    invalidate_tree(my_widget_get_child(widget, i));
  }
}

my_ret_t my_widget_apply_theme(my_widget_t* widget, my_theme_t* theme) {
  if (widget == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  widget->theme = theme;
  invalidate_tree(widget);
  return MY_RET_OK;
}
