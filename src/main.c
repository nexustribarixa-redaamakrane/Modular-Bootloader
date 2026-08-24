/*
 * main.c - bootloader entry (kmain) and kernel handoff.
 *
 * Flow: probe the OpenWindows OWFS volume -> enumerate root catalog ->
 * GRUB-style menu with 10s timeout -> stream the selected kernel to
 * MBL_KERNEL_ADDR -> jump to it with a boot configuration block.
 *
 * UEFI version: GOP framebuffer for display, UEFI runtime services for time.
 */

#include "efi.h"
#include "mbl.h"

static mbl_entry_t g_entries[MBL_MENU_MAX];

/* Forward declaration: init GOP renderer (defined in gop.c) */
extern void vga_init_gop(void);

static void fail(const char *msg) {
    char diagbuf[48];
    const mbl_diag_t *d = mbl_diag_last();

    vga_fill(12, 20, ' ', 40, 0x0Fu);
    vga_write(12, 20, msg, 0x0Fu);
    if (d != NULL) {
        mbl_diag_format(diagbuf, d->code);
        vga_fill(13, 20, ' ', 40, 0x07u);
        vga_write(13, 20, "BANcode ", 0x07u);
        vga_write(13, 28, diagbuf, 0x07u);
    }
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Volume-probe failure screen: distinguishes the recoverable soft
 * faults (no volume / USFS media / encrypted volume / future version)
 * from fatal storage corruption, mirroring the BANcode severity. */
static void probe_failed(bancode_t rc) {
    char diagbuf[48];

    vga_fill(10, 20, ' ', 44, 0x0Fu);
    if (mbl_code_is_soft(rc)) {
        vga_write(10, 20, "No bootable OWFS volume.", 0x0Fu);
    } else {
        vga_write(10, 20, "OWFS volume unusable.", 0x0Fu);
    }
    mbl_diag_format(diagbuf, rc);
    vga_fill(11, 20, ' ', 44, 0x07u);
    vga_write(11, 20, diagbuf, 0x07u);
    vga_fill(12, 20, ' ', 44, 0x07u);
    vga_write(12, 20, "Press Esc to reboot.", 0x07u);
    for (;;) {
        int k = kbd_poll();
        if (k == MBL_KEY_ESC || k == MBL_KEY_REBOOT) {
            kbd_reboot();
        }
    }
}

void kmain(void) {
    int count;
    int sel;
    uint32_t size = 0;
    bancode_t rc;

    /* Initialize the GOP framebuffer renderer */
    vga_init_gop();
    vga_clear();

    rc = owfs_probe(0);
    if (!mbl_code_is_success(rc)) {
        probe_failed(rc);
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
        /* Shutdown via UEFI */
        efi_reset_shutdown();
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    if (sel < 0 || sel >= count) {
        kbd_reboot();
    }

    /* loading screen */
    vga_clear();
    vga_write(10, 30, "Loading kernel...", 0x0Fu);

    if (owfs_load_file(g_entries[sel].inode, MBL_KERNEL_ADDR, &size)
            != MBL_COM_KERNEL_LOAD_OK) {
        fail("Kernel load failed!");
    }
    if (size < 16) {
        mbl_diag_raise(MBL_SOFT_KERNEL_TOO_SMALL);
        fail("Kernel image too small!");
    }

    /* publish boot configuration for the kernel */
    /* (fixed low address 0x510; silence the near-NULL region analyzer) */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    {
        mbl_boot_config_t *cfg = (mbl_boot_config_t *)(uintptr_t)MBL_BOOTCONFIG;
        cfg->magic = MBL_MAGIC_BOOTCFG;
        cfg->boot_drive = 0;
        cfg->kernel_size = size;
        sucs_init_boot_config(&cfg->sucs_cfg, SUCS_MODE_BASE);
        mbl_diag_raise(MBL_COM_BOOT_HANDOFF_OK);
    }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    vga_write(11, 30, "Booting...", 0x0Fu);

    {
        /* Exit UEFI boot services before jumping to the kernel.
         * The kernel is a bare-metal 32-bit program that uses VGA text.
         * After ExitBootServices, only the memory map is valid. */
        EFI_STATUS status;
        UINTN mem_map_size = 0;
        EFI_MEMORY_DESCRIPTOR *mem_map = NULL;
        UINTN map_key = 0;
        UINTN desc_size = 0;
        UINT32 desc_ver = 0;

        EFI_PHYSICAL_ADDRESS pages = 0;
        UINTN num_pages = 0;

        /* Two-pass: first call to get required buffer size */
        status = gBS->GetMemoryMap(&mem_map_size, mem_map, &map_key,
                                   &desc_size, &desc_ver);
        mem_map_size += 4096;  /* extra room */
        mem_map = (EFI_MEMORY_DESCRIPTOR *)(UINTN)0;  /* will allocate below */

        num_pages = (mem_map_size + 4095) / 4096;

        status = gBS->AllocatePages(EfiLoaderData, num_pages, &pages);
        if (!EFI_ERROR(status)) {
            mem_map = (EFI_MEMORY_DESCRIPTOR *)(UINTN)pages;
            status = gBS->GetMemoryMap(&mem_map_size, mem_map, &map_key,
                                       &desc_size, &desc_ver);
            if (!EFI_ERROR(status) && gExitBootServices) {
                gExitBootServices(gImageHandle, map_key);
            }
        }

        /* After ExitBootServices: no UEFI services available.
         * Jump to the kernel. The kernel is a 32-bit bare-metal program
         * loaded at MBL_KERNEL_ADDR. */
        {
            void (*kernel)(mbl_boot_config_t *);
            mbl_boot_config_t *cfg = (mbl_boot_config_t *)(uintptr_t)MBL_BOOTCONFIG;
            kernel = (void (*)(mbl_boot_config_t *))(void *)(uintptr_t)MBL_KERNEL_ADDR;

            /* Set up segments for 32-bit mode */
            __asm__ volatile (
                "mov $0x10, %%ax\n"
                "mov %%ax, %%ds\n"
                "mov %%ax, %%es\n"
                "mov %%ax, %%ss\n"
                "mov $0x180000, %%esp\n"
                "call *%[kern]\n"
                :
                : [kern] "r" (kernel), "D" (cfg)
                : "eax", "memory"
            );
        }
    }

    for (;;) {
        /* kernel returned - never expected */
        __asm__ volatile ("hlt");
    }
}
