/* bancode_all.h - BANcode Framework freestanding mirror for the
 * Modular Bootloader (MBL).
 *
 * Mirrors the API surface of the auto-generated master header in the
 * BANcode project (`BANcode/kernel_inc/bancode/bancode_all.h`):
 *   - bancode_t type, registry block boundaries, Kernel Security Trap
 *     geometry and classification/trap-mapping inline helpers.
 *   - BANcode v1.1.0 operating modes: System mode (fatal BANcodes route
 *     to Kernel Security Traps, the krnl path) or App mode (fatal
 *     BANcodes bypass kernel dispatch to an App-level crash handler).
 *     Both modes share the identical codepoint registry.
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
#define BANCODE_VERSION_MINOR 1
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

/* ------------------------------------------------------------------ */
/*  BANcode Operating Modes (BANcode v1.1.0)                           */
/*  System mode (default): fatal B+ BANcodes dispatch to the Kernel    */
/*  Security Trap handlers (bancode_trap_dispatch), the krnl path.     */
/*  App mode: fatal B+ BANcodes bypass Kernel Security Trap dispatch   */
/*  entirely and are delivered to the registered App-level crash       */
/*  handler. Both modes share the identical codepoint registry.         */
/* ------------------------------------------------------------------ */

#ifndef BANCODE_MODE_T_DEFINED
#define BANCODE_MODE_T_DEFINED
typedef enum {
    BANCODE_MODE_SYSTEM = 0,  /* Kernel mode: fatal BANcodes use krnl trap dispatch */
    BANCODE_MODE_APP    = 1   /* App mode: fatal BANcodes use app-level crash handler */
} bancode_mode_t;
#endif

/* Compile-time default mode. Override with -DBANCODE_DEFAULT_MODE=1 to ship
 * an app-mode build by default. Runtime mode can still be changed at any time
 * via bancode_set_mode(). */
#ifndef BANCODE_DEFAULT_MODE
#define BANCODE_DEFAULT_MODE BANCODE_MODE_SYSTEM
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MBL_BANCODE_UNUSED __attribute__((unused))
#else
#define MBL_BANCODE_UNUSED
#endif

/* File-scope mode state (per-translation-unit, like the rest of this
 * header-only mirror). Normally System for the boot path. */
static MBL_BANCODE_UNUSED bancode_mode_t g_bancode_mode = BANCODE_DEFAULT_MODE;

/* Returns the current BANcode operating mode. */
static inline bancode_mode_t bancode_get_mode(void) {
    return g_bancode_mode;
}

/* Sets the BANcode operating mode at runtime.
 * Returns true on success, false if an invalid mode was specified. */
static inline bool bancode_set_mode(bancode_mode_t mode) {
    if (mode != BANCODE_MODE_SYSTEM && mode != BANCODE_MODE_APP) {
        return false;
    }
    g_bancode_mode = mode;
    return true;
}

/* Returns true if the framework is in System mode. */
static inline bool bancode_is_system_mode(void) {
    return g_bancode_mode == BANCODE_MODE_SYSTEM;
}

/* Returns true if the framework is in App mode. */
static inline bool bancode_is_app_mode(void) {
    return g_bancode_mode == BANCODE_MODE_APP;
}

/* App-level crash handler signature (BANCODE_MODE_APP path): a fatal B+
 * BANcode in App mode is delivered here - never to the krnl trap table.
 * Mirror-only surface: the registration/dispatch functions
 * (bancode_register_app_crash_handler, bancode_unregister_app_crash_handler,
 * bancode_app_crash_handler_installed, bancode_app_dispatch_fatal) are
 * provided by the upstream libbancode kernel sources, not by the boot
 * path mirror. */
typedef void (*bancode_app_crash_handler_t)(bancode_t bancode_cp, void *context);

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
