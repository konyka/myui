/**
 * @file my_menu.h
 * @brief Popup / context menu (M13c): in-window overlay (no extra pal
 * window; works headless).
 *
 * A menu is a data model (items with id/text, optional child menu).
 * my_menu_popup() hangs a floating overlay on the window's root: the
 * menu box paints last (on top), clicks outside dismiss, items emit the
 * select callback with their id. Submenus cascade up to 3 levels.
 * Keyboard: Up/Down move the highlight, Enter activates, ESC dismisses.
 */
#ifndef MY_MENU_H
#define MY_MENU_H

#include "myui/my_window.h"

/** @brief Menu (opaque data model + popup state). */
typedef struct my_menu_t my_menu_t;

typedef void (*my_menu_select_cb)(void* ctx, int32_t id);

my_menu_t* my_menu_create(const my_allocator_t* allocator);
void my_menu_destroy(my_menu_t* menu);

/** @brief Append a leaf item (id reported on select). */
my_ret_t my_menu_add_item(my_menu_t* menu, const char* text, int32_t id);

/** @brief Append a submenu entry; returns the child menu (fill it). */
my_menu_t* my_menu_add_submenu(my_menu_t* menu, const char* text);

/** @brief Popup at window-local (x, y); flips at the right/bottom
 * edges. cb fires with the leaf item's id. */
my_ret_t my_menu_popup(my_window_t* win, my_menu_t* menu, int32_t x,
                       int32_t y, my_menu_select_cb cb, void* ctx);

/** @brief Dismiss the popup (and cascaded children). */
void my_menu_dismiss(my_menu_t* menu);

#endif /* MY_MENU_H */
