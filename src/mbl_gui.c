#include "../include/mbl_gui.h"
#include "../include/mbl_sutf_gui.h"


static const mbl_gui_theme_t g_theme = {
    .background = 0x000F0C20U,  /* Deep dark violet background */
    .panel      = 0x001B152EU,  /* Dark panel container */
    .accent     = 0x0000E5FFU,  /* Neon cyan accent glow */
    .text       = 0x00FFFFFFU,  /* Bright white text */
    .text_dim   = 0x00A0A0C0U,  /* Dim text */
    .highlight  = 0x004DEEEAU   /* Cyan glow border */
};

/* 12x16 Neon Cyan Mouse Arrow Cursor Sprite Mask */
static const uint16_t g_mouse_sprite[16] = {
    0b1000000000000000,
    0b1100000000000000,
    0b1110000000000000,
    0b1111000000000000,
    0b1111100000000000,
    0b1111110000000000,
    0b1111111000000000,
    0b1111111100000000,
    0b1111111110000000,
    0b1111110000000000,
    0b1110111000000000,
    0b1100011100000000,
    0b1000001110000000,
    0b0000000111000000,
    0b0000000011000000,
    0b0000000000000000
};

void mbl_gui_init_defaults(mbl_gui_menu_t *menu) {
    menu->title = "Secure System Select";
    menu->button_count = 4;

    uint32_t start_y = 280;
    uint32_t btn_w   = 360;
    uint32_t btn_h   = 44;
    uint32_t start_x = (1024 - btn_w) / 2;

    menu->buttons[0] = (mbl_gui_button_t){"1. OpenWindows Kernel (Secured)", MBL_ACTION_BOOT_KERNEL, start_x, start_y,       btn_w, btn_h, g_theme.panel, g_theme.accent, 1};
    menu->buttons[1] = (mbl_gui_button_t){"2. Boot from Device",           MBL_ACTION_BOOT_DEVICE, start_x, start_y + 56,  btn_w, btn_h, g_theme.panel, g_theme.accent, 0};
    menu->buttons[2] = (mbl_gui_button_t){"3. System Configuration",        MBL_ACTION_SYS_CONFIG,  start_x, start_y + 112, btn_w, btn_h, g_theme.panel, g_theme.accent, 0};
    menu->buttons[3] = (mbl_gui_button_t){"4. Shutdown",                    MBL_ACTION_SHUTDOWN,    start_x, start_y + 168, btn_w, btn_h, g_theme.panel, g_theme.accent, 0};
}

void mbl_gui_draw_emblem(mbl_framebuffer_t *fb, uint32_t cx, uint32_t top_y) {
    /* Draw stylized geometric 'M' logo emblem in bright cyan-white vector line segments */
    uint32_t c = g_theme.accent;
    uint32_t w = 0x00FFFFFFU;

    /* Outer Shield Lines */
    mbl_framebuffer_draw_line(fb, cx - 40, top_y, cx, top_y + 40, c);
    mbl_framebuffer_draw_line(fb, cx, top_y + 40, cx + 40, top_y, c);
    mbl_framebuffer_draw_line(fb, cx - 40, top_y, cx - 40, top_y + 50, c);
    mbl_framebuffer_draw_line(fb, cx + 40, top_y, cx + 40, top_y + 50, c);
    mbl_framebuffer_draw_line(fb, cx - 40, top_y + 50, cx, top_y + 90, c);
    mbl_framebuffer_draw_line(fb, cx + 40, top_y + 50, cx, top_y + 90, c);

    /* Inner Chevron 'M' Lines */
    mbl_framebuffer_draw_line(fb, cx - 25, top_y + 20, cx, top_y + 45, w);
    mbl_framebuffer_draw_line(fb, cx, top_y + 45, cx + 25, top_y + 20, w);
    mbl_framebuffer_draw_line(fb, cx - 25, top_y + 20, cx - 25, top_y + 60, w);
    mbl_framebuffer_draw_line(fb, cx + 25, top_y + 20, cx + 25, top_y + 60, w);
    mbl_framebuffer_draw_line(fb, cx - 25, top_y + 60, cx, top_y + 75, w);
    mbl_framebuffer_draw_line(fb, cx, top_y + 75, cx + 25, top_y + 60, w);
}

void mbl_gui_draw_cursor(mbl_framebuffer_t *fb, int32_t mx, int32_t my) {
    if (mx < 0 || my < 0 || (uint32_t)mx >= fb->width || (uint32_t)my >= fb->height) return;
    int r, c;
    for (r = 0; r < 16; ++r) {
        uint16_t row_bits = g_mouse_sprite[r];
        for (c = 0; c < 12; ++c) {
            if (row_bits & (0x8000U >> c)) {
                mbl_framebuffer_put_pixel(fb, (uint32_t)mx + c, (uint32_t)my + r, g_theme.accent);
            }
        }
    }
}

void mbl_gui_render_menu(mbl_framebuffer_t *fb, mbl_boot_state_t *state, mbl_gui_menu_t *menu, mbl_mouse_state_t *mouse) {
    mbl_sutf_renderer_t utf;
    uint32_t i;

    /* 1. Clear Framebuffer to Dark Violet Background */
    mbl_framebuffer_clear(fb, g_theme.background);

    /* 2. Draw Top Geometric Emblem */
    mbl_gui_draw_emblem(fb, fb->width / 2, 110);

    /* 3. Render Subtitle Section Header */
    mbl_sutf_init(&utf, fb, (fb->width - (20 * MBL_FONT_WIDTH)) / 2, 225, g_theme.text);
    utf.bold = true;
    mbl_sutf_puts_sutf8(&utf, menu->title);

    /* 4. Render Dynamic Menu List Entries */
    for (i = 0; i < menu->button_count; ++i) {
        mbl_gui_button_t *btn = &menu->buttons[i];
        btn->selected = (state->selected_entry == i);

        if (btn->selected) {
            /* Draw glowing cyan rounded selection box */
            mbl_framebuffer_draw_round_rect(fb, btn->x, btn->y, btn->width, btn->height, g_theme.highlight, g_theme.panel);

            /* Draw glowing cyan left chevron '>' indicator */
            mbl_sutf_init(&utf, fb, btn->x - 24, btn->y + 14, g_theme.accent);
            utf.bold = true;
            mbl_sutf_puts_sutf8(&utf, ">");

            /* Draw selected text in white */
            uint32_t text_len = 0;
            const char *p = btn->label;
            while (*p++) text_len++;
            uint32_t text_x = btn->x + (btn->width - (text_len * MBL_FONT_WIDTH)) / 2;
            mbl_sutf_init(&utf, fb, text_x, btn->y + 14, g_theme.text);
            utf.bold = true;
            mbl_sutf_puts_sutf8(&utf, btn->label);
        } else {
            /* Draw inactive text cleanly without box */
            uint32_t text_len = 0;
            const char *p = btn->label;
            while (*p++) text_len++;
            uint32_t text_x = btn->x + (btn->width - (text_len * MBL_FONT_WIDTH)) / 2;
            mbl_sutf_init(&utf, fb, text_x, btn->y + 14, g_theme.text_dim);
            utf.bold = false;
            mbl_sutf_puts_sutf8(&utf, btn->label);
        }
    }

    /* 5. Render Software Mouse Cursor */
    if (mouse && mouse->visible) {
        mbl_gui_draw_cursor(fb, mouse->x, mouse->y);
    }
}

void mbl_gui_handle_keyboard(mbl_boot_state_t *state, mbl_gui_menu_t *menu, uint8_t scancode) {
    if (scancode == 0x48 || scancode == 0x11) { /* Up Arrow / 'W' */
        if (state->selected_entry > 0) {
            state->selected_entry--;
        }
    } else if (scancode == 0x50 || scancode == 0x1F) { /* Down Arrow / 'S' */
        if (state->selected_entry < menu->button_count - 1) {
            state->selected_entry++;
        }
    } else if (scancode >= 0x02 && scancode <= 0x05) { /* Number Keys '1' - '4' */
        uint32_t idx = scancode - 0x02;
        if (idx < menu->button_count) {
            state->selected_entry = idx;
        }
    }
}

void mbl_gui_handle_mouse(mbl_boot_state_t *state, mbl_gui_menu_t *menu, mbl_mouse_state_t *mouse, uint8_t b0, uint8_t b1, uint8_t b2) {
    if (!mouse) return;
    int rel_x = (int8_t)b1;
    int rel_y = (int8_t)b2;

    mouse->x += rel_x;
    mouse->y -= rel_y; /* Y-axis inverted in PS/2 */

    if (mouse->x < 0) mouse->x = 0;
    if (mouse->y < 0) mouse->y = 0;
    if (mouse->x >= 1024) mouse->x = 1023;
    if (mouse->y >= 768) mouse->y = 767;

    mouse->left_button = (b0 & 0x01);
    mouse->right_button = (b0 & 0x02);

    /* Check mouse hover over button hitboxes */
    uint32_t i;
    for (i = 0; i < menu->button_count; ++i) {
        mbl_gui_button_t *btn = &menu->buttons[i];
        if ((uint32_t)mouse->x >= btn->x && (uint32_t)mouse->x <= (btn->x + btn->width) &&
            (uint32_t)mouse->y >= btn->y && (uint32_t)mouse->y <= (btn->y + btn->height)) {
            state->selected_entry = i;
            break;
        }
    }
}
