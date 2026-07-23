#include "../include/sutf16.h"

size_t sutf16_encode_char(sucs_char_t cp, uint16_t* out_words, size_t buf_words) {
    if (!out_words || !sucs_is_valid(cp)) {
        return 0;
    }

    if (cp <= 0xFFFFUL) {
        if (buf_words < 1) return 0;
        out_words[0] = (uint16_t)(cp & 0xFFFFUL);
        return 1;
    } else if (cp <= 0x7FFFFFFFUL) {
        if (buf_words < 2) return 0;
        out_words[0] = (uint16_t)(0x8000U | ((cp >> 16) & 0x7FFFUL));
        out_words[1] = (uint16_t)(cp & 0xFFFFUL);
        return 2;
    }

    return 0;
}

size_t sutf16_decode_char(const uint16_t* in_words, size_t buf_words, sucs_char_t* out_cp) {
    if (!out_cp) {
        return 0;
    }
    *out_cp = SUCS_INVALID_CODEPOINT;

    if (!in_words || buf_words == 0) {
        return 0;
    }

    if (buf_words >= 2 && (in_words[0] & 0x8000U)) {
        sucs_char_t cp = (((sucs_char_t)(in_words[0] & 0x7FFFU)) << 16) |
                         (sucs_char_t)in_words[1];
        if (!sucs_is_valid(cp)) return 0;
        *out_cp = cp;
        return 2;
    }

    sucs_char_t cp = (sucs_char_t)in_words[0];
    if (!sucs_is_valid(cp)) return 0;
    *out_cp = cp;
    return 1;
}
