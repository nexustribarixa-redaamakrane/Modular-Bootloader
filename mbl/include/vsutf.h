#ifndef VSUTF_H
#define VSUTF_H

#include "extsucs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VSUTF_EXT64_PREFIX   ((uint8_t)0xFE)
#define VSUTF_RESERVED_PREFIX ((uint8_t)0xFF)
#define VSUTF_EXT64_TOTAL     9
#define VSUTF_MAX_BYTES       9

static inline size_t vsutf_codepoint_length(sucs_ex_char_t ex_cp) {
    if (!extsucs_is_valid(ex_cp)) {
        return 0;
    }
    if (ex_cp <= 0x7FULL) {
        return 1;
    } else if (ex_cp <= 0x7FFULL) {
        return 2;
    } else if (ex_cp <= 0xFFFFULL) {
        return 3;
    } else if (ex_cp <= 0x1FFFFFULL) {
        return 4;
    } else if (ex_cp <= 0x3FFFFFFULL) {
        return 5;
    } else if (ex_cp <= 0x7FFFFFFULL) {
        return 6;
    } else {
        return VSUTF_EXT64_TOTAL;
    }
}

size_t vsutf_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size);
size_t vsutf_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

#ifdef __cplusplus
}
#endif

#endif /* VSUTF_H */
