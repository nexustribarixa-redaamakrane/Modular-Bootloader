/*
 * gop.c - GOP framebuffer text renderer (replaces vga.c for UEFI).
 *
 * Renders 8x16 bitmap font glyphs directly into the linear framebuffer
 * exposed by EFI_GRAPHICS_OUTPUT_PROTOCOL.  Provides the same public
 * API as the original vga.c so menu.c and main.c need no changes.
 *
 * Attribute mapping (GRUB-style black & white):
 *   0x07 (ATTR_NORMAL)  -> white text on black background
 *   0x70 (ATTR_HILITE)  -> black text on white background (reverse video)
 *   0x0F (ATTR_BRIGHT)  -> bright white on black
 *   0x07 (ATTR_DIM)     -> same as normal
 */

#include "efi.h"
#include "mbl.h"
#include "efi_font.h"

/* Text grid dimensions (set from GOP mode info) */
static uint32_t g_text_cols = 80;
static uint32_t g_text_rows = 25;
static uint32_t g_fb_pitch;       /* bytes per scanline */
static uint32_t g_fb_hres;        /* horizontal resolution in pixels */
static uint32_t g_fb_vres;        /* vertical resolution in pixels */
static uint32_t g_pixel_format;   /* EFI_GRAPHICS_PIXEL_FORMAT */

/* Pixel colours */
#define COL_BLACK   0x00000000u
#define COL_WHITE   0x00FFFFFFu
#define COL_DIM     0x00AAAAAAu
#define COL_BRIGHT  0x00FFFFFFu
#define COL_ERROR   0x00FF4444u

/* Cursor position (character cell) */
static uint32_t g_row;
static uint32_t g_col;

/* ============================================================================
 * Initialization - called once from kmain after GOP is available
 * ============================================================================ */
void vga_init_gop(void)
{
    if (!gGOP || !gGOP->Mode || !gGOP->Mode->Info) {
        return;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gGOP->Mode->Info;
    g_fb_hres     = info->HorizontalResolution;
    g_fb_vres     = info->VerticalResolution;
    g_fb_pitch    = info->PixelsPerScanLine;
    g_pixel_format = info->PixelFormat;

    /* Compute text grid: 8-pixel-wide, 16-pixel-tall cells */
    g_text_cols = g_fb_hres / FONT_W;
    g_text_rows = g_fb_vres / FONT_H;
    if (g_text_cols > 200) g_text_cols = 200;  /* sanity cap */
    if (g_text_rows > 100) g_text_rows = 100;

    g_row = 0;
    g_col = 0;
}

/* ============================================================================
 * Low-level pixel output
 * ============================================================================ */
static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    uint8_t *fb = (uint8_t *)(UINTN)gGOP->Mode->FrameBufferBase;

    if (x >= g_fb_hres || y >= g_fb_vres) {
        return;
    }

    /* Handle different pixel formats */
    switch (g_pixel_format) {
    case PixelBlueGreenRedReserved8BitPerColor: {
        /* BGRA: Blue at offset 0, Green at 1, Red at 2, Reserved at 3 */
        uint8_t *px = fb + y * g_fb_pitch + x * 4;
        px[0] = (uint8_t)(color & 0xFF);          /* Blue  */
        px[1] = (uint8_t)((color >> 8) & 0xFF);   /* Green */
        px[2] = (uint8_t)((color >> 16) & 0xFF);  /* Red   */
        px[3] = 0;
        break;
    }
    case PixelRedGreenBlueReserved8BitPerColor: {
        /* RGBA: Red at offset 0, Green at 1, Blue at 2, Reserved at 3 */
        uint8_t *px = fb + y * g_fb_pitch + x * 4;
        px[0] = (uint8_t)((color >> 16) & 0xFF);  /* Red   */
        px[1] = (uint8_t)((color >> 8) & 0xFF);   /* Green */
        px[2] = (uint8_t)(color & 0xFF);           /* Blue  */
        px[3] = 0;
        break;
    }
    default:
        /* Fallback: assume BGRA (most common on x86 UEFI) */
        {
            uint8_t *px = fb + y * g_fb_pitch + x * 4;
            px[0] = (uint8_t)(color & 0xFF);
            px[1] = (uint8_t)((color >> 8) & 0xFF);
            px[2] = (uint8_t)((color >> 16) & 0xFF);
            px[3] = 0;
        }
        break;
    }
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color)
{
    uint32_t dy;
    for (dy = 0; dy < h; dy++) {
        uint32_t dx;
        for (dx = 0; dx < w; dx++) {
            put_pixel(x + dx, y + dy, color);
        }
    }
}

/* ============================================================================
 * Character rendering
 * ============================================================================ */
static void render_glyph(uint32_t col, uint32_t row, char ch,
                         uint32_t fg, uint32_t bg)
{
    const uint8_t *glyph = font_glyph(ch);
    uint32_t px = col * FONT_W;
    uint32_t py = row * FONT_H;
    int y, x;

    for (y = 0; y < FONT_H; y++) {
        uint8_t bits = glyph[y];
        for (x = 0; x < FONT_W; x++) {
            uint32_t c = (bits & (0x80 >> x)) ? fg : bg;
            put_pixel((uint32_t)(px + (uint32_t)x), (uint32_t)(py + (uint32_t)y), c);
        }
    }
}

/* Map a VGA-style attribute to foreground/background pixel colours */
static void attr_to_colors(uint8_t attr, uint32_t *fg, uint32_t *bg)
{
    /* Reverse video (0x70 = white-on-black reversed) */
    if (attr == 0x70u) {
        *fg = COL_BLACK;
        *bg = COL_WHITE;
        return;
    }
    /* Bright (0x0F) */
    if (attr == 0x0Fu) {
        *fg = COL_BRIGHT;
        *bg = COL_BLACK;
        return;
    }
    /* Error highlight (0x4F = red bg) */
    if (attr == 0x4Fu) {
        *fg = COL_WHITE;
        *bg = COL_ERROR;
        return;
    }
    /* Default: normal white on black */
    *fg = COL_WHITE;
    *bg = COL_BLACK;
}

/* ============================================================================
 * Public API (matches vga.c interface)
 * ============================================================================ */

void vga_clear(void)
{
    uint32_t fb_size = g_fb_pitch * g_fb_vres;
    uint8_t *fb = (uint8_t *)(UINTN)gGOP->Mode->FrameBufferBase;
    /* Fill framebuffer with black */
    gBS->SetMem(fb, fb_size, 0);
    g_row = 0;
    g_col = 0;
}

void vga_goto(uint8_t row, uint8_t col)
{
    if (row >= (uint8_t)g_text_rows) row = (uint8_t)(g_text_rows - 1);
    if (col >= (uint8_t)g_text_cols) col = (uint8_t)(g_text_cols - 1);
    g_row = row;
    g_col = col;
}

static void vga_advance(void)
{
    g_col++;
    if (g_col >= g_text_cols) {
        g_col = 0;
        if (g_row + 1 < g_text_rows) {
            g_row++;
        }
    }
}

void vga_putc(char c)
{
    if (c == '\n') {
        g_col = 0;
        if (g_row + 1 < g_text_rows) {
            g_row++;
        }
        return;
    }
    render_glyph(g_col, g_row, c, COL_WHITE, COL_BLACK);
    vga_advance();
}

void vga_puts(const char *s)
{
    while (*s) {
        vga_putc(*s);
        s++;
    }
}

void vga_write(uint8_t row, uint8_t col, const char *s, uint8_t attr)
{
    uint32_t fg, bg;
    uint32_t r = row, c = col;

    if (r >= g_text_rows || c >= g_text_cols) return;

    attr_to_colors(attr, &fg, &bg);

    while (*s && c < g_text_cols) {
        render_glyph(c, r, *s, fg, bg);
        s++;
        c++;
    }
}

void vga_fill(uint8_t row, uint8_t col, uint8_t ch, uint8_t n, uint8_t attr)
{
    uint32_t fg, bg;
    uint32_t r = row, c = col;
    uint8_t i;

    if (r >= g_text_rows || c >= g_text_cols) return;

    attr_to_colors(attr, &fg, &bg);

    for (i = 0; i < n && c < g_text_cols; i++) {
        render_glyph(c, r, (char)ch, fg, bg);
        c++;
    }
}
