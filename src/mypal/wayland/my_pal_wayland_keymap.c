/**
 * @file my_pal_wayland_keymap.c
 * @brief xkb keysym -> my_key_t translation table.
 */
#include "mypal/wayland/my_pal_wayland_keymap.h"

#include <xkbcommon/xkbcommon-keysyms.h>

typedef struct wl_keymap_entry_t {
  uint32_t keysym;
  uint32_t key;
} wl_keymap_entry_t;

static const wl_keymap_entry_t KEYMAP[] = {
    {XKB_KEY_Return, MY_KEY_RETURN},   {XKB_KEY_Escape, MY_KEY_ESCAPE},
    {XKB_KEY_BackSpace, MY_KEY_BACKSPACE}, {XKB_KEY_Tab, MY_KEY_TAB},
    {XKB_KEY_Left, MY_KEY_LEFT},       {XKB_KEY_Right, MY_KEY_RIGHT},
    {XKB_KEY_Up, MY_KEY_UP},           {XKB_KEY_Down, MY_KEY_DOWN},
    {XKB_KEY_Home, MY_KEY_HOME},       {XKB_KEY_End, MY_KEY_END},
    {XKB_KEY_Prior, MY_KEY_PAGE_UP},   {XKB_KEY_Next, MY_KEY_PAGE_DOWN},
    {XKB_KEY_Insert, MY_KEY_INSERT},   {XKB_KEY_Delete, MY_KEY_DELETE},
    {XKB_KEY_F1, MY_KEY_F1},           {XKB_KEY_F2, MY_KEY_F2},
    {XKB_KEY_F3, MY_KEY_F3},           {XKB_KEY_F4, MY_KEY_F4},
    {XKB_KEY_F5, MY_KEY_F5},           {XKB_KEY_F6, MY_KEY_F6},
    {XKB_KEY_F7, MY_KEY_F7},           {XKB_KEY_F8, MY_KEY_F8},
    {XKB_KEY_F9, MY_KEY_F9},           {XKB_KEY_F10, MY_KEY_F10},
    {XKB_KEY_F11, MY_KEY_F11},         {XKB_KEY_F12, MY_KEY_F12},
};

uint32_t my_pal_wayland_key_from_keysym(uint32_t keysym) {
  size_t i;
  if (keysym >= 32 && keysym <= 126) {
    return keysym; /* printable ASCII: identity */
  }
  for (i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); i++) {
    if (KEYMAP[i].keysym == keysym) {
      return KEYMAP[i].key;
    }
  }
  return MY_KEY_UNKNOWN;
}
