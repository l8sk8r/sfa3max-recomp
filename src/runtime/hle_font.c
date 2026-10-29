/* sceFont (libfont): the PSP's system font, used by the game for player names in the network modes.
 * There is no PSP font here, so the library offers one font of its own: the 5x7 pixel font of
 * font5x7.h drawn at double size (10x14 pixels, 12 pixels apart), which suits the game's look.
 * Structures and pixel formats follow PPSSPP's sceFont/PGF code (GPL-2.0-or-later). */
#include "rt.h"
#include "font5x7.h"
#include <string.h>

#define LIB_HANDLE  0x46000001u
#define FONT_HANDLE 0x46000002u
#define GW 10          /* glyph bitmap */
#define GH 14
#define BASE 12        /* rows above the baseline */
#define ADV 12

static void wf(uint32_t a, float f) { uint32_t u; memcpy(&u, &f, 4); WW(a, u); }

/* PGFFontStyle, 168 bytes */
static void put_style(uint32_t a) {
    memset(HOST(a), 0, 168);
    wf(a + 0, 16.0f); wf(a + 4, 16.0f); wf(a + 8, 128.0f); wf(a + 12, 128.0f); wf(a + 16, 1.0f);
    WH(a + 20, 1); WH(a + 22, 1); WH(a + 24, 0); WH(a + 26, 1); WH(a + 28, 1); WH(a + 30, 1);
    strcpy((char *)HOST(a + 32), "SFA3MR Pixel");
    strcpy((char *)HOST(a + 96), "sfa3mr.pgf");
}
static int glyph_of(uint32_t ch) { return ch >= 32 && ch <= 126 ? (int)ch - 32 : '?' - 32; }

void hle_sceFontNewLib(Cpu *c) { if (ARG(1)) WW(ARG(1), 0); RET(LIB_HANDLE); }
void hle_sceFontDoneLib(Cpu *c) { RET(0); }
void hle_sceFontGetNumFontList(Cpu *c) { if (ARG(1)) WW(ARG(1), 0); RET(1); }
void hle_sceFontGetFontList(Cpu *c) { if (ARG(1) && (int)ARG(2) > 0) put_style(ARG(1)); RET(0); }
void hle_sceFontFindOptimumFont(Cpu *c) { if (ARG(2)) WW(ARG(2), 0); RET(0); }
void hle_sceFontOpen(Cpu *c) { if (ARG(3)) WW(ARG(3), 0); RET(FONT_HANDLE); }
void hle_sceFontClose(Cpu *c) { RET(0); }

/* PGFFontInfo, 264 bytes */
void hle_sceFontGetFontInfo(Cpu *c) {
    uint32_t a = ARG(1);
    if (!a) RET(0x80460001);
    memset(HOST(a), 0, 264);
    const int iv[10] = { GW << 6, GH << 6, BASE << 6, -(GH - BASE) << 6, 0, BASE << 6, (GW / 2) << 6, BASE << 6, ADV << 6, 16 << 6 };
    for (int i = 0; i < 10; i++) { WW(a + 4u * (uint32_t)i, (uint32_t)iv[i]); wf(a + 40 + 4u * (uint32_t)i, (float)iv[i] / 64.0f); }
    WH(a + 80, GW); WH(a + 82, GH);
    WW(a + 84, 95); WW(a + 88, 0);
    put_style(a + 92);
    WB(a + 260, 4);
    RET(0);
}

/* PGFCharInfo, 60 bytes */
void hle_sceFontGetCharInfo(Cpu *c) {
    uint32_t a = ARG(2);
    if (!a) RET(0x80460001);
    memset(HOST(a), 0, 60);
    WW(a + 0, GW); WW(a + 4, GH); WW(a + 8, 0); WW(a + 12, BASE);
    WW(a + 16, GW << 6); WW(a + 20, GH << 6);
    WW(a + 24, BASE << 6); WW(a + 28, (uint32_t)(-(GH - BASE) << 6));
    WW(a + 32, 0); WW(a + 36, BASE << 6);                 /* horizontal bearing */
    WW(a + 40, (uint32_t)(-(GW / 2) << 6)); WW(a + 44, 0); /* vertical bearing */
    WW(a + 48, ADV << 6); WW(a + 52, 16 << 6);            /* advance */
    RET(0);
}

/* draw a glyph into the game's buffer at (xPos64, yPos64) / 64, inside the clip rectangle */
static void put_px(uint32_t buf, int bpl, int fmt, int x, int y, int v) {
    if (fmt <= 1) {                                       /* 4 bits per pixel; format 1 swaps the nibbles */
        uint32_t a = buf + (uint32_t)(y * bpl + (x >> 1));
        int hi = (x & 1) != fmt, old = RB(a);
        int cur = hi ? old >> 4 : old & 15;
        if (v <= cur) return;
        WB(a, (uint32_t)(hi ? (v << 4) | (old & 15) : (old & 0xF0) | v));
    } else if (fmt == 2) {
        uint32_t a = buf + (uint32_t)(y * bpl + x); int v8 = v * 17; if (v8 > (int)RB(a)) WB(a, (uint32_t)v8);
    } else if (fmt == 3) {
        uint32_t a = buf + (uint32_t)(y * bpl + x * 3); int v8 = v * 17; WB(a, (uint32_t)v8); WB(a + 1, (uint32_t)v8); WB(a + 2, (uint32_t)v8);
    } else {
        uint32_t a = buf + (uint32_t)(y * bpl + x * 4); int v8 = v * 17; WW(a, 0x00FFFFFFu | ((uint32_t)v8 << 24));
    }
}
static void draw(uint32_t img, uint32_t ch, int cx, int cy, int cw, int chh) {
    int fmt = (int)RW(img), x0 = (int)RW(img + 4) >> 6, y0 = (int)RW(img + 8) >> 6;
    int bw = RH(img + 12), bh = RH(img + 14), bpl = RH(img + 16);
    uint32_t buf = RW(img + 20);
    if (!buf) return;
    const uint8_t *g = font5x7[glyph_of(ch)];
    for (int r = 0; r < 7; r++) for (int b = 0; b < 5; b++) {
        if (!(g[r] & (0x10 >> b))) continue;
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
            int x = x0 + b * 2 + i, y = y0 + r * 2 + j;
            if (x < cx || y < cy || x >= cx + cw || y >= cy + chh || x >= bw || y >= bh || x < 0 || y < 0) continue;
            put_px(buf, bpl, fmt, x, y, 15);
        }
    }
}
void hle_sceFontGetCharGlyphImage_Clip(Cpu *c) { if (ARG(2)) draw(ARG(2), ARG(1), (int)ARG(3), (int)ARG(4), (int)ARG(5), (int)ARG(6)); RET(0); }
