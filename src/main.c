/*
 * main.c - bootloader entry (kmain) and kernel handoff.
 *
 * Flow: probe the OpenWindows OWFS volume -> enumerate root catalog ->
 * GRUB-style menu with 10s timeout -> stream the selected kernel to
 * MBL_KERNEL_ADDR -> jump to it with a boot configuration block.
 */

#include "mbl.h"

#define ATTR_NORMAL 0x07u
#define ATTR_BRIGHT 0x0Fu

static mbl_entry_t g_entries[MBL_MENU_MAX];

static void fail(const char *msg) {
    vga_fill(12, 20, ' ', 40, ATTR_BRIGHT);
    vga_write(12, 20, msg, ATTR_BRIGHT);
    for (;;) {
        /* park */
    }
}

void kmain(void) {
    mbl_bootinfo_t *bi = (mbl_bootinfo_t *)MBL_BOOTINFO;
    uint8_t drive;
    int count;
    int sel;
    uint32_t size = 0;
    mbl_boot_config_t *cfg;

    vga_clear();

    if (bi->magic != MBL_MAGIC_BOOTINFO) {
        fail("No boot information block! Reboot.");
    }
    drive = bi->boot_drive;

    if (owfs_probe(drive) != 0) {
        vga_fill(10, 20, ' ', 40, ATTR_BRIGHT);
        vga_write(10, 20, "No OWFS volume found.", ATTR_BRIGHT);
        vga_fill(11, 20, ' ', 40, ATTR_BRIGHT);
        vga_write(11, 20, "Press Esc to reboot.", 0x07u);
        for (;;) {
            if (kbd_poll() == MBL_KEY_ESC || kbd_poll() == MBL_KEY_REBOOT) {
                kbd_reboot();
            }
        }
    }

    count = owfs_enumerate(g_entries, MBL_MENU_MAX);
    if (count < 0) {
        count = 0;
    }

    sel = menu_run(g_entries, count, 10);

    if (sel == -1) {
        kbd_reboot();
    }
    if (sel == -2) {
        outw(0x0604u, 0x2000u);     /* QEMU ACPI power off */
        for (;;) {
        }
    }
    if (sel < 0 || sel >= count) {
        kbd_reboot();
    }

    /* loading screen */
    vga_clear();
    vga_write(10, 30, "Loading kernel...", ATTR_BRIGHT);

    if (owfs_load_file(g_entries[sel].inode, MBL_KERNEL_ADDR, &size) != 0) {
        fail("Kernel load failed!");
    }
    if (size < 16) {
        fail("Kernel image too small!");
    }

    /* publish boot configuration for the kernel */
    cfg = (mbl_boot_config_t *)MBL_BOOTCONFIG;
    cfg->magic = MBL_MAGIC_BOOTCFG;
    cfg->boot_drive = drive;
    cfg->kernel_size = size;
    sucs_init_boot_config(&cfg->sucs_cfg, SUCS_MODE_BASE);

    vga_write(11, 30, "Booting...", ATTR_BRIGHT);

    {
        void (*kernel)(mbl_boot_config_t *);
        kernel = (void (*)(mbl_boot_config_t *))(void *)MBL_KERNEL_ADDR;
        kernel(cfg);
    }

    for (;;) {
        /* kernel returned - never expected */
    }
}
