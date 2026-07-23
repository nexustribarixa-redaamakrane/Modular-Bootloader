#include "../include/mbl_core.h"
#include "../include/io.h"

typedef void (*kernel_entry_fn)(mbl_boot_state_t *boot_state);

void mbl_load_and_jump(mbl_boot_state_t *state, void *kernel_target_addr, const void *kernel_src_addr, size_t image_size) {
    if (kernel_target_addr && kernel_src_addr && image_size > 0) {
        mbl_memcpy(kernel_target_addr, kernel_src_addr, image_size);
    }

    /* Disable interrupts prior to kernel handoff */
    cli();

    /* Cast target entry point address and execute kernel entry point */
    kernel_entry_fn entry = (kernel_entry_fn)kernel_target_addr;
    if (entry) {
        entry(state);
    }

    /* Fallback halt loop */
    hlt_loop();
}
