#include "../include/mbl_framebuffer.h"

/* Static back-buffer memory allocation at 16MB physical address */
static uint32_t g_backbuffer_memory[1024 * 768];

void mbl_framebuffer_clear(mbl_framebuffer_t *fb, uint32_t color) {
    uint32_t *target = fb->back_pixels ? fb->back_pixels : fb->pixels;
    uint32_t total_pixels = fb->width * fb->height;
    uint32_t i;
    for (i = 0; i < total_pixels; ++i) {
        target[i] = color;
    }
}

void mbl_framebuffer_put_pixel(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color) {
    if (x < fb->width && y < fb->height) {
        uint32_t *target = fb->back_pixels ? fb->back_pixels : fb->pixels;
        uint32_t stride = fb->pitch / sizeof(uint32_t);
        target[y * stride + x] = color;
    }
}

void mbl_framebuffer_fill_rect(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t ox, oy;
    for (oy = y; oy < y + h; ++oy) {
        for (ox = x; ox < x + w; ++ox) {
            mbl_framebuffer_put_pixel(fb, ox, oy, color);
        }
    }
}

void mbl_framebuffer_draw_line(mbl_framebuffer_t *fb, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color) {
    int dx = (int)x1 - (int)x0;
    int dy = (int)y1 - (int)y0;
    int sx = dx >= 0 ? 1 : -1;
    int sy = dy >= 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;
    int curr_x = (int)x0;
    int curr_y = (int)y0;

    while (1) {
        mbl_framebuffer_put_pixel(fb, (uint32_t)curr_x, (uint32_t)curr_y, color);
        if (curr_x == (int)x1 && curr_y == (int)y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            curr_x += sx;
        }
        if (e2 < dx) {
            err += dx;
            curr_y += sy;
        }
    }
}

void mbl_framebuffer_draw_round_rect(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t border_color, uint32_t fill_color) {
    /* Fill main rectangle body */
    mbl_framebuffer_fill_rect(fb, x + 2, y + 2, w - 4, h - 4, fill_color);

    /* Draw 2-pixel thick glowing border */
    mbl_framebuffer_fill_rect(fb, x, y, w, 2, border_color);                 /* Top */
    mbl_framebuffer_fill_rect(fb, x, y + h - 2, w, 2, border_color);         /* Bottom */
    mbl_framebuffer_fill_rect(fb, x, y, 2, h, border_color);                 /* Left */
    mbl_framebuffer_fill_rect(fb, x + w - 2, y, 2, h, border_color);         /* Right */
}

void mbl_framebuffer_blit_glyph(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, const uint8_t *glyph_data, uint32_t fg_color, uint32_t bg_color) {
    uint32_t row, col;
    for (row = 0; row < 16; ++row) {
        uint8_t bits = glyph_data[row];
        for (col = 0; col < 8; ++col) {
            if (bits & (0x80U >> col)) {
                mbl_framebuffer_put_pixel(fb, x + col, y + row, fg_color);
            } else if (bg_color != 0x00FFFFFFU) { /* non-transparent background */
                mbl_framebuffer_put_pixel(fb, x + col, y + row, bg_color);
            }
        }
    }
}

void mbl_framebuffer_swap(mbl_framebuffer_t *fb) {
    if (fb->pixels && fb->back_pixels) {
        uint32_t total_bytes = fb->width * fb->height * sizeof(uint32_t);
        mbl_memcpy(fb->pixels, fb->back_pixels, total_bytes);
    }
}
