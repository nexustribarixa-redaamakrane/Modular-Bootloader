#ifndef MBL_GUI_H
#define MBL_GUI_H

#include "mbl_core.h"
#include "mbl_framebuffer.h"

#define MBL_MAX_BOOT_ENTRIES 8

typedef struct mbl_gui_button {
    const char       *label;
    mbl_boot_action_t action;
    uint32_t          x;
    uint32_t          y;
    uint32_t          width;
    uint32_t          height;
    uint32_t          color;
    uint32_t          accent;
    uint8_t           selected;
} mbl_gui_button_t;

typedef struct mbl_gui_theme {
    uint32_t background;   /* Dark violet: 0x000F0C20 */
    uint32_t panel;        /* Dark panel: 0x001B152E */
    uint32_t accent;       /* Neon cyan: 0x0000E5FF */
    uint32_t text;         /* White text: 0x00FFFFFF */
    uint32_t text_dim;     /* Dim white text: 0x00A0A0C0 */
    uint32_t highlight;    /* Cyan glow: 0x004DEEEA */
} mbl_gui_theme_t;

typedef struct mbl_gui_menu {
    const char       *title;
    mbl_gui_button_t  buttons[MBL_MAX_BOOT_ENTRIES];
    uint32_t          button_count;
} mbl_gui_menu_t;

typedef struct mbl_mouse_state {
    int32_t  x;
    int32_t  y;
    uint8_t  left_button;
    uint8_t  right_button;
    uint8_t  visible;
} mbl_mouse_state_t;

void mbl_gui_init_defaults(mbl_gui_menu_t *menu);
void mbl_gui_render_menu(mbl_framebuffer_t *fb, mbl_boot_state_t *state, mbl_gui_menu_t *menu, mbl_mouse_state_t *mouse);
void mbl_gui_draw_emblem(mbl_framebuffer_t *fb, uint32_t center_x, uint32_t top_y);
void mbl_gui_draw_cursor(mbl_framebuffer_t *fb, int32_t x, int32_t y);
void mbl_gui_handle_keyboard(mbl_boot_state_t *state, mbl_gui_menu_t *menu, uint8_t scancode);
void mbl_gui_handle_mouse(mbl_boot_state_t *state, mbl_gui_menu_t *menu, mbl_mouse_state_t *mouse, uint8_t byte0, uint8_t byte1, uint8_t byte2);

#endif /* MBL_GUI_H */
