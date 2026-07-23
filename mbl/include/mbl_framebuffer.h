#ifndef MBL_FRAMEBUFFER_H
#define MBL_FRAMEBUFFER_H

#include "mbl_core.h"

typedef struct mbl_framebuffer {
    uint32_t *pixels;
    uint32_t *back_pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t backbuffer_allocated;
} mbl_framebuffer_t;

void mbl_framebuffer_clear(mbl_framebuffer_t *fb, uint32_t color);
void mbl_framebuffer_put_pixel(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
void mbl_framebuffer_fill_rect(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void mbl_framebuffer_draw_line(mbl_framebuffer_t *fb, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
void mbl_framebuffer_draw_round_rect(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t border_color, uint32_t fill_color);
void mbl_framebuffer_blit_glyph(mbl_framebuffer_t *fb, uint32_t x, uint32_t y, const uint8_t *glyph_data, uint32_t fg_color, uint32_t bg_color);
void mbl_framebuffer_swap(mbl_framebuffer_t *fb);

#endif /* MBL_FRAMEBUFFER_H */
