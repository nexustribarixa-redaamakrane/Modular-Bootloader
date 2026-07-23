#include "../include/extsutf_fixed.h"

static void extsutf_zero_fill(uint8_t* buf, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        buf[i] = 0;
    }
}

static void extsutf_write_be64(uint8_t* buf, size_t slot_bytes, sucs_ex_char_t val) {
    size_t i;
    extsutf_zero_fill(buf, slot_bytes);
    if (slot_bytes >= 8) {
        size_t base = slot_bytes - 8;
        for (i = 0; i < 8; i++) {
            buf[base + i] = (uint8_t)((val >> (56 - i * 8)) & 0xFFULL);
        }
    }
}

static sucs_ex_char_t extsutf_read_be64(const uint8_t* buf, size_t slot_bytes) {
    sucs_ex_char_t val = 0;
    size_t i;
    if (slot_bytes >= 8) {
        size_t base = slot_bytes - 8;
        for (i = 0; i < 8; i++) {
            val |= ((sucs_ex_char_t)buf[base + i]) << (56 - i * 8);
        }
    }
    return val;
}

size_t sutf32_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < SUTF32_BYTES || !extsucs_is_valid(ex_cp)) {
        return 0;
    }
    if (ex_cp > 0xFFFFFFFFULL) {
        return 0;
    }
    uint32_t v = (uint32_t)(ex_cp & 0xFFFFFFFFULL);
    out_buf[0] = (uint8_t)((v >> 24) & 0xFF);
    out_buf[1] = (uint8_t)((v >> 16) & 0xFF);
    out_buf[2] = (uint8_t)((v >> 8)  & 0xFF);
    out_buf[3] = (uint8_t)(v         & 0xFF);
    return SUTF32_BYTES;
}

size_t sutf32_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || buf_size < SUTF32_BYTES) {
        return 0;
    }
    sucs_ex_char_t cp = ((sucs_ex_char_t)in_buf[0] << 24) |
                        ((sucs_ex_char_t)in_buf[1] << 16) |
                        ((sucs_ex_char_t)in_buf[2] << 8)  |
                        ((sucs_ex_char_t)in_buf[3]);
    if (!extsucs_is_valid(cp)) {
        return 0;
    }
    *out_cp = cp;
    return SUTF32_BYTES;
}

size_t sutf64_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < SUTF64_BYTES || !extsucs_is_valid(ex_cp)) {
        return 0;
    }
    extsutf_write_be64(out_buf, SUTF64_BYTES, ex_cp);
    return SUTF64_BYTES;
}

size_t sutf64_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || buf_size < SUTF64_BYTES) {
        return 0;
    }
    sucs_ex_char_t cp = extsutf_read_be64(in_buf, SUTF64_BYTES);
    if (!extsucs_is_valid(cp)) {
        return 0;
    }
    *out_cp = cp;
    return SUTF64_BYTES;
}

size_t sutf128_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < SUTF128_BYTES || !extsucs_is_valid(ex_cp)) {
        return 0;
    }
    extsutf_write_be64(out_buf, SUTF128_BYTES, ex_cp);
    return SUTF128_BYTES;
}

size_t sutf128_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || buf_size < SUTF128_BYTES) {
        return 0;
    }
    sucs_ex_char_t cp = extsutf_read_be64(in_buf, SUTF128_BYTES);
    if (!extsucs_is_valid(cp)) {
        return 0;
    }
    *out_cp = cp;
    return SUTF128_BYTES;
}

size_t sutf256_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < SUTF256_BYTES || !extsucs_is_valid(ex_cp)) {
        return 0;
    }
    extsutf_write_be64(out_buf, SUTF256_BYTES, ex_cp);
    return SUTF256_BYTES;
}

size_t sutf256_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || buf_size < SUTF256_BYTES) {
        return 0;
    }
    sucs_ex_char_t cp = extsutf_read_be64(in_buf, SUTF256_BYTES);
    if (!extsucs_is_valid(cp)) {
        return 0;
    }
    *out_cp = cp;
    return SUTF256_BYTES;
}

size_t sutf512_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < SUTF512_BYTES || !extsucs_is_valid(ex_cp)) {
        return 0;
    }
    extsutf_write_be64(out_buf, SUTF512_BYTES, ex_cp);
    return SUTF512_BYTES;
}

size_t sutf512_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || buf_size < SUTF512_BYTES) {
        return 0;
    }
    sucs_ex_char_t cp = extsutf_read_be64(in_buf, SUTF512_BYTES);
    if (!extsucs_is_valid(cp)) {
        return 0;
    }
    *out_cp = cp;
    return SUTF512_BYTES;
}

size_t sutfn_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t slot_bytes) {
    if (!out_buf || slot_bytes < 8 || !extsucs_is_valid(ex_cp)) {
        return 0;
    }
    extsutf_write_be64(out_buf, slot_bytes, ex_cp);
    return slot_bytes;
}

size_t sutfn_decode(const uint8_t* in_buf, size_t slot_bytes, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || slot_bytes < 8) {
        return 0;
    }
    sucs_ex_char_t cp = extsutf_read_be64(in_buf, slot_bytes);
    if (!extsucs_is_valid(cp)) {
        return 0;
    }
    *out_cp = cp;
    return slot_bytes;
}
