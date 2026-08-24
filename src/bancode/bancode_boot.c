/*
 * bancode_boot.c - BANcode diagnostic recorder for the boot path.
 *
 * Implements the MBL-side companion API declared in mbl.h:
 *   - mbl_diag_raise(): record a raised BANcode (ring buffer, no alloc)
 *   - mbl_diag_last() / mbl_diag_count(): read back recorded diagnostics
 *   - mbl_diag_format(): render "0x0011A2E5 MBL_BAN_SB_CHECKSUM" text
 *   - mbl_bancode_name(): codename lookup for the provisional MBL codes
 *
 * Fatal B+ codes additionally record their Kernel Security Trap
 * codepoint (0x7FFFFFF0-0x7FFFFFFE) via bancode_to_trap(), matching the
 * damage-control geometry shared with SuperUnicode and vip.
 */

#include "mbl.h"

#define MBL_DIAG_MAX 8

static mbl_diag_t g_diag_ring[MBL_DIAG_MAX];
static uint32_t g_diag_count;
static uint32_t g_diag_head;

void mbl_diag_raise(bancode_t code) {
    mbl_diag_t *slot = &g_diag_ring[g_diag_head];
    slot->code = code;
    slot->slot = BANCODE_TRAP_SLOT_COUNT;
    slot->trap_cp = bancode_to_trap(code);
    if (slot->trap_cp != BANCODE_INVALID_CODEPOINT) {
        slot->slot = (uint32_t)(slot->trap_cp - BANCODE_KERNEL_TRAP_MIN);
    }
    g_diag_head = (g_diag_head + 1u) % MBL_DIAG_MAX;
    if (g_diag_count < MBL_DIAG_MAX) {
        g_diag_count++;
    }
}

const mbl_diag_t *mbl_diag_last(void) {
    uint32_t idx;
    if (g_diag_count == 0u) {
        return NULL;
    }
    idx = (g_diag_head + MBL_DIAG_MAX - 1u) % MBL_DIAG_MAX;
    return &g_diag_ring[idx];
}

int mbl_diag_count(void) {
    return (int)g_diag_count;
}

/* Codename table for the provisional MBL placements. */
typedef struct {
    bancode_t   code;
    const char *name;
} mbl_code_name_t;

static const mbl_code_name_t g_code_names[] = {
    { MBL_COM_VOLUME_PROBE_OK,    "MBL_COM_VOLUME_PROBE_OK" },
    { MBL_COM_CATALOG_ENUM_OK,    "MBL_COM_CATALOG_ENUM_OK" },
    { MBL_COM_KERNEL_LOAD_OK,     "MBL_COM_KERNEL_LOAD_OK" },
    { MBL_COM_BOOT_HANDOFF_OK,    "MBL_COM_BOOT_HANDOFF_OK" },
    { MBL_WARN_VOLUME_DIRTY,      "MBL_WARN_VOLUME_DIRTY" },
    { MBL_WARN_VOLUME_LOCKED,     "MBL_WARN_VOLUME_LOCKED" },
    { MBL_WARN_STATE_ERROR,       "MBL_WARN_STATE_ERROR" },
    { MBL_WARN_VERSION_MISMATCH,  "MBL_WARN_VERSION_MISMATCH" },
    { MBL_SOFT_NO_VOLUME,         "MBL_SOFT_NO_VOLUME" },
    { MBL_SOFT_IS_USFS,           "MBL_SOFT_IS_USFS" },
    { MBL_SOFT_NOT_A_FILE,        "MBL_SOFT_NOT_A_FILE" },
    { MBL_SOFT_KERNEL_TOO_SMALL,  "MBL_SOFT_KERNEL_TOO_SMALL" },
    { MBL_SOFT_KERNEL_TOO_LARGE,  "MBL_SOFT_KERNEL_TOO_LARGE" },
    { MBL_SOFT_ENCRYPTED_VOLUME,  "MBL_SOFT_ENCRYPTED_VOLUME" },
    { MBL_SOFT_BAD_BLOCK_MAP,     "MBL_SOFT_BAD_BLOCK_MAP" },
    { MBL_BAN_SB_MAGIC,           "MBL_BAN_SB_MAGIC" },
    { MBL_BAN_SB_CHECKSUM,        "MBL_BAN_SB_CHECKSUM" },
    { MBL_BAN_INODE_CHECKSUM,     "MBL_BAN_INODE_CHECKSUM" },
    { MBL_BAN_CATALOG_CHECKSUM,   "MBL_BAN_CATALOG_CHECKSUM" },
    { MBL_BAN_IO_ERROR,           "MBL_BAN_IO_ERROR" },
    { MBL_BAN_LOAD_FAILED,        "MBL_BAN_LOAD_FAILED" }
};

const char *mbl_bancode_name(bancode_t code) {
    uint32_t i;
    for (i = 0; i < sizeof(g_code_names) / sizeof(g_code_names[0]); i++) {
        if (g_code_names[i].code == code) {
            return g_code_names[i].name;
        }
    }
    return "UNKNOWN";
}

/* Render "0x0011A2E5 MBL_BAN_SB_CHECKSUM" (or "... UNKNOWN") into buf.
 * buf must have room for at least 48 bytes; output is NUL-terminated.
 * No printf - freestanding hex formatting only. */
void mbl_diag_format(char *buf, bancode_t code) {
    static const char hex[] = "0123456789ABCDEF";
    const char *name = mbl_bancode_name(code);
    uint32_t i = 0;
    int d;

    buf[i++] = '0';
    buf[i++] = 'x';
    for (d = 7; d >= 0; d--) {
        buf[i++] = hex[(code >> (d * 4)) & 0x0Fu];
    }
    buf[i++] = ' ';
    while (*name && i < 46u) {
        buf[i++] = *name++;
    }
    buf[i] = '\0';
}
