#include "../include/sutf.h"
#include "../include/mbl_core.h"

int sucs_strlen(const SUCS_STRING *str, size_t *out_char_count) {
    size_t count = 0;
    size_t offset = 0;

    if (!str || !str->buffer || !out_char_count) {
        return SUES_ERR_INVALID_BYTE;
    }

    while (offset < str->length_bytes) {
        sucs_char_t cp = 0;
        size_t bytes_read = 0;
        int status = sutf_decode_char(str->buffer + offset, str->length_bytes - offset, &cp, &bytes_read);
        if (status != SUES_SUCCESS) {
            return status;
        }

        if (!sucs_is_formatting_char(cp)) {
            count++;
        }

        offset += bytes_read;
    }

    *out_char_count = count;
    return SUES_SUCCESS;
}

int sucs_strcpy(SUCS_STRING *dest, const SUCS_STRING *src) {
    uint32_t i;

    if (!dest || !dest->buffer || !src || !src->buffer) {
        return SUES_ERR_INVALID_BYTE;
    }

    if (dest->capacity_bytes < src->length_bytes) {
        return SUES_ERR_BUFFER_TOO_SMALL;
    }

    for (i = 0; i < src->length_bytes; ++i) {
        dest->buffer[i] = src->buffer[i];
    }
    dest->length_bytes = src->length_bytes;
    return SUES_SUCCESS;
}

int sucs_streq(const SUCS_STRING *a, const SUCS_STRING *b, bool *out_equal) {
    uint32_t i;

    if (!a || !a->buffer || !b || !b->buffer || !out_equal) {
        return SUES_ERR_INVALID_BYTE;
    }

    if (a->length_bytes != b->length_bytes) {
        *out_equal = false;
        return SUES_SUCCESS;
    }

    for (i = 0; i < a->length_bytes; ++i) {
        if (a->buffer[i] != b->buffer[i]) {
            *out_equal = false;
            return SUES_SUCCESS;
        }
    }

    *out_equal = true;
    return SUES_SUCCESS;
}
