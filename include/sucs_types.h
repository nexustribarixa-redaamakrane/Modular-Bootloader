#ifndef SUTF_SUCS_TYPES_H
#define SUTF_SUCS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * SuperUnicode (SUCS) Character Encoding Specification
 * 
 * SUCS (SuperUnicode) is strictly a CHARACTER ENCODING defining the abstract
 * 31-bit codepoint numerical address space (0x00000000 to 0x7FFFFFFF).
 */
typedef uint32_t sucs_char_t;

/* Sentinels and Character Encoding Boundaries */
#define SUCS_INVALID_CODEPOINT 0x7FFFFFFFUL
#define SUCS_MAX_CODEPOINT     0x7FFFFFFFUL

/* Kernel Security Trap Range in SUCS Address Space */
#define SUCS_TRAP_RANGE_MIN    0x7FFFFFF0UL
#define SUCS_TRAP_RANGE_MAX    0x7FFFFFFEUL

/**
 * Codepoint Validator
 * Checks whether a given SUCS codepoint address is valid within the character encoding map.
 */
static inline bool sucs_is_valid(sucs_char_t cp) {
    if (cp > SUCS_MAX_CODEPOINT) {
        return false;
    }
    if (cp >= SUCS_TRAP_RANGE_MIN && cp <= SUCS_TRAP_RANGE_MAX) {
        return false;
    }
    if (cp == SUCS_INVALID_CODEPOINT) {
        return false;
    }
    return true;
}

#if defined(__has_include)
  #if __has_include("extsucs_types.h")
    #include "extsucs_types.h"
  #endif
#endif

#endif /* SUTF_SUCS_TYPES_H */
