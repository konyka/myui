/**
 * @file qx_chart.c
 * @brief Chart widget implementation (M15).
 *
 * Line mode: white card, 5 horizontal grid lines, y ticks (min/mid/max),
 * x time ticks (09:30/11:30/13:00/15:00), title top-left, polyline
 * stroked in the site red #E64C62 (2px, backend AA applies).
 * Bar mode: center axis + signed buckets (red above / green below).
 */
#include "qx_chart.h"

#include <stdio.h>

#include "../dxx_theme.h"

#define CHART_PAD_L 36
#define CHART_PAD_B 20
#define CHART_PAD_T 24
#define CHART_PAD_R 8
#define CHART_GRID_LINES 5

typedef struct dxx_chart_t {
  my_widget_t base;
  dxx_chart_mode_t mode;
  char name[32];
  const float* points; /**< borrowed */
  int count;
  float ymin;
  float ymax;
} dxx_chart_t;

static void chart_grid(my_widget_t* widget, my_vgcanvas_t* vg, int32_t plot_w,
                       int32_t plot_h) {
  int i;
  (void)widget;
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(DXX_COLOR_LINE));
  for (i = 0; i < CHART_GRID_LINES; i++) {
    float y = (float)CHART_PAD_T +
              (float)plot_h * (float)i / (float)(CHART_GRID_LINES - 1);
    my_vgcanvas_fill_rect(vg, &(my_rectf_t){(float)CHART_PAD_L, y,
                                            (float)plot_w, 1});
  }
}

static void chart_line_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  dxx_chart_t* c = (dxx_chart_t*)widget;
  int32_t plot_w = widget->rect.w - CHART_PAD_L - CHART_PAD_R;
  int32_t plot_h = widget->rect.h - CHART_PAD_T - CHART_PAD_B;
  char buf[24];
  int i;
  /* title */
  my_vgcanvas_set_font(vg, NULL, 12);
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(DXX_COLOR_TEXT));
  my_vgcanvas_draw_text(vg, c->name, CHART_PAD_L, 6);
  if (plot_w <= 0 || plot_h <= 0) {
    return;
  }
  chart_grid(widget, vg, plot_w, plot_h);
  /* y ticks: min / mid / max */
  my_vgcanvas_set_font(vg, NULL, 10);
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(DXX_COLOR_MUTED));
  snprintf(buf, sizeof(buf), "%.0f", (double)c->ymax);
  my_vgcanvas_draw_text(vg, buf, 2, CHART_PAD_T - 5);
  snprintf(buf, sizeof(buf), "%.0f", (double)((c->ymin + c->ymax) / 2.0f));
  my_vgcanvas_draw_text(vg, buf, 2, CHART_PAD_T + plot_h / 2 - 5);
  snprintf(buf, sizeof(buf), "%.0f", (double)c->ymin);
  my_vgcanvas_draw_text(vg, buf, 2, CHART_PAD_T + plot_h - 5);
  /* x time ticks */
  {
    static const char* const T[] = {"09:30", "11:30", "13:00", "15:00"};
    for (i = 0; i < 4; i++) {
      float x = (float)CHART_PAD_L + (float)plot_w * (float)i / 3.0f - 14.0f;
      my_vgcanvas_draw_text(vg, T[i], x,
                            (float)(CHART_PAD_T + plot_h + 5));
    }
  }
  /* polyline */
  if (c->points != NULL && c->count > 0 && c->ymax > c->ymin) {
    float span = c->ymax - c->ymin;
    my_vgcanvas_set_stroke_color(vg,
                                 my_color_from_rgba32(DXX_COLOR_PRIMARY));
    my_vgcanvas_set_line_width(vg, 2);
    my_vgcanvas_begin_path(vg);
    for (i = 0; i < c->count; i++) {
      float x = (float)CHART_PAD_L +
                (c->count > 1
                     ? (float)plot_w * (float)i / (float)(c->count - 1)
                     : (float)plot_w / 2.0f);
      float v = c->points[i];
      float y;
      if (v < c->ymin) {
        v = c->ymin;
      }
      if (v > c->ymax) {
        v = c->ymax;
      }
      y = (float)CHART_PAD_T +
          (float)plot_h * (1.0f - (v - c->ymin) / span);
      if (i == 0) {
        my_vgcanvas_move_to(vg, x, y);
      } else {
        my_vgcanvas_line_to(vg, x, y);
      }
    }
    my_vgcanvas_stroke(vg);
  }
}

static void chart_bar_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  dxx_chart_t* c = (dxx_chart_t*)widget;
  int32_t plot_w = widget->rect.w - CHART_PAD_R - 8;
  int32_t plot_h = widget->rect.h - CHART_PAD_T - 8;
  float mid_y = (float)CHART_PAD_T + (float)plot_h / 2.0f;
  float max_v = 1.0f;
  float bw;
  int i;
  my_vgcanvas_set_font(vg, NULL, 12);
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(DXX_COLOR_TEXT));
  my_vgcanvas_draw_text(vg, c->name, 8, 6);
  if (c->points == NULL || c->count <= 0 || plot_w <= 0 || plot_h <= 0) {
    return;
  }
  for (i = 0; i < c->count; i++) {
    float v = c->points[i] < 0 ? -c->points[i] : c->points[i];
    if (v > max_v) {
      max_v = v;
    }
  }
  /* center axis */
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(0xDDDDDDFFu));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){4, mid_y, (float)plot_w, 1});
  bw = (float)plot_w / (float)c->count - 2.0f;
  for (i = 0; i < c->count; i++) {
    float v = c->points[i];
    float h = (v < 0 ? -v : v) / max_v * ((float)plot_h / 2.0f - 2.0f);
    float x = 4.0f + (float)i * ((float)plot_w / (float)c->count);
    my_vgcanvas_set_fill_color(
        vg, my_color_from_rgba32(v >= 0 ? DXX_COLOR_UP : DXX_COLOR_DOWN));
    my_vgcanvas_fill_rect(vg, &(my_rectf_t){x, v >= 0 ? mid_y - h : mid_y + 1,
                                            bw, h});
  }
}

static void chart_on_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  dxx_chart_t* c = (dxx_chart_t*)widget;
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(DXX_COLOR_WHITE));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
  my_vgcanvas_set_stroke_color(vg, my_color_from_rgba32(DXX_COLOR_LINE));
  my_vgcanvas_set_line_width(vg, 1);
  my_vgcanvas_stroke_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                            (float)widget->rect.h});
  if (c->mode == DXX_CHART_BAR) {
    chart_bar_paint(widget, vg);
  } else {
    chart_line_paint(widget, vg);
  }
}

static const my_widget_vtable_t s_chart_vtable = {chart_on_paint, NULL, NULL};

my_widget_t* dxx_chart_create(const my_allocator_t* allocator,
                              dxx_chart_mode_t mode) {
  dxx_chart_t* c =
      (dxx_chart_t*)my_mem_calloc(allocator, 1, sizeof(dxx_chart_t));
  if (c == NULL) {
    return NULL;
  }
  if (my_widget_init((my_widget_t*)c, allocator, &s_chart_vtable,
                     "dxx_chart") != MY_RET_OK) {
    my_mem_free(allocator, c);
    return NULL;
  }
  c->mode = mode;
  return (my_widget_t*)c;
}

void dxx_chart_set_series(my_widget_t* chart, const char* name,
                          const float* points, int count, float ymin,
                          float ymax) {
  dxx_chart_t* c = (dxx_chart_t*)chart;
  if (chart == NULL) {
    return;
  }
  snprintf(c->name, sizeof(c->name), "%s", name != NULL ? name : "");
  c->points = points;
  c->count = count;
  c->ymin = ymin;
  c->ymax = ymax;
  my_widget_invalidate(chart, NULL);
}

const char* dxx_chart_get_series_name(my_widget_t* chart) {
  dxx_chart_t* c = (dxx_chart_t*)chart;
  if (chart == NULL || c->name[0] == '\0') {
    return NULL;
  }
  return c->name;
}
