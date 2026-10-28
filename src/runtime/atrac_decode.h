/* Whole-file ATRAC3/ATRAC3+ decoding (C interface to the vendored C++ decoder). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int codec;            /* 0x1000 ATRAC3+, 0x1001 ATRAC3 */
    int channels, block_align;
    uint32_t data_off, data_size, extra_off, extra_size;
    int total_samples;    /* logical samples (fact chunk) */
    int first_offset;     /* fact chunk's first-sample offset */
    int skip;             /* decoded samples before logical sample 0 */
    int loop_start, loop_end;   /* logical samples, -1 if none */
} AtracTrack;
int      atrac_parse(const uint8_t *file, uint32_t size, AtracTrack *t);
int16_t *atrac_decode_all(const uint8_t *file, const AtracTrack *t, int *out_samples);   /* stereo s16, free() */
#ifdef __cplusplus
}
#endif
