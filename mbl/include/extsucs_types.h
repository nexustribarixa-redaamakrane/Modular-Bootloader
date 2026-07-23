#ifndef EXTSUCS_TYPES_H
#define EXTSUCS_TYPES_H

/**
 * ExtSUCS (SuperUnicode Extended) Character Encoding Specification
 *
 * ExtSUCS is strictly a CHARACTER ENCODING defining an abstract,
 * unbounded codepoint numerical address space (0 -> infinity).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint64_t sucs_ex_char_t;

#ifndef SUTF_SUCS_TYPES_H
typedef uint32_t sucs_char_t;
#define SUCS_INVALID_CODEPOINT  ((sucs_char_t)0x7FFFFFFFUL)
#define SUCS_MAX_CODEPOINT      ((sucs_char_t)0x7FFFFFFFUL)
#define SUCS_TRAP_RANGE_MIN     ((sucs_char_t)0x7FFFFFF0UL)
#define SUCS_TRAP_RANGE_MAX     ((sucs_char_t)0x7FFFFFFEUL)
#endif

#define EXTSUCS_BASE_SUCS_MAX   ((sucs_ex_char_t)0x7FFFFFFFULL)

static inline bool extsucs_is_valid(sucs_ex_char_t ex_cp) {
    if (ex_cp >= (sucs_ex_char_t)SUCS_TRAP_RANGE_MIN &&
        ex_cp <= (sucs_ex_char_t)SUCS_TRAP_RANGE_MAX) {
        return false;
    }
    return true;
}

static inline bool extsucs_is_base_sucs(sucs_ex_char_t ex_cp) {
    return ex_cp <= EXTSUCS_BASE_SUCS_MAX;
}

static inline sucs_ex_char_t sucs_upcast(sucs_char_t cp) {
    return (sucs_ex_char_t)cp;
}

static inline bool sucs_downcast(sucs_ex_char_t ex_cp, sucs_char_t* out_cp) {
    if (!out_cp) {
        return false;
    }
    if (ex_cp > EXTSUCS_BASE_SUCS_MAX) {
        *out_cp = SUCS_INVALID_CODEPOINT;
        return false;
    }
    sucs_char_t cp = (sucs_char_t)(ex_cp & 0x7FFFFFFFUL);
    if (cp == SUCS_INVALID_CODEPOINT) {
        *out_cp = SUCS_INVALID_CODEPOINT;
        return false;
    }
    if (cp >= SUCS_TRAP_RANGE_MIN && cp <= SUCS_TRAP_RANGE_MAX) {
        *out_cp = SUCS_INVALID_CODEPOINT;
        return false;
    }
    *out_cp = cp;
    return true;
}

#endif /* EXTSUCS_TYPES_H */
