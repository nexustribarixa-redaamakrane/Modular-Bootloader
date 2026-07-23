#ifndef SUTF_SUTF8_H
#define SUTF_SUTF8_H

#include "sucs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t sutf8_codepoint_length(sucs_char_t cp) {
    if (!sucs_is_valid(cp)) {
        return 0;
    }
    if (cp <= 0x7FUL) {
        return 1;
    } else if (cp <= 0x7FFUL) {
        return 2;
    } else if (cp <= 0xFFFFUL) {
        return 3;
    } else if (cp <= 0x1FFFFFUL) {
        return 4;
    } else if (cp <= 0x3FFFFFFUL) {
        return 5;
    } else {
        return 6;
    }
}

size_t sutf8_encode_char(sucs_char_t cp, uint8_t* out_buf, size_t buf_size);
size_t sutf8_decode_char(const uint8_t* in_buf, size_t buf_size, sucs_char_t* out_cp);

#ifdef __cplusplus
}
#endif

#endif /* SUTF_SUTF8_H */
