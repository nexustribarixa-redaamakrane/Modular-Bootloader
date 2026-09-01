/*
 * compat_smoke.c - Modular-Bootloader ecosystem compatibility smoke test.
 *
 * Single translation unit containing mbl.h together with the public
 * headers of all four sibling projects, then runtime-verifies every
 * integration surface:
 *
 *   BANcode              <bancode/bancode_all.h>   (upstream generated)
 *   SuperUnicode/libsutf <sutf8.h> <sucs_types.h> <sucs_mode.h>
 *   OpenWindows-Storage  <owfs_types.h> <owfs_superblock.h>
 *                        <owfs_inode.h> <owfs_catalog.h> <usfs_types.h>
 *   vip                  "univip_fvip.h"
 *   Modular-Bootloader   "mbl.h"
 *
 * Include-order strategy: the upstream BANcode master header is included
 * FIRST, so it owns the shared BANCODE_ALL_H guard; mbl.h's vendored
 * mirror then contributes only its provisional MBL_* placements. Storage
 * headers precede mbl.h so the authoritative struct definitions win;
 * mbl.h's copies merge with them under C11 identical-type rules.
 * Compiled as hosted C11 (-std=c11) with the host gcc and linked against
 * the real sibling implementations:
 *   vip/univip_fvip.c, superunicode/sutf/src/{sutf8.c,sucs_mode.c}
 *
 * Run via: python tools/compat_smoke.py
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <bancode/bancode_all.h>        /* BANcode (owns BANCODE_ALL_H) */

#include <owfs_types.h>                 /* OpenWindows-Storage owfs     */
#include <owfs_superblock.h>
#include <owfs_inode.h>
#include <owfs_catalog.h>
#include <usfs_types.h>                 /* OpenWindows-Storage usfs     */

#include "univip_fvip.h"                /* vip                          */

#include <sutf8.h>                      /* SuperUnicode libsutf         */
#include <sucs_types.h>
#include <sucs_mode.h>

#include "mbl.h"                        /* Modular-Bootloader (last)    */

static int g_checks;
static int g_failures;

#define CHECK(cond) do {                                                \
    g_checks++;                                                         \
    if (!(cond)) {                                                      \
        g_failures++;                                                   \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                   \
} while (0)

/* ------------------------------------------------------------------ */
/* OpenWindows-Storage: byte-for-byte on-disk layout parity           */
/* ------------------------------------------------------------------ */
static void test_storage_layout_parity(void) {
    /* Authoritative 4096-byte superblock geometry (libowfs layout). */
    CHECK(sizeof(owfs_superblock_t) == 0x1000);
    CHECK(offsetof(owfs_superblock_t, checksum)       == 0x84);
    CHECK(offsetof(owfs_superblock_t, security_flags) == 0x88);
    CHECK(offsetof(owfs_superblock_t, key_slot_1)     == 0x8C);
    CHECK(offsetof(owfs_superblock_t, key_slot_2)     == 0x18C);
    CHECK(offsetof(owfs_superblock_t, crypto_nonce)   == 0x28C);

    CHECK(sizeof(owfs_inode_t)         == 0x100);
    CHECK(sizeof(owfs_catalog_entry_t) == 0x100);
    CHECK(offsetof(owfs_inode_t, checksum)         == 0xFC);
    CHECK(offsetof(owfs_catalog_entry_t, checksum) == 0xFC);

    /* Shared entry-type bits across OWFS / USFS / MBL / VIP. */
    CHECK(OWFS_ENTRY_FILE    == USFS_ENTRY_FILE    &&
          OWFS_ENTRY_FILE    == VIP_STORAGE_ENTRY_FILE);
    CHECK(OWFS_ENTRY_CATALOG == USFS_ENTRY_CATALOG &&
          OWFS_ENTRY_CATALOG == VIP_STORAGE_ENTRY_CATALOG);
    CHECK(OWFS_ENTRY_DELETED == USFS_ENTRY_DELETED &&
          OWFS_ENTRY_DELETED == VIP_STORAGE_ENTRY_DELETED);

    /* Security flag bit positions (identical across the ecosystem). */
    CHECK(OWFS_SEC_ENCRYPTED == USFS_SEC_ENCRYPTED &&
          OWFS_SEC_ENCRYPTED == VIP_STORAGE_SEC_ENCRYPTED);
    CHECK(OWFS_SEC_READONLY  == USFS_SEC_READONLY  &&
          OWFS_SEC_READONLY  == VIP_STORAGE_SEC_READONLY);
    CHECK(OWFS_SEC_HIDDEN    == USFS_SEC_HIDDEN    &&
          OWFS_SEC_HIDDEN    == VIP_STORAGE_SEC_HIDDEN);

    /* Power-cut state flags shared by both filesystems. */
    CHECK(OWFS_STATE_DIRTY  == USFS_STATE_DIRTY);
    CHECK(OWFS_STATE_ERROR  == USFS_STATE_ERROR);
    CHECK(OWFS_STATE_LOCKED == USFS_STATE_LOCKED);

    /* Sibling magic values known to the bootloader probe. */
    CHECK(USFS_MAGIC == 0x55534653UL);
    CHECK(USFS_MAGIC != OWFS_MAGIC);
}

/* ------------------------------------------------------------------ */
/* Addressing model: MBL Block I/O LBAs vs vip absolute resolution     */
/* ------------------------------------------------------------------ */
static void test_addressing_model(void) {
    fvip_table_t table;
    fvip_entry_t entry;
    uint64_t lba = 0;
    uint32_t rc;

    CHECK(OWFS_PARTITION_LBA == 131200u);          /* GPT partition 2   */
    CHECK(VIP_OWFS_PARTITION_LBA == OWFS_PARTITION_LBA);

    /* MBL reserved drive region honored by vip. */
    CHECK(VIP_MBL_RESERVED_BASE_LBA == 0u);
    CHECK(VIP_MBL_RESERVED_LBA_COUNT == 128u);

    CHECK((uint32_t)OWFS_BLOCK_SIZE == VIP_STORAGE_BLOCK_SIZE);
    CHECK((uint32_t)USFS_BLOCK_SIZE == OWFS_BLOCK_SIZE);
    CHECK(VIP_LBAS_PER_STORAGE_BLOCK == 8u);

    /* Register the boot volume exactly as MBL addresses it and resolve
     * an entry back through vip's absolute-LBA model. The result must
     * equal the MBL driver formula: partition LBA + block * 8 sectors.
     * (fvip_init_volume_index / fvip_entry_absolute_lba return VIP_OK.) */
    rc = univip_init_system();
    CHECK(rc == VIP_INIT_OK);
    rc = univip_register_volume(7, (uint64_t)OWFS_PARTITION_LBA, "sysdisk");
    CHECK(rc == VIP_REGISTER_OK);
    rc = fvip_init_volume_index(7, &table);
    CHECK(rc == VIP_OK);
    rc = fvip_insert_entry(&table, "/kernel.bin", 0x200000,
                           FVIP_FLAG_FILE | FVIP_FLAG_READONLY);
    CHECK(rc == VIP_INSERT_OK);
    rc = fvip_lookup_entry(&table, "/kernel.bin", &entry);
    CHECK(rc == VIP_LOOKUP_OK);
    rc = fvip_entry_absolute_lba(&table, &entry, &lba);
    CHECK(rc == VIP_OK);
    CHECK(lba == (uint64_t)OWFS_PARTITION_LBA + (0x200000u / VIP_SECTOR_SIZE));
}

/* ------------------------------------------------------------------ */
/* SuperUnicode: sentinel parity + libsutf/vip codec agreement        */
/* ------------------------------------------------------------------ */
static void test_sutf_codec(void) {
    static const struct { uint32_t cp; size_t len; } samples[] = {
        { 0x00000041UL, 1 },
        { 0x0000007FUL, 1 },
        { 0x00000080UL, 2 },
        { 0x000007FFUL, 2 },
        { 0x00000800UL, 3 },
        { 0x0000FFFFUL, 3 },
        { 0x00010000UL, 4 },
        { 0x0010FFFFUL, 4 },
        { 0x00200001UL, 5 },
        { 0x03FFFFFFUL, 5 },
        { 0x04000000UL, 6 }
    };
    uint8_t buf[8];
    size_t i;

    CHECK(SUCS_INVALID_CODEPOINT == 0x7FFFFFFFUL);
    CHECK(SUCS_MAX_CODEPOINT == 0x7FFFFFFFUL);
    CHECK(SUCS_TRAP_RANGE_MIN == 0x7FFFFFF0UL);
    CHECK(SUCS_TRAP_RANGE_MAX == 0x7FFFFFFEUL);
    CHECK(BANCODE_INVALID_CODEPOINT == SUCS_INVALID_CODEPOINT);

    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        sucs_char_t cp = samples[i].cp;
        size_t n_enc, n_dec;
        sucs_char_t back = 0;

        CHECK(sutf8_codepoint_length(cp) == samples[i].len);
        CHECK(fvip_sutf8_codepoint_length(cp) == samples[i].len);

        n_enc = sutf8_encode_char(cp, buf, sizeof(buf));
        CHECK(n_enc == samples[i].len);
        n_dec = sutf8_decode_char(buf, n_enc, &back);
        CHECK(n_dec == n_enc);
        CHECK(back == cp);

        /* vip's bundled codec must mirror libsutf byte-for-byte. */
        back = 0;
        n_dec = fvip_sutf8_decode_char(buf, n_enc, &back);
        CHECK(n_dec == n_enc);
        CHECK(back == cp);
    }

    /* Out-of-band exclusions: sentinel and Kernel Security Trap range. */
    CHECK(sutf8_codepoint_length(SUCS_INVALID_CODEPOINT) == 0);
    CHECK(sutf8_codepoint_length(SUCS_TRAP_RANGE_MIN) == 0);
    CHECK(sutf8_encode_char(SUCS_TRAP_RANGE_MAX, buf, sizeof(buf)) == 0);
    CHECK(fvip_sutf8_codepoint_length(SUCS_TRAP_RANGE_MIN) == 0);

    /* Overlong encodings are rejected by both codecs. */
    buf[0] = 0xC1; buf[1] = 0x81;               /* overlong 'A' */
    {
        sucs_char_t back = 0;
        CHECK(sutf8_decode_char(buf, 2, &back) == 0);
        CHECK(back == SUCS_INVALID_CODEPOINT);
        back = 0;
        CHECK(fvip_sutf8_decode_char(buf, 2, &back) == 0);
        CHECK(back == SUCS_INVALID_CODEPOINT);
    }

    /* Truncated streams are rejected. */
    {
        sucs_char_t back = 0;
        (void)sutf8_encode_char(0x00001000UL, buf, sizeof(buf)); /* 3 B */
        CHECK(sutf8_decode_char(buf, 2, &back) == 0);
    }
}

/* ------------------------------------------------------------------ */
/* SuperUnicode kernel mode controller (libsutf parity)                */
/* ------------------------------------------------------------------ */
static void test_sucs_mode_controller(void) {
    sucs_kernel_boot_config_t cfg;

    CHECK(SUCS_MODE_BASE == 0);
    CHECK(SUCS_MODE_EXTENDED == 1);

    sucs_init_boot_config(&cfg, SUCS_MODE_BASE);
    CHECK(cfg.active_mode == SUCS_MODE_BASE);
    CHECK(cfg.pending_mode == SUCS_MODE_BASE);
    CHECK(cfg.reboot_required == false);
    CHECK(cfg.mode_change_count == 0);

    /* Stage a switch: request operates on the controller globals. */
    CHECK(sucs_request_mode_switch(SUCS_MODE_EXTENDED) ==
          SUCS_SWITCH_REBOOT_REQUIRED);
    CHECK(sucs_get_pending_mode() == SUCS_MODE_EXTENDED);
    CHECK(sucs_get_active_mode() == SUCS_MODE_BASE);
    CHECK(sucs_is_reboot_required());

    /* Early-kernel boot handoff: the staged state travels in the boot
     * config block, which commit then applies (libsutf semantics). */
    cfg.pending_mode = sucs_get_pending_mode();
    cfg.reboot_required = sucs_is_reboot_required();
    CHECK(sucs_commit_mode_on_boot(&cfg));
    CHECK(cfg.active_mode == SUCS_MODE_EXTENDED);
    CHECK(cfg.pending_mode == SUCS_MODE_EXTENDED);
    CHECK(!cfg.reboot_required);
    CHECK(cfg.mode_change_count == 1);
}

/* ------------------------------------------------------------------ */
/* BANcode: registry blocks, trap geometry, MBL provisional placements */
/* ------------------------------------------------------------------ */
static void test_bancode_integration(void) {
    static const bancode_t mbl_codes[] = {
        MBL_COM_VOLUME_PROBE_OK, MBL_COM_CATALOG_ENUM_OK,
        MBL_COM_KERNEL_LOAD_OK, MBL_COM_BOOT_HANDOFF_OK,
        MBL_WARN_VOLUME_DIRTY, MBL_WARN_VOLUME_LOCKED,
        MBL_WARN_STATE_ERROR, MBL_WARN_VERSION_MISMATCH,
        MBL_SOFT_NO_VOLUME, MBL_SOFT_IS_USFS, MBL_SOFT_NOT_A_FILE,
        MBL_SOFT_KERNEL_TOO_SMALL, MBL_SOFT_KERNEL_TOO_LARGE,
        MBL_SOFT_ENCRYPTED_VOLUME, MBL_SOFT_BAD_BLOCK_MAP,
        MBL_BAN_SB_MAGIC, MBL_BAN_SB_CHECKSUM, MBL_BAN_INODE_CHECKSUM,
        MBL_BAN_CATALOG_CHECKSUM, MBL_BAN_IO_ERROR, MBL_BAN_LOAD_FAILED
    };
    static const bancode_t vip_codes[] = {
        VIP_OK, VIP_INIT_OK, VIP_REGISTER_OK, VIP_RESOLVE_OK,
        VIP_INSERT_OK, VIP_LOOKUP_OK, VIP_REMOVE_OK, VIP_INTEGRITY_OK,
        VIP_ERR_INVALID_PARAM, VIP_ERR_NULL_POINTER, VIP_ERR_NOT_FOUND,
        VIP_ERR_ALREADY_EXISTS, VIP_ERR_TABLE_FULL,
        VIP_ERR_VOLUME_NOT_REGISTERED, VIP_ERR_VOLUME_LIMIT,
        VIP_ERR_LABEL_TOO_LONG, VIP_ERR_PATH_TOO_LONG,
        VIP_ERR_NOT_INITIALIZED, VIP_ERR_SYSTEM_NOT_READY,
        VIP_ERR_INTEGRITY_FAIL, VIP_ERR_INVALID_SUTF8,
        VIP_BAN_TABLE_CORRUPT, VIP_BAN_NODE_CORRUPT,
        VIP_BAN_INDEX_OVERRUN, VIP_BAN_POOL_EXHAUSTED,
        VIP_BAN_TRIE_DEPTH_EXCEEDED, VIP_BAN_SYSTEM_NOT_INITIALIZED,
        VIP_BAN_ENTRY_CORRUPT
    };
    size_t i, j;

    CHECK(BANCODE_TRAP_SLOT_COUNT == 15);
    CHECK(BANCODE_BANCODES_PER_TRAP == 128);
    CHECK(VIP_TRAP_SLOT_COUNT == BANCODE_TRAP_SLOT_COUNT);
    CHECK(VIP_BANCODES_PER_TRAP == BANCODE_BANCODES_PER_TRAP);
    CHECK(BANCODE_BANCODE_START == VIP_BANCODE_BLOCK_START);
    CHECK(BANCODE_SOFTCODE_END == VIP_SOFTCODE_BLOCK_END);

    /* BANcode v1.1.0 operating modes: the boot path runs System mode
     * (fatal BANcodes dispatch through Kernel Security Traps, the krnl
     * path); App mode is the application-crash-handler path. These are
     * compile-time checks only - this TU links the upstream header but
     * not libbancode, so the runtime mode getters are not callable here. */
    CHECK(BANCODE_MODE_SYSTEM == 0);
    CHECK(BANCODE_MODE_APP == 1);
#ifndef BANCODE_DEFAULT_MODE
#error BANCODE_DEFAULT_MODE must be provided by the upstream master header
#endif
    CHECK(BANCODE_DEFAULT_MODE == BANCODE_MODE_SYSTEM);

    /* Every MBL provisional code sits inside its intended block.
     * Direct range comparisons: the upstream master header does not
     * ship W+/C+/S+ classifier helpers. */
    for (i = 0; i < sizeof(mbl_codes) / sizeof(mbl_codes[0]); i++) {
        bancode_t c = mbl_codes[i];
        if (c >= MBL_COM_VOLUME_PROBE_OK && c <= MBL_COM_BOOT_HANDOFF_OK) {
            CHECK(c >= BANCODE_COMCODE_START && c <= BANCODE_COMCODE_END);
        } else if (c >= MBL_WARN_VOLUME_DIRTY && c <= MBL_WARN_VERSION_MISMATCH) {
            CHECK(c >= BANCODE_WARNCODE_START && c <= BANCODE_WARNCODE_END);
        } else if (c >= MBL_SOFT_NO_VOLUME && c <= MBL_SOFT_BAD_BLOCK_MAP) {
            CHECK(c >= BANCODE_SOFTCODE_START && c <= BANCODE_SOFTCODE_END);
        } else {
            CHECK(bancode_is_bancode(c));
            CHECK(c >= 0x0011A2E0U && c <= 0x0011A2FFU); /* reserved run */
        }
        /* No collisions with any concrete vip placement. */
        for (j = 0; j < sizeof(vip_codes) / sizeof(vip_codes[0]); j++) {
            CHECK(mbl_codes[i] != vip_codes[j]);
        }
    }

    /* Trap geometry mirrors the ecosystem mapping helpers. */
    CHECK(bancode_to_trap(MBL_BAN_SB_MAGIC) == 0x7FFFFFF5UL);   /* slot 5 */
    CHECK(bancode_to_trap(VIP_BAN_TABLE_CORRUPT) == 0x7FFFFFF7UL);
    CHECK(bancode_to_trap(MBL_SOFT_NO_VOLUME) == BANCODE_INVALID_CODEPOINT);
    CHECK(bancode_to_trap(0x0011A7FFU) == BANCODE_INVALID_CODEPOINT);
    {
        bancode_t lo = 0, hi = 0;
        CHECK(bancode_trap_to_bancode_range(0x7FFFFFF5UL, &lo, &hi));
        CHECK(lo == 0x0011A280U);
        CHECK(hi == 0x0011A2FFU);
    }
}

int main(void) {
    test_storage_layout_parity();
    test_addressing_model();
    test_sutf_codec();
    test_sucs_mode_controller();
    test_bancode_integration();

    printf("compat_smoke: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
