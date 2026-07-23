#include "../include/mbl_core.h"
#include "../include/mbl_framebuffer.h"
#include "../include/mbl_gui.h"
#include "../include/io.h"


void *mbl_memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void *mbl_memset(void *dest, int val, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    size_t i;
    for (i = 0; i < count; ++i) {
        d[i] = (uint8_t)val;
    }
    return dest;
}

/* Static backbuffer pixel array (1024x768x4 = 3,145,728 bytes) */
static uint32_t g_backbuffer[1024 * 768];

void mbl_main(mbl_boot_state_t *state) {
    mbl_framebuffer_t fb;
    mbl_gui_menu_t menu;
    mbl_mouse_state_t mouse;

    /* 1. Setup Framebuffer Structure from Stage2 Boot Info Block */
    if (state && state->framebuffer) {
        fb.pixels = state->framebuffer->pixels;
        fb.width  = state->framebuffer->width;
        fb.height = state->framebuffer->height;
        fb.pitch  = state->framebuffer->pitch;
        fb.bpp    = state->framebuffer->bpp;
    } else {
        /* Default VBE 1024x768x32 fallback */
        fb.pixels = (uint32_t *)0xE0000000;
        fb.width  = 1024;
        fb.height = 768;
        fb.pitch  = 4096;
        fb.bpp    = 32;
    }
    fb.back_pixels = g_backbuffer;
    if (state) state->framebuffer = &fb;

    /* 2. Initialize Kernel Mode-Switching Subsystem */
    if (state) {
        sucs_init_boot_config(&state->mode_cfg, SUCS_MODE_BASE);
        sucs_commit_mode_on_boot(&state->mode_cfg);
    }

    /* 3. Initialize Default GUI Menu Choices */
    mbl_gui_init_defaults(&menu);

    /* 4. Initialize Mouse State */
    mouse.x = 512;
    mouse.y = 384;
    mouse.left_button = 0;
    mouse.right_button = 0;
    mouse.visible = 1;

    /* Initial Render Pass & Double Buffer Swap */
    mbl_gui_render_menu(&fb, state, &menu, &mouse);
    mbl_framebuffer_swap(&fb);

    /* 5. Main Bootloader Event Loop (Keyboard & Mouse Input Polling) */
    uint8_t mouse_bytes[3];
    uint8_t mouse_cycle = 0;

    while (1) {
        /* Check PS/2 Controller Output Buffer Status (Port 0x64 bit 0) */
        uint8_t status = inb(0x64);
        if (status & 0x01) {
            uint8_t data = inb(0x60);

            /* Check if byte is PS/2 Mouse Data (Port 0x64 bit 5) */
            if (status & 0x20) {
                mouse_bytes[mouse_cycle++] = data;
                if (mouse_cycle == 3) {
                    mouse_cycle = 0;
                    mbl_gui_handle_mouse(state, &menu, &mouse, mouse_bytes[0], mouse_bytes[1], mouse_bytes[2]);
                    mbl_gui_render_menu(&fb, state, &menu, &mouse);
                    mbl_framebuffer_swap(&fb);
                }
            } else {
                /* Keyboard Scancode */
                if (!(data & 0x80)) { /* Key Press (make code) */
                    if (data == 0x1C) { /* ENTER Key */
                        mbl_boot_action_t action = menu.buttons[state->selected_entry].action;
                        if (action == MBL_ACTION_SHUTDOWN) {
                            mbl_poweroff();
                        } else if (action == MBL_ACTION_SYS_CONFIG) {
                            /* Toggle Kernel Mode */
                            sucs_kernel_mode_t cur = state->mode_cfg.active_mode;
                            sucs_request_mode_switch(cur == SUCS_MODE_BASE ? SUCS_MODE_EXTENDED : SUCS_MODE_BASE);
                            sucs_commit_mode_on_boot(&state->mode_cfg);
                        } else if (action == MBL_ACTION_BOOT_KERNEL) {
                            break; /* Exit loop and jump to kernel */
                        }
                    }
                    mbl_gui_handle_keyboard(state, &menu, data);
                    mbl_gui_render_menu(&fb, state, &menu, &mouse);
                    mbl_framebuffer_swap(&fb);
                }
            }
        }
        io_wait();
    }

    /* 6. Execute Kernel Handoff */
    extern void mbl_load_and_jump(mbl_boot_state_t *state, void *kernel_target_addr, const void *kernel_src_addr, size_t image_size);
    mbl_load_and_jump(state, (void *)0x100000, (const void *)0x200000, 0x40000);
}
