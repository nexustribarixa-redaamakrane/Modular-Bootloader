#ifndef ESUTF_H
#define ESUTF_H

#include "extsucs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESUTF_PAGE_SIZE         4096ULL
#define ESUTF_PAGE_SHIFT        12
#define ESUTF_OFFSET_MASK       0x0FFFULL
#define ESUTF_IPC_FRAME_BYTES   6

typedef struct {
    sucs_ex_char_t  host_base;
    uint32_t        page_index;
    uint32_t        flags;
} esutf_page_entry_t;

#define ESUTF_PAGE_READ     0x01U
#define ESUTF_PAGE_WRITE    0x02U
#define ESUTF_PAGE_EXEC     0x04U
#define ESUTF_PAGE_PRESENT  0x08U

bool esutf_translate_to_guest(sucs_ex_char_t host_cp, uint32_t* out_page_index, uint16_t* out_offset);
bool esutf_translate_to_host(uint32_t page_index, uint16_t offset, sucs_ex_char_t* out_host_cp);
size_t esutf_encode_ipc(sucs_ex_char_t host_cp, uint8_t* out_buf, size_t buf_size);
size_t esutf_decode_ipc(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

#ifdef __cplusplus
}
#endif

#endif /* ESUTF_H */
