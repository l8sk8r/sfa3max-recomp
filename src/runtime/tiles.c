/* The OPTION MODE tiles, lettered in one font.
 *
 * The font is the MODE MENU tiles' (pages 0x30, 0x31, 0x33 while the menus are up): letters with a flat
 * fill (green, or yellow on the second set of tiles), an edge colour on their rounded corners and a
 * purple outline, 13 pixels high, 3-pixel strokes. They are lifted at run time from those tiles (each
 * tile's text is known; a letter is a connected run of fill, with the edge pixels that touch it), and
 * for each character the shape seen most often is kept. The tiles have A-G, I, K-P, R-W, Y, 0, 1 and
 * the full stop; the rest (H, J, Q, X, Z, 2-9, / and -) are drawn here in the same style.
 *
 * A tile is a game tile's background with its letters wiped (the background is a diagonal gradient:
 * every pixel with red in it, the letters and their blends, is filled from the nearest clean pixels
 * along the gradient's diagonal) and the text drawn on it, centred, in yellow with the purple outline
 * (the colours of the MODE MENU's yellow tiles). No game picture is kept in the source: only our own
 * letters and the rules for lifting the rest.
 */
#include "rt.h"
#include <string.h>

#define PAGES      0x08B0C334u
#define PAGE(n)    (PAGES + 0x18u * (uint32_t)(n))
#define TW 176                                      /* a MODE MENU tile: 176 x 31 at 32-pixel steps */
#define TH 31
#define CAP 13                                      /* letter height */
#define MAXW 16
static int r5(uint16_t p) { return p & 31; }
static int g5(uint16_t p) { return (p >> 5) & 31; }
static int b5(uint16_t p) { return (p >> 10) & 31; }
static uint16_t rgb(int r, int g, int b) { return (uint16_t)(0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10); }

/* ---------------------------------------------------------------- the font */
typedef struct { int w, off; char rows[CAP + 2][MAXW + 1]; } Glyph;   /* '#' fill, 'e' edge; off: rows below the cap line */
static Glyph font[128];
static int have[128];

static const struct { char c; const char *rows[CAP]; } hand[] = {
    { 'H', { "###......###", "###......###", "###......###", "###......###", "###......###", "############", "############", "###......###", "###......###", "###......###", "###......###", "###......###", "###......###" } },
    { 'J', { "........###", "........###", "........###", "........###", "........###", "........###", "........###", "........###", "###.....###", "###.....###", "####e.e####", "###########", ".e#######e." } },
    { 'Q', { "..e.####.e...", "..########...", "e###....###e.", ".##......##..", "###......###.", "###......###.", "###......###.", "###......###.", "###...##.###.", ".##...#####..", "e###...####e.", "..##########.", "..e.####.e###" } },
    { 'X', { "###.......###", "####.....####", ".####...####.", "..####.####..", "...#######...", "....#####....", ".....###.....", "....#####....", "...#######...", "..####.####..", ".####...####.", "####.....####", "###.......###" } },
    { 'Z', { "############", "############", "........####", ".......####.", "......####..", ".....####...", "....####....", "...####.....", "..####......", ".####.......", "####........", "############", "############" } },
    { '2', { ".e########e.", "############", "###......###", ".........###", "........####", ".....######.", "...######...", ".#####e.....", "####e.......", "###.........", "###.........", "############", "############" } },
    { '3', { ".e########e.", "############", "###......###", ".........###", ".........###", "....#######.", "....#######.", ".........###", ".........###", ".........###", "###......###", "############", ".e########e." } },
    { '4', { ".......####.", "......#####.", ".....######.", "....###.###.", "...###..###.", "..###...###.", ".###....###.", "###.....###.", "############", "############", "........###.", "........###.", "........###." } },
    { '5', { "############", "############", "###.........", "###.........", "###.........", "##########e.", "###########e", ".........###", ".........###", ".........###", "###......###", "############", ".e########e." } },
    { '6', { ".e########e.", "############", "###......###", "###.........", "###.........", "##########e.", "###########e", "###......###", "###......###", "###......###", "###......###", "############", ".e########e." } },
    { '7', { "############", "############", "###......###", "........####", ".......####.", "......####..", ".....####...", ".....###....", ".....###....", ".....###....", ".....###....", ".....###....", ".....###...." } },
    { '8', { ".e########e.", "############", "###......###", "###......###", "###......###", ".##########.", ".##########.", "###......###", "###......###", "###......###", "###......###", "############", ".e########e." } },
    { '9', { ".e########e.", "############", "###......###", "###......###", "###......###", "e###########", ".e##########", ".........###", ".........###", ".........###", "###......###", "############", ".e########e." } },
    { '/', { ".....###", ".....###", "....###.", "....###.", "...###..", "...###..", "...###..", "..###...", "..###...", ".###....", ".###....", "###.....", "###....." } },
    { '-', { "........", "........", "........", "........", "........", "########", "########", "........", "........", "........", "........", "........", "........" } },
};

/* the MODE MENU tiles and their texts (page index, row) */
static const struct { int page, y0; const char *text; } source[] = {
    { 0x30, 0, "ARCADE MODE" }, { 0x30, 32, "VS MODE" }, { 0x30, 64, "TEAM BATTLE" }, { 0x30, 128, "OPTION MODE" },
    { 0x30, 160, "ENTRY MODE" }, { 0x30, 192, "NETWORK" }, { 0x30, 224, "SURVIVAL MODE" },
    { 0x31, 0, "DRAMATIC BATTLE" }, { 0x31, 32, "FINAL BATTLE" }, { 0x31, 64, "TRAINING MODE" }, { 0x31, 96, "SCORE RANKING" },
    { 0x31, 128, "WORLD TOUR" }, { 0x31, 160, "EDIT MODE" },
    { 0x33, 0, "ARCADE MODE" }, { 0x33, 32, "R.DRAMATIC BATTLE" }, { 0x33, 64, "SURVIVAL MODE" }, { 0x33, 96, "VS 100 KUMITE" },
    { 0x33, 160, "FREE BATTLE" },
};
static int is_fill(uint16_t p) {
    if (!(p >> 15)) return 0;
    int r = r5(p), g = g5(p), b = b5(p);
    return (g >= 22 && r <= 16 && b <= 11) || (r >= 25 && g >= 25 && b <= 5);   /* green or yellow */
}
static int is_edge(uint16_t p) {
    return (p >> 15) && (p == rgb(64, 104, 136) || p == rgb(128, 160, 152) || p == rgb(96, 88, 120));
}
/* candidates seen per character: shape and count */
#define NCAND 24
static struct { Glyph g; int n; } cand[128][NCAND];
static int ncand[128];
static void seen_glyph(char ch, const Glyph *g) {
    for (int i = 0; i < ncand[(int)ch]; i++)
        if (!memcmp(&cand[(int)ch][i].g, g, sizeof *g)) { cand[(int)ch][i].n++; return; }
    if (ncand[(int)ch] < NCAND) { cand[(int)ch][ncand[(int)ch]].g = *g; cand[(int)ch][ncand[(int)ch]].n = 1; ncand[(int)ch]++; }
}
static int lift_tile(uint32_t pc, int y0, const char *text) {
    static uint8_t cls[TH][TW], comp[TH][TW];
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
        uint16_t p = (uint16_t)RH(pc + (uint32_t)(((y0 + y) * 256 + x) * 2));
        cls[y][x] = is_fill(p) ? 1 : is_edge(p) ? 3 : 0;
    }
    /* fill components, left to right */
    memset(comp, 0, sizeof comp);
    struct { int x0, x1, y0, y1, id; } let[24]; int nl = 0, id = 0;
    static int stk[TH * TW];
    for (int x = 0; x < TW; x++) for (int y = 0; y < TH; y++) {
        if (cls[y][x] != 1 || comp[y][x]) continue;
        id++; int sp = 0, n = 0, bx0 = x, bx1 = x, by0 = y, by1 = y;
        stk[sp++] = y * TW + x; comp[y][x] = (uint8_t)id;
        while (sp) {
            int v = stk[--sp], cy = v / TW, cx = v % TW; n++;
            if (cx < bx0) bx0 = cx;
            if (cx > bx1) bx1 = cx;
            if (cy < by0) by0 = cy;
            if (cy > by1) by1 = cy;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int ny = cy + dy, nx = cx + dx;
                if (ny >= 0 && ny < TH && nx >= 0 && nx < TW && cls[ny][nx] == 1 && !comp[ny][nx]) { comp[ny][nx] = (uint8_t)id; stk[sp++] = ny * TW + nx; }
            }
        }
        if (n < 3) continue;
        if (nl && bx0 <= let[nl - 1].x1 - 1) {                 /* (one letter in two pieces) */
            if (bx1 > let[nl - 1].x1) let[nl - 1].x1 = bx1;
            if (by0 < let[nl - 1].y0) let[nl - 1].y0 = by0;
            if (by1 > let[nl - 1].y1) let[nl - 1].y1 = by1;
            continue;
        }
        if (nl < 24) { let[nl].x0 = bx0; let[nl].x1 = bx1; let[nl].y0 = by0; let[nl].y1 = by1; let[nl].id = id; nl++; }
    }
    char chars[24]; int nc = 0;
    for (const char *t = text; *t; t++) if (*t != ' ' && nc < 24) chars[nc++] = *t;
    if (nl != nc) return 0;
    int cap = TH;                                              /* the cap line: the highest letter top */
    for (int i = 0; i < nl; i++) if (let[i].y0 < cap) cap = let[i].y0;
    for (int i = 0; i < nl; i++) {
        Glyph g; memset(&g, 0, sizeof g);
        int w = let[i].x1 - let[i].x0 + 1, h = let[i].y1 - let[i].y0 + 1;
        if (w > MAXW || h > CAP) continue;
        g.w = w; g.off = let[i].y0 - cap;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int ty = let[i].y0 + y, tx = let[i].x0 + x;
            char c = '.';
            if (cls[ty][tx] == 1 && comp[ty][tx] && tx >= let[i].x0 && tx <= let[i].x1) c = '#';
            else if (cls[ty][tx] == 3) {
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    int ny = ty + dy, nx = tx + dx;
                    if (ny >= 0 && ny < TH && nx >= 0 && nx < TW && cls[ny][nx] == 1) c = 'e';
                }
            }
            g.rows[y][x] = c;
        }
        for (int y = h; y < CAP + 2; y++) for (int x = 0; x < w; x++) g.rows[y][x] = '.';
        seen_glyph(chars[i], &g);
    }
    return 1;
}
static int font_ready;
static int build_font(void) {
    memset(ncand, 0, sizeof ncand); memset(have, 0, sizeof have);
    int tiles = 0;
    for (size_t i = 0; i < sizeof source / sizeof *source; i++) {
        uint32_t pc = RW(PAGE(source[i].page) + 4);
        if (!pc) continue;
        tiles += lift_tile(pc, source[i].y0, source[i].text);
    }
    for (int ch = 0; ch < 128; ch++) {
        int best = -1;
        for (int i = 0; i < ncand[ch]; i++) if (best < 0 || cand[ch][i].n > cand[ch][best].n) best = i;
        if (best >= 0) { font[ch] = cand[ch][best].g; have[ch] = 1; }
    }
    for (size_t i = 0; i < sizeof hand / sizeof *hand; i++) {
        Glyph g; memset(&g, 0, sizeof g);
        g.w = (int)strlen(hand[i].rows[0]); g.off = 0;
        for (int y = 0; y < CAP; y++) memcpy(g.rows[y], hand[i].rows[y], (size_t)g.w);
        font[(int)hand[i].c] = g; have[(int)hand[i].c] = 1;
    }
    static const char need[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./-";
    int missing = 0;
    for (const char *p = need; *p; p++) if (!have[(int)*p]) missing++;
    rt_log("tiles: letters from %d MODE MENU tiles, %d of %d characters missing", tiles, missing, (int)sizeof need - 1);
    font_ready = missing == 0;
    return font_ready;
}

/* ---------------------------------------------------------------- a tile */
#define OW 192
#define OH 32
#define YELLOW  rgb(224, 240, 8)
#define EDGE    rgb(128, 160, 152)
#define OUTLINE rgb(72, 16, 152)
/* a game tile's background, its letters wiped along the gradient's diagonal; MIRROR flips the inside */
static void background(uint16_t out[OH][OW], uint32_t pc, int v0, int mirror) {
    static uint16_t t[OH][OW]; static uint8_t m[OH][OW], m2[OH][OW];
    for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) t[y][x] = (uint16_t)RH(pc + (uint32_t)(((v0 + y) * 256 + x) * 2));
    memset(m, 0, sizeof m);
    for (int y = 2; y < OH - 2; y++) for (int x = 3; x < OW - 3; x++) m[y][x] = (t[y][x] >> 15) && r5(t[y][x]) >= 5;   /* red: letters */
    for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) {
        int any = 0;
        for (int dy = -1; dy <= 1 && !any; dy++) for (int dx = -1; dx <= 1; dx++) {
            int yy = y + dy, xx = x + dx;
            if (yy >= 0 && yy < OH && xx >= 0 && xx < OW && m[yy][xx]) { any = 1; break; }
        }
        m2[y][x] = (uint8_t)any;
    }
    /* which diagonal the gradient runs along: the one with the smallest change between clean neighbours */
    int dir = 0; long bests = -1;
    for (int d = -1; d <= 1; d++) {
        long s = 0, n = 0;
        for (int y = 3; y < OH - 4; y++) for (int x = 4; x < OW - 5; x++) {
            if (m2[y][x] || m2[y + 1][x + d]) continue;
            uint16_t a = t[y][x], b = t[y + 1][x + d];
            s += abs(r5(a) - r5(b)) + abs(g5(a) - g5(b)) + abs(b5(a) - b5(b)); n++;
        }
        long sc = n ? s * 1000 / n : 1L << 40;
        if (bests < 0 || sc < bests) { bests = sc; dir = d; }
    }
    for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) {
        out[y][x] = t[y][x];
        if (!m2[y][x]) continue;
        int rs = 0, gs = 0, bs = 0, ws = 0;
        for (int sg = 1; sg >= -1; sg -= 2) for (int k = 1; k < OW; k++) {
            int yy = y + sg * k, xx = x + sg * k * dir;
            if (yy < 0 || yy >= OH || xx < 0 || xx >= OW) break;
            if (!m2[yy][xx] && yy >= 2 && yy < OH - 2 && xx >= 3 && xx < OW - 3) {
                int w = 1000 / k; uint16_t p = t[yy][xx];
                rs += r5(p) * w; gs += g5(p) * w; bs += b5(p) * w; ws += w; break;
            }
        }
        if (ws) out[y][x] = (uint16_t)(0x8000 | ((rs + ws / 2) / ws) | ((gs + ws / 2) / ws) << 5 | ((bs + ws / 2) / ws) << 10);
    }
    if (mirror) for (int y = 2; y < OH - 2; y++) for (int x = 3; x < OW / 2; x++) {
        uint16_t a = out[y][x]; out[y][x] = out[y][OW - 1 - x]; out[y][OW - 1 - x] = a;
    }
}
/* the text, centred, in yellow with the purple outline */
static void letter(uint16_t out[OH][OW], const char *text) {
    static uint8_t g[OH][OW];                                 /* 1 fill, 2 edge */
    memset(g, 0, sizeof g);
    int width = 0;
    for (const char *p = text; *p; p++) width += *p == ' ' ? 6 : font[(int)(unsigned char)*p].w + 2;
    width -= 2;
    int x = (OW - width) / 2, y0 = (OH - CAP) / 2;
    for (const char *p = text; *p; p++) {
        if (*p == ' ') { x += 6; continue; }
        const Glyph *f = &font[(int)(unsigned char)*p];
        for (int y = 0; y < CAP + 2; y++) for (int i = 0; i < f->w; i++) {
            int yy = y0 + f->off + y, xx = x + i;
            if (yy < 0 || yy >= OH || xx < 0 || xx >= OW) continue;
            if (f->rows[y][i] == '#') g[yy][xx] = 1; else if (f->rows[y][i] == 'e') g[yy][xx] = 2;
        }
        x += f->w + 2;
    }
    for (int y = 0; y < OH; y++) for (int xx = 0; xx < OW; xx++) {
        if (g[y][xx] == 1) { out[y][xx] = YELLOW; continue; }
        if (g[y][xx] == 2) { out[y][xx] = EDGE; continue; }
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int yy = y + dy, xs = xx + dx;
            if (yy >= 0 && yy < OH && xs >= 0 && xs < OW && g[yy][xs]) { out[y][xx] = OUTLINE; dy = 2; break; }
        }
    }
}
/* Draw tile TEXT on the background of the game's tile at (page entry PAGE_ENTRY, row V0), into the
 * 256-wide 16-bit texture TEX at row DST (32 high). Returns 0 if the font could not be made. */
int tiles_draw(uint32_t tex, int dst, int page_entry, int v0, int mirror, const char *text) {
    if (!font_ready && !build_font()) return 0;
    uint32_t pc = RW(PAGE(page_entry) + 4);
    if (!pc) return 0;
    static uint16_t out[OH][OW];
    background(out, pc, v0, mirror);
    letter(out, text);
    for (int y = 0; y < OH; y++) memcpy(HOST(tex + (uint32_t)((32 * dst + y) * 512)), out[y], OW * 2);
    return 1;
}
void tiles_forget(void) { font_ready = 0; }   /* the pages may have been loaded afresh */
