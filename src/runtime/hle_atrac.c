/* sceAtrac3plus: the game hands over a whole .at3 file in memory; it is decoded once to PCM and then
 * served 2048 samples per DecodeData call, honouring the loop points and loop count. */
#include "rt.h"
#include "atrac_decode.h"
#include <string.h>

#define MAX_ATRAC 8
#define ERR_ATRAC_NO_ID        0x80630003u
#define ERR_ATRAC_BAD_ID       0x80630005u
#define ERR_ATRAC_UNKNOWN_FMT  0x80630006u
#define ERR_ATRAC_ALL_DECODED  0x80630024u
#define ERR_ATRAC_NO_LOOP      0x80630021u
#define ERR_ATRAC_SECOND_BUFFER_NOT_NEEDED 0x80630022u

typedef struct {
    int used;
    AtracTrack t;
    int16_t *pcm; int pcm_samples;
    int cur;            /* next logical sample */
    int loop_num;
    int ended;
} Atrac;
static Atrac at[MAX_ATRAC];

static Atrac *get(uint32_t id) { return id < MAX_ATRAC && at[id].used ? &at[id] : NULL; }

void hle_sceAtracSetDataAndGetID(Cpu *c) {
    uint32_t buf = ARG(0), size = ARG(1);
    int id;
    for (id = 0; id < MAX_ATRAC && at[id].used; id++) {}
    if (id >= MAX_ATRAC) RET(ERR_ATRAC_NO_ID);
    Atrac *a = &at[id];
    memset(a, 0, sizeof *a);
    if (atrac_parse(HOST(buf), size, &a->t) < 0) RET(ERR_ATRAC_UNKNOWN_FMT);
    a->pcm = atrac_decode_all(HOST(buf), &a->t, &a->pcm_samples);
    a->used = 1;
    TRACE("Atrac %d: codec %X, %d samples, loop %d..%d, %d decoded", id, a->t.codec, a->t.total_samples,
          a->t.loop_start, a->t.loop_end, a->pcm_samples);
    RET(id);
}
void hle_sceAtracReleaseAtracID(Cpu *c) {
    Atrac *a = get(ARG(0)); if (!a) RET(ERR_ATRAC_BAD_ID);
    free(a->pcm); memset(a, 0, sizeof *a); RET(0);
}
void hle_sceAtracDecodeData(Cpu *c) {
    Atrac *a = get(ARG(0)); uint32_t out = ARG(1), nsamp = ARG(2), finish = ARG(3), remain = ARG(4);
    if (!a) RET(ERR_ATRAC_BAD_ID);
    int end = a->t.total_samples;                 /* one past the last logical sample */
    if (a->ended || a->cur >= end) {
        if (nsamp) WW(nsamp, 0);
        if (finish) WW(finish, 1);
        if (remain) WW(remain, 0xFFFFFFFF);
        RET(ERR_ATRAC_ALL_DECODED);
    }
    int limit = end;
    int looping = a->t.loop_end >= 0 && a->loop_num != 0;
    if (looping && a->cur <= a->t.loop_end) limit = a->t.loop_end + 1;
    int n = limit - a->cur; if (n > 2048) n = 2048;
    int16_t *dst = (int16_t *)HOST(out);
    for (int i = 0; i < n; i++) {
        long k = (long)a->cur + i + a->t.skip;
        if (k >= 0 && k < a->pcm_samples) { dst[2 * i] = a->pcm[2 * k]; dst[2 * i + 1] = a->pcm[2 * k + 1]; }
        else dst[2 * i] = dst[2 * i + 1] = 0;
    }
    a->cur += n;
    int fin = 0;
    if (looping && a->cur > a->t.loop_end) {
        a->cur = a->t.loop_start;
        if (a->loop_num > 0) a->loop_num--;
    } else if (a->cur >= end) { fin = 1; a->ended = 1; }
    if (nsamp) WW(nsamp, (uint32_t)n);
    if (finish) WW(finish, (uint32_t)fin);
    if (remain) WW(remain, 0xFFFFFFFF);          /* the whole file is in memory */
    RET(0);
}
void hle_sceAtracGetRemainFrame(Cpu *c) { if (!get(ARG(0))) RET(ERR_ATRAC_BAD_ID); if (ARG(1)) WW(ARG(1), 0xFFFFFFFF); RET(0); }
void hle_sceAtracGetSecondBufferInfo(Cpu *c) {
    if (!get(ARG(0))) RET(ERR_ATRAC_BAD_ID);
    if (ARG(1)) WW(ARG(1), 0);
    if (ARG(2)) WW(ARG(2), 0);
    RET(ERR_ATRAC_SECOND_BUFFER_NOT_NEEDED);
}
void hle_sceAtracGetSoundSample(Cpu *c) {
    Atrac *a = get(ARG(0)); if (!a) RET(ERR_ATRAC_BAD_ID);
    if (ARG(1)) WW(ARG(1), (uint32_t)(a->t.total_samples - 1));
    if (ARG(2)) WW(ARG(2), (uint32_t)a->t.loop_start);
    if (ARG(3)) WW(ARG(3), (uint32_t)a->t.loop_end);
    RET(0);
}
void hle_sceAtracSetLoopNum(Cpu *c) {
    Atrac *a = get(ARG(0)); if (!a) RET(ERR_ATRAC_BAD_ID);
    if (a->t.loop_end < 0) RET(ERR_ATRAC_NO_LOOP);
    a->loop_num = (int)ARG(1); RET(0);
}
void hle_sceAtracGetInternalErrorInfo(Cpu *c) { if (ARG(1)) WW(ARG(1), 0); RET(get(ARG(0)) ? 0 : ERR_ATRAC_BAD_ID); }
