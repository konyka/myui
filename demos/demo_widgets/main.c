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
#include "mymvvm_myui/my_mvvm.h"
#include "myui/my_animator.h"
#include "myui/my_layout.h"
#include "myui/my_window_manager.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_checkbox.h"
#include "myui/widgets/my_image.h"

#include "stb/stb_image_write.h"
#include "myui/widgets/my_label.h"
#include "myui/widgets/my_list_view.h"
#include "myui/widgets/my_progress_bar.h"
#include "myui/widgets/my_scroll_bar.h"
#include "myui/widgets/my_slider.h"
#include "myui/widgets/my_text_area.h"

/* simple code-driven adapter for the demo list_view (500 rows) */
static size_t demo_row_count(my_list_adapter_t* adapter) {
  (void)adapter;
  return 500;
}

static my_widget_t* demo_create_row(my_list_adapter_t* adapter) {
  (void)adapter;
  return my_widget_create(NULL, "row");
}

static void demo_bind_row(my_list_adapter_t* adapter, my_widget_t* row,
                          size_t index) {
  char text[32];
  my_widget_t* label;
  (void)adapter;
  while (my_widget_child_count(row) > 0) {
    my_widget_remove_child(row, my_widget_get_child(row, 0));
  }
  snprintf(text, sizeof(text), "row %zu", index);
  label = my_label_create(NULL, text);
  my_widget_set_rect(label, &(my_rect_t){4, 2, 180, 20});
  my_widget_add_child(row, label);
  my_widget_unref(label);
}

static const my_list_adapter_vtable_t DEMO_ADAPTER = {
    demo_row_count, demo_create_row, demo_bind_row, NULL};
static my_list_adapter_t g_demo_adapter = {&DEMO_ADAPTER};

/** @brief Prefer a system TTF (stb backend), fall back to the 8x8 font. */
static my_font_t* create_default_font(void) {
  static const char* candidates[] = {
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf", NULL};
  my_font_t* font = NULL;
  int i;
  for (i = 0; candidates[i] != NULL && font == NULL; i++) {
    font = my_font_stb_create(NULL, candidates[i], 0);
  }
  if (font == NULL) {
    font = my_font_bitmap_create(NULL);
  }
  return font;
}

#ifdef MYUI_PAL_DUMMY
#include "mypal/dummy/my_pal_dummy.h"
#include "myr/my_lcd_mem.h"
#endif

static my_font_t* app_font = NULL;

typedef struct app_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_theme_t* light;
  my_theme_t* dark;
  bool is_dark;
  my_widget_t* anim_btn;
  my_view_model_t* vm;
  my_mvvm_context_t* mc;
  my_window_t* win_ar; /**< i18n Arabic window (M11a, NULL when skipped) */
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
  my_widget_set_layout_params(app->anim_btn, "w:160 h:48");
  my_widget_add_child(root, app->anim_btn);
  my_widget_unref(app->anim_btn);
  my_widget_on(app->anim_btn, "click", on_anim_click, app);

  /* controls row: checkbox + slider (TwoWay) + progress bar (OneWay) */
  {
    my_widget_t* row2 = my_widget_create(NULL, "row2");
    my_widget_t* cb = my_checkbox_create(NULL, "notify");
    my_widget_t* slider = my_slider_create(NULL);
    my_widget_t* bar = my_progress_bar_create(NULL);

    my_widget_set_layout_params(row2, "h:40");
    my_widget_set_layouter(row2, my_layouter_linear_create(NULL, true, 12));
    my_widget_add_child(root, row2);
    my_widget_unref(row2);

    my_widget_set_layout_params(cb, "w:120 h:32");
    my_widget_add_child(row2, cb);
    my_widget_unref(cb);

    my_widget_set_layout_params(slider, "w:240 h:32");
    my_widget_set_bind_rules(slider, "v:value={volume, Mode=TwoWay}");
    my_widget_add_child(row2, slider);
    my_widget_unref(slider);

    my_widget_set_layout_params(bar, "w:160 h:16");
    my_widget_set_bind_rules(bar, "v:value={volume}");
    my_widget_add_child(row2, bar);
    my_widget_unref(bar);
  }

  /* list_view (500 virtualized rows) + linked scroll_bar + image */
  {
    my_widget_t* lv = my_list_view_create(NULL);
    my_widget_t* bar = my_scroll_bar_create(NULL);
    my_widget_t* img = my_image_create(NULL);
    my_widget_t* lvrow = my_widget_create(NULL, "lvrow");
    my_widget_set_layout_params(lvrow, "h:150");
    my_widget_set_layouter(lvrow, my_layouter_linear_create(NULL, true, 2));
    my_widget_add_child(root, lvrow);
    my_widget_unref(lvrow);

    my_widget_set_layout_params(lv, "w:1f h:150");
    my_list_view_set_row_height(lv, 24);
    my_list_view_set_adapter(lv, &g_demo_adapter);
    my_widget_add_child(lvrow, lv);
    my_widget_unref(lv);

    my_widget_set_layout_params(bar, "w:14 h:150");
    my_widget_add_child(lvrow, bar);
    my_widget_unref(bar);
    my_list_view_set_scroll_bar(lv, bar);

    my_widget_set_layout_params(img, "h:96");
    my_image_set_image(img, "/tmp/myui_demo.png");
    my_image_set_scale_mode(img, MY_IMAGE_SCALE_FIT);
    my_widget_add_child(root, img);
    my_widget_unref(img);
  }

  /* multi-line text area demo */
  {
    my_widget_t* ta = my_text_area_create(NULL);
    my_widget_set_layout_params(ta, "h:1f");
    my_text_area_set_font(ta, app_font, 16);
    my_text_area_set_text(ta, "line one\nline two\nline three\n"
                              "edit me: click, type, arrows work\n"
                              "a long line that wraps: the quick brown fox "
                              "jumps over the lazy dog, pack my box with "
                              "five dozen liquor jugs and keep going. How "
                              "vexingly quick daft zebras jump! Bright "
                              "vixens jump; dozy fowl quack and quack\n"
                              "line five\nline six\nline seven");
    my_text_area_set_wrap(ta, true);
    my_widget_set_bind_rules(ta, "v:text={note, Mode=TwoWay}");
    my_widget_add_child(root, ta);
    my_widget_unref(ta);
  }
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

/** @brief i18n demo window (M11a): one label in the given script, shown
 * with a script-appropriate font; window is only created when the font
 * file exists. RTL shaping/reorder happens in draw_text. Returns the
 * window (borrowed, owned by the wm) or NULL. */
static my_window_t* i18n_window(app_t* app, const char* title,
                                const char* font_path, const char* text) {
  my_font_t* font = my_font_stb_create(NULL, font_path, 0);
  my_window_t* win;
  my_widget_t* label;
  if (font == NULL) {
    return NULL; /* font not installed: skip this window */
  }
  win = my_window_create(NULL, app->pal, 480, 140, title);
  my_window_set_font(win, font, 28);
  my_widget_set_layouter(my_window_widget(win),
                         my_layouter_linear_create(NULL, false, 8));
  label = my_label_create(NULL, text);
  my_widget_set_layout_params(label, "h:60");
  my_widget_add_child(my_window_widget(win), label);
  my_widget_unref(label);
  my_window_manager_open(app->wm, win);
  my_widget_unref(my_window_widget(win));
  return win;
}

int main(void) {
  app_t app;
  memset(&app, 0, sizeof(app));

  app.pal = my_pal_create(NULL);
  app.loop = my_pal_main_loop_create(app.pal);
  app.wm = my_window_manager_create(NULL, app.pal, app.loop);
  /* test image: generated at runtime, no binary assets */
  {
    uint8_t px[64 * 48 * 4];
    int x, y;
    for (y = 0; y < 48; y++) {
      for (x = 0; x < 64; x++) {
        uint8_t* q = px + (y * 64 + x) * 4;
        q[0] = (uint8_t)(x * 4);
        q[1] = (uint8_t)(y * 5);
        q[2] = 160;
        q[3] = ((x / 8 + y / 8) % 2) ? 255 : 90; /* checker alpha */
      }
    }
    stbi_write_png("/tmp/myui_demo.png", 64, 48, 4, px, 64 * 4);
  }
  app.light = make_light_theme();
  app.dark = make_dark_theme();
  if (app.pal == NULL || app.loop == NULL || app.wm == NULL || app.light == NULL ||
      app.dark == NULL) {
    fprintf(stderr, "demo_widgets: init failed\n");
    return 1;
  }

  app.win = my_window_create(NULL, app.pal, 800, 560, "myui demo_widgets");
  my_window_set_theme(app.win, app.light, false);
#ifndef MYUI_PAL_DUMMY
  /* MYUI_DEMO_GLES=1: render through the GLES2 backend on a real GL
   * window (M10c); falls back to the soft path when unavailable */
  if (getenv("MYUI_DEMO_GLES") != NULL) {
    if (my_window_enable_gl(app.win) == MY_RET_OK) {
      printf("demo_widgets: GLES rendering enabled\n");
    } else {
      fprintf(stderr, "demo_widgets: GLES unavailable, using soft path\n");
    }
  }
#endif
  {
    app_font = create_default_font();
    my_window_set_font(app.win, app_font, 16);
    my_value_t v;
    app.vm = my_view_model_dummy_create(NULL);
    my_value_init(&v, NULL);
    my_value_set_double(&v, 30.0);
    my_view_model_set_prop(app.vm, "volume", &v);
    my_value_reset(&v);
  }
  build_ui(&app);
  my_window_manager_open(app.wm, app.win);
  app.mc = my_mvvm_bind(app.wm, app.win, app.vm);
  my_widget_unref(my_window_widget(app.win));

  /* i18n (M11a): Arabic + Hebrew RTL windows (skipped when the Noto
   * fonts are not installed) */
  app.win_ar = i18n_window(&app, "myui Arabic (RTL)",
              "/usr/share/fonts/google-noto-vf/NotoNaskhArabic[wght].ttf",
              "\xD8\xA7\xD9\x84\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 "
              "\xD8\xB9\xD9\x84\xD9\x8A\xD9\x83\xD9\x85"); /* السلام عليكم */
  i18n_window(&app, "myui Hebrew (RTL)",
              "/usr/share/fonts/google-noto-vf/NotoSansHebrew[wght].ttf",
              "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"); /* שלום */

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      my_event_t e;
      /* layout + first paint so widgets have real rects, then simulate */
      my_window_paint(app.win);
      click_widget(app.win, my_widget_find_child(
                                my_widget_get_child(my_window_widget(app.win), 1),
                                "toggle"));
      click_widget(app.win, app.anim_btn);
      /* drag the slider (row2: checkbox x0..120, slider x132..372, y~164..196)
       * to ~2/3: volume -> progress bar via MVVM */
      e = my_event_init(MY_EVENT_POINTER_DOWN);
      e.u.pointer.x = 300;
      e.u.pointer.y = 180;
      my_window_on_pal_event(app.win, &e);
      e = my_event_init(MY_EVENT_POINTER_UP);
      e.u.pointer.x = 300;
      e.u.pointer.y = 180;
      my_window_on_pal_event(app.win, &e);
      /* focus the text_area, type, then Ctrl+Z the last stream */
      {
        my_widget_t* root = my_window_widget(app.win);
        size_t i, n = my_widget_child_count(root);
        my_widget_t* ta = NULL;
        for (i = 0; i < n; i++) {
          my_widget_t* c = my_widget_get_child(root, i);
          if (strcmp(c->widget_type, "text_area") == 0) {
            ta = c;
          }
        }
        if (ta != NULL) {
          static const char typed[] = "UNDO-DEMO";
          my_event_t e;
          int32_t cx = ta->rect.w / 2, cy = ta->rect.y + 10;
          size_t k;
          my_widget_local_to_global(ta, &cx, &cy);
          e = my_event_init(MY_EVENT_POINTER_DOWN);
          e.u.pointer.x = cx;
          e.u.pointer.y = cy;
          my_window_on_pal_event(app.win, &e);
          for (k = 0; typed[k] != '\0'; k++) {
            e = my_event_init(MY_EVENT_KEY_DOWN);
            e.u.key.key = (uint8_t)typed[k];
            my_window_on_pal_event(app.win, &e);
          }
          /* Ctrl+Z: removes the whole "UNDO-DEMO" stream at once */
          e = my_event_init(MY_EVENT_KEY_DOWN);
          e.u.key.key = 'z';
          e.u.key.modifiers = MY_KEYMOD_CTRL;
          my_window_on_pal_event(app.win, &e);
        }
      }
      my_pal_dummy_set_now_ms(app.pal, 200);
      my_pal_main_loop_run(app.loop);
      dump_ppm(app.win->pal_window, dump);
      if (app.win_ar != NULL) {
        my_widget_invalidate(my_window_widget(app.win_ar), NULL);
        my_window_paint(app.win_ar);
        dump_ppm(app.win_ar->pal_window, "/tmp/myui_demo_arabic.ppm");
      }
      my_mvvm_context_destroy(app.mc);
      my_view_model_unref(app.vm);
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

  my_mvvm_context_destroy(app.mc);
  my_view_model_unref(app.vm);
  my_theme_destroy(app.light);
  my_theme_destroy(app.dark);
  my_window_manager_destroy(app.wm);
  my_pal_main_loop_destroy(app.loop);
  my_pal_destroy(app.pal);
  return 0;
}
