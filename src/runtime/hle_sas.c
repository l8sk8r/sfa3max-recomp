/* sceSasCore: the PSP's 32-voice sample synthesiser, used for every sound effect and voice.
 *
 * Behaviour follows PPSSPP's SasAudio (GPL-2.0-or-later, https://github.com/hrydgard/ppsspp):
 * VAG (PS-ADPCM) decoding with loop flags, the ADSR envelope state machine and rate tables,
 * two-tap linear resampling by pitch, the 32-sample key-on delay, and the reverb on the "effect"
 * send (sas_reverb.c).
 */
#include "rt.h"
#include <string.h>

#define VOICES 32
#define PITCH_BASE 0x1000
#define PITCH_SHIFT 12
#define PITCH_MASK 0xFFF
#define ENV_MAX 0x40000000LL
#define VOL_MAX 0x1000

enum { C_LIN_INC, C_LIN_DEC, C_LIN_BENT, C_EXP_DEC, C_EXP_INC, C_DIRECT };
enum { S_ATTACK, S_DECAY, S_SUSTAIN, S_RELEASE, S_OFF, S_KEYON, S_KEYON_STEP };
enum { V_NONE, V_VAG, V_NOISE };

typedef struct {
    int attack_rate, decay_rate, sustain_rate, sustain_level, release_rate;
    int attack_type, decay_type, sustain_type, release_type;
    int state; int64_t height;
} Env;

typedef struct {
    uint32_t data, read; int num_blocks, cur_block, cur_sample, loop_start_block;
    int loop_enabled, loop_at_next, end; int s1, s2; int16_t samples[28];
} Vag;

typedef struct {
    int type, playing, on, paused, loop;
    uint32_t vag_addr; int vag_size;
    int pitch, vol_l, vol_r, eff_l, eff_r, noise_freq;
    uint32_t sample_frac; int16_t hist[2];
    Env env; Vag vag;
    uint32_t noise_state;
} Voice;

static Voice voices[VOICES];
static int grain = 256, output_mode;
static int32_t mixbuf[2048 * 2], sendbuf[2048 * 2];
static int16_t send_down[2048], send_wet[2048 * 2];
static struct { int type, dry, wet, vol_l, vol_r, delay, feedback; } effect = { -1, 1, 0, 0, 0, 0, 0 };
void sas_reverb_preset(int type);                 /* sas_reverb.c */
void sas_reverb_params(int delay, int feedback);
void sas_reverb_process(int16_t *out, const int16_t *in, int n, int vol_l, int vol_r);
static int16_t temp[2048 * 4 + 8];

static int16_t clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

/* ---------------------------------------------------------------- VAG */
static const int vag_f[16][2] = { {0,0},{60,0},{115,52},{98,55},{122,60} };

static void vag_start(Vag *v, uint32_t data, int size, int loop) {
    memset(v, 0, sizeof *v);
    v->loop_enabled = loop; v->loop_start_block = -1; v->num_blocks = size / 16;
    v->data = v->read = data; v->cur_sample = 28; v->cur_block = -1;
}
static void vag_block(Vag *v) {
    if (v->cur_block == v->num_blocks - 1) { v->end = 1; return; }
    const uint8_t *p = HOST(v->read);
    int predict = p[0] >> 4, shift = p[0] & 15, flags = p[1];
    if (flags == 7) { v->end = 1; return; }
    if (flags == 6) v->loop_start_block = v->cur_block;
    else if (flags == 3 && v->loop_enabled) v->loop_at_next = 1;
    int s1 = v->s1, s2 = v->s2, c1 = vag_f[predict][0], c2 = -vag_f[predict][1];
    for (int i = 0; i < 28; i += 2) {
        uint8_t d = p[2 + i / 2];
        int a = (int16_t)((d & 0xF) << 12) >> shift, b = (int16_t)((d & 0xF0) << 8) >> shift;
        s2 = clamp16(a + ((s1 * c1 + s2 * c2) >> 6));
        s1 = clamp16(b + ((s2 * c1 + s1 * c2) >> 6));
        v->samples[i] = (int16_t)s2; v->samples[i + 1] = (int16_t)s1;
    }
    v->s1 = s1; v->s2 = s2; v->cur_sample = 0; v->cur_block++; v->read += 16;
}
static void vag_get(Vag *v, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        if (v->end) { memset(out + i, 0, (size_t)(n - i) * 2); return; }
        if (v->cur_sample == 28) {
            if (v->loop_at_next) {
                v->read = v->data + 16u * (uint32_t)v->loop_start_block + 16u;
                v->cur_block = v->loop_start_block; v->loop_at_next = 0;
            }
            vag_block(v);
            if (v->end) { memset(out + i, 0, (size_t)(n - i) * 2); return; }
        }
        out[i] = v->samples[v->cur_sample++];
    }
}

/* ---------------------------------------------------------------- envelope */
static int simple_rate(int n) { n &= 0x7F; if (n == 0x7F) return 0; int r = ((7 - (n & 3)) << 26) >> (n >> 2); return r ? r : 1; }
static int exp_rate(int n) { n &= 0x7F; if (n == 0x7F) return 0; int r = ((7 - (n & 3)) << 24) >> (n >> 2); return r ? r : 1; }

static void env_set_simple(Env *e, uint32_t a, uint32_t b) {
    e->attack_rate = simple_rate((int)(a >> 8));
    e->attack_type = (a & 0x8000) ? C_LIN_BENT : C_LIN_INC;
    int dn = (a >> 4) & 15; e->decay_rate = dn ? (int)(0x80000000u >> dn) : 0x7FFFFFFF;
    e->decay_type = C_EXP_DEC;
    e->sustain_type = (b >> 14) & 3;
    e->sustain_rate = e->sustain_type == C_EXP_DEC ? exp_rate((int)(b >> 6)) : simple_rate((int)(b >> 6));
    e->release_type = (b & 0x20) ? C_EXP_DEC : C_LIN_DEC;
    int rn = b & 31;
    if (rn == 31) e->release_rate = 0;
    else if (e->release_type == C_LIN_DEC) e->release_rate = rn == 30 ? 0x40000000 : rn == 29 ? 1 : 0x10000000 >> rn;
    else e->release_rate = rn ? (int)(0x80000000u >> rn) : 0x7FFFFFFF;
    e->sustain_level = (int)(((a & 15) + 1) << 26);
}
static void env_walk(Env *e, int type, int rate) {
    int64_t d;
    switch (type) {
    case C_LIN_INC: e->height += rate; break;
    case C_LIN_DEC: e->height -= rate; break;
    case C_LIN_BENT: e->height += e->height <= ENV_MAX * 3 / 4 ? rate : rate / 4; break;
    case C_EXP_DEC: d = e->height - ENV_MAX; d += (-d * (int64_t)(uint32_t)rate) >> 32; e->height = d + ENV_MAX - ((uint32_t)rate + 3u) / 4u; break;
    case C_EXP_INC: d = e->height - ENV_MAX; d += (-d * (int64_t)(uint32_t)rate) >> 32; e->height = d + 0x4000 + ENV_MAX; break;
    case C_DIRECT: e->height = rate; break;
    }
}
static void env_state(Env *e, int s) { if (e->height > ENV_MAX) e->height = ENV_MAX; e->state = s; }
static void env_step(Env *e) {
    switch (e->state) {
    case S_ATTACK: env_walk(e, e->attack_type, e->attack_rate); if (e->height >= ENV_MAX || e->height < 0) env_state(e, S_DECAY); break;
    case S_DECAY: env_walk(e, e->decay_type, e->decay_rate); if (e->height < e->sustain_level) env_state(e, S_SUSTAIN); break;
    case S_SUSTAIN: env_walk(e, e->sustain_type, e->sustain_rate); if (e->height <= 0) { e->height = 0; env_state(e, S_RELEASE); } break;
    case S_RELEASE: env_walk(e, e->release_type, e->release_rate); if (e->height <= 0) { e->height = 0; env_state(e, S_OFF); } break;
    case S_KEYON: e->height = 0; env_state(e, S_KEYON_STEP); break;
    case S_KEYON_STEP: if (++e->height >= 31) { e->height = 0; env_state(e, S_ATTACK); } break;
    default: break;
    }
}
static int env_height(const Env *e) { return (int)(e->height > ENV_MAX ? ENV_MAX : e->height); }

/* ---------------------------------------------------------------- mixing */
static void read_samples(Voice *v, int16_t *out, int n) {
    if (v->type == V_VAG) { vag_get(&v->vag, out, n); return; }
    if (v->type == V_NOISE) {
        for (int i = 0; i < n; i++) {                  /* simple LFSR noise */
            v->noise_state = v->noise_state * 1103515245u + 12345u;
            out[i] = (int16_t)(v->noise_state >> 16);
        }
        return;
    }
    memset(out, 0, (size_t)n * 2);
}

static void mix_voice(Voice *v) {
    if (v->type == V_VAG && !v->vag_addr) return;
    int keyon = v->env.state == S_KEYON;
    int delay = 0;
    if (keyon) delay = (int)((32u * (uint32_t)v->pitch) >> PITCH_SHIFT) + (v->type == V_VAG);
    temp[0] = v->hist[0]; temp[1] = v->hist[1];
    uint32_t frac = v->sample_frac;
    int left = grain - delay; if (left < 0) left = 0;
    int to_read = (int)((frac + (uint32_t)v->pitch * (uint32_t)left) >> PITCH_SHIFT);
    if (to_read > (int)(sizeof temp / 2) - 2) to_read = (int)(sizeof temp / 2) - 2;
    int pos = 2;
    if (keyon) { pos = 0; to_read += 2; }
    read_samples(v, &temp[pos], to_read);
    int tpos = pos + to_read;
    for (int i = 0; i < delay; i++) env_step(&v->env);
    int interp = v->pitch != PITCH_BASE || (frac & PITCH_MASK);
    for (int i = delay; i < grain; i++) {
        const int16_t *s = temp + (frac >> PITCH_SHIFT);
        int smp = s[0];
        if (interp) { int f = (int)(frac & PITCH_MASK); smp = s[0] - (((s[0] - s[1]) * f) >> PITCH_SHIFT); }
        frac += (uint32_t)v->pitch;
        int ev = (env_height(&v->env) + (1 << 14)) >> 15;
        env_step(&v->env);
        smp = (smp * ev + (1 << 14)) >> 15;
        mixbuf[2 * i] += (smp * v->vol_l) >> 12;
        mixbuf[2 * i + 1] += (smp * v->vol_r) >> 12;
        sendbuf[2 * i] += (smp * v->eff_l) >> 12;
        sendbuf[2 * i + 1] += (smp * v->eff_r) >> 12;
    }
    v->hist[0] = temp[tpos - 2]; v->hist[1] = temp[tpos - 1];
    v->sample_frac = frac - (uint32_t)(tpos - 2) * PITCH_BASE;
    if (v->type == V_VAG && v->vag.end) { v->env.height = 0; env_state(&v->env, S_OFF); }
    if (v->env.state == S_OFF) { v->playing = 0; v->on = 0; }
}

/* the effect send through the reverb: down to 22 kHz (every other sample), reverb, back up (the
   reverb writes each output sample followed by a zero, as PPSSPP has it) */
static void apply_effect(void) {
    for (int i = 0; i < grain / 2; i++) { send_down[2 * i] = clamp16(sendbuf[4 * i]); send_down[2 * i + 1] = clamp16(sendbuf[4 * i + 1]); }
    sas_reverb_process(send_wet, send_down, grain / 2, (effect.vol_l << 3) & 0xFFFF, (effect.vol_r << 3) & 0xFFFF);
}
static void mix(uint32_t out, uint32_t in, int lv, int rv) {
    memset(mixbuf, 0, sizeof(int32_t) * 2 * (size_t)grain);
    memset(sendbuf, 0, sizeof(int32_t) * 2 * (size_t)grain);
    for (int i = 0; i < VOICES; i++) if (voices[i].playing && !voices[i].paused) mix_voice(&voices[i]);
    int16_t *o = (int16_t *)HOST(out);
    const int16_t *p = in ? (const int16_t *)HOST(in) : NULL;
    if (output_mode == 0) {
        int dry = effect.dry, wet = effect.wet;
        if (wet) apply_effect();
        for (int i = 0; i < grain * 2; i += 2) {
            int l = 0, r = 0;
            if (p) { l += (p[i] * lv) >> 12; r += (p[i + 1] * rv) >> 12; }
            if (dry) { l += mixbuf[i]; r += mixbuf[i + 1]; }
            if (wet) { l += send_wet[i]; r += send_wet[i + 1]; }
            o[i] = clamp16(l); o[i + 1] = clamp16(r);
        }
    } else {                                   /* raw mode: L, R, send L, send R planes */
        for (int i = 0; i < grain; i++) {
            o[i] = clamp16(mixbuf[2 * i]); o[grain + i] = clamp16(mixbuf[2 * i + 1]);
            o[2 * grain + i] = clamp16(sendbuf[2 * i]); o[3 * grain + i] = clamp16(sendbuf[2 * i + 1]);
        }
    }
}

/* ---------------------------------------------------------------- the API */
#define ERR_SAS_VOICE 0x80420010u
#define ERR_SAS_PARAM 0x80420013u
#define ERR_SAS_PAUSED 0x80420016u
#define VOICE_OR_FAIL(n) if ((int)(n) < 0 || (int)(n) >= VOICES) RET(ERR_SAS_VOICE); Voice *v = &voices[n]

void hle___sceSasInit(Cpu *c) {
    grain = (int)ARG(1); output_mode = (int)ARG(3);
    effect.type = -1; effect.dry = 1; effect.wet = 0; effect.vol_l = effect.vol_r = 0; sas_reverb_preset(-1);
    if (grain < 0x40 || grain > 0x800) grain = 256;
    memset(voices, 0, sizeof voices);
    for (int i = 0; i < VOICES; i++) {
        Voice *v = &voices[i];
        v->pitch = PITCH_BASE; v->vol_l = v->vol_r = VOL_MAX;
        v->env.decay_type = v->env.sustain_type = v->env.release_type = C_LIN_DEC;
        v->env.state = S_OFF; v->noise_state = 0x1234u + (uint32_t)i;
    }
    TRACE("SasInit grain=%d mode=%d", grain, output_mode);
    RET(0);
}
void hle___sceSasCore(Cpu *c) { mix(ARG(1), 0, 0, 0); RET(0); }
void hle___sceSasCoreWithMix(Cpu *c) { mix(ARG(1), ARG(1), (int)ARG(2), (int)ARG(3)); RET(0); }
void hle___sceSasGetEndFlag(Cpu *c) {
    uint32_t f = 0;
    for (int i = 0; i < VOICES; i++) if (!voices[i].playing) f |= 1u << i;
    RET(f);
}
void hle___sceSasSetVolume(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    v->vol_l = (int)ARG(2); v->vol_r = (int)ARG(3); v->eff_l = (int)ARG(4); v->eff_r = (int)ARG(5);
    RET(0);
}
void hle___sceSasSetPitch(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    if ((int)ARG(2) < 0 || ARG(2) > 0x4000) RET(0x80420011);
    v->pitch = (int)ARG(2); RET(0);
}
void hle___sceSasSetVoice(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    uint32_t addr = ARG(2); int size = (int)ARG(3), loop = (int)ARG(4);
    if (size <= 0 || (size & 15)) RET(ERR_SAS_PARAM);
    int reset = v->type != V_VAG || v->vag_addr != addr || v->vag_size != size || v->loop != (loop != 0);
    v->type = V_VAG; v->vag_addr = addr; v->vag_size = size; v->loop = loop != 0;
    if (v->on) v->playing = 1;
    if (reset) vag_start(&v->vag, addr, size, loop != 0);
    RET(0);
}
void hle___sceSasSetNoise(Cpu *c) { VOICE_OR_FAIL(ARG(1)); v->type = V_NOISE; v->noise_freq = (int)ARG(2); RET(0); }
void hle___sceSasSetADSR(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    uint32_t f = ARG(2);
    if (f & 1) v->env.attack_rate = (int)ARG(3);
    if (f & 2) v->env.decay_rate = (int)ARG(4);
    if (f & 4) v->env.sustain_rate = (int)ARG(5);
    if (f & 8) v->env.release_rate = (int)ARG(6);
    RET(0);
}
void hle___sceSasSetADSRmode(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    uint32_t f = ARG(2);
    if (f & 1) v->env.attack_type = (int)ARG(3);
    if (f & 2) v->env.decay_type = (int)ARG(4);
    if (f & 4) v->env.sustain_type = (int)ARG(5);
    if (f & 8) v->env.release_type = (int)ARG(6);
    RET(0);
}
void hle___sceSasSetSL(Cpu *c) { VOICE_OR_FAIL(ARG(1)); v->env.sustain_level = (int)ARG(2); RET(0); }
void hle___sceSasSetSimpleADSR(Cpu *c) { VOICE_OR_FAIL(ARG(1)); env_set_simple(&v->env, ARG(2) & 0xFFFF, ARG(3) & 0xFFFF); RET(0); }
void hle___sceSasGetEnvelopeHeight(Cpu *c) { VOICE_OR_FAIL(ARG(1)); RET(env_height(&v->env)); }
void hle___sceSasSetKeyOn(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    if (v->paused || v->on) RET(ERR_SAS_PAUSED);
    v->env.state = S_KEYON;
    TRACE("SasKeyOn voice %d type %d vag %08X size %d pitch %X vol %d/%d env A%X/%d D%X S%X/%d R%X SL%X",
          (int)ARG(1), v->type, v->vag_addr, v->vag_size, v->pitch, v->vol_l, v->vol_r, v->env.attack_rate, v->env.attack_type,
          v->env.decay_rate, v->env.sustain_rate, v->env.sustain_type, v->env.release_rate, v->env.sustain_level);
    if (v->type == V_VAG) vag_start(&v->vag, v->vag_addr, v->vag_size, v->loop);
    v->playing = v->on = 1; v->paused = 0; v->sample_frac = 0;
    RET(0);
}
void hle___sceSasSetKeyOff(Cpu *c) {
    VOICE_OR_FAIL(ARG(1));
    if (v->paused || !v->on) RET(ERR_SAS_PAUSED);
    v->on = 0; env_state(&v->env, S_RELEASE);
    RET(0);
}
void hle___sceSasSetPause(Cpu *c) {
    uint32_t bits = ARG(1); int p = ARG(2) != 0;
    for (int i = 0; i < VOICES && bits; i++, bits >>= 1) if (bits & 1) voices[i].paused = p;
    RET(0);
}
void hle___sceSasGetPauseFlag(Cpu *c) {
    uint32_t f = 0;
    for (int i = 0; i < VOICES; i++) if (voices[i].paused) f |= 1u << i;
    RET(f);
}
/* reverb (the effect send): on / off for the dry and wet mixes, the wet volume, the type (-1 off,
   0-8 presets) and the delay / feedback that Echo and Delay use */
void hle___sceSasRevVON(Cpu *c) {
    effect.dry = ARG(1) != 0; effect.wet = ARG(2) != 0;
    TRACE("SasRevVON dry %d wet %d", effect.dry, effect.wet);
    RET(0);
}
void hle___sceSasRevEVOL(Cpu *c) {
    if (ARG(1) > 0x1000 || ARG(2) > 0x1000) RET(0x80420023);
    effect.vol_l = (int)ARG(1); effect.vol_r = (int)ARG(2);
    TRACE("SasRevEVOL %d %d", effect.vol_l, effect.vol_r);
    RET(0);
}
void hle___sceSasRevType(Cpu *c) {
    int t = (int)ARG(1);
    if (t < -1 || t > 8) RET(0x80420020);
    if (t != effect.type) { effect.type = t; sas_reverb_preset(t); }
    TRACE("SasRevType %d", t);
    RET(0);
}
void hle___sceSasRevParam(Cpu *c) {
    int d = (int)ARG(1), f = (int)ARG(2);
    if (d < 0 || d >= 128) RET(0x80420022);
    if (f < 0 || f >= 128) RET(0x80420021);
    effect.delay = d; effect.feedback = f; sas_reverb_params(d, f);
    TRACE("SasRevParam %d %d", d, f);
    RET(0);
}
