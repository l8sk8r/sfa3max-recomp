/* The SAS reverb: the PS1 SPU's reverb with the PSP's own presets.
 *
 * A C port of PPSSPP's SasReverb (Core/HW/SasReverb.cpp, Copyright (c) 2015- PPSSPP Project,
 * GPL-2.0-or-later, https://github.com/hrydgard/ppsspp): the formula is nocash's
 * (http://problemkaputt.de/psx-spx.htm#spureverbformula), the preset constants the PSP's. It runs at
 * 22 kHz on the effect send of the voices; Echo and Delay recompute some of their taps from
 * sceSasRevParam's delay and feedback.
 */
#include "rt.h"
#include <string.h>

typedef struct {
    int32_t size;
    int16_t dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL;
    int16_t vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2;
    int16_t dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4;
    int16_t dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2;
} Preset;

#define H(x) ((int16_t)(x))
static const Preset presets[9] = {
    { 0x26C0,   /* Room */
      0x007D,0x005B,0x6D80,0x54B8,H(0xBED0),0x0000,0x0000,H(0xBA80),
      0x5800,0x5300,0x04D6,0x0333,0x03F0,0x0227,0x0374,0x01EF,
      0x0336,0x01B7,0x0335,0x01B6,0x0334,0x01B5,0x0334,0x01B5,
      0x0334,0x01B5,0x01B4,0x0136,0x00B8,0x005C },
    { 0x1F40,   /* Studio Small */
      0x0033,0x0025,0x70F0,0x4FA8,H(0xBCE0),0x4410,H(0xC0F0),H(0x9C00),
      0x5280,0x4EC0,0x03E4,0x031B,0x03A4,0x02AF,0x0372,0x0266,
      0x031C,0x025D,0x025C,0x018E,0x022F,0x0135,0x01D2,0x00B7,
      0x018F,0x00B5,0x00B4,0x0080,0x004C,0x0026 },
    { 0x4840,   /* Studio Medium */
      0x00B1,0x007F,0x70F0,0x4FA8,H(0xBCE0),0x4510,H(0xBEF0),H(0xB4C0),
      0x5280,0x4EC0,0x0904,0x076B,0x0824,0x065F,0x07A2,0x0616,
      0x076C,0x05ED,0x05EC,0x042E,0x050F,0x0305,0x0462,0x02B7,
      0x042F,0x0265,0x0264,0x01B2,0x0100,0x0080 },
    { 0x6FE0,   /* Studio Large */
      0x00E3,0x00A9,0x6F60,0x4FA8,H(0xBCE0),0x4510,H(0xBEF0),H(0xA680),
      0x5680,0x52C0,0x0DFB,0x0B58,0x0D09,0x0A3C,0x0BD9,0x0973,
      0x0B59,0x08DA,0x08D9,0x05E9,0x07EC,0x04B0,0x06EF,0x03D2,
      0x05EA,0x031D,0x031C,0x0238,0x0154,0x00AA },
    { 0xADE0,   /* Hall */
      0x01A5,0x0139,0x6000,0x5000,0x4C00,H(0xB800),H(0xBC00),H(0xC000),
      0x6000,0x5C00,0x15BA,0x11BB,0x14C2,0x10BD,0x11BC,0x0DC1,
      0x11C0,0x0DC3,0x0DC0,0x09C1,0x0BC4,0x07C1,0x0A00,0x06CD,
      0x09C2,0x05C1,0x05C0,0x041A,0x0274,0x013A },
    { 0xF6C0,   /* Space Echo */
      0x033D,0x0231,0x7E00,0x5000,H(0xB400),H(0xB000),0x4C00,H(0xB000),
      0x6000,0x5400,0x1ED6,0x1A31,0x1D14,0x183B,0x1BC2,0x16B2,
      0x1A32,0x15EF,0x15EE,0x1055,0x1334,0x0F2D,0x11F6,0x0C5D,
      0x1056,0x0AE1,0x0AE0,0x07A2,0x0464,0x0232 },
    { 0x18040,  /* Echo (almost infinite): partly recomputed from delay / feedback */
      0x0003,0x0003,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,H(0x8100),
      0x0000,0x0000,0x1FFD,0x0FFD,0x1009,0x0009,0x0000,0x0000,
      0x1009,0x0009,0x1FFF,0x1FFF,0x1FFE,0x1FFE,0x1FFE,0x1FFE,
      0x1FFE,0x1FFE,0x1008,0x1004,0x0008,0x0004 },
    { 0x18040,  /* Delay (one-shot echo): as Echo but no feedback by default */
      0x0003,0x0003,0x7FFF,0x7FFF,0x0000,0x0000,0x0000,0x0000,
      0x0000,0x0000,0x1FFD,0x0FFD,0x1009,0x0009,0x0000,0x0000,
      0x1009,0x0009,0x1FFF,0x1FFF,0x1FFE,0x1FFE,0x1FFE,0x1FFE,
      0x1FFE,0x1FFE,0x1008,0x1004,0x0008,0x0004 },
    { 0x3C00,   /* Half Echo */
      0x0017,0x0013,0x70F0,0x4FA8,H(0xBCE0),0x4510,H(0xBEF0),H(0x8500),
      0x5F80,0x54C0,0x0371,0x02AF,0x02E5,0x01DF,0x02B0,0x01D7,
      0x0358,0x026A,0x01D6,0x011E,0x012D,0x00B1,0x011F,0x0059,
      0x01A0,0x00E3,0x0058,0x0040,0x0028,0x0014 },
};
enum { T_ECHO = 6, T_DELAY = 7 };

#define BUFSIZE 0x20000
static int16_t work[BUFSIZE];
static Preset d;                               /* the preset in effect (Echo / Delay patched) */
static int preset = -1, pos, delay_, feedback_;

static int16_t clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

static void apply_params(void) {
    if (preset < 0) return;
    d = presets[preset];
    if (preset != T_ECHO && preset != T_DELAY) return;
    int d16 = (delay_ + 1) * 16;               /* the hardware recomputes these from delay and feedback */
    d.mLSAME = (int16_t)(d16 * 2 - d.dAPF1);
    d.mRSAME = (int16_t)(d16 - d.dAPF2);
    d.mLCOMB1 = (int16_t)(d.mRCOMB1 + d16);
    d.dLSAME = (int16_t)(d.dRSAME + d16);
    d.mLAPF1 = (int16_t)(d.mLAPF2 + d16);
    d.mRAPF1 = (int16_t)(d16 + (d.mLAPF2 >> 1));
    d.vWALL = (int16_t)(-(feedback_ << 8));
}
void sas_reverb_preset(int type) {
    if (type >= -1 && type < 9) preset = type;
    if (preset >= 0) { pos = BUFSIZE - presets[preset].size; memset(work, 0, sizeof work); }
    else pos = 0;
    apply_params();
}
void sas_reverb_params(int delay, int feedback) { delay_ = delay; feedback_ = feedback; apply_params(); }

/* the work area: the top SIZE samples of the buffer, wrapping */
static int lo_, size_;
static inline int16_t *B(int i) {
    int a = pos + i;
    if (a >= BUFSIZE) a -= size_;
    if (a < lo_) a += size_;
    return &work[a];
}

/* IN: N stereo samples at 22 kHz; OUT: 2N stereo samples at 44 kHz (each followed by a zero), the
   wet signal at VOL_L / VOL_R (0x8000 = 1) */
void sas_reverb_process(int16_t *out, const int16_t *in, int n, int vol_l, int vol_r) {
    if (preset < 0) {                          /* off: the send itself */
        for (int i = 0; i < n; i++) {
            out[4 * i + 0] = clamp16(in[2 * i] * vol_l >> 15); out[4 * i + 1] = clamp16(in[2 * i + 1] * vol_r >> 15);
            out[4 * i + 2] = clamp16(in[2 * i] * vol_l >> 15); out[4 * i + 3] = clamp16(in[2 * i + 1] * vol_r >> 15);
        }
        return;
    }
    vol_l <<= 1; vol_r <<= 1;                  /* the return is (evol * out) >> 11 */
    size_ = d.size; lo_ = BUFSIZE - size_;
    for (int i = 0; i < n; i++) {
        int Lin = in[2 * i] >> 2, Rin = in[2 * i + 1] >> 2;   /* the send enters at a quarter */
        *B(d.mLSAME) = clamp16(Lin + (*B(d.dLSAME) * d.vWALL >> 15) - (*B(d.mLSAME - 1) * d.vIIR >> 15) + *B(d.mLSAME - 1));
        *B(d.mRSAME) = clamp16(Rin + (*B(d.dRSAME) * d.vWALL >> 15) - (*B(d.mRSAME - 1) * d.vIIR >> 15) + *B(d.mRSAME - 1));
        *B(d.mLDIFF) = clamp16(Lin + (*B(d.dRDIFF) * d.vWALL >> 15) - (*B(d.mLDIFF - 1) * d.vIIR >> 15) + *B(d.mLDIFF - 1));
        *B(d.mRDIFF) = clamp16(Rin + (*B(d.dLDIFF) * d.vWALL >> 15) - (*B(d.mRDIFF - 1) * d.vIIR >> 15) + *B(d.mRDIFF - 1));
        int Lout = (d.vCOMB1 * *B(d.mLCOMB1) + d.vCOMB2 * *B(d.mLCOMB2) + d.vCOMB3 * *B(d.mLCOMB3) + d.vCOMB4 * *B(d.mLCOMB4)) >> 15;
        int Rout = (d.vCOMB1 * *B(d.mRCOMB1) + d.vCOMB2 * *B(d.mRCOMB2) + d.vCOMB3 * *B(d.mRCOMB3) + d.vCOMB4 * *B(d.mRCOMB4)) >> 15;
        *B(d.mLAPF1) = clamp16(Lout - (d.vAPF1 * *B(d.mLAPF1 - d.dAPF1) >> 15));
        Lout = *B(d.mLAPF1 - d.dAPF1) + (*B(d.mLAPF1) * d.vAPF1 >> 15);
        *B(d.mRAPF1) = clamp16(Rout - (d.vAPF1 * *B(d.mRAPF1 - d.dAPF1) >> 15));
        Rout = *B(d.mRAPF1 - d.dAPF1) + (*B(d.mRAPF1) * d.vAPF1 >> 15);
        *B(d.mLAPF2) = clamp16(Lout - (d.vAPF2 * *B(d.mLAPF2 - d.dAPF2) >> 15));
        Lout = *B(d.mLAPF2 - d.dAPF2) + (*B(d.mLAPF2) * d.vAPF2 >> 15);
        *B(d.mRAPF2) = clamp16(Rout - (d.vAPF2 * *B(d.mRAPF2 - d.dAPF2) >> 15));
        Rout = *B(d.mRAPF2 - d.dAPF2) + (*B(d.mRAPF2) * d.vAPF2 >> 15);
        out[4 * i + 0] = clamp16((Lout * vol_l) >> 15);
        out[4 * i + 1] = clamp16((Rout * vol_r) >> 15);
        out[4 * i + 2] = 0; out[4 * i + 3] = 0;
        if (++pos >= BUFSIZE) pos -= size_;
    }
}
