/**
 * @file ui2c.c
 * @brief XML -> C UI generator: turns an XML UI file into a C builder
 * function (zero runtime parsing; the embedded counterpart of
 * my_ui_loader for MYUI_UI_XML=OFF builds).
 *
 * Usage: ui2c <input.xml> [function_name]
 * Output goes to stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "myui/my_xml.h"

static FILE* out;

/** @brief Escape a string for embedding in a C string literal. */
static void c_str(FILE* f, const char* s) {
  fputc('"', f);
  while (*s != '\0') {
    switch (*s) {
      case '\\':
        fputs("\\\\", f);
        break;
      case '"':
        fputs("\\\"", f);
        break;
      case '\n':
        fputs("\\n", f);
        break;
      case '\r':
        fputs("\\r", f);
        break;
      case '\t':
        fputs("\\t", f);
        break;
      default:
        fputc(*s, f);
        break;
    }
    s++;
  }
  fputc('"', f);
}

static const char* attr(const my_xml_node_t* n, const char* name) {
  return my_xml_node_attr(n, name);
}

static bool has_attr(const my_xml_node_t* n, const char* name) {
  return attr(n, name) != NULL;
}

/** @brief The widget factory call for a tag (matches my_ui_loader). */
static const char* factory_for(const my_xml_node_t* n) {
  const char* tag = n->name;
  if (strcmp(tag, "widget") == 0) {
    return "my_widget_create(a, \"container\")";
  }
  return NULL; /* complex factories are emitted manually */
}

static void emit_create(const my_xml_node_t* n, int idx) {
  const char* tag = n->name;
  fprintf(out, "  w%d = ", idx);
  if (strcmp(tag, "button") == 0) {
    fprintf(out, "my_button_create(a, NULL);\n");
  } else if (strcmp(tag, "label") == 0) {
    fprintf(out, "my_label_create(a, NULL);\n");
  } else if (strcmp(tag, "edit") == 0) {
    fprintf(out, "my_edit_create(a);\n");
  } else if (strcmp(tag, "checkbox") == 0) {
    if (attr(n, "text") != NULL) {
      fprintf(out, "my_checkbox_create(a, ");
      c_str(out, attr(n, "text"));
      fprintf(out, ");\n");
    } else {
      fprintf(out, "my_checkbox_create(a, NULL);\n");
    }
  } else if (strcmp(tag, "slider") == 0) {
    fprintf(out, "my_slider_create(a);\n");
  } else if (strcmp(tag, "progress_bar") == 0) {
    fprintf(out, "my_progress_bar_create(a);\n");
  } else if (strcmp(tag, "list_view") == 0) {
    fprintf(out, "my_list_view_create(a);\n");
  } else if (strcmp(tag, "image") == 0) {
    fprintf(out, "my_image_create(a);\n");
  } else if (strcmp(tag, "scroll_bar") == 0) {
    fprintf(out, "my_scroll_bar_create(a);\n");
  } else if (strcmp(tag, "text_area") == 0) {
    fprintf(out, "my_text_area_create(a);\n");
  } else {
    fprintf(out, "my_widget_create(a, \"container\");\n");
  }
}

static void emit_tag_specific(const my_xml_node_t* n, int idx) {
  const char* tag = n->name;
  if (strcmp(tag, "button") == 0 && attr(n, "text") != NULL) {
    fprintf(out, "  my_button_set_text(w%d, ", idx);
    c_str(out, attr(n, "text"));
    fprintf(out, ");\n");
  } else if (strcmp(tag, "label") == 0 && attr(n, "text") != NULL) {
    fprintf(out, "  my_label_set_text(w%d, ", idx);
    c_str(out, attr(n, "text"));
    fprintf(out, ");\n");
  } else if (strcmp(tag, "edit") == 0) {
    if (attr(n, "hint") != NULL) {
      fprintf(out, "  my_edit_set_hint(w%d, ", idx);
      c_str(out, attr(n, "hint"));
      fprintf(out, ");\n");
    }
    if (attr(n, "text") != NULL) {
      fprintf(out, "  my_edit_set_text(w%d, ", idx);
      c_str(out, attr(n, "text"));
      fprintf(out, ");\n");
    }
    if (attr(n, "password") != NULL) {
      fprintf(out, "  my_edit_set_password(w%d, true);\n", idx);
    }
    if (attr(n, "max_len") != NULL) {
      fprintf(out, "  my_edit_set_max_len(w%d, %s);\n", idx, attr(n, "max_len"));
    }
  } else if (strcmp(tag, "checkbox") == 0) {
    if (attr(n, "checked") != NULL) {
      fprintf(out, "  my_checkbox_set_checked(w%d, true);\n", idx);
    }
  } else if (strcmp(tag, "slider") == 0) {
    if (attr(n, "min") != NULL || attr(n, "max") != NULL) {
      fprintf(out, "  my_slider_set_range(w%d, %s, %s);\n", idx,
              attr(n, "min") != NULL ? attr(n, "min") : "0.0f",
              attr(n, "max") != NULL ? attr(n, "max") : "100.0f");
    }
    if (attr(n, "step") != NULL) {
      fprintf(out, "  my_slider_set_step(w%d, %s);\n", idx, attr(n, "step"));
    }
    if (attr(n, "value") != NULL) {
      fprintf(out, "  my_slider_set_value(w%d, %s);\n", idx, attr(n, "value"));
    }
  } else if (strcmp(tag, "progress_bar") == 0 && attr(n, "value") != NULL) {
    fprintf(out, "  my_progress_bar_set_value(w%d, %s);\n", idx,
            attr(n, "value"));
  } else if (strcmp(tag, "list_view") == 0 && attr(n, "row_height") != NULL) {
    fprintf(out, "  my_list_view_set_row_height(w%d, %s);\n", idx,
            attr(n, "row_height"));
  } else if (strcmp(tag, "image") == 0 && attr(n, "src") != NULL) {
    fprintf(out, "  my_image_set_image(w%d, ", idx);
    c_str(out, attr(n, "src"));
    fprintf(out, ");\n");
  } else if (strcmp(tag, "text_area") == 0) {
    if (attr(n, "hint") != NULL) {
      fprintf(out, "  my_text_area_set_hint(w%d, ", idx);
      c_str(out, attr(n, "hint"));
      fprintf(out, ");\n");
    }
    if (attr(n, "text") != NULL) {
      fprintf(out, "  my_text_area_set_text(w%d, ", idx);
      c_str(out, attr(n, "text"));
      fprintf(out, ");\n");
    }
  }
}

static void emit_common(const my_xml_node_t* n, int idx) {
  if (attr(n, "name") != NULL) {
    fprintf(out, "  my_widget_set_name(w%d, ", idx);
    c_str(out, attr(n, "name"));
    fprintf(out, ");\n");
  }
  fprintf(out, "  my_widget_set_rect(w%d, &(my_rect_t){%s, %s, %s, %s});\n",
          idx, attr(n, "x") != NULL ? attr(n, "x") : "0",
          attr(n, "y") != NULL ? attr(n, "y") : "0",
          attr(n, "w") != NULL ? attr(n, "w") : "0",
          attr(n, "h") != NULL ? attr(n, "h") : "0");
  if (has_attr(n, "visible")) {
    fprintf(out, "  my_widget_set_visible(w%d, %s);\n", idx,
            strcmp(attr(n, "visible"), "false") == 0 ? "false" : "true");
  }
  if (attr(n, "lp") != NULL) {
    fprintf(out, "  my_widget_set_layout_params(w%d, ", idx);
    c_str(out, attr(n, "lp"));
    fprintf(out, ");\n");
  }
  if (attr(n, "layout") != NULL && strncmp(attr(n, "layout"), "linear:", 7) == 0) {
    const char* spec = attr(n, "layout") + 7;
    const char* colon = strchr(spec, ':');
    fprintf(out, "  my_widget_set_layouter(w%d, my_layouter_linear_create(a, %s, %s));\n",
            idx, spec[0] == 'h' ? "true" : "false",
            colon != NULL ? colon + 1 : "0");
  }
}

/** @brief Emit bind_rules covering ALL v:* attributes in one literal. */
static void emit_bind_rules(const my_xml_node_t* n, int idx) {
  size_t i;
  bool any = false;
  for (i = 0; i < n->attr_count; i++) {
    if (strncmp(n->attrs[i].name, "v:", 2) == 0) {
      any = true;
      break;
    }
  }
  if (!any) {
    return;
  }
  fprintf(out, "  my_widget_set_bind_rules(w%d, ", idx);
  for (i = 0; i < n->attr_count; i++) {
    if (strncmp(n->attrs[i].name, "v:", 2) == 0) {
      fprintf(out, "\"%s=%s;\"", n->attrs[i].name, n->attrs[i].value);
    }
  }
  fprintf(out, ");\n");
}

/** @brief Emit node idx and all descendants; returns the next free index. */
static int emit_node(const my_xml_node_t* n, int idx, int parent) {
  size_t i;
  int next = idx + 1;
  (void)factory_for;
  emit_create(n, idx);
  fprintf(out, "  if (w%d == NULL) goto fail;\n", idx);
  emit_tag_specific(n, idx);
  emit_common(n, idx);
  emit_bind_rules(n, idx);
  if (parent >= 0) {
    fprintf(out, "  my_widget_add_child(w%d, w%d);\n", parent, idx);
    fprintf(out, "  my_widget_unref(w%d);\n", idx);
  }
  for (i = 0; i < n->child_count; i++) {
    const my_xml_node_t* c = my_xml_node_child(n, i);
    if (strcmp(c->name, "style") != 0) {
      next = emit_node(c, next, idx);
    }
  }
  return next;
}

static void emit_styles(const my_xml_node_t* root) {
  size_t i;
  for (i = 0; i < root->child_count; i++) {
    const my_xml_node_t* c = my_xml_node_child(root, i);
    if (strcmp(c->name, "style") == 0 && c->text != NULL) {
      fprintf(out, "  my_theme_load_str(((my_window_t*)w0)->theme, ");
      c_str(out, c->text);
      fprintf(out, ");\n");
    }
  }
}

static const char* REQUIRED_HEADERS =
    "#include \"myui/my_window.h\"\n"
    "#include \"myui/my_layout.h\"\n"
    "#include \"myui/widgets/my_button.h\"\n"
    "#include \"myui/widgets/my_checkbox.h\"\n"
    "#include \"myui/widgets/my_edit.h\"\n"
    "#include \"myui/widgets/my_image.h\"\n"
    "#include \"myui/widgets/my_label.h\"\n"
    "#include \"myui/widgets/my_list_view.h\"\n"
    "#include \"myui/widgets/my_progress_bar.h\"\n"
    "#include \"myui/widgets/my_scroll_bar.h\"\n"
    "#include \"myui/widgets/my_slider.h\"\n"
    "#include \"myui/widgets/my_text_area.h\"\n";

int main(int argc, char** argv) {
  const char* fn = "my_page_create";
  const char* path;
  my_xml_error_t xerr;
  my_xml_doc_t* doc;
  int nvars;
  size_t i, count;

  if (argc < 2) {
    fprintf(stderr, "usage: ui2c <input.xml> [function_name]\n");
    return 1;
  }
  path = argv[1];
  if (argc > 2) {
    fn = argv[2];
  }

  {
    FILE* f = fopen(path, "rb");
    char* buf;
    long size;
    if (f == NULL) {
      fprintf(stderr, "ui2c: cannot open %s\n", path);
      return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char*)malloc((size_t)size + 1);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
      fclose(f);
      free(buf);
      return 1;
    }
    fclose(f);
    buf[size] = '\0';
    doc = my_xml_parse(NULL, buf, &xerr);
    free(buf);
  }
  if (doc == NULL) {
    fprintf(stderr, "ui2c: %s:%d:%d: %s\n", path, xerr.line, xerr.col,
            xerr.message);
    return 1;
  }

  out = stdout;
  /* count widgets for declarations */
  nvars = 0;
  count = 1; /* root */
  {
    const my_xml_node_t* stack[128];
    size_t sp = 0;
    stack[sp++] = doc->root;
    while (sp > 0) {
      const my_xml_node_t* n = stack[--sp];
      for (i = 0; i < n->child_count; i++) {
        if (strcmp(my_xml_node_child(n, i)->name, "style") != 0) {
          count++;
          if (sp < 128) {
            stack[sp++] = my_xml_node_child(n, i);
          }
        }
      }
    }
  }
  nvars = (int)count;

  fprintf(out, "/* generated by ui2c from %s, do not edit */\n", path);
  fprintf(out, "%s\n", REQUIRED_HEADERS);
  if (strcmp(doc->root->name, "window") == 0) {
    fprintf(out,
            "my_widget_t* %s(const my_allocator_t* a, my_pal_t* pal) {\n", fn);
    fprintf(out, "  my_widget_t* w0 = (my_widget_t*)my_window_create(a, pal, %s, %s, ",
            attr(doc->root, "w") != NULL ? attr(doc->root, "w") : "640",
            attr(doc->root, "h") != NULL ? attr(doc->root, "h") : "480");
    c_str(out, attr(doc->root, "title") != NULL ? attr(doc->root, "title")
                                                : "myui");
    fprintf(out, ");\n");
  } else {
    fprintf(out, "my_widget_t* %s(const my_allocator_t* a) {\n", fn);
    fprintf(out, "  my_widget_t* w0 = my_widget_create(a, \"container\");\n");
  }
  {
    int v;
    for (v = 1; v < nvars; v++) {
      fprintf(out, "  my_widget_t* w%d = NULL;\n", v);
    }
  }
  fprintf(out, "  if (w0 == NULL) return NULL;\n");
  emit_common(doc->root, 0);
  {
    size_t k = 0;
    int next = 1;
    for (k = 0; k < doc->root->child_count; k++) {
      const my_xml_node_t* c = my_xml_node_child(doc->root, k);
      if (strcmp(c->name, "style") != 0) {
        next = emit_node(c, next, 0);
      }
    }
  }
  if (strcmp(doc->root->name, "window") == 0) {
    emit_styles(doc->root);
  }
  fprintf(out, "  return w0;\n");
  fprintf(out, "fail:\n  my_widget_unref(w0);\n  return NULL;\n}\n");

  my_xml_doc_destroy(doc);
  return 0;
}
