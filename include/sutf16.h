#ifndef SUTF_SUTF16_H
#define SUTF_SUTF16_H

#include "sucs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t sutf16_codepoint_length(sucs_char_t cp) {
    if (!sucs_is_valid(cp)) {
        return 0;
    }
    if (cp <= 0xFFFFUL) {
        return 1;
    } else {
        return 2;
    }
}

size_t sutf16_encode_char(sucs_char_t cp, uint16_t* out_words, size_t buf_words);
size_t sutf16_decode_char(const uint16_t* in_words, size_t buf_words, sucs_char_t* out_cp);

#ifdef __cplusplus
}
#endif

#endif /* SUTF_SUTF16_H */
