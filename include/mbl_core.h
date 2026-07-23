#ifndef MBL_CORE_H
#define MBL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sucs_types.h"
#include "sucs_mode.h"

#define MBL_BOOT_FLAG_VBE_ACTIVE      (1U << 0)
#define MBL_BOOT_FLAG_KBD_READY       (1U << 1)
#define MBL_BOOT_FLAG_MOUSE_READY     (1U << 2)
#define MBL_BOOT_FLAG_EXTSUCS_ENABLED (1U << 3)

typedef struct mbl_framebuffer mbl_framebuffer_t;

typedef enum {
    MBL_ACTION_BOOT_KERNEL = 0,
    MBL_ACTION_BOOT_DEVICE = 1,
    MBL_ACTION_SYS_CONFIG  = 2,
    MBL_ACTION_SHUTDOWN    = 3
} mbl_boot_action_t;

typedef struct mbl_boot_state {
    mbl_framebuffer_t       *framebuffer;
    uint32_t                 boot_flags;
    uint32_t                 selected_entry;
    uint32_t                 mode;
    uint32_t                *memory_map_ptr;
    uint32_t                 memory_map_entries;
    sucs_kernel_boot_config_t mode_cfg;
} mbl_boot_state_t;

void mbl_main(mbl_boot_state_t *state);
void *mbl_memcpy(void *dest, const void *src, size_t count);
void *mbl_memset(void *dest, int val, size_t count);

#endif /* MBL_CORE_H */
