/**
 * @file main.c
 * @brief demo_widgets: buttons, labels, linear layout, themes, animation.
 *
 * Under the dummy port (headless): set MYUI_DEMO_DUMP_PPM=<path> to
 * simulate a click on the animated button, advance the fake clock so the
 * animation runs, dump the frame as PPM and exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mypal/my_pal.h"
#include "myui/my_animator.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_label.h"

#ifdef MYUI_PAL_DUMMY
#include "mypal/dummy/my_pal_dummy.h"
#include "myr/my_lcd_mem.h"
#endif

typedef struct app_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_theme_t* light;
  my_theme_t* dark;
  bool is_dark;
  my_widget_t* anim_btn;
} app_t;

static my_theme_t* make_dark_theme(void) {
  my_theme_t* t = my_theme_create(NULL);
  my_theme_load_str(
      t,
      "window.normal.bg_color=#263238\n"
      "button.normal.bg_color=#37474F\n"
      "button.hover.bg_color=#455A64\n"
      "button.pressed.bg_color=#263238\n"
      "button.disabled.bg_color=#303030\n"
      "button.normal.border_color=#78909C\n"
      "button[ok].normal.bg_color=#2E7D32\n"
      "button[ok].pressed.bg_color=#1B5E20\n"
      "button[cancel].normal.bg_color=#C62828\n"
      "button[cancel].pressed.bg_color=#8E0000\n"
      "label.normal.bg_color=#263238\n"
      "label.normal.fg_color=#ECEFF1\n");
  return t;
}

static my_theme_t* make_light_theme(void) {
  my_theme_t* t = my_theme_default_create(NULL);
  my_theme_load_str(
      t,
      "button[ok].normal.bg_color=#A5D6A7\n"
      "button[ok].pressed.bg_color=#66BB6A\n"
      "button[cancel].normal.bg_color=#EF9A9A\n"
      "button[cancel].pressed.bg_color=#E57373\n");
  return t;
}

static void on_toggle_theme(void* ctx, const char* event, void* data) {
  app_t* app = (app_t*)ctx;
  (void)event;
  (void)data;
  app->is_dark = !app->is_dark;
  my_window_set_theme(app->win, app->is_dark ? app->dark : app->light, false);
}

static void on_anim_click(void* ctx, const char* event, void* data) {
  app_t* app = (app_t*)ctx;
  my_widget_t* btn = app->anim_btn;
  int32_t target = btn->rect.x < 300 ? 560 : 20;
  (void)event;
  (void)data;
  my_animator_move_to(btn, target, btn->rect.y, 400, my_easing_ease_in_out);
}

static void on_quit_click(void* ctx, const char* event, void* data) {
  app_t* app = (app_t*)ctx;
  (void)event;
  (void)data;
  my_window_manager_close(app->wm, app->win);
}

static my_widget_t* add_button(app_t* app, my_widget_t* parent, const char* text,
                               const char* name, const char* params) {
  my_widget_t* b = my_button_create(NULL, text);
  my_widget_set_name(b, name);
  my_widget_set_layout_params(b, params);
  my_widget_add_child(parent, b);
  my_widget_unref(b);
  (void)app;
  return b;
}

static void build_ui(app_t* app) {
  my_widget_t* root = my_window_widget(app->win);
  my_widget_t* title = my_label_create(NULL, "myui demo_widgets");
  my_widget_t* row = my_widget_create(NULL, "row");
  my_widget_t* quit;

  my_widget_set_layouter(root, my_layouter_linear_create(NULL, false, 8));

  my_widget_set_layout_params(title, "h:36");
  my_widget_add_child(root, title);
  my_widget_unref(title);

  my_widget_set_layout_params(row, "h:56");
  my_widget_set_layouter(row, my_layouter_linear_create(NULL, true, 8));
  my_widget_add_child(root, row);
  my_widget_unref(row);

  add_button(app, row, "OK", "ok", "w:120 h:48");
  add_button(app, row, "Cancel", "cancel", "w:120 h:48");

  quit = add_button(app, row, "Toggle theme", "toggle", "w:160 h:48");
  my_widget_on(quit, "click", on_toggle_theme, app);

  quit = add_button(app, row, "Quit", "quit", "w:120 h:48");
  my_widget_on(quit, "click", on_quit_click, app);

  app->anim_btn = my_button_create(NULL, "Click me: move");
  my_widget_set_rect(app->anim_btn, &(my_rect_t){20, 140, 160, 48});
  /* escape the layouter: place it in the root with absolute position via
   * a spacing child trick — simplest: absolute layer below root's linear
   * flow is not available, so give it fixed layout params instead */
  my_widget_set_layout_params(app->anim_btn, "w:160 h:48");
  my_widget_add_child(root, app->anim_btn);
  my_widget_unref(app->anim_btn);
  my_widget_on(app->anim_btn, "click", on_anim_click, app);
}

#ifdef MYUI_PAL_DUMMY
static void dump_ppm(my_pal_window_t* window, const char* path) {
  my_lcd_t* lcd = my_pal_window_get_lcd(window);
  uint8_t* buf = my_lcd_mem_get_buffer(lcd);
  uint32_t stride = my_lcd_mem_get_stride(lcd);
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
  printf("demo_widgets: dumped %s\n", path);
}

/** @brief Simulate a click at the center of a widget. */
static void click_widget(my_window_t* win, my_widget_t* widget) {
  int32_t cx = widget->rect.w / 2;
  int32_t cy = widget->rect.h / 2;
  my_event_t e;
  my_widget_local_to_global(widget, &cx, &cy);
  e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  e.u.pointer.button = 1;
  my_window_on_pal_event(win, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = cx;
  e.u.pointer.y = cy;
  e.u.pointer.button = 1;
  my_window_on_pal_event(win, &e);
}
#endif

int main(void) {
  app_t app;
  memset(&app, 0, sizeof(app));

  app.pal = my_pal_create(NULL);
  app.loop = my_pal_main_loop_create(app.pal);
  app.wm = my_window_manager_create(NULL, app.pal, app.loop);
  app.light = make_light_theme();
  app.dark = make_dark_theme();
  if (app.pal == NULL || app.loop == NULL || app.wm == NULL || app.light == NULL ||
      app.dark == NULL) {
    fprintf(stderr, "demo_widgets: init failed\n");
    return 1;
  }

  app.win = my_window_create(NULL, app.pal, 800, 480, "myui demo_widgets");
  my_window_set_theme(app.win, app.light, false);
  build_ui(&app);
  my_window_manager_open(app.wm, app.win);
  my_widget_unref(my_window_widget(app.win));

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      /* layout + first paint so widgets have real rects, then simulate */
      my_window_paint(app.win);
      click_widget(app.win, my_widget_find_child(
                                my_widget_get_child(my_window_widget(app.win), 1),
                                "toggle"));
      click_widget(app.win, app.anim_btn);
      my_pal_dummy_set_now_ms(app.pal, 200);
      my_pal_main_loop_run(app.loop);
      dump_ppm(app.win->pal_window, dump);
      my_theme_destroy(app.light);
      my_theme_destroy(app.dark);
      my_window_manager_destroy(app.wm);
      my_pal_main_loop_destroy(app.loop);
      my_pal_destroy(app.pal);
      return 0;
    }
  }
#endif

  my_pal_main_loop_run(app.loop);

  my_theme_destroy(app.light);
  my_theme_destroy(app.dark);
  my_window_manager_destroy(app.wm);
  my_pal_main_loop_destroy(app.loop);
  my_pal_destroy(app.pal);
  return 0;
}
