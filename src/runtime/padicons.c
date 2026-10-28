/* Pad icons on the MODE MENU: a small pad for each gamepad plugged in, in the menu bar (1P, 2P on the left, 3P, 4P on the right), with its player
 * number (1P - 4P) in that player's colours: red, blue and green as the game's own 1P / 2P / 3P
 * boxes, yellow for 4P (ours). Each icon has a light outline and a black shadow, like those boxes, so
 * it stands out on the menu's background. Gamepad k is player k (host_sdl.c); one that is not plugged
 * in has no icon.
 *
 * Drawn while the MODE MENU's running state (0x0888C5B0) runs, at the end of the text pass
 * (selectscreen.c calls padicons_draw), as textured quads through the game's 2D library like JOIN /
 * WAIT: our 16-bit texture on page 0x3D only while drawing, layer 22. */
#include "rt.h"
#include <string.h>

int host_pad_connected(int i);
extern int rt_human[3];
int rt_pads_forced = -1;                    /* --pads: the gamepads taken as plugged in (bit k = pad k + 1), tests */
void f_089E0CF4(Cpu *c);
void f_089E1618(Cpu *c);

#define PAGES      0x08B0C334u
#define TEXT_TEXTURES 0x08A05080u
#define OUR_PAGE   0x3D
#define LAYER      22
#define TEX_W      128
#define TEX_H      32
#define ICON_W     21                       /* the pad (18 x 14), its outline and shadow */
#define ICON_H     15
#define CELL       32                       /* each player's icon at u = CELL * k */
/* Drawn on the game's own pixel grid, the text pass's 384 x 224 (stretched to the 480 x 272 screen),
 * one texel a unit like the rest of the menu. Each pad has its own place: icons do not move when
 * another pad comes or goes. */
#define EDGE       2                        /* from the bar's ends */
#define BAR_Y      13                       /* the menu bar is 12 - 28: centred in it */
#define STEP       22                       /* a couple of pixels between two icons */

/* the pad, 18 x 12: R rim, B body */
#define PAD_H      12
static const char *const pad[PAD_H] = {
    "...RRRRRRRRRRRR...",
    ".RRBBBBBBBBBBBBRR.",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBBBBBBBBBBBR",
    "RBBBBBBRRRRBBBBBBR",
    ".RRRRR......RRRRR.",
};
/* the number and P, 6 x 5, in the chunky style of the game's 1P / 2P / 3P */
static const char *const digits[5][5] = {
    { ".###..", "..##..", "..##..", "..##..", ".####." },
    { "######", "....##", "######", "##....", "######" },
    { "######", "....##", ".#####", "....##", "######" },
    { "##..##", "##..##", "######", "....##", "....##" },
    { "#####.", "##..##", "#####.", "##....", "##...." },   /* P */
};
/* per player: text outline, body, rim, text (RGB) */
static const uint32_t colours[4][4] = {
    { 0x520000, 0x842110, 0xAD7C29, 0xF7F7E7 },   /* 1P red, as the game's 1P box */
    { 0x002163, 0x104284, 0x1F7384, 0xE7F7F7 },   /* 2P blue */
    { 0x082108, 0x214219, 0x5B7319, 0xF7F7E7 },   /* 3P green */
    { 0x4A3100, 0x9C7B10, 0xD6B529, 0xF7F7E7 },   /* 4P yellow (ours) */
};
#define LIGHT 0xF7F7E7                      /* the outline round the pad, like the boxes' frame */

static uint16_t c5551(uint32_t rgb) { return (uint16_t)(0x8000 | ((rgb >> 19) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 3) & 31) << 10)); }

static uint32_t tex, desc, list, prim;
static int build(void) {
    if (tex) return 1;
    uint32_t m = rt_alloc(TEX_W * TEX_H * 2 + 0x100, 1, 256, "pad icons");
    if (!m) return 0;
    for (int k = 0; k < 4; k++) {
        uint32_t px[ICON_H][ICON_W]; memset(px, 0, sizeof px);   /* RGB | 1 << 24 when set */
        /* the pad at 1, 1 (inside its outline) */
        for (int y = 0; y < PAD_H; y++) for (int x = 0; x < 18; x++) {
            char ch = pad[y][x];
            if (ch == 'R') px[y + 1][x + 1] = colours[k][2] | 1u << 24;
            else if (ch == 'B') px[y + 1][x + 1] = colours[k][1] | 1u << 24;
        }
        /* the text "nP", 13 x 5 in a 1 pixel outline, over the body */
        uint8_t ink[7][15]; memset(ink, 0, sizeof ink);
        for (int y = 0; y < 5; y++) for (int x = 0; x < 6; x++) {
            if (digits[k][y][x] == '#') ink[y + 1][x + 1] = 1;
            if (digits[4][y][x] == '#') ink[y + 1][x + 8] = 1;
        }
        for (int y = 0; y < 7; y++) for (int x = 0; x < 15; x++) {
            int on = ink[y][x], edge = 0;
            for (int dy = -1; dy <= 1 && !on && !edge; dy++) for (int dx = -1; dx <= 1; dx++) {
                int yy = y + dy, xx = x + dx;
                if (yy >= 0 && yy < 7 && xx >= 0 && xx < 15 && ink[yy][xx]) { edge = 1; break; }
            }
            if (on) px[y + 3][x + 2] = colours[k][3] | 1u << 24;
            else if (edge) px[y + 3][x + 2] = colours[k][0] | 1u << 24;
        }
        /* the light outline round the pad, then the black shadow down and right */
        uint32_t out[ICON_H][ICON_W]; memcpy(out, px, sizeof out);
        for (int y = 0; y < ICON_H; y++) for (int x = 0; x < ICON_W; x++) {
            if (px[y][x]) continue;
            int near = 0;
            for (int dy = -1; dy <= 1 && !near; dy++) for (int dx = -1; dx <= 1; dx++) {
                int yy = y + dy, xx = x + dx;
                if ((dx || dy) && (dx == 0 || dy == 0) && yy >= 0 && yy < ICON_H && xx >= 0 && xx < ICON_W && px[yy][xx]) { near = 1; break; }
            }
            if (near) out[y][x] = LIGHT | 1u << 24;
        }
        for (int y = ICON_H - 1; y >= 1; y--) for (int x = ICON_W - 1; x >= 1; x--)
            if (!out[y][x] && out[y - 1][x - 1] && out[y - 1][x - 1] != (1u << 24)) out[y][x] = 1u << 24;   /* black */
        for (int y = 0; y < ICON_H; y++) for (int x = 0; x < ICON_W; x++)
            WH(m + (uint32_t)((y * TEX_W + CELL * k + x) * 2), out[y][x] ? c5551(out[y][x] & 0xFFFFFF) : 0);
    }
    tex = m;
    desc = m + TEX_W * TEX_H * 2;
    list = desc + 0x48; prim = list + 0x18;
    memcpy(HOST(desc), HOST(RW(PAGES + 0x14)), 0x44);   /* like page 0's (16-bit 5551) */
    WW(desc, OUR_PAGE); WW(desc + 4, 0); WW(desc + 0x18, 0x01000000); WW(desc + 0x24, TEX_W); WW(desc + 0x28, TEX_H);
    uint32_t d0 = RW(TEXT_TEXTURES);
    WW(list + 8, RW(d0)); WW(list + 12, RW(d0 + 4)); WW(list + 16, OUR_PAGE);
    WW(list, list + 8); WW(list + 4, 1);
    return 1;
}

static int plugged(int k) {
    if (rt_pads_forced >= 0) return (rt_pads_forced >> k) & 1;
    if (k >= 1 && k <= 2 && rt_human[k] >= 0) return rt_human[k];
    return host_pad_connected(k);
}

static uint64_t menu_frame = ~0ull;
void f_0888C5B0(Cpu *c) { menu_frame = rt_vblank_count(); orig_f_0888C5B0(c); }

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float ffrom(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

void padicons_draw(Cpu *c) {
    uint64_t now = rt_vblank_count();
    if (menu_frame == ~0ull || now - menu_frame > 2) return;
    int any = 0;
    for (int k = 0; k < 4; k++) any |= plugged(k);
    if (!any || !build()) return;
    uint32_t keep_r[32], keep_f12 = c->f[12]; memcpy(keep_r, c->r, sizeof keep_r);
    uint32_t e = PAGES + 0x18u * OUR_PAGE, keep_page[6];
    for (int i = 0; i < 6; i++) keep_page[i] = RW(e + 4u * (uint32_t)i);
    WW(e, tex); WW(e + 4, tex); WW(e + 8, 0); WW(e + 0xC, 0); WW(e + 0x10, 0x80000000u); WW(e + 0x14, desc);
    c->r[4] = list; c->r[31] = 0x0880B0E8; f_089E0CF4(c);
    float sx = ffrom(RW(0x08E759E8)), sy = ffrom(RW(0x08E759EC)), ox = ffrom(RW(0x08E75A04)), oy = ffrom(RW(0x08E75A0C));
    for (int k = 0; k < 4; k++) {
        if (!plugged(k)) continue;
        /* 1P and 2P from the bar's left end, 3P and 4P to its right end */
        int x0 = k < 2 ? (int)(EDGE * sx + ox + 0.5f) + STEP * k
                       : (int)((384 - EDGE) * sx + ox + 0.5f) - ICON_W - STEP * (3 - k);
        int y0 = (int)(BAR_Y * sy + oy - 8.0f + 0.5f);
        int x1 = x0 + ICON_W, y1 = y0 + ICON_H;
        int u0 = CELL * k, v0 = 0, u1 = u0 + ICON_W, v1 = ICON_H;
        uint32_t xy = prim + 0x10, col = xy + 0x10, uv = col + 0x10;
        WH(xy + 0, x0); WH(xy + 2, y0); WH(xy + 4, x1); WH(xy + 6, y0); WH(xy + 8, x1); WH(xy + 0xA, y1); WH(xy + 0xC, x0); WH(xy + 0xE, y1);
        for (int i = 0; i < 4; i++) WW(col + 4u * (uint32_t)i, 0xFFFFFFFFu);
        WH(uv + 0, u0); WH(uv + 2, v0); WH(uv + 4, u1); WH(uv + 6, v0); WH(uv + 8, u1); WH(uv + 0xA, v1); WH(uv + 0xC, u0); WH(uv + 0xE, v1);
        WW(prim, xy); WW(prim + 4, col); WW(prim + 8, uv); WW(prim + 0xC, 4);
        c->r[4] = prim; c->r[5] = 4; c->r[6] = 0x80000060u; c->f[12] = fbits(LAYER);
        c->r[31] = 0x0880B0E8; f_089E1618(c);
    }
    c->r[4] = TEXT_TEXTURES; c->r[31] = 0x0880B0E8; f_089E0CF4(c);
    for (int i = 0; i < 6; i++) WW(e + 4u * (uint32_t)i, keep_page[i]);
    memcpy(c->r, keep_r, sizeof keep_r); c->f[12] = keep_f12;
}
