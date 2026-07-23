#ifndef MBL_SUTF_GUI_H
#define MBL_SUTF_GUI_H

#include "mbl_core.h"
#include "mbl_framebuffer.h"
#include "sucs_types.h"
#include "sutf.h"

#define MBL_FONT_WIDTH  8
#define MBL_FONT_HEIGHT 16

typedef struct mbl_sutf_renderer {
    mbl_framebuffer_t *framebuffer;
    uint32_t           cursor_x;
    uint32_t           cursor_y;
    uint32_t           origin_x;
    uint32_t           color;
    uint32_t           bg_color;
    bool               bold;
} mbl_sutf_renderer_t;

void mbl_sutf_init(mbl_sutf_renderer_t *ctx, mbl_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t color);
void mbl_sutf_putchar_base(mbl_sutf_renderer_t *ctx, sucs_char_t cp);
void mbl_sutf_putchar_ext(mbl_sutf_renderer_t *ctx, sucs_ex_char_t ex_cp);
void mbl_sutf_puts_sutf8(mbl_sutf_renderer_t *ctx, const char *sutf8_text);
void mbl_sutf_puts_vsutf(mbl_sutf_renderer_t *ctx, const uint8_t *vsutf_stream, size_t stream_size);

#endif /* MBL_SUTF_GUI_H */
