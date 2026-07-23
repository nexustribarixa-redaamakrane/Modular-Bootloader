#ifndef SUCS_MODE_H
#define SUCS_MODE_H

/**
 * OpenWindows Kernel Mode-Switching Subsystem
 */

#include "sucs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SUCS_MODE_BASE     = 0,
    SUCS_MODE_EXTENDED = 1
} sucs_kernel_mode_t;

typedef enum {
    SUCS_SWITCH_SUCCESS               = 0,
    SUCS_SWITCH_ERR_INVALID_MODE      = 1,
    SUCS_SWITCH_ERR_ALREADY_ACTIVE    = 2,
    SUCS_SWITCH_REBOOT_REQUIRED       = 3
} sucs_switch_status_t;

typedef struct {
    sucs_kernel_mode_t active_mode;
    sucs_kernel_mode_t pending_mode;
    bool               reboot_required;
    uint32_t           mode_change_count;
} sucs_kernel_boot_config_t;

sucs_kernel_mode_t sucs_get_active_mode(void);
sucs_kernel_mode_t sucs_get_pending_mode(void);
bool sucs_is_reboot_required(void);
sucs_switch_status_t sucs_request_mode_switch(sucs_kernel_mode_t new_mode);
bool sucs_commit_mode_on_boot(sucs_kernel_boot_config_t* boot_cfg);
void sucs_init_boot_config(sucs_kernel_boot_config_t* boot_cfg, sucs_kernel_mode_t initial_mode);

#ifdef __cplusplus
}
#endif

#endif /* SUCS_MODE_H */
