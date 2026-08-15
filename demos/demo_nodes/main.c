/**
 * @file main.c
 * @brief demo_nodes: Blender-style node editor demo (M19c) — a partial
 * recreation of the shader editor reference: big "Principled BSDF"
 * node + small Mapping/Color/Environment nodes, an embedded slider,
 * bezier links, all part colors via one CSS string.
 *
 * Under the dummy port: MYUI_DEMO_DUMP_PPM=<path> dumps one frame.
 */
#include <stdio.h>
#include <stdlib.h>

#include "mypal/my_pal.h"
#include "myui/my_css.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_node_view.h"
#include "myui/widgets/my_slider.h"

/* Part palette demo — every CSS rule maps to a visual part (M19b):
 * canvas bg / node body / per-category headers (descendant) / socket
 * dots / link selected + preview. M21b: no `node_link` base rule on
 * purpose — unselected links tint from their SOURCE socket type color
 * (a theme `node_link` would still win over the type color). */
static const char* NODES_CSS =
    "node_view { background-color: #282828 } "
    "node { background-color: #3A3A3A } "
    "node.shader .header { background-color: #33662E } " /* green: shader */
    "node.color .header { background-color: #663399 } "  /* purple: color */
    "node.vector .header { background-color: #2E4D66 } " /* blue: mapping */
    "node.input .header { background-color: #66502E } "  /* brown: env */
    "node_socket.input { background-color: #8A8A8A } "
    "node_socket.output { background-color: #A0A060 } "
    "node_link.selected { color: #E0A030 } "
    "node_link.preview { color: #70C0E8 }";

static my_font_t* create_demo_font(void) {
  static const char* paths[] = {
      "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      NULL};
  my_font_t* font = my_font_create_chain(NULL, paths, 2, 0);
  if (font == NULL) {
    font = my_font_bitmap_create(NULL);
  }
  return font;
}

#include "myr/my_lcd_mem.h"
#ifdef MYUI_PAL_DUMMY
#include "mypal/dummy/my_pal_dummy.h"
#endif

static my_ret_t live_dump_tick(void* ctx);
static void dump_ppm(my_pal_window_t* window, const char* path) {
  my_lcd_t* lcd = my_pal_window_get_lcd(window);
  uint8_t* buf = my_lcd_get_buffer(lcd);
  uint32_t stride = my_lcd_get_stride(lcd);
  uint32_t w = my_lcd_get_width(lcd);
  uint32_t h = my_lcd_get_height(lcd);
  uint32_t x, y;
  FILE* f = fopen(path, "wb");
  if (f == NULL || buf == NULL) {
    return;
  }
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      const uint8_t* p = buf + (size_t)y * stride + (size_t)x * 4;
      fputc(p[2], f);
      fputc(p[1], f);
      fputc(p[0], f);
    }
  }
  fclose(f);
  printf("demo_nodes: dumped %s\n", path);
}

int main(void) {
  my_pal_t* pal = my_pal_create(NULL);
  my_pal_main_loop_t* loop = my_pal_main_loop_create(pal);
  my_window_manager_t* wm = my_window_manager_create(NULL, pal, loop);
  my_window_t* win;
  my_theme_t* theme;
  my_widget_t* hint;
  my_widget_t* view;
  my_widget_t* bsdf;
  my_widget_t* mapping;
  my_widget_t* color;
  my_widget_t* env;
  my_widget_t* slider;
  my_font_t* font;
  if (pal == NULL || loop == NULL || wm == NULL) {
    fprintf(stderr, "demo_nodes: init failed\n");
    return 1;
  }
  win = my_window_create(NULL, pal, 960, 640, "myui demo_nodes");
#ifndef MYUI_PAL_DUMMY
  /* MYUI_GPU_BACKEND=soft|gles2|opengl|vulkan (M25a); legacy
   * MYUI_DEMO_GLES=1 is equivalent to gles2 */
  {
    const char* be = getenv("MYUI_GPU_BACKEND");
    my_gpu_backend_t backend = MY_GPU_AUTO;
    if (be == NULL && getenv("MYUI_DEMO_GLES") != NULL) {
      be = "gles2";
    }
    if (be != NULL) {
      if (strcmp(be, "soft") == 0) {
        backend = MY_GPU_SOFT;
      } else if (strcmp(be, "gles2") == 0) {
        backend = MY_GPU_GLES2;
      } else if (strcmp(be, "opengl") == 0) {
        backend = MY_GPU_OPENGL;
      } else if (strcmp(be, "vulkan") == 0) {
        backend = MY_GPU_VULKAN;
      }
      if (my_window_enable_gpu(win, backend) == MY_RET_OK) {
        printf("demo_nodes: GPU backend '%s' enabled\n", be);
      } else {
        fprintf(stderr, "demo_nodes: backend '%s' unavailable, soft path\n",
                be);
      }
    }
  }
#endif
  theme = my_theme_default_create(NULL);
  my_theme_load_css(theme, NODES_CSS);
  my_window_set_theme(win, theme, true);
  font = create_demo_font();
  my_window_set_font(win, font, 16);

  hint = my_label_create(NULL,
                         "拖标题栏移动节点，右点拖到左点连线（磁吸高亮），点线选中流动，"
                         "Del 删除；滚轮缩放，中键平移，空白左拖框选，右下角小地图；"
                         "连线按源接口类型着色带箭头，Environment 节点尺寸自适应");
  my_widget_set_rect(hint, &(my_rect_t){10, 6, 940, 24});
  my_widget_add_child(my_window_widget(win), hint);
  my_widget_unref(hint);

  view = my_node_view_create(NULL);
  my_widget_set_rect(view, &(my_rect_t){10, 36, 940, 594});
  my_widget_add_child(my_window_widget(win), view);
  my_widget_unref(view);

  /* Principled BSDF (big, shader-green header) */
  bsdf = my_node_view_add_node(view, "bsdf", "Principled BSDF", "shader",
                               400, 120, 200, 200);
  my_node_add_socket(bsdf, MY_SOCKET_IN, "Base Color", 0x808080FFu);
  my_node_add_socket(bsdf, MY_SOCKET_IN, "Metallic", 0x808080FFu);
  my_node_add_socket(bsdf, MY_SOCKET_IN, "Roughness", 0x808080FFu);
  my_node_add_socket(bsdf, MY_SOCKET_IN, "Normal", 0x808080FFu);
  my_node_add_socket(bsdf, MY_SOCKET_OUT, "BSDF", 0x60A060FFu);

  /* Mapping (vector-blue) */
  mapping = my_node_view_add_node(view, "mapping", "Mapping", "vector",
                                  60, 80, 160, 80);
  my_node_add_socket(mapping, MY_SOCKET_OUT, "Vector", 0x4060A0FFu);

  /* Color (purple) */
  color = my_node_view_add_node(view, "color", "Color", "color", 60, 220,
                                160, 80);
  my_node_add_socket(color, MY_SOCKET_OUT, "Color", 0xA060A0FFu);

  /* Environment (brown) + embedded slider — M21b: w/h = 0 auto-sizes
   * the node to its content (title/socket row/slider). The slider is
   * added BEFORE the socket so the add_socket recompute sees it. */
  env = my_node_view_add_node(view, "env", "Environment", "input", 60,
                              360, 0, 0);
  slider = my_slider_create(NULL);
  my_widget_set_rect(slider, &(my_rect_t){10, 64, 180, 24});
  my_widget_add_child(env, slider);
  my_widget_unref(slider);
  my_node_add_socket(env, MY_SOCKET_OUT, "Color", 0xA08850FFu);

  /* links per the reference: Color->BaseColor, Mapping->Metallic; the
   * Environment output stays unlinked (dangling-output demo) */
  my_node_view_connect(view, color, 0, bsdf, 0);
  my_node_view_connect(view, mapping, 0, bsdf, 1);

  my_window_manager_open(wm, win);
  my_widget_unref(my_window_widget(win));

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      my_widget_invalidate(my_window_widget(win), NULL);
      my_window_paint(win);
      dump_ppm(win->pal_window, dump);
      /* second frame: the Color->BaseColor link selected (click its
       * computed midpoint (310, 240) in window coords) */
      {
        my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
        e.u.pointer.x = 320;
        e.u.pointer.y = 240; /* canvas (310,204) + view offset (10,36) */
        my_window_on_pal_event(win, &e);
        e = my_event_init(MY_EVENT_POINTER_UP);
        e.u.pointer.x = 320;
        e.u.pointer.y = 240;
        my_window_on_pal_event(win, &e);
        my_window_paint(win);
        dump_ppm(win->pal_window, "/tmp/demo_nodes_selected.ppm");
      }
      /* zoomed (wheel at the window center) */
      {
        my_event_t e = my_event_init(MY_EVENT_POINTER_WHEEL);
        e.u.pointer.x = 480;
        e.u.pointer.y = 320;
        e.u.pointer.delta = 2;
        my_window_on_pal_event(win, &e);
        my_window_paint(win);
        dump_ppm(win->pal_window, "/tmp/demo_nodes_zoom.ppm");
      }
      /* rubber band in progress (left drag on empty space) */
      {
        my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
        e.u.pointer.x = 40;
        e.u.pointer.y = 60;
        e.u.pointer.button = 1;
        my_window_on_pal_event(win, &e);
        e = my_event_init(MY_EVENT_POINTER_MOVE);
        e.u.pointer.x = 500;
        e.u.pointer.y = 460;
        e.u.pointer.button = 1;
        my_window_on_pal_event(win, &e);
        my_window_paint(win);
        dump_ppm(win->pal_window, "/tmp/demo_nodes_band.ppm");
        e = my_event_init(MY_EVENT_POINTER_UP);
        my_window_on_pal_event(win, &e);
      }
      /* magnet preview: drag from Mapping's out socket near a BSDF
       * input (ring + snapped endpoint) */
      {
        my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
        e.u.pointer.x = 230; /* Mapping out socket: view (10,36) + node (60,80) + (160,34+10) */
        e.u.pointer.y = 150;
        e.u.pointer.button = 1;
        my_window_on_pal_event(win, &e);
        e = my_event_init(MY_EVENT_POINTER_MOVE);
        e.u.pointer.x = 400; /* near Base Color input */
        e.u.pointer.y = 194;
        e.u.pointer.button = 1;
        my_window_on_pal_event(win, &e);
        my_window_paint(win);
        dump_ppm(win->pal_window, "/tmp/demo_nodes_magnet.ppm");
        e = my_event_init(MY_EVENT_POINTER_UP);
        my_window_on_pal_event(win, &e);
      }
      my_font_destroy(font);
      my_window_manager_destroy(wm);
      my_pal_main_loop_destroy(loop);
      my_pal_destroy(pal);
      return 0;
    }
  }
#endif

  /* live debugging aid: MYUI_LIVE_DUMP=<path> dumps the frame every 500ms */
  if (getenv("MYUI_LIVE_DUMP") != NULL) {
    my_pal_main_loop_add_timer(loop, live_dump_tick, wm, 500);
  }
  my_pal_main_loop_run(loop);
  my_window_manager_destroy(wm);
  my_pal_main_loop_destroy(loop);
  my_pal_destroy(pal);
  return 0;
}

static my_ret_t live_dump_tick(void* ctx) {
  my_window_manager_t* wm = (my_window_manager_t*)ctx;
  my_window_t* top = my_window_manager_top(wm);
  const char* path = getenv("MYUI_LIVE_DUMP");
  if (top != NULL && path != NULL) {
    dump_ppm(top->pal_window, path);
  }
  return MY_RET_OK; /* repeat */
}
