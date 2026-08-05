/**
 * @file main.c
 * @brief demo_mvvm: counter, person list (items binding) and navigation,
 * all driven by bindings — no manual UI-refresh code.
 *
 * Headless (dummy port): MYUI_DEMO_DUMP_PPM=<path> renders a few
 * interactions (increment, delete a row) and dumps the frame as PPM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mymvvm/my_view_model_array.h"
#include "mymvvm_myui/my_mvvm.h"
#include "myui/my_ui_loader.h"

#include "myc/my_str.h"
#include "myui/widgets/my_button.h"
#include "myui/widgets/my_edit.h"
#include "myui/widgets/my_label.h"

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

/* ---------------- model ---------------- */

typedef struct app_t {
  my_pal_t* pal;
  my_pal_main_loop_t* loop;
  my_window_manager_t* wm;
  my_window_t* win;
  my_view_model_t* vm;
  my_view_model_array_t* persons;
  my_mvvm_context_t* mc;
  my_navigator_wm_t* nav;
  my_font_t* font;
} app_t;

static void set_vm_int(my_view_model_t* vm, const char* name, int32_t i) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_int32(&v, i);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

static int32_t get_vm_int(my_view_model_t* vm, const char* name) {
  my_value_t v;
  int32_t r = 0;
  my_value_init(&v, NULL);
  if (my_view_model_get_prop(vm, name, &v) == MY_RET_OK) {
    r = my_value_get_int32(&v);
  }
  my_value_reset(&v);
  return r;
}

static void set_vm_str(my_view_model_t* vm, const char* name, const char* s) {
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_str(&v, s);
  my_view_model_set_prop(vm, name, &v);
  my_value_reset(&v);
}

static my_ret_t on_inc(void* ctx, const char* args) {
  my_view_model_t* vm = (my_view_model_t*)ctx;
  (void)args;
  set_vm_int(vm, "count", get_vm_int(vm, "count") + 1);
  return MY_RET_OK;
}

static my_ret_t on_dec(void* ctx, const char* args) {
  my_view_model_t* vm = (my_view_model_t*)ctx;
  (void)args;
  set_vm_int(vm, "count", get_vm_int(vm, "count") - 1);
  return MY_RET_OK;
}

static my_view_model_t* person(const char* name) {
  my_view_model_t* vm = my_view_model_dummy_create(NULL);
  my_value_t v;
  my_value_init(&v, NULL);
  my_value_set_str(&v, name);
  my_view_model_set_prop(vm, "name", &v);
  my_value_reset(&v);
  return vm;
}

/* ---------------- item template ---------------- */

typedef struct row_ctx_t {
  my_view_model_array_t* arr;
  my_pal_main_loop_t* loop;
} row_ctx_t;

static my_ret_t delete_first_row_later(void* ctx) {
  row_ctx_t* rc = (row_ctx_t*)ctx;
  if (my_view_model_array_get_count(rc->arr) > 0) {
    my_view_model_array_remove(rc->arr, 0);
  }
  return MY_RET_FAIL; /* one-shot */
}

static void on_delete_row(void* ctx, const char* event, void* data) {
  row_ctx_t* rc = (row_ctx_t*)ctx;
  (void)event;
  (void)data;
  /* defer the mutation: deleting a row rebuilds the list, which would
   * destroy the very button currently dispatching this click */
  my_pal_main_loop_add_timer(rc->loop, delete_first_row_later, rc, 1);
}

static my_widget_t* build_person_row(my_widget_t* parent, size_t index,
                                     my_item_props_fn_t props, void* props_ctx,
                                     void* builder_ctx) {
  my_value_t v;
  my_widget_t* btn;
  char text[64];
  (void)parent;
  my_value_init(&v, NULL);
  props(props_ctx, index, "name", &v);
  snprintf(text, sizeof(text), "del: %s",
           my_value_get_str(&v) != NULL ? my_value_get_str(&v) : "?");
  my_value_reset(&v);
  btn = my_button_create(NULL, text);
  my_widget_set_rect(btn, &(my_rect_t){0, (int32_t)index * 40, 200, 32});
  my_widget_on(btn, "click", on_delete_row, builder_ctx);
  return btn;
}

/* ---------------- detail page ---------------- */

static my_window_t* detail_page(my_pal_t* pal, const char* args, void* ctx) {
  my_window_t* win = my_window_create(NULL, pal, 400, 200, "detail");
  my_widget_t* label = my_label_create(NULL, "detail page");
  my_widget_t* back = my_button_create(NULL, "back");
  (void)args;
  (void)ctx;
  my_widget_set_rect(label, &(my_rect_t){20, 20, 200, 32});
  my_widget_add_child(my_window_widget(win), label);
  my_widget_unref(label);
  my_widget_set_rect(back, &(my_rect_t){20, 80, 120, 36});
  my_widget_set_bind_rules(back, "v:on_click={back, CloseWindow=true}");
  my_widget_add_child(my_window_widget(win), back);
  my_widget_unref(back);
  return win;
}

/* ---------------- main page ---------------- */

static my_ret_t on_submit(void* ctx, const char* args) {
  my_view_model_t* vm = (my_view_model_t*)ctx;
  my_value_t v;
  const char* name = "";
  (void)args;
  my_value_init(&v, NULL);
  if (my_view_model_get_prop(vm, "name", &v) == MY_RET_OK) {
    name = my_value_get_str(&v);
  }
  {
    char msg[80];
    snprintf(msg, sizeof(msg), "Hello, %s!", name != NULL ? name : "?");
    my_value_reset(&v);
    my_value_init(&v, NULL);
    my_value_set_str(&v, msg);
    my_view_model_set_prop(vm, "greeting", &v);
    my_value_reset(&v);
  }
  return MY_RET_OK;
}

static const char* MAIN_PAGE_XML =
    "<window w=\"800\" h=\"480\" title=\"myui demo_mvvm\">"
    "<style>"
    "button[ok].normal.bg_color=#A5D6A7 "
    "</style>"
    "<label name=\"count\" x=\"20\" y=\"20\" w=\"200\" h=\"32\" "
    "v:text=\"{count, Converter=int_to_str}\"/>"
    "<button x=\"20\" y=\"64\" w=\"90\" h=\"36\" text=\"+1\" "
    "v:on_click=\"{inc}\"/>"
    "<button x=\"120\" y=\"64\" w=\"90\" h=\"36\" text=\"-1\" "
    "v:on_click=\"{dec}\"/>"
    "<button x=\"240\" y=\"64\" w=\"120\" h=\"36\" text=\"detail\" "
    "v:on_click=\"{goto, ToPage=detail}\"/>"
    "<widget name=\"list\" x=\"20\" y=\"120\" w=\"300\" h=\"200\" "
    "v:items=\"{persons, ItemTemplate=person_row}\"/>"
    "<edit x=\"20\" y=\"380\" w=\"240\" h=\"32\" hint=\"your name\" "
    "v:text=\"{name, Mode=TwoWay, Validator=not_empty}\"/>"
    "<button x=\"280\" y=\"380\" w=\"120\" h=\"36\" text=\"submit\" "
    "v:on_click=\"{submit}\"/>"
    "<label x=\"420\" y=\"380\" w=\"240\" h=\"32\" v:text=\"{greeting}\"/>"
    "</window>";

static my_window_t* build_ui_xml(app_t* app) {
  my_widget_t* win = my_ui_load_str(NULL, app->pal, MAIN_PAGE_XML, NULL);
  if (win == NULL) {
    return NULL;
  }
  {
    /* the edit needs a font for click-to-locate measuring */
    size_t i, n = my_widget_child_count(win);
    for (i = 0; i < n; i++) {
      my_widget_t* c = my_widget_get_child(win, i);
      if (my_str_eq(c->widget_type, "edit")) {
        my_edit_set_font(c, app->font, 16);
      }
    }
  }
  return (my_window_t*)win;
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
  printf("demo_mvvm: dumped %s\n", path);
}

static void click_at(app_t* app, int32_t x, int32_t y) {
  my_event_t e = my_event_init(MY_EVENT_POINTER_DOWN);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_window_on_pal_event(app->win, &e);
  e = my_event_init(MY_EVENT_POINTER_UP);
  e.u.pointer.x = x;
  e.u.pointer.y = y;
  my_window_on_pal_event(app->win, &e);
}
#endif

int main(void) {
  app_t app;
  static row_ctx_t row_ctx;
  memset(&app, 0, sizeof(app));

  app.pal = my_pal_create(NULL);
  app.loop = my_pal_main_loop_create(app.pal);
  app.wm = my_window_manager_create(NULL, app.pal, app.loop);
  app.nav = my_navigator_wm_create(NULL, app.wm, app.pal);
  my_navigator_wm_add_page(app.nav, "detail", detail_page, NULL);
  my_navigator_set_default((my_navigator_t*)app.nav);

  app.vm = my_view_model_dummy_create(NULL);
  my_view_model_dummy_add_command(app.vm, "inc", on_inc, app.vm);
  my_view_model_dummy_add_command(app.vm, "dec", on_dec, app.vm);
  my_view_model_dummy_add_command(app.vm, "submit", on_submit, app.vm);
  set_vm_int(app.vm, "count", 0);
  set_vm_str(app.vm, "name", "");
  set_vm_str(app.vm, "greeting", "-");

  app.persons = my_view_model_array_dummy_create(NULL);
  my_view_model_array_dummy_push(app.persons, person("alice"));
  my_view_model_array_dummy_push(app.persons, person("bob"));
  my_view_model_array_dummy_push(app.persons, person("carol"));
  {
    my_value_t v;
    my_value_init(&v, NULL);
    my_value_set_pointer(&v, app.persons);
    my_view_model_set_prop(app.vm, "persons", &v);
    my_value_reset(&v);
  }

  row_ctx.arr = app.persons;
  row_ctx.loop = app.loop;
  my_mvvm_register_template("person_row", build_person_row, &row_ctx);

  app.font = create_default_font();
  app.win = build_ui_xml(&app);
  if (app.win == NULL) {
    fprintf(stderr, "demo_mvvm: failed to load XML UI\n");
    return 1;
  }
  my_window_set_font(app.win, app.font, 16);
  my_window_manager_open(app.wm, app.win);
  app.mc = my_mvvm_bind(app.wm, app.win, app.vm);
  my_widget_unref(my_window_widget(app.win));

#ifdef MYUI_PAL_DUMMY
  {
    const char* dump = getenv("MYUI_DEMO_DUMP_PPM");
    if (dump != NULL) {
      static const char typed[] = "myui";
      size_t i;
      click_at(&app, 65, 82);   /* +1 twice */
      click_at(&app, 65, 82);
      click_at(&app, 100, 136); /* delete first row (deferred via timer) */
      click_at(&app, 60, 396);  /* focus the name edit */
      for (i = 0; typed[i] != '\0'; i++) {
        my_event_t e = my_event_init(MY_EVENT_KEY_DOWN);
        e.u.key.key = (uint8_t)typed[i];
        my_window_on_pal_event(app.win, &e);
      }
      click_at(&app, 340, 396); /* submit */
      my_pal_dummy_set_now_ms(app.pal, 50);
      my_pal_main_loop_run(app.loop); /* fire the deferred delete */
      my_pal_dummy_set_now_ms(app.pal, 100);
      my_pal_main_loop_run(app.loop); /* next paint tick redraws */
      dump_ppm(app.win->pal_window, dump);
      my_mvvm_context_destroy(app.mc);
      my_view_model_array_unref(app.persons);
      my_view_model_unref(app.vm);
      my_navigator_wm_destroy(app.nav);
      my_window_manager_destroy(app.wm);
      my_pal_main_loop_destroy(app.loop);
      my_pal_destroy(app.pal);
      return 0;
    }
  }
#endif

  my_pal_main_loop_run(app.loop);

  my_mvvm_context_destroy(app.mc);
  my_view_model_array_unref(app.persons);
  my_view_model_unref(app.vm);
  my_navigator_wm_destroy(app.nav);
  my_window_manager_destroy(app.wm);
  my_pal_main_loop_destroy(app.loop);
  my_pal_destroy(app.pal);
  return 0;
}
