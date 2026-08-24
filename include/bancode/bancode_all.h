/* bancode_all.h - BANcode Framework freestanding mirror for the
 * Modular Bootloader (MBL).
 *
 * Mirrors the API surface of the auto-generated master header in the
 * BANcode project (`BANcode/kernel_inc/bancode/bancode_all.h`):
 *   - bancode_t type, registry block boundaries, Kernel Security Trap
 *     geometry and classification/trap-mapping inline helpers.
 * The file uses the SAME include guard as the upstream generated header
 * (BANCODE_ALL_H) so exactly one of the two is ever expanded in a
 * translation unit, in any include order - mirroring the coexistence
 * strategy used by the vip project. When the upstream master header is
 * included first (compatibility smoke test, kernels linking libbancode),
 * this entire file becomes a no-op.
 *
 * MBL boot-path diagnostic codes live separately in <bancode/mbl_bancode.h>
 * so they survive either inclusion order.
 *
 * Standard: C99 freestanding (-ffreestanding -nostdlib), zero deps.
 */
#ifndef BANCODE_ALL_H
#define BANCODE_ALL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint32_t bancode_t;

#define BANCODE_VERSION_MAJOR 1
#define BANCODE_VERSION_MINOR 0
#define BANCODE_VERSION_PATCH 0

#define BANCODE_BANCODE_START   0x0011A000U
#define BANCODE_BANCODE_END     0x0011A7FFU
#define BANCODE_WARNCODE_START  0x0011A800U
#define BANCODE_WARNCODE_END    0x0011ABFFU
#define BANCODE_COMCODE_START   0x0011AC00U
#define BANCODE_COMCODE_END     0x0011ADFFU
#define BANCODE_SOFTCODE_START  0x0011AE00U
#define BANCODE_SOFTCODE_END    0x0011AEFFU

/* Kernel Security Trap Range & Sentinel (31-bit Base SUCS) */
#define BANCODE_KERNEL_TRAP_MIN   0x7FFFFFF0U
#define BANCODE_KERNEL_TRAP_MAX   0x7FFFFFFEU
#define BANCODE_INVALID_CODEPOINT 0x7FFFFFFFU

/* Kernel Security Trap Damage Control Dispatch geometry */
#define BANCODE_TRAP_SLOT_COUNT    15U
#define BANCODE_BANCODES_PER_TRAP  128U

/* Legacy aliases for the Kernel Security Trap range */
#define BANCODE_TRAP_RANGE_START  BANCODE_KERNEL_TRAP_MIN
#define BANCODE_TRAP_RANGE_END    BANCODE_KERNEL_TRAP_MAX
#define BANCODE_TRAP_RANGE_COUNT  BANCODE_TRAP_SLOT_COUNT

/* Returns true if code is a fatal B+ BANcode (the only codes that route to traps) */
static inline bool bancode_is_bancode(bancode_t code) {
    return (code >= BANCODE_BANCODE_START && code <= BANCODE_BANCODE_END);
}

/* Returns true if cp is inside the Kernel Security Trap range (0x7FFFFFF0-0x7FFFFFFE) */
static inline bool bancode_is_kernel_trap(bancode_t cp) {
    return (cp >= BANCODE_KERNEL_TRAP_MIN && cp <= BANCODE_KERNEL_TRAP_MAX);
}

#define BANCODE_IS_TRAP_RANGE(addr)  bancode_is_kernel_trap(addr)

/* Resolves the Kernel Security Trap codepoint (0x7FFFFFF0-0x7FFFFFFE) governing
 * a B+ BANcode for damage control dispatch upon kernel crash. Returns
 * BANCODE_INVALID_CODEPOINT if the input is not a B+ BANcode or falls beyond
 * the 15 assigned trap slots (0x0011A780-0x0011A7FF is unmapped). */
static inline bancode_t bancode_to_trap(bancode_t bancode_cp) {
    uint32_t slot;
    if (!bancode_is_bancode(bancode_cp)) {
        return BANCODE_INVALID_CODEPOINT;
    }
    slot = (uint32_t)((bancode_cp - BANCODE_BANCODE_START) / BANCODE_BANCODES_PER_TRAP);
    if (slot >= BANCODE_TRAP_SLOT_COUNT) {
        return BANCODE_INVALID_CODEPOINT;
    }
    return (bancode_t)(BANCODE_KERNEL_TRAP_MIN + slot);
}

/* Returns the B+ BANcode cluster range (inclusive min/max) managed by a specific
 * Kernel Security Trap handler. Returns false for non-trap codepoints or null
 * output pointers. */
static inline bool bancode_trap_to_bancode_range(bancode_t trap_cp, bancode_t* out_min, bancode_t* out_max) {
    uint32_t slot;
    if (!bancode_is_kernel_trap(trap_cp) || out_min == NULL || out_max == NULL) {
        return false;
    }
    slot = (uint32_t)(trap_cp - BANCODE_KERNEL_TRAP_MIN);
    *out_min = (bancode_t)(BANCODE_BANCODE_START + slot * BANCODE_BANCODES_PER_TRAP);
    *out_max = (bancode_t)(*out_min + (BANCODE_BANCODES_PER_TRAP - 1UL));
    return true;
}

#endif /* BANCODE_ALL_H */
