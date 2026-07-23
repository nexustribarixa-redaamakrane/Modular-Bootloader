#ifndef EXTSUTF_FIXED_H
#define EXTSUTF_FIXED_H

#include "extsucs_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUTF32_BYTES 4
size_t sutf32_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size);
size_t sutf32_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

#define SUTF64_BYTES 8
size_t sutf64_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size);
size_t sutf64_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

#define SUTF128_BYTES 16
size_t sutf128_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size);
size_t sutf128_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

#define SUTF256_BYTES 32
size_t sutf256_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size);
size_t sutf256_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

#define SUTF512_BYTES 64
size_t sutf512_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t buf_size);
size_t sutf512_decode(const uint8_t* in_buf, size_t buf_size, sucs_ex_char_t* out_cp);

size_t sutfn_encode(sucs_ex_char_t ex_cp, uint8_t* out_buf, size_t slot_bytes);
size_t sutfn_decode(const uint8_t* in_buf, size_t slot_bytes, sucs_ex_char_t* out_cp);

#ifdef __cplusplus
}
#endif

#endif /* EXTSUTF_FIXED_H */
