/**
 * @file my_ui_loader.c
 * @brief XML UI loader.
 */
#include "myui/my_ui_loader.h"

#ifdef MYUI_UI_XML

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "myc/my_str.h"
#include "myui/my_layout.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_checkbox.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_progress_bar.h"
#include "myui/widgets/my_slider.h"

/* ---------------- factory registry ---------------- */

#define MY_UI_MAX_FACTORIES 32

typedef struct ui_factory_entry_t {
  char tag[24];
  my_ui_factory_fn_t factory;
} ui_factory_entry_t;

static ui_factory_entry_t g_factories[MY_UI_MAX_FACTORIES];
static size_t g_factory_count = 0;

my_ret_t my_ui_loader_register(const char* tag, my_ui_factory_fn_t factory) {
  size_t i;
  if (tag == NULL || factory == NULL || strlen(tag) >= 24) {
    return MY_RET_INVALID_PARAMS;
  }
  for (i = 0; i < g_factory_count; i++) {
    if (my_str_eq(g_factories[i].tag, tag)) {
      g_factories[i].factory = factory;
      return MY_RET_OK;
    }
  }
  if (g_factory_count >= MY_UI_MAX_FACTORIES) {
    return MY_RET_OOM;
  }
  strncpy(g_factories[g_factory_count].tag, tag, 23);
  g_factories[g_factory_count].factory = factory;
  g_factory_count++;
  return MY_RET_OK;
}

static my_ui_factory_fn_t find_factory(const char* tag) {
  size_t i;
  for (i = 0; i < g_factory_count; i++) {
    if (my_str_eq(g_factories[i].tag, tag)) {
      return g_factories[i].factory;
    }
  }
  return NULL;
}

/* ---------------- attribute helpers ---------------- */

static int32_t attr_int(const my_xml_node_t* node, const char* name,
                        int32_t fallback) {
  const char* s = my_xml_node_attr(node, name);
  return s != NULL ? (int32_t)strtol(s, NULL, 10) : fallback;
}

static float attr_float(const my_xml_node_t* node, const char* name,
                        float fallback) {
  const char* s = my_xml_node_attr(node, name);
  return s != NULL ? strtof(s, NULL) : fallback;
}

static bool attr_bool(const my_xml_node_t* node, const char* name,
                      bool fallback) {
  const char* s = my_xml_node_attr(node, name);
  if (s == NULL) {
    return fallback;
  }
  return my_str_eq(s, "true") || my_str_eq(s, "1");
}

/* ---------------- built-in factories ---------------- */

static my_widget_t* make_widget(const my_allocator_t* a,
                                const my_xml_node_t* n) {
  (void)n;
  return my_widget_create(a, "container");
}

static my_widget_t* make_button(const my_allocator_t* a,
                                const my_xml_node_t* n) {
  return my_button_create(a, my_xml_node_attr(n, "text"));
}

static my_widget_t* make_label(const my_allocator_t* a,
                               const my_xml_node_t* n) {
  return my_label_create(a, my_xml_node_attr(n, "text"));
}

static my_widget_t* make_edit(const my_allocator_t* a, const my_xml_node_t* n) {
  my_widget_t* w = my_edit_create(a);
  const char* hint;
  if (w == NULL) {
    return NULL;
  }
  hint = my_xml_node_attr(n, "hint");
  if (hint != NULL) {
    my_edit_set_hint(w, hint);
  }
  if (attr_bool(n, "password", false)) {
    my_edit_set_password(w, true);
  }
  if (attr_bool(n, "readonly", false)) {
    my_edit_set_readonly(w, true);
  }
  if (my_xml_node_attr(n, "max_len") != NULL) {
    my_edit_set_max_len(w, (size_t)attr_int(n, "max_len", 0));
  }
  if (my_xml_node_attr(n, "text") != NULL) {
    my_edit_set_text(w, my_xml_node_attr(n, "text"));
  }
  return w;
}

static my_widget_t* make_checkbox(const my_allocator_t* a,
                                  const my_xml_node_t* n) {
  my_widget_t* w = my_checkbox_create(a, my_xml_node_attr(n, "text"));
  if (w != NULL && attr_bool(n, "checked", false)) {
    my_checkbox_set_checked(w, true);
  }
  return w;
}

static my_widget_t* make_slider(const my_allocator_t* a,
                                const my_xml_node_t* n) {
  my_widget_t* w = my_slider_create(a);
  if (w == NULL) {
    return NULL;
  }
  my_slider_set_range(w, attr_float(n, "min", 0.0f),
                      attr_float(n, "max", 100.0f));
  my_slider_set_step(w, attr_float(n, "step", 0.0f));
  my_slider_set_value(w, attr_float(n, "value", 0.0f));
  return w;
}

static my_widget_t* make_progress(const my_allocator_t* a,
                                  const my_xml_node_t* n) {
  my_widget_t* w = my_progress_bar_create(a);
  if (w != NULL) {
    my_progress_bar_set_value(w, attr_float(n, "value", 0.0f));
  }
  return w;
}

static void register_builtins(void) {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  my_ui_loader_register("widget", make_widget);
  my_ui_loader_register("button", make_button);
  my_ui_loader_register("label", make_label);
  my_ui_loader_register("edit", make_edit);
  my_ui_loader_register("checkbox", make_checkbox);
  my_ui_loader_register("slider", make_slider);
  my_ui_loader_register("progress_bar", make_progress);
}

/* ---------------- generic attribute application ---------------- */

static my_ret_t apply_common(my_widget_t* widget, const my_xml_node_t* node,
                             my_ui_error_t* err) {
  const char* name = my_xml_node_attr(node, "name");
  const char* lp = my_xml_node_attr(node, "lp");
  const char* layout = my_xml_node_attr(node, "layout");
  size_t i;
  char rules[512];
  size_t rules_len = 0;

  if (name != NULL) {
    my_widget_set_name(widget, name);
  }
  my_widget_set_rect(widget, &(my_rect_t){attr_int(node, "x", 0),
                                          attr_int(node, "y", 0),
                                          attr_int(node, "w", 0),
                                          attr_int(node, "h", 0)});
  if (my_xml_node_attr(node, "visible") != NULL) {
    my_widget_set_visible(widget, attr_bool(node, "visible", true));
  }
  if (my_xml_node_attr(node, "enable") != NULL) {
    widget->enable = attr_bool(node, "enable", true);
  }
  if (lp != NULL &&
      my_widget_set_layout_params(widget, lp) != MY_RET_OK) {
    if (err != NULL) {
      err->line = node->line;
      snprintf(err->message, sizeof(err->message), "bad lp: %s", lp);
    }
    return MY_RET_FAIL;
  }
  if (layout != NULL && strncmp(layout, "linear:", 7) == 0) {
    bool horizontal = layout[7] == 'h';
    int32_t spacing = 0;
    const char* colon = strchr(layout + 7, ':');
    if (colon != NULL) {
      spacing = (int32_t)strtol(colon + 1, NULL, 10);
    }
    my_widget_set_layouter(widget,
                           my_layouter_linear_create(NULL, horizontal, spacing));
  }

  /* collect v:* attributes into bind_rules (";" separated) */
  rules[0] = '\0';
  for (i = 0; i < node->attr_count; i++) {
    const char* an = node->attrs[i].name;
    const char* av = node->attrs[i].value;
    size_t need;
    if (strncmp(an, "v:", 2) != 0) {
      continue;
    }
    need = strlen(an) + 1 + strlen(av) + 1; /* "name=value;" */
    if (rules_len + need >= sizeof(rules)) {
      if (err != NULL) {
        err->line = node->line;
        snprintf(err->message, sizeof(err->message), "bind rules too long");
      }
      return MY_RET_FAIL;
    }
    rules_len += (size_t)snprintf(rules + rules_len, sizeof(rules) - rules_len,
                                  "%s=%s;", an, av);
  }
  if (rules_len > 0) {
    my_widget_set_bind_rules(widget, rules);
  }
  return MY_RET_OK;
}

/* ---------------- recursive build ---------------- */

static my_widget_t* build_node(const my_allocator_t* allocator, my_pal_t* pal,
                               const my_xml_node_t* node, my_ui_error_t* err);

static my_widget_t* build_children(const my_allocator_t* allocator,
                                   my_pal_t* pal, my_widget_t* parent,
                                   const my_xml_node_t* node,
                                   my_ui_error_t* err) {
  size_t i;
  for (i = 0; i < node->child_count; i++) {
    const my_xml_node_t* child = my_xml_node_child(node, i);
    my_widget_t* w;
    if (my_str_eq(child->name, "style")) {
      continue; /* handled at window level */
    }
    w = build_node(allocator, pal, child, err);
    if (w == NULL) {
      return NULL;
    }
    my_widget_add_child(parent, w);
    my_widget_unref(w);
  }
  return parent;
}

static my_widget_t* build_node(const my_allocator_t* allocator, my_pal_t* pal,
                               const my_xml_node_t* node, my_ui_error_t* err) {
  my_widget_t* widget;
  my_ui_factory_fn_t factory;
  (void)pal;

  if (my_str_eq(node->name, "window")) {
    if (err != NULL) {
      err->line = node->line;
      snprintf(err->message, sizeof(err->message),
               "<window> only allowed as root");
    }
    return NULL;
  }
  factory = find_factory(node->name);
  if (factory == NULL) {
    if (err != NULL) {
      err->line = node->line;
      snprintf(err->message, sizeof(err->message), "unknown tag <%s>",
               node->name);
    }
    return NULL;
  }
  widget = factory(allocator, node);
  if (widget == NULL) {
    return NULL;
  }
  if (apply_common(widget, node, err) != MY_RET_OK ||
      build_children(allocator, pal, widget, node, err) == NULL) {
    my_widget_unref(widget);
    return NULL;
  }
  return widget;
}

static void apply_style_children(my_window_t* win, const my_xml_node_t* root) {
  size_t i;
  for (i = 0; i < root->child_count; i++) {
    const my_xml_node_t* child = my_xml_node_child(root, i);
    if (my_str_eq(child->name, "style") && child->text != NULL &&
        win->theme != NULL) {
      my_theme_load_str(win->theme, child->text);
    }
  }
}

my_widget_t* my_ui_load_str(const my_allocator_t* allocator, my_pal_t* pal,
                            const char* xml_str, my_ui_error_t* err) {
  my_xml_doc_t* doc;
  my_xml_error_t xerr;
  my_widget_t* result = NULL;

  register_builtins();
  if (xml_str == NULL) {
    return NULL;
  }
  doc = my_xml_parse(allocator, xml_str, &xerr);
  if (doc == NULL) {
    if (err != NULL) {
      err->line = xerr.line;
      snprintf(err->message, sizeof(err->message), "xml: %s (col %d)",
               xerr.message, xerr.col);
    }
    return NULL;
  }

  if (my_str_eq(doc->root->name, "window")) {
    my_window_t* win;
    if (pal == NULL) {
      if (err != NULL) {
        err->line = doc->root->line;
        snprintf(err->message, sizeof(err->message),
                 "<window> root requires a pal");
      }
      my_xml_doc_destroy(doc);
      return NULL;
    }
    win = my_window_create(allocator, pal, attr_int(doc->root, "w", 640),
                           attr_int(doc->root, "h", 480),
                           my_xml_node_attr(doc->root, "title"));
    if (win != NULL) {
      if (apply_common((my_widget_t*)win, doc->root, err) != MY_RET_OK ||
          build_children(allocator, pal, (my_widget_t*)win, doc->root, err) ==
              NULL) {
        my_widget_unref((my_widget_t*)win);
        win = NULL;
      } else {
        apply_style_children(win, doc->root);
      }
    }
    result = (my_widget_t*)win;
  } else {
    result = build_node(allocator, pal, doc->root, err);
  }
  my_xml_doc_destroy(doc);
  return result;
}

my_widget_t* my_ui_load_file(const my_allocator_t* allocator, my_pal_t* pal,
                             const char* path, my_ui_error_t* err) {
  FILE* f;
  long size;
  char* buf;
  my_widget_t* result;
  if (path == NULL) {
    return NULL;
  }
  f = fopen(path, "rb");
  if (f == NULL) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = (char*)my_mem_alloc(allocator, (size_t)size + 1);
  if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
    fclose(f);
    my_mem_free(allocator, buf);
    return NULL;
  }
  fclose(f);
  buf[size] = '\0';
  result = my_ui_load_str(allocator, pal, buf, err);
  my_mem_free(allocator, buf);
  return result;
}

#else /* !MYUI_UI_XML */

my_ret_t my_ui_loader_register(const char* tag, my_ui_factory_fn_t factory) {
  (void)tag;
  (void)factory;
  return MY_RET_NOT_SUPPORTED;
}

my_widget_t* my_ui_load_str(const my_allocator_t* allocator, my_pal_t* pal,
                            const char* xml_str, my_ui_error_t* err) {
  (void)allocator;
  (void)pal;
  (void)xml_str;
  (void)err;
  return NULL;
}

my_widget_t* my_ui_load_file(const my_allocator_t* allocator, my_pal_t* pal,
                             const char* path, my_ui_error_t* err) {
  (void)allocator;
  (void)pal;
  (void)path;
  (void)err;
  return NULL;
}

#endif /* MYUI_UI_XML */
