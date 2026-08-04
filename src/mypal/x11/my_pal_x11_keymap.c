/**
 * @file my_pal_x11_keymap.c
 * @brief X11 KeySym -> my_key_t translation table.
 */
#include "mypal/x11/my_pal_x11_keymap.h"

#include <X11/keysym.h>

typedef struct keymap_entry_t {
  unsigned long keysym;
  uint32_t key;
} keymap_entry_t;

static const keymap_entry_t KEYMAP[] = {
    {XK_Return, MY_KEY_RETURN},   {XK_Escape, MY_KEY_ESCAPE},
    {XK_BackSpace, MY_KEY_BACKSPACE}, {XK_Tab, MY_KEY_TAB},
    {XK_Left, MY_KEY_LEFT},       {XK_Right, MY_KEY_RIGHT},
    {XK_Up, MY_KEY_UP},           {XK_Down, MY_KEY_DOWN},
    {XK_Home, MY_KEY_HOME},       {XK_End, MY_KEY_END},
    {XK_Prior, MY_KEY_PAGE_UP},   {XK_Next, MY_KEY_PAGE_DOWN},
    {XK_Insert, MY_KEY_INSERT},   {XK_Delete, MY_KEY_DELETE},
    {XK_F1, MY_KEY_F1},           {XK_F2, MY_KEY_F2},
    {XK_F3, MY_KEY_F3},           {XK_F4, MY_KEY_F4},
    {XK_F5, MY_KEY_F5},           {XK_F6, MY_KEY_F6},
    {XK_F7, MY_KEY_F7},           {XK_F8, MY_KEY_F8},
    {XK_F9, MY_KEY_F9},           {XK_F10, MY_KEY_F10},
    {XK_F11, MY_KEY_F11},         {XK_F12, MY_KEY_F12},
};

uint32_t my_pal_x11_key_from_keysym(unsigned long keysym) {
  size_t i;
  if (keysym >= 32 && keysym <= 126) {
    return (uint32_t)keysym; /* printable ASCII: identity */
  }
  for (i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); i++) {
    if (KEYMAP[i].keysym == keysym) {
      return KEYMAP[i].key;
    }
  }
  return MY_KEY_UNKNOWN;
}
