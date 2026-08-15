/**
 * @file my_node_view.c
 * @brief Node editor canvas implementation (M19b).
 */
#include "myui/widgets/my_node_view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "myc/my_darray.h"
#include "myc/my_str.h"
#include "myr/my_bezier.h"
#include "myr/my_vgcanvas_soft.h"
#include "myui/my_theme.h"
#include "myui/my_window.h"

typedef struct node_link_t {
  my_widget_t* out_node; /**< weak (tree-owned node) */
  size_t out_slot;
  my_widget_t* in_node;
  size_t in_slot;
} node_link_t;

typedef struct link_preview_t {
  bool active;
  my_widget_t* out_node; /**< weak: source socket's node */
  size_t out_slot;
  int32_t cur_x, cur_y;  /**< cursor (CANVAS coords) */
  /* magnet (M20a): snapped target socket or NULL */
  my_widget_t* magnet_node;
  size_t magnet_slot;
} link_preview_t;

typedef struct my_node_view_t {
  my_widget_t base;
  my_darray_t* links; /**< node_link_t* */
  link_preview_t preview;
  int32_t selected;     /**< link index, -1 = none */
  my_widget_t* selected_node; /**< weak, NULL = none (M20a) */
  bool panning;
  int32_t pan_x, pan_y; /**< last pointer pos (screen local) */
  float zoom;           /**< 0.25..2.0 (M20a) */
  float pan_off_x;      /**< screen-px offset: screen = canvas*zoom+off */
  float pan_off_y;
  /* node dragging (moved here from my_node: ALL pointer logic lives in
   * the view so the canvas/screen transform stays in one place) */
  my_widget_t* drag_node;
  float drag_cx, drag_cy; /**< grab point (canvas coords) */
} my_node_view_t;

#define NV_ZOOM_MIN 0.25f
#define NV_ZOOM_MAX 2.0f
#define NV_MAGNET_DIST 20.0f /* px, canvas coords */

/* ---------------- coordinate model (M20a) ----------------
 * screen (view-local px) = canvas * zoom + pan_off */

void my_node_view_screen_to_canvas(const my_widget_t* view, int32_t sx,
                                   int32_t sy, float* cx, float* cy) {
  const my_node_view_t* v = (const my_node_view_t*)view;
  *cx = ((float)sx - v->pan_off_x) / v->zoom;
  *cy = ((float)sy - v->pan_off_y) / v->zoom;
}

void my_node_view_canvas_to_screen(const my_widget_t* view, float cx,
                                   float cy, float* sx, float* sy) {
  const my_node_view_t* v = (const my_node_view_t*)view;
  *sx = cx * v->zoom + v->pan_off_x;
  *sy = cy * v->zoom + v->pan_off_y;
}

void my_node_view_set_zoom(my_widget_t* view, float zoom) {
  my_node_view_t* v = (my_node_view_t*)view;
  if (view == NULL) {
    return;
  }
  if (zoom < NV_ZOOM_MIN) {
    zoom = NV_ZOOM_MIN;
  }
  if (zoom > NV_ZOOM_MAX) {
    zoom = NV_ZOOM_MAX;
  }
  if (zoom != v->zoom) {
    v->zoom = zoom;
    my_widget_invalidate(view, NULL);
  }
}

float my_node_view_get_zoom(const my_widget_t* view) {
  return view != NULL ? ((const my_node_view_t*)view)->zoom : 1.0f;
}

void my_node_view_zoom_at(my_widget_t* view, int32_t sx, int32_t sy,
                          float factor) {
  my_node_view_t* v = (my_node_view_t*)view;
  float cx, cy, nz;
  if (view == NULL || factor <= 0.0f) {
    return;
  }
  my_node_view_screen_to_canvas(view, sx, sy, &cx, &cy);
  nz = v->zoom * factor;
  if (nz < NV_ZOOM_MIN) {
    nz = NV_ZOOM_MIN;
  }
  if (nz > NV_ZOOM_MAX) {
    nz = NV_ZOOM_MAX;
  }
  /* keep the anchor's canvas coordinate: off = screen - canvas*zoom */
  v->zoom = nz;
  v->pan_off_x = (float)sx - cx * nz;
  v->pan_off_y = (float)sy - cy * nz;
  my_widget_invalidate(view, NULL);
}

/* ---------------- helpers ---------------- */

static node_link_t* nv_link_find_in(my_node_view_t* v, my_widget_t* in_node,
                                    size_t in_slot) {
  size_t i, n = my_darray_size(v->links);
  for (i = 0; i < n; i++) {
    node_link_t* l = (node_link_t*)my_darray_get(v->links, i);
    if (l->in_node == in_node && l->in_slot == in_slot) {
      return l;
    }
  }
  return NULL;
}

/** @brief Socket center under (x, y) within the hit radius. */
static bool nv_socket_at(my_node_view_t* v, int32_t x, int32_t y,
                         my_socket_dir_t dir, my_widget_t** out_node,
                         size_t* out_slot) {
  size_t ci, cn = my_widget_child_count((my_widget_t*)v);
  for (ci = 0; ci < cn; ci++) {
    my_widget_t* node = my_widget_get_child((my_widget_t*)v, ci);
    size_t i, cnt = my_node_socket_count(node, dir);
    for (i = 0; i < cnt; i++) {
      int32_t sx = 0, sy = 0;
      if (my_node_socket_center(node, dir, i, &sx, &sy) &&
          abs(x - sx) <= MY_NODE_SOCKET_HIT &&
          abs(y - sy) <= MY_NODE_SOCKET_HIT) {
        *out_node = node;
        *out_slot = i;
        return true;
      }
    }
  }
  return false;
}

/* ---------------- model API ---------------- */

my_ret_t my_node_view_connect(my_widget_t* view, my_widget_t* out_node,
                              size_t out_slot, my_widget_t* in_node,
                              size_t in_slot) {
  my_node_view_t* v = (my_node_view_t*)view;
  node_link_t* l;
  if (view == NULL || out_node == NULL || in_node == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  /* input slots are unique: replace (Blender semantics, documented) */
  l = nv_link_find_in(v, in_node, in_slot);
  if (l == NULL) {
    l = (node_link_t*)my_mem_calloc(((my_object_t*)view)->allocator, 1,
                                    sizeof(node_link_t));
    if (l == NULL) {
      return MY_RET_OOM;
    }
    if (my_darray_push(v->links, l) != MY_RET_OK) {
      my_mem_free(((my_object_t*)view)->allocator, l);
      return MY_RET_OOM;
    }
  }
  l->out_node = out_node;
  l->out_slot = out_slot;
  l->in_node = in_node;
  l->in_slot = in_slot;
  my_widget_invalidate(view, NULL);
  my_emitter_emit(view->emitter, "changed", NULL);
  return MY_RET_OK;
}

my_ret_t my_node_view_remove_node(my_widget_t* view, const char* node_id) {
  my_node_view_t* v = (my_node_view_t*)view;
  size_t ci, n;
  size_t li;
  if (view == NULL || node_id == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  n = my_widget_child_count(view);
  for (ci = 0; ci < n; ci++) {
    my_widget_t* node = my_widget_get_child(view, ci);
    const char* id = my_node_get_id(node);
    if (id != NULL && strcmp(id, node_id) == 0) {
      /* cascade: drop every link referencing this node */
      li = 0;
      while (li < my_darray_size(v->links)) {
        node_link_t* l = (node_link_t*)my_darray_get(v->links, li);
        if (l->out_node == node || l->in_node == node) {
          my_mem_free(((my_object_t*)view)->allocator, l);
          my_darray_remove_at(v->links, li);
        } else {
          li++;
        }
      }
      if (v->selected_node == node) {
        v->selected_node = NULL;
      }
      if (v->preview.active && v->preview.out_node == node) {
        v->preview.active = false;
        v->preview.out_node = NULL;
        v->preview.magnet_node = NULL;
      }
      if (v->drag_node == node) {
        v->drag_node = NULL;
      }
      v->selected = -1;
      my_widget_remove_child(view, node);
      my_widget_unref(node);
      my_widget_invalidate(view, NULL);
      my_emitter_emit(view->emitter, "changed", NULL);
      return MY_RET_OK;
    }
  }
  return MY_RET_NOT_FOUND;
}

my_ret_t my_node_view_disconnect_in(my_widget_t* view, my_widget_t* in_node,
                                    size_t in_slot) {
  my_node_view_t* v = (my_node_view_t*)view;
  size_t i, n;
  if (view == NULL || in_node == NULL) {
    return MY_RET_INVALID_PARAMS;
  }
  n = my_darray_size(v->links);
  for (i = 0; i < n; i++) {
    node_link_t* l = (node_link_t*)my_darray_get(v->links, i);
    if (l->in_node == in_node && l->in_slot == in_slot) {
      my_mem_free(((my_object_t*)view)->allocator, l);
      my_darray_remove_at(v->links, i);
      v->selected = -1;
      my_widget_invalidate(view, NULL);
      my_emitter_emit(view->emitter, "changed", NULL);
      return MY_RET_OK;
    }
  }
  return MY_RET_NOT_FOUND;
}

size_t my_node_view_link_count(const my_widget_t* view) {
  const my_node_view_t* v = (const my_node_view_t*)view;
  return view != NULL ? my_darray_size(v->links) : 0;
}

bool my_node_view_get_link(const my_widget_t* view, size_t index,
                           my_widget_t** out_node, size_t* out_slot,
                           my_widget_t** in_node, size_t* in_slot) {
  const my_node_view_t* v = (const my_node_view_t*)view;
  node_link_t* l;
  if (view == NULL || index >= my_darray_size(v->links)) {
    return false;
  }
  l = (node_link_t*)my_darray_get(v->links, index);
  if (out_node != NULL) *out_node = l->out_node;
  if (out_slot != NULL) *out_slot = l->out_slot;
  if (in_node != NULL) *in_node = l->in_node;
  if (in_slot != NULL) *in_slot = l->in_slot;
  return true;
}

int32_t my_node_view_get_selected(const my_widget_t* view) {
  return view != NULL ? ((const my_node_view_t*)view)->selected : -1;
}

void my_node_view_pan_by(my_widget_t* view, int32_t dx, int32_t dy) {
  my_node_view_t* v = (my_node_view_t*)view;
  if (view == NULL) {
    return;
  }
  /* M20a: pan is a view-level screen-px offset (node rects stay in
   * canvas coords) */
  v->pan_off_x += (float)dx;
  v->pan_off_y += (float)dy;
  my_widget_invalidate(view, NULL);
}

/* ---------------- link geometry ---------------- */

/** @brief Bezier endpoints/handles for a link (or preview). */
static bool nv_link_geo(my_widget_t* out_node, size_t out_slot,
                        my_widget_t* in_node, size_t in_slot, float* x0,
                        float* y0, float* cx1, float* cy1, float* cx2,
                        float* cy2, float* x1, float* y1) {
  int32_t ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
  float dx;
  if (!my_node_socket_center(out_node, MY_SOCKET_OUT, out_slot, &ix0, &iy0) ||
      !my_node_socket_center(in_node, MY_SOCKET_IN, in_slot, &ix1, &iy1)) {
    return false;
  }
  dx = (float)abs(ix1 - ix0) * 0.5f;
  if (dx < 40.0f) {
    dx = 40.0f; /* Blender-like horizontal tangents */
  }
  *x0 = (float)ix0;
  *y0 = (float)iy0;
  *x1 = (float)ix1;
  *y1 = (float)iy1;
  *cx1 = *x0 + dx;
  *cy1 = *y0;
  *cx2 = *x1 - dx;
  *cy2 = *y1;
  return true;
}

/** @brief Subdivide ctx for find_link_at. */
typedef struct link_pts_t {
  float xs[256];
  float ys[256];
  int n;
} link_pts_t;

static my_ret_t link_pts_emit(void* ctx, float x, float y) {
  link_pts_t* p = (link_pts_t*)ctx;
  if (p->n < 256) {
    p->xs[p->n] = x;
    p->ys[p->n] = y;
    p->n++;
  }
  return MY_RET_OK;
}

/** @brief Min distance from (px, py) to the polyline. */
static float link_pts_dist(const link_pts_t* p, float px, float py) {
  int i;
  float best = 1e9f;
  for (i = 0; i + 1 < p->n; i++) {
    float ax = p->xs[i], ay = p->ys[i], bx = p->xs[i + 1], by = p->ys[i + 1];
    float dx = bx - ax, dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 1e-9f
                  ? ((px - ax) * dx + (py - ay) * dy) / len2
                  : 0.0f;
    float qx, qy, ddx, ddy;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    qx = ax + t * dx;
    qy = ay + t * dy;
    ddx = px - qx;
    ddy = py - qy;
    if (ddx * ddx + ddy * ddy < best) {
      best = ddx * ddx + ddy * ddy;
    }
  }
  return best; /* squared */
}

int32_t my_node_view_find_link_at(my_widget_t* view, int32_t x, int32_t y) {
  my_node_view_t* v = (my_node_view_t*)view;
  size_t i, n;
  if (view == NULL) {
    return -1;
  }
  n = my_darray_size(v->links);
  for (i = n; i > 0; i--) { /* later links win on overlap */
    node_link_t* l = (node_link_t*)my_darray_get(v->links, i - 1);
    float x0, y0, cx1, cy1, cx2, cy2, x1, y1;
    link_pts_t pts;
    if (!nv_link_geo(l->out_node, l->out_slot, l->in_node, l->in_slot, &x0,
                     &y0, &cx1, &cy1, &cx2, &cy2, &x1, &y1)) {
      continue;
    }
    pts.n = 0;
    pts.xs[0] = x0;
    pts.ys[0] = y0;
    pts.n = 1;
    my_bezier_cubic_to_lines(x0, y0, cx1, cy1, cx2, cy2, x1, y1, 0.25f, 16,
                             link_pts_emit, &pts, NULL);
    if (link_pts_dist(&pts, (float)x, (float)y) <= 16.0f) { /* 4px */
      return (int32_t)(i - 1);
    }
  }
  return -1;
}

/* ---------------- paint ---------------- */

static void nv_stroke_link(my_widget_t* widget, my_vgcanvas_t* vg, float x0,
                           float y0, float cx1, float cy1, float cx2,
                           float cy2, float x1, float y1,
                           const char* cls, uint32_t fallback) {
  uint32_t c = my_widget_part_color(widget, "node_link", cls,
                                    MY_STATE_NORMAL, "fg_color", fallback);
  my_vgcanvas_set_stroke_color(vg, my_color_from_rgba32(c));
  my_vgcanvas_set_line_width(vg, 3);
  my_vgcanvas_begin_path(vg);
  my_vgcanvas_move_to(vg, x0, y0);
  my_vgcanvas_curve_to(vg, cx1, cy1, cx2, cy2, x1, y1);
  my_vgcanvas_stroke(vg);
}

static void nv_paint(my_widget_t* widget, my_vgcanvas_t* vg) {
  my_node_view_t* v = (my_node_view_t*)widget;
  uint32_t bg = my_widget_style_get_color(widget, MY_STATE_NORMAL,
                                          "bg_color", 0x282828FFu);
  size_t i, n;
  my_vgcanvas_set_fill_color(vg, my_color_from_rgba32(bg));
  my_vgcanvas_fill_rect(vg, &(my_rectf_t){0, 0, (float)widget->rect.w,
                                          (float)widget->rect.h});
  /* AA 固化（M20a）：连线/圆点走 level 2 覆盖率 AA，不依赖调用方
   * 状态（soft 默认已是 2；幂等设置，gles 端走 MSAA 路径——注释） */
  my_vgcanvas_soft_set_antialias_level(vg, 2);
  /* canvas transform: device = (canvas + pan/zoom) * zoom (soft CTM is
   * (x+tx)*scale; HiDPI 下 window 的 base scale 由 restore 还原，故
   * 这里取 window 基础 scale 相乘） */
  if (v->zoom != 1.0f || v->pan_off_x != 0.0f || v->pan_off_y != 0.0f) {
    my_widget_t* root = widget;
    float base_scale = 1.0f;
    while (root->parent != NULL) {
      root = root->parent;
    }
    if (my_str_eq(root->widget_type, "window")) {
      base_scale = ((my_window_t*)root)->scale;
    }
    my_vgcanvas_translate(vg, v->pan_off_x / (base_scale * v->zoom),
                          v->pan_off_y / (base_scale * v->zoom));
    my_vgcanvas_soft_set_scale(vg, base_scale * v->zoom);
  }
  /* links (nodes paint after us: children over links) */
  n = my_darray_size(v->links);
  for (i = 0; i < n; i++) {
    node_link_t* l = (node_link_t*)my_darray_get(v->links, i);
    float x0, y0, cx1, cy1, cx2, cy2, x1, y1;
    if (!nv_link_geo(l->out_node, l->out_slot, l->in_node, l->in_slot, &x0,
                     &y0, &cx1, &cy1, &cx2, &cy2, &x1, &y1)) {
      continue;
    }
    nv_stroke_link(widget, vg, x0, y0, cx1, cy1, cx2, cy2, x1, y1,
                   (int32_t)i == v->selected ? "selected" : NULL,
                   (int32_t)i == v->selected ? 0xE0A030FFu : 0xA0A0A0FFu);
  }
  /* link preview follows the cursor (magnet-snapped when a target is
   * in range; the snapped socket gets a highlight ring) */
  if (v->preview.active && v->preview.out_node != NULL) {
    int32_t ix0 = 0, iy0 = 0;
    if (my_node_socket_center(v->preview.out_node, MY_SOCKET_OUT,
                              v->preview.out_slot, &ix0, &iy0)) {
      float tx = (float)v->preview.cur_x;
      float ty = (float)v->preview.cur_y;
      if (v->preview.magnet_node != NULL) {
        int32_t mx = 0, my = 0;
        if (my_node_socket_center(v->preview.magnet_node, MY_SOCKET_IN,
                                  v->preview.magnet_slot, &mx, &my)) {
          tx = (float)mx;
          ty = (float)my;
        }
      }
      {
        float dx = fabsf(tx - (float)ix0) * 0.5f;
        if (dx < 40.0f) {
          dx = 40.0f;
        }
        nv_stroke_link(widget, vg, (float)ix0, (float)iy0, (float)ix0 + dx,
                       (float)iy0, tx - dx, ty, tx, ty, "preview",
                       0x70C0E8FFu);
      }
    }
  }
  /* magnet ring */
  if (v->preview.magnet_node != NULL) {
    int32_t mx = 0, my = 0;
    if (my_node_socket_center(v->preview.magnet_node, MY_SOCKET_IN,
                              v->preview.magnet_slot, &mx, &my)) {
      uint32_t ring = my_widget_part_color(widget, "node_socket", "magnet",
                                           MY_STATE_NORMAL, "bg_color",
                                           0xFFD050FFu);
      my_vgcanvas_set_stroke_color(vg, my_color_from_rgba32(ring));
      my_vgcanvas_set_line_width(vg, 2);
      my_vgcanvas_begin_path(vg);
      my_node_path_circle(vg, (float)mx, (float)my, 9.0f);
      my_vgcanvas_stroke(vg);
    }
  }
}

/* ---------------- events ---------------- */

/** @brief Magnet scan: nearest INPUT socket within NV_MAGNET_DIST of
 * the preview cursor (canvas coords); updates preview.magnet_*. */
static void nv_magnet_scan(my_node_view_t* v, float cx, float cy) {
  size_t ci, cn = my_widget_child_count((my_widget_t*)v);
  float best = NV_MAGNET_DIST * NV_MAGNET_DIST;
  my_widget_t* best_node = NULL;
  size_t best_slot = 0;
  for (ci = 0; ci < cn; ci++) {
    my_widget_t* node = my_widget_get_child((my_widget_t*)v, ci);
    size_t i, cnt = my_node_socket_count(node, MY_SOCKET_IN);
    for (i = 0; i < cnt; i++) {
      int32_t sx = 0, sy = 0;
      float dx, dy, d2;
      if (!my_node_socket_center(node, MY_SOCKET_IN, i, &sx, &sy)) {
        continue;
      }
      dx = cx - (float)sx;
      dy = cy - (float)sy;
      d2 = dx * dx + dy * dy;
      if (d2 < best) {
        best = d2;
        best_node = node;
        best_slot = i;
      }
    }
  }
  v->preview.magnet_node = best_node;
  v->preview.magnet_slot = best_node != NULL ? best_slot : 0;
}

/** @brief Node under a canvas point (topmost = last child wins). */
static my_widget_t* nv_node_at(my_node_view_t* v, float cx, float cy,
                               bool* out_titlebar) {
  size_t ci = my_widget_child_count((my_widget_t*)v);
  while (ci > 0) {
    my_widget_t* node;
    ci--;
    node = my_widget_get_child((my_widget_t*)v, ci);
    if (cx >= node->rect.x && cx < node->rect.x + node->rect.w &&
        cy >= node->rect.y && cy < node->rect.y + node->rect.h) {
      *out_titlebar = cy < node->rect.y + MY_NODE_HEADER_H;
      return node;
    }
  }
  return NULL;
}

static my_ret_t nv_event(my_widget_t* widget, const my_event_t* event) {
  my_node_view_t* v = (my_node_view_t*)widget;
  switch (event->type) {
    case MY_EVENT_POINTER_DOWN: {
      int32_t lx = event->u.pointer.x, ly = event->u.pointer.y;
      float cx, cy;
      my_widget_t* node = NULL;
      size_t slot = 0;
      my_widget_global_to_local(widget, &lx, &ly);
      my_node_view_screen_to_canvas(widget, lx, ly, &cx, &cy);
      /* drag out of an output socket: preview */
      if (nv_socket_at(v, (int32_t)cx, (int32_t)cy, MY_SOCKET_OUT, &node,
                       &slot)) {
        v->preview.active = true;
        v->preview.out_node = node;
        v->preview.out_slot = slot;
        v->preview.cur_x = (int32_t)cx;
        v->preview.cur_y = (int32_t)cy;
        v->preview.magnet_node = NULL; /* magnet scans on MOVE only —
                                        * DOWN must not snap */
        my_widget_invalidate(widget, NULL);
        return MY_RET_OK;
      }
      /* drag out of a CONNECTED input socket: pick the link up */
      if (nv_socket_at(v, (int32_t)cx, (int32_t)cy, MY_SOCKET_IN, &node,
                       &slot) &&
          nv_link_find_in(v, node, slot) != NULL) {
        node_link_t* l = nv_link_find_in(v, node, slot);
        my_widget_t* out_node = l->out_node;
        size_t out_slot = l->out_slot;
        my_node_view_disconnect_in(widget, node, slot); /* emits changed */
        v->preview.active = true;
        v->preview.out_node = out_node;
        v->preview.out_slot = out_slot;
        v->preview.cur_x = (int32_t)cx;
        v->preview.cur_y = (int32_t)cy;
        v->preview.magnet_node = NULL; /* magnet scans on MOVE only */
        my_widget_invalidate(widget, NULL);
        return MY_RET_OK;
      }
      /* click a link: select */
      {
        int32_t li = my_node_view_find_link_at(widget, (int32_t)cx,
                                               (int32_t)cy);
        if (li >= 0) {
          v->selected = li;
          v->selected_node = NULL;
          my_widget_invalidate(widget, NULL);
          return MY_RET_OK;
        }
      }
      /* node: select + (title bar) drag */
      {
        bool titlebar = false;
        my_widget_t* hit = nv_node_at(v, cx, cy, &titlebar);
        if (hit != NULL) {
          v->selected = -1;
          v->selected_node = hit;
          if (titlebar) {
            v->drag_node = hit;
            v->drag_cx = cx;
            v->drag_cy = cy;
          }
          my_widget_invalidate(widget, NULL);
          return MY_RET_OK;
        }
      }
      v->selected = -1;
      v->selected_node = NULL;
      /* empty space: pan */
      v->panning = true;
      v->pan_x = lx;
      v->pan_y = ly;
      return MY_RET_OK;
    }
    case MY_EVENT_POINTER_MOVE: {
      int32_t lx = event->u.pointer.x, ly = event->u.pointer.y;
      float cx, cy;
      my_widget_global_to_local(widget, &lx, &ly);
      my_node_view_screen_to_canvas(widget, lx, ly, &cx, &cy);
      if (v->preview.active) {
        v->preview.cur_x = (int32_t)cx;
        v->preview.cur_y = (int32_t)cy;
        nv_magnet_scan(v, cx, cy); /* magnet updates continuously */
        my_widget_invalidate(widget, NULL);
        return MY_RET_OK;
      }
      if (v->drag_node != NULL) {
        float dx = cx - v->drag_cx;
        float dy = cy - v->drag_cy;
        v->drag_cx = cx;
        v->drag_cy = cy;
        v->drag_node->rect.x += (int32_t)(dx >= 0.0f ? dx + 0.5f : dx - 0.5f);
        v->drag_node->rect.y += (int32_t)(dy >= 0.0f ? dy + 0.5f : dy - 0.5f);
        my_widget_invalidate(widget, NULL);
        return MY_RET_OK;
      }
      if (v->panning) {
        my_node_view_pan_by(widget, lx - v->pan_x, ly - v->pan_y);
        v->pan_x = lx;
        v->pan_y = ly;
        return MY_RET_OK;
      }
      return MY_RET_FAIL;
    }
    case MY_EVENT_POINTER_UP:
      if (v->preview.active) {
        if (v->preview.magnet_node != NULL) {
          /* snapped: connect to the magnet target */
          my_node_view_connect(widget, v->preview.out_node,
                               v->preview.out_slot, v->preview.magnet_node,
                               v->preview.magnet_slot);
        } else {
          int32_t lx = event->u.pointer.x, ly = event->u.pointer.y;
          float cx, cy;
          my_widget_t* node = NULL;
          size_t slot = 0;
          my_widget_global_to_local(widget, &lx, &ly);
          my_node_view_screen_to_canvas(widget, lx, ly, &cx, &cy);
          if (nv_socket_at(v, (int32_t)cx, (int32_t)cy, MY_SOCKET_IN,
                           &node, &slot)) {
            my_node_view_connect(widget, v->preview.out_node,
                                 v->preview.out_slot, node, slot);
          }
        }
        v->preview.active = false;
        v->preview.out_node = NULL;
        v->preview.magnet_node = NULL;
        my_widget_invalidate(widget, NULL);
        return MY_RET_OK;
      }
      if (v->drag_node != NULL) {
        v->drag_node = NULL;
        return MY_RET_OK;
      }
      if (v->panning) {
        v->panning = false;
        return MY_RET_OK;
      }
      return MY_RET_FAIL;
    case MY_EVENT_POINTER_WHEEL: {
      /* Blender convention: wheel = zoom anchored at the cursor */
      int32_t lx = event->u.pointer.x, ly = event->u.pointer.y;
      float factor = event->u.pointer.delta > 0 ? 1.1f : 1.0f / 1.1f;
      my_widget_global_to_local(widget, &lx, &ly);
      my_node_view_zoom_at(widget, lx, ly, factor);
      return MY_RET_OK;
    }
    case MY_EVENT_KEY_DOWN:
      if (event->u.key.key == MY_KEY_DELETE ||
          event->u.key.key == MY_KEY_BACKSPACE) {
        /* selected node: cascade remove (M20a); selected link: drop */
        if (v->selected_node != NULL) {
          const char* id = my_node_get_id(v->selected_node);
          if (id != NULL) {
            my_node_view_remove_node(widget, id);
          }
          return MY_RET_OK;
        }
        if (v->selected >= 0 &&
            (size_t)v->selected < my_darray_size(v->links)) {
          node_link_t* l =
              (node_link_t*)my_darray_get(v->links, (size_t)v->selected);
          my_widget_t* in_node = l->in_node;
          size_t in_slot = l->in_slot;
          my_node_view_disconnect_in(widget, in_node, in_slot);
          return MY_RET_OK;
        }
      }
      return MY_RET_FAIL;
    default:
      return MY_RET_FAIL;
  }
}

/* ---------------- lifecycle ---------------- */

static void nv_destroy_chain(my_object_t* obj) {
  my_node_view_t* v = (my_node_view_t*)obj;
  size_t i, n;
  if (v->links != NULL) {
    n = my_darray_size(v->links);
    for (i = 0; i < n; i++) {
      my_mem_free(obj->allocator, my_darray_get(v->links, i));
    }
    my_darray_destroy(v->links);
  }
  my_widget_destroy((my_widget_t*)v);
  my_object_destroy(obj);
}

static const my_widget_vtable_t s_nv_vtable = {nv_paint, nv_event, NULL};

my_widget_t* my_node_view_create(const my_allocator_t* allocator) {
  my_node_view_t* v =
      (my_node_view_t*)my_mem_calloc(allocator, 1, sizeof(my_node_view_t));
  if (v == NULL) {
    return NULL;
  }
  if (my_widget_init((my_widget_t*)v, allocator, &s_nv_vtable,
                     "node_view") != MY_RET_OK) {
    my_mem_free(allocator, v);
    return NULL;
  }
  ((my_object_t*)v)->destroy = nv_destroy_chain;
  ((my_widget_t*)v)->widget_type = "node_view"; /* theme selector name */
  v->zoom = 1.0f; /* M20a: identity by default (canvas == screen) */
  v->links = my_darray_create(allocator, 0);
  if (v->links == NULL) {
    my_widget_unref((my_widget_t*)v);
    return NULL;
  }
  v->selected = -1;
  ((my_widget_t*)v)->focusable = true;
  return (my_widget_t*)v;
}

my_widget_t* my_node_view_add_node(my_widget_t* view, const char* id,
                                   const char* title, const char* category,
                                   int32_t x, int32_t y, int32_t w,
                                   int32_t h) {
  my_widget_t* node;
  if (view == NULL) {
    return NULL;
  }
  node = my_node_create(((my_object_t*)view)->allocator, view, id, title,
                        category);
  if (node == NULL) {
    return NULL;
  }
  my_widget_set_rect(node, &(my_rect_t){x, y, w, h});
  if (my_widget_add_child(view, node) != MY_RET_OK) {
    my_widget_unref(node);
    return NULL;
  }
  my_widget_unref(node); /* the tree owns it */
  return node;
}
