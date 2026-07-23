#include "../include/esutf.h"

bool esutf_translate_to_guest(sucs_ex_char_t host_cp,
                              uint32_t* out_page_index,
                              uint16_t* out_offset) {
    if (!out_page_index || !out_offset) {
        return false;
    }
    if (!extsucs_is_valid(host_cp)) {
        return false;
    }

    sucs_ex_char_t page = host_cp >> ESUTF_PAGE_SHIFT;

    if (page > 0xFFFFFFFFULL) {
        return false;
    }

    *out_page_index = (uint32_t)(page & 0xFFFFFFFFULL);
    *out_offset = (uint16_t)(host_cp & ESUTF_OFFSET_MASK);
    return true;
}

bool esutf_translate_to_host(uint32_t page_index,
                             uint16_t offset,
                             sucs_ex_char_t* out_host_cp) {
    if (!out_host_cp) {
        return false;
    }

    if (offset >= ESUTF_PAGE_SIZE) {
        return false;
    }

    sucs_ex_char_t host_cp = ((sucs_ex_char_t)page_index << ESUTF_PAGE_SHIFT) |
                             (sucs_ex_char_t)offset;

    if (!extsucs_is_valid(host_cp)) {
        return false;
    }

    *out_host_cp = host_cp;
    return true;
}

size_t esutf_encode_ipc(sucs_ex_char_t host_cp, uint8_t* out_buf, size_t buf_size) {
    if (!out_buf || buf_size < ESUTF_IPC_FRAME_BYTES) {
        return 0;
    }

    uint32_t page_index;
    uint16_t offset;

    if (!esutf_translate_to_guest(host_cp, &page_index, &offset)) {
        return 0;
    }

    out_buf[0] = (uint8_t)((page_index >> 24) & 0xFFU);
    out_buf[1] = (uint8_t)((page_index >> 16) & 0xFFU);
    out_buf[2] = (uint8_t)((page_index >> 8)  & 0xFFU);
    out_buf[3] = (uint8_t)(page_index          & 0xFFU);

    out_buf[4] = (uint8_t)((offset >> 8) & 0xFFU);
    out_buf[5] = (uint8_t)(offset        & 0xFFU);

    return ESUTF_IPC_FRAME_BYTES;
}

size_t esutf_decode_ipc(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp) {
    if (!in_buf || !out_cp || buf_size < ESUTF_IPC_FRAME_BYTES) {
        return 0;
    }

    uint32_t page_index = ((uint32_t)in_buf[0] << 24) |
                          ((uint32_t)in_buf[1] << 16) |
                          ((uint32_t)in_buf[2] << 8)  |
                          ((uint32_t)in_buf[3]);

    uint16_t offset = ((uint16_t)in_buf[4] << 8) |
                      ((uint16_t)in_buf[5]);

    sucs_ex_char_t host_cp;
    if (!esutf_translate_to_host(page_index, offset, &host_cp)) {
        return 0;
    }

    *out_cp = host_cp;
    return ESUTF_IPC_FRAME_BYTES;
}
