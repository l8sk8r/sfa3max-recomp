/* Select screens with more than one human picking (decision 20).
 *
 * Survival and Free Battle, Dramatic kinds (one select screen, code 0x088A2xxx - 0x088A6xxx, context
 * 0x092E1228): every box's cursor reads the buttons of fighter +0x334 (0x088A5174 finds it; 0x088A507C /
 * 0x088A5100 read its +0x16A, the held word of slot +775), which the game sets to the player who
 * started, so one player picked every box. A box whose fighter a pad plays (hooks.c: it joined with
 * Start, or its JOIN-IN is HUMAN) takes that pad's buttons instead. The boxes' states (+4): 1
 * character (+5 = 0 and +0x338 = 0 until its turn), 2 chosen, 3 ISM, 7 speed. A box a pad can still
 * join says JOIN (blinking), a box a pad has joined says WAIT until its turn.
 *
 * Dramatic Battle (mode 8, code 0x08894xxx - 0x0889Dxxx): the cursors take turns (0x0889494C) and both
 * read the starter's slot (0x08896AA4 new presses, 0x08896B18 held); offline, a box whose fighter a pad
 * plays reads that pad, and a joiner's box says WAIT until its turn (before anyone joins, an empty grey
 * box of ours with JOIN on it). With both players present, each is drawn on its own side (own_side).
 *
 * JOIN and WAIT are drawn in a 5 x 7 pixel font of our own (the game's fonts are 8 to 12 pixels a
 * letter, too wide for the 28 pixel boxes), as one textured quad per word through the game's 2D library
 * (0x089E0CF4 texture list, 0x089E1618 draw; our texture on page 0x3E only while drawing) after its
 * text pass (0x08812520, or 0x0880B098), in the text's own colours (white to grey, colour 9). The 2D
 * list is drawn layer by layer, 0 to 23 (0x089E4408); the words go on layer 22, over the boxes. */
#include "rt.h"
#include <string.h>

int hooks_pad_of(uint32_t f);
int hooks_join_box(int pad);
int hooks_join_target(int pad);
int hooks_join_kind(void);
int hooks_join_first_state(void);
int hooks_join_boxes(int out[4], int *pad3_box);
enum { JK_NONE, JK_SURVIVAL, JK_ARCADE, JK_TEAM, JK_VARIABLE, JK_DRAMATIC, JK_RDRAMATIC };   /* (hooks.c) */
int dramatic_select(void);
int host_pad_connected(int i);
extern int rt_human[3];
void f_089E0CF4(Cpu *c);                    /* 2D library: a0 = texture list {descriptors, count} */
void f_089E1618(Cpu *c);                    /* 2D library: draw a0 = {xy, colours, uv, n}, a1 = n, a2 = flags, f12 = depth */

#define GAME       0x08E75910u
#define SLOTS      0x08E75BA0u
#define PAGES      0x08B0C334u
#define FIGHTER(k) (0x092E3EA0u + 0x498u * (uint32_t)(k))
#define TEXT_TEXTURES 0x08A05080u           /* the text pass's texture list */
#define TEXT_COLOURS  0x08A03D04u           /* per text colour: top and bottom vertex colours */
#define WORD_COLOUR   9
#define WORD_LAYER 22                       /* the 2D list draws layers 0 - 23, in order: over the boxes (16) */
#define BOX_LAYER  21
#define OUR_PAGE   0x3E                     /* a page index nothing uses on these screens; set only while drawing */

/* ---------------------------------------------------------------- the words */
enum { W_JOIN, W_WAIT, W_COUNT, W_BOX = W_COUNT, W_TWO };   /* (W_BOX: an empty box, grey like Survival's; W_TWO: the 2 of L2 / R2) */
static const char *const glyphs[] = {       /* 5 x 7, I 3 wide */
    "J", "..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##..",
    "O", ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.",
    "I", "###", ".#.", ".#.", ".#.", ".#.", ".#.", "###",
    "N", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#",
    "W", "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#",
    "A", ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#",
    "T", "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..",
};
#define TEX_W 64
#define TEX_H 64
#define ROW_H 16                            /* each word on its own row of the texture */
static uint32_t tex, desc, list, prim;      /* our texture, page description, texture list, primitive */
static int word_w[W_TWO + 1], word_h[W_TWO + 1] = { 9, 9, 22, 14 };   /* words with a 1 pixel outline; the box; the 2 */
#define BOX_V 32                            /* the box's row of the texture */
#define TWO_U 32                            /* the 2 of L2 / R2, right of the box */
#define TWO_V 32
#define BOX_W 22
static const char *const words[W_COUNT] = { "JOIN", "WAIT" };

static const char *const *glyph(char ch, int *w) {
    for (size_t i = 0; i < sizeof glyphs / sizeof glyphs[0]; i += 8)
        if (glyphs[i][0] == ch) { *w = (int)strlen(glyphs[i + 1]); return &glyphs[i + 1]; }
    *w = 0; return 0;
}
static int build(void) {
    if (tex) return 1;
    uint32_t m = rt_alloc(TEX_W * TEX_H * 2 + 0x100, 1, 256, "join wait");
    if (!m) return 0;
    uint8_t ink[TEX_H][TEX_W]; memset(ink, 0, sizeof ink);
    for (int w = 0; w < W_COUNT; w++) {
        int x = 1;
        for (const char *p = words[w]; *p; p++) {
            int gw; const char *const *g = glyph(*p, &gw);
            for (int y = 0; y < 7 && g; y++) for (int i = 0; i < gw; i++) if (g[y][i] == '#') ink[ROW_H * w + 1 + y][x + i] = 1;
            x += gw + 1;
        }
        word_w[w] = x;                     /* letters, gaps and the outline */
    }
    word_w[W_BOX] = BOX_W;
    for (int y = 0; y < TEX_H; y++) for (int x = 0; x < TEX_W; x++) {
        uint16_t px = 0;
        if (y >= BOX_V) {                  /* the box: Survival's empty box colour (99, 99, 132), round corners */
            int by = y - BOX_V, corner = (x == 0 || x == BOX_W - 1) && (by == 0 || by == BOX_W - 1);
            if (x < BOX_W && by < BOX_W && !corner) px = 0x8000 | (16 << 10) | (12 << 5) | 12;
            WH(m + (uint32_t)((y * TEX_W + x) * 2), px);
            continue;
        }
        if (ink[y][x]) px = 0xFFFF;        /* white, coloured by the vertices */
        else for (int dy = -1; dy <= 1 && !px; dy++) for (int dx = -1; dx <= 1; dx++) {
            int yy = y + dy, xx = x + dx;
            if (yy >= 0 && yy < TEX_H && xx >= 0 && xx < TEX_W && ink[yy][xx] && (yy / ROW_H) == (y / ROW_H)) { px = 0x8000 | (2 << 10) | (2 << 5) | 3; break; }   /* dark outline */
        }
        WH(m + (uint32_t)((y * TEX_W + x) * 2), px);
    }
    tex = m;
    desc = m + TEX_W * TEX_H * 2;          /* page description (0x44), list (8), descriptor (12), primitive */
    list = desc + 0x48; prim = list + 0x18;
    memcpy(HOST(desc), HOST(RW(PAGES + 0x14)), 0x44);   /* like page 0's (16-bit 5551) */
    WW(desc, OUR_PAGE); WW(desc + 4, 0); WW(desc + 0x18, 0x01000000); WW(desc + 0x24, TEX_W); WW(desc + 0x28, TEX_H);
    uint32_t d0 = RW(TEXT_TEXTURES);       /* the text pass's first descriptor, with our page */
    WW(list + 8, RW(d0)); WW(list + 12, RW(d0 + 4)); WW(list + 16, OUR_PAGE);
    WW(list, list + 8); WW(list + 4, 1);
    return 1;
}

/* words queued this frame by the select screens, drawn at the end of the text pass */
static struct { int w, x, y; } queue[12];
static int nqueue;
static void say2(int w, int cx2, int cy2) { /* a word centred on a point of the 480 x 272 screen, in half pixels */
    /* (+1 pixel across, +half down: the drawn letters land that much up and left of the quad's centre) */
    if (nqueue < 12) { queue[nqueue].w = w; queue[nqueue].x = cx2 + (w == W_BOX ? 0 : 2); queue[nqueue].y = cy2 + (w == W_BOX ? 0 : 1); nqueue++; }
}
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float ffrom(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void draw_words(Cpu *c) {
    if (!nqueue || !build()) { nqueue = 0; return; }
    uint32_t keep_r[32], keep_f12 = c->f[12]; memcpy(keep_r, c->r, sizeof keep_r);
    uint32_t e = PAGES + 0x18u * OUR_PAGE, keep_page[6];
    for (int i = 0; i < 6; i++) keep_page[i] = RW(e + 4u * (uint32_t)i);
    WW(e, tex); WW(e + 4, tex); WW(e + 8, 0); WW(e + 0xC, 0); WW(e + 0x10, 0x80000000u); WW(e + 0x14, desc);
    c->r[4] = list; c->r[31] = 0x0880B0E8; f_089E0CF4(c);
    float sx = ffrom(RW(0x08E759E8)), sy = ffrom(RW(0x08E759EC)), ox = ffrom(RW(0x08E75A04)), oy = ffrom(RW(0x08E75A0C));
    float depth = WORD_LAYER;
    uint32_t top = RW(TEXT_COLOURS + 8u * WORD_COLOUR), bottom = RW(TEXT_COLOURS + 8u * WORD_COLOUR + 4);
    for (int q = 0; q < nqueue; q++) {
        int w = queue[q].w, ww = word_w[w], wh = word_h[w], box = w == W_BOX;
        /* a point of the screen (x2 for half pixels) -> the text pass's coordinates (384 x 224): x * 0.8 * sx + ox;
           the quad is ww x wh of those units, centred there */
        int x0 = (int)((float)queue[q].x * 0.4f * sx + ox - (float)ww / 2.0f + 0.5f);
        int y0 = (int)((float)queue[q].y * 0.5f * 224.0f / 272.0f * sy + oy - (float)wh / 2.0f + 0.5f);
        int u0 = 0, v0 = box ? BOX_V : ROW_H * w;
        if (w == W_TWO) {                  /* top left given in the text's units */
            box = 1; x0 = (int)((float)queue[q].x * sx + ox + 0.5f); y0 = (int)((float)queue[q].y * sy + oy + 0.5f); u0 = TWO_U; v0 = TWO_V;
        }
        int x1 = x0 + ww, y1 = y0 + wh, u1 = u0 + ww, v1 = v0 + wh;
        uint32_t xy = prim + 0x10, col = xy + 0x10, uv = col + 0x10;
        WH(xy + 0, x0); WH(xy + 2, y0); WH(xy + 4, x1); WH(xy + 6, y0); WH(xy + 8, x1); WH(xy + 0xA, y1); WH(xy + 0xC, x0); WH(xy + 0xE, y1);
        uint32_t t = box ? 0xFFFFFFFFu : top, b = box ? 0xFFFFFFFFu : bottom;
        WW(col + 0, t); WW(col + 4, t); WW(col + 8, b); WW(col + 0xC, b);
        WH(uv + 0, u0); WH(uv + 2, v0); WH(uv + 4, u1); WH(uv + 6, v0); WH(uv + 8, u1); WH(uv + 0xA, v1); WH(uv + 0xC, u0); WH(uv + 0xE, v1);
        WW(prim, xy); WW(prim + 4, col); WW(prim + 8, uv); WW(prim + 0xC, 4);
        c->r[4] = prim; c->r[5] = 4; c->r[6] = 0x80000060u; c->f[12] = fbits(box && w != W_TWO ? BOX_LAYER : depth);
        c->r[31] = 0x0880B0E8; f_089E1618(c);
    }
    c->r[4] = TEXT_TEXTURES; c->r[31] = 0x0880B0E8; f_089E0CF4(c);
    for (int i = 0; i < 6; i++) WW(e + 4u * (uint32_t)i, keep_page[i]);
    memcpy(c->r, keep_r, sizeof keep_r); c->f[12] = keep_f12;
    nqueue = 0;
}
/* The 2 of L2 / R2 for the pause menu's BUTTON CONFIG (exsetting.c): SELECT's S (font 6's picture,
 * 0x09739DC0, at 0, 162: white, 3 wide, in a dark outline, 14 high with it) turned round, at TWO_U,
 * TWO_V of our texture; drawn with its top left at text coordinates X, Y. */
#define FONT6_TEX 0x09739DC0u
static int two_have;
void selectscreen_two(int x, int y) {
    if (!build()) return;
    if (!two_have) {
        uint32_t t = FONT6_TEX;
        if ((RH(t + (161 * 256) * 2) >> 15) || !(RH(t + (162 * 256) * 2) >> 15) || RH(t + (164 * 256 + 1) * 2) != 0xFFFF) return;
        for (int yy = 0; yy < 14; yy++) for (int xx = 0; xx < 5; xx++) {
            uint16_t p = (uint16_t)RH(t + (uint32_t)(((162 + yy) * 256 + (4 - xx)) * 2));
            if (xx == 0 && !(RH(t + (uint32_t)(((162 + yy) * 256 + 3) * 2)) >> 15)) p = 0;   /* E's outline */
            WH(tex + (uint32_t)(((TWO_V + yy) * TEX_W + TWO_U + xx) * 2), p);
        }
        word_w[W_TWO] = 5; two_have = 1;
    }
    if (nqueue < 12) { queue[nqueue].w = W_TWO; queue[nqueue].x = x; queue[nqueue].y = y; nqueue++; }
}
/* the text passes (two of them, both drawing the queue at 0x092DF240 with the texture list 0x08A05080) */
void padicons_draw(Cpu *c);                 /* padicons.c: the MODE MENU's pad icons */
void pause_frame(void);                     /* exsetting.c: the pause BUTTON CONFIG closing */
void f_0880B098(Cpu *c) { orig_f_0880B098(c); draw_words(c); padicons_draw(c); pause_frame(); }
void f_08812520(Cpu *c) { orig_f_08812520(c); draw_words(c); padicons_draw(c); pause_frame(); }

static int plugged(int k) { return k == 0 || (rt_human[k] >= 0 ? rt_human[k] : host_pad_connected(k)); }
static int blink_on(void) { return (rt_vblank_count() / 30) % 2 == 0; }   /* half a second on, half off */

/* ---------------------------------------------------------------- Survival Dramatic, Free Battle */
void f_088A5174(Cpu *c) {
    uint32_t obj = c->r[4];
    orig_f_088A5174(c);
    if (!dramatic_select()) return;
    int pad = hooks_pad_of(obj);
    if (pad < 0) return;
    for (int f = 0; f < 4; f++)                    /* the fighter whose +0x16A holds that pad's slot (+775; R.Dramatic: 0, -, 1, 2) */
        if ((int8_t)RB(FIGHTER(f) + 775) == pad) { c->r[2] = (uint32_t)f; return; }
}
/* The box of fighter i on the screen, in half pixels (centres measured): Survival's and Free Battle
 * Variable / Dramatic's team boxes 1P 49.5 and 2P 99.5 on the left (player 1 started) or 1P 379.5 and
 * 2P 429.5 on the right (player 2 started), the opponent (fighter 2) on the other side at 379.5 or 99.5,
 * all at 72; R.Dramatic's opponents (fighters 2, 3) 379.5 and 429.5, or 49.5 and 99.5 when player 2
 * started; Arcade's and Team's boxes 1P 99.5 and 2P 379.5 at 43, whoever started. Arcade's empty box is
 * grey like Survival's; on Team's (the team-size icons) the word goes on an empty box of ours. */
static int box_xy(int i, int *x, int *y) {         /* returns 1 if the word needs our empty box under it */
    int left = RB(GAME + 0x35) & 1, kind = hooks_join_kind();
    *y = 2 * 72;
    if (kind == JK_ARCADE || kind == JK_TEAM) { *y = 2 * 43; *x = i == 0 ? 199 : 759; return kind == JK_TEAM; }
    if (kind == JK_RDRAMATIC) { *x = left ? (i == 2 ? 759 : 859) : (i == 2 ? 99 : 199); return 0; }
    if (i == 2) { *x = left ? 759 : 199; return 0; }
    int side = RB(FIGHTER(i) + 0x151) & 1;
    if (left) *x = side ? 199 : 99;
    else *x = side ? 859 : 759;
    return 0;
}
static int box_waits(uint32_t o) { return RB(o + 4) == (uint32_t)hooks_join_first_state() && RB(o + 5) == 0 && RW(o + 0x338) == 0; }
/* once a frame on that select screen (it draws SPEED:), a0 = 0x08E75910 */
void f_088A1548(Cpu *c) {
    orig_f_088A1548(c);
    if (!dramatic_select()) return;
    uint32_t gp = RB(GAME + 0x35) & 7;
    int box[4], p3, n = hooks_join_boxes(box, &p3);
    for (int b = 0; b < n; b++) {
        int i = box[b];
        uint32_t o = FIGHTER(i);
        if (RB(o) != 1 || RB(o + 4) != (uint32_t)hooks_join_first_state()) continue;   /* its turn has passed */
        int pad = hooks_pad_of(o), x, y, under = box_xy(i, &x, &y);
        if (pad >= 0 && !(gp & (1u << pad))) {               /* a pad (not a starter) picks this box */
            if (box_waits(o)) { if (under) say2(W_BOX, x, y); say2(W_WAIT, x, y); }
            continue;
        }
        if (pad >= 0) continue;
        int can = 0;                                           /* a plugged-in pad could join it */
        for (int k = 0; k < 3; k++) if (hooks_join_target(k) == i && plugged(k)) can = 1;
        if (!can) continue;
        if (under) say2(W_BOX, x, y);
        if (blink_on()) say2(W_JOIN, x, y);
    }
}

/* ---------------------------------------------------------------- Dramatic Battle */
static int dramatic_battle(void) { uint32_t m = RB(GAME + 0x72); return m == 8 || m == 15; }   /* Dramatic, Variable Battle: one screen */
static int box_waits_dramatic(uint32_t o) {        /* joined (Start), its turn not come yet */
    return RB(o + 0x154) == 3 && RB(o + 4) == 0 && RB(o + 5) == 0 && RW(o + 0x338) == 0;
}
static int pad_for(uint32_t o) {                   /* offline Dramatic Battle: the pad that picks for box o */
    if (!dramatic_battle()) return -1;
    return hooks_pad_of(o);
}
void f_08896AA4(Cpu *c) {                          /* a box's newly pressed buttons */
    int p = pad_for(c->r[4]);
    if (p < 0) { orig_f_08896AA4(c); return; }
    c->r[2] = RW(SLOTS + 0x2Cu * (uint32_t)p + 0xC) & 0xFFFF;
}
void f_08896B18(Cpu *c) {                          /* a box's held buttons */
    int p = pad_for(c->r[4]);
    if (p < 0) { orig_f_08896B18(c); return; }
    c->r[2] = RW(SLOTS + 0x2Cu * (uint32_t)p + 4) & 0xFFFF;
}
/* The select screen's display tasks (box, portrait, sprite, name strip, the ISM panel ...) run through
 * one entry per kind of task (a0 = the task, its player at +0x5C), which calls the task's state handler.
 * In modes 8 and 15 they put a player on the team's side, (0x08E75910 + 0x35) & 1, but parts of them
 * (the ISM panel's column) go by the player's own side: with both players present that mixed the two
 * (player 2's ISM banner drawn left of its column, over the grid). Offline in Dramatic Battle with both
 * present, bit 0 is hidden from the tasks of the player on the right (side 1): every player is drawn on
 * its own side, whoever started. */
static void own_side(Cpu *c, void (*orig)(Cpu *)) {
    uint32_t o = RW(c->r[4] + 0x5C), gp = RB(GAME + 0x35);
    int flip = dramatic_battle() && (gp & 3) == 3 && (o == FIGHTER(0) || o == FIGHTER(1)) && (RB(o + 0x151) & 1)
               && hooks_pad_of(o) >= 0;       /* (offline: hooks.c knows the pads) */
    if (flip) WB(GAME + 0x35, gp & ~1u);
    orig(c);
    if (flip) WB(GAME + 0x35, RB(GAME + 0x35) | 1u);
}
#define OWN_SIDE(A) void f_##A(Cpu *c) { own_side(c, orig_f_##A); }
OWN_SIDE(08899558) OWN_SIDE(08899A80) OWN_SIDE(0889A7C8) OWN_SIDE(0889B048) OWN_SIDE(0889BAB0) OWN_SIDE(0889BF30)
OWN_SIDE(0889E434) OWN_SIDE(088A712C) OWN_SIDE(088A7694) OWN_SIDE(088A78E0) OWN_SIDE(088A7CB4) OWN_SIDE(088A8038)
OWN_SIDE(088A81FC) OWN_SIDE(088A8404)
/* Player 2 started and player 1 joined: once player 1 has chosen, the rest of the screen (player 2's
 * ISM and speed) is player 2's, but its panel's parts are set up as they start, some by the team's side
 * ((0x08E75910 + 0x35) & 1, now 1: left) and some by player 2's own (right): the ISM banner landed left
 * of its column, over the grid. From then until the screen ends, the players present read 2 (player 2
 * only, as without player 1) for everything but the screen's own logic (0x08894214, below), which gets
 * the real 3: its end copies it into + 0x39 / + 0x29, the players who fight. */
static int starter_right, hid_p1;
void f_08894214(Cpu *c) {                          /* the select screen, once a frame */
    if (hid_p1) { WB(GAME + 0x35, RB(GAME + 0x35) | 1u); hid_p1 = 0; }
    uint32_t phase = RH(GAME + 4);
    orig_f_08894214(c);
    if (!dramatic_battle()) return;
    int gp0 = RB(GAME + 0x35) & 3;
    if (gp0 == 1 || gp0 == 2) starter_right = gp0 == 2;
    if (gp0 == 3 && starter_right && RH(GAME + 4) == phase && RB(GAME + 2) == 1 && hooks_pad_of(FIGHTER(1)) >= 0 && RB(FIGHTER(0) + 0x154) == 3
        && (RB(FIGHTER(0) + 4) != 0 || RB(FIGHTER(0) + 5) >= 2)) {   /* player 1 has chosen (state 0.2 on) */
        WB(GAME + 0x35, RB(GAME + 0x35) & ~1u); hid_p1 = 1;
    }
    /* the box (frame 135 - 164 x 34 - 62, centre 149.5, 48) has its label at the top for player 2 and at
       the bottom for player 1: the word sits below or above it */
    int gp = hid_p1 ? 3 : RB(GAME + 0x35) & 3;
    for (int k = 0; k < 2; k++) {
        uint32_t o = FIGHTER(k);
        int side = RB(o + 0x151) & 1;
        if (hooks_pad_of(o) >= 0 && box_waits_dramatic(o)) say2(W_WAIT, 299, side ? 2 * 51 : 2 * 45);
    }
    /* before anyone joins: the other player's box can be joined (the game's own join, Start): an empty box
       with JOIN blinking on it, while that player's pad is plugged in */
    if (gp == 1 || gp == 2) {
        int other = gp == 1 ? 1 : 0;                   /* side and pad of the player who can join */
        uint32_t o = 0;
        for (int k = 0; k < 2; k++) if ((RB(FIGHTER(k) + 0x151) & 1) == other) o = FIGHTER(k);
        if (o && RB(o + 0x154) != 3 && RB(o + 4) == 0 && plugged(other)) {
            say2(W_BOX, 299, 96);
            if (blink_on()) say2(W_JOIN, 299, 96);
        }
    }
}
