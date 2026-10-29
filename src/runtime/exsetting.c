/* OPTION MODE gets a sixth button, EX. SETTING, between SCREEN SETTING and SAVE/LOAD.
 *
 * The option menu (0x088B6164, context 0x08E75912: +0 state, +1 cursor, +5 submenu shown, +6 in a
 * submenu) has five buttons: screen objects (kind 0xC, types 0x0F-0x13) whose picture is a 192x32
 * part of a texture page (+0x3C page, +0x40/+0x42 u, +0x4C/+0x4E v; page n is entry 0x30 + n of the
 * page table at 0x08B0C334). Each frame 0x088BA82C makes the one under the cursor bright and a bit
 * bigger and the others dim; the cursor moves with 0x088B7EC0 (a2 = last item).
 *
 * Here: the cursor runs 0-5 (item 3 is EX. SETTING); a sixth button object is a copy of EXIT's, drawing
 * a picture made at run time (SCREEN SETTING's background mirrored, lettered by tiles.c) on a texture
 * page of our own, and the other buttons get pictures made the same way; all six are laid out in the height the five had, slightly shorter each. Choosing a
 * button below EX. SETTING passes the game its own item number. EX. SETTING opens a screen of ours
 * with SFA3MR's own options (settings.c); Circle or EXIT goes back and saves them.
 */
#include "rt.h"
#include "version.h"
#include <string.h>
#include <stdio.h>

void f_0880A6CC(Cpu *c);                    /* text: a0 string, a1 font, a2 x, a3 y, t0 colour */
void rt_hold_display(int frames);           /* main.c: keep the last frame on screen */
void rt_hold_display_set(int frames);
void f_0887C4CC(Cpu *c);                    /* sound effect: a0 0, a1 id, a2 0xC0 */

#define OPT        0x08E75912u              /* +0 state, +1 cursor, +5 submenu shown, +6 in a submenu */
#define SLOTS      0x08E75BA0u              /* per-slot input, 0x2C each; +0xC new presses (game bits) */
#define OBJS       0x092E2B88u              /* screen objects, 32 x 0x94 */
#define OBJ_N      32
#define OBJ_SZ     0x94u
#define OBJ_COUNT  0x095176F9u              /* free objects (the allocator counts down) */
#define PAGES      0x08B0C334u              /* texture pages, 0x18 each: +0 address, +4 copy, +0x14 description */
#define PAGE_BASE  0x30                     /* an object's page n is entry 0x30 + n */
#define OUR_PAGE   4
#define OUR_PAGE2  5
#define B_CONFIRM  0x8080u
#define B_BACK     0x100u
#define N_ITEMS    6
#define EX_ITEM    3
#define PLAYER_ITEM 1                       /* BUTTON CONFIG becomes PLAYER CONFIG */
#define SL_ITEM    3                        /* the game's number for SAVE/LOAD */

static uint32_t mem;                        /* our block: two page textures (256 x 256 x 2), their descriptions, strings */
#define TEX       (mem)
#define TEX2      (mem + 0x20000u)
#define DESC      (mem + 0x40000u)
#define DESC2     (mem + 0x40080u)
#define LIST      (mem + 0x40100u)          /* our texture list for the buttons' layer: 8 + 6 x 12 bytes */
#define STR(i)    (mem + 0x40180u + 0x20u * (uint32_t)(i))   /* our strings, 32 bytes each */
enum { S_TITLE, S_P1, S_P2, S_P3, S_COOP, S_EXIT, S_ON, S_OFF, S_HUMAN, S_CPU, S_PLAYER, S_P1C, S_P2C, S_P3C, S_P4C, S_TAUNT,
       S_COMMAND, S_TYPE, S_RAPID, S_DEFAULT, S_EXIT2, S_VERSION, S_COUNT };
static const char *const strings[S_COUNT] = { "EX. SETTING", "1P JOIN-IN", "2P JOIN-IN", "3P JOIN-IN", "COUCH CO-OP", "EXIT", "ON", "OFF", "HUMAN", "CPU",
    "PLAYER CONFIG", "P1 CONFIG", "P2 CONFIG", "P3 CONFIG", "P4 CONFIG", "TAUNT", "COMMAND", "TYPE", "RAPID", "DEFAULT", "EXIT", SFA3MR_VERSION };
static int clone = -1, item_v, sub_from, built;
static int starting;                        /* the option menu just started, not laid out yet (f_088B5FB8) */
static long seen = -1;
static int16_t x_even, x_odd;

static uint32_t obj(int k) { return OBJS + OBJ_SZ * (uint32_t)k; }
static int button(int k) {                  /* the game's item number of a menu button, or -1 */
    uint32_t o = obj(k);
    if (!RB(o + 0x5A) || RB(o + 0x5B) != 0xC) return -1;
    int t = RB(o + 0x5C) - 0x0F;
    return t >= 0 && t < 5 ? t : -1;
}
static uint32_t pressed(void) { return RW(SLOTS + 0xC) | RW(SLOTS + 0x2C + 0xC); }
static void sound(Cpu *c, int id) {
    uint32_t keep[8]; memcpy(keep, &c->r[4], sizeof keep);
    c->r[4] = 0; c->r[5] = (uint32_t)id; c->r[6] = 0xC0; f_0887C4CC(c);
    memcpy(&c->r[4], keep, sizeof keep);
}
static void text(Cpu *c, uint32_t s, int font, int x, int y, int colour) {
    uint32_t keep[8], ra = c->r[31]; memcpy(keep, &c->r[4], sizeof keep);
    c->r[4] = s; c->r[5] = (uint32_t)font; c->r[6] = (uint32_t)x; c->r[7] = (uint32_t)y; c->r[8] = (uint32_t)colour;
    f_0880A6CC(c);
    memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra;
}

/* ---------------------------------------------------------------- the button pictures */
/* Every OPTION MODE tile is drawn afresh in one font (tiles.c): the game's own tiles' backgrounds
 * with their letters wiped, lettered in the MODE MENU font in yellow with a purple outline. Two
 * texture pages of ours: page 4 (EX. SETTING, PLAYER CONFIG, P1 - P4 CONFIG, SAVE/LOAD; the 2 of
 * L2 / R2 at the bottom) and page 5 (GAME OPTION, SCREEN SETTING, EXIT). */
int tiles_draw(uint32_t tex, int dst, int page_entry, int v0, int mirror, const char *text);
enum { ROW_EX, ROW_PLAYER, ROW_P1, ROW_SL = ROW_P1 + 4 };   /* rows of page 4 (32 high) */
enum { ROW_GO, ROW_SCR, ROW_EXIT };                         /* rows of page 5 */
static int tiles_ok, two_ready;
static void build_picture(void) {
    static const struct { int page, row, src, v0, mirror; const char *text; } t[] = {
        { 4, ROW_EX, 2, 64, 1, "EX. SETTING" },     { 4, ROW_PLAYER, 2, 32, 0, "PLAYER CONFIG" },
        { 4, ROW_P1, 2, 0, 0, "P1 CONFIG" },        { 4, ROW_P1 + 1, 2, 32, 0, "P2 CONFIG" },
        { 4, ROW_P1 + 2, 2, 64, 0, "P3 CONFIG" },   { 4, ROW_P1 + 3, 3, 128, 0, "P4 CONFIG" },
        { 4, ROW_SL, 3, 128, 0, "SAVE/LOAD" },
        { 5, ROW_GO, 2, 0, 0, "GAME OPTION" },      { 5, ROW_SCR, 2, 64, 0, "SCREEN SETTING" },
        { 5, ROW_EXIT, 2, 96, 0, "EXIT" },
    };
    memset(HOST(TEX), 0, 256 * 256 * 2); memset(HOST(TEX2), 0, 256 * 256 * 2);
    two_ready = 0;                                            /* (the 2 of L2 / R2 is on page 4 too) */
    tiles_ok = 1;
    for (size_t i = 0; i < sizeof t / sizeof *t; i++)
        tiles_ok &= tiles_draw(t[i].page == 4 ? TEX : TEX2, t[i].row, PAGE_BASE + t[i].src, t[i].v0, t[i].mirror, t[i].text);
    if (!tiles_ok) rt_log("exsetting: tiles not made, the game's pictures stay");
    built = 1;
}

/* our texture pages: entries 0x30 + 4 and 0x30 + 5, described like page 3 */
static void one_page_on(int n, uint32_t tex, uint32_t desc) {
    uint32_t e = PAGES + 0x18u * (uint32_t)(PAGE_BASE + n), e3 = PAGES + 0x18u * (PAGE_BASE + 3);
    if (RW(e) == tex || RW(e) != 0) return;                    /* ours already, or in use by the game */
    memcpy(HOST(desc), HOST(RW(e3 + 0x14)), 0x44);
    WW(desc, (uint32_t)(PAGE_BASE + n));
    WW(e, tex); WW(e + 4, tex); WW(e + 8, RW(e3 + 8)); WW(e + 0xC, RW(e3 + 0xC)); WW(e + 0x10, RW(e3 + 0x10)); WW(e + 0x14, desc);
}
/* An object's page is an index into the texture list of its layer (+0x36; the lists are at
 * 0x08E75A18 + 4 x layer: entries of 12 bytes, +8 the page table entry). The buttons' layer 0x12 has
 * five (0x30 - 0x34); while the menu is up it gets a copy of ours with a sixth, page 5 = entry 0x35. */
#define LAYER_LISTS 0x08E75A18u
#define BTN_LAYER   0x12u
static uint32_t game_list;
static void list_on(void) {
    uint32_t slot = LAYER_LISTS + 4 * BTN_LAYER, l = RW(slot);
    if (l == LIST || !l || RW(l + 4) != OUR_PAGE2 || RW(RW(l) + 12 * OUR_PAGE + 8) != PAGE_BASE + OUR_PAGE) return;   /* not the list we know */
    game_list = l;
    memcpy(HOST(LIST + 8), HOST(RW(l)), 12 * OUR_PAGE2);
    WW(LIST + 8 + 12 * OUR_PAGE2, RW(RW(l))); WW(LIST + 8 + 12 * OUR_PAGE2 + 4, RW(RW(l) + 4)); WW(LIST + 8 + 12 * OUR_PAGE2 + 8, PAGE_BASE + OUR_PAGE2);
    WW(LIST, LIST + 8); WW(LIST + 4, OUR_PAGE2 + 1);
    WW(slot, LIST);
}
static void page_on(void) { one_page_on(OUR_PAGE, TEX, DESC); one_page_on(OUR_PAGE2, TEX2, DESC2); list_on(); }
static int page2_ours(void) { return RW(PAGES + 0x18u * (PAGE_BASE + OUR_PAGE2)) == TEX2 && RW(LAYER_LISTS + 4 * BTN_LAYER) == LIST; }
static void page_off(void) {
    uint32_t e = PAGES + 0x18u * (PAGE_BASE + OUR_PAGE), e2 = PAGES + 0x18u * (PAGE_BASE + OUR_PAGE2);
    if (mem && RW(e) == TEX) memset(HOST(e), 0, 0x18);
    if (mem && RW(e2) == TEX2) memset(HOST(e2), 0, 0x18);
    if (mem && RW(LAYER_LISTS + 4 * BTN_LAYER) == LIST) { WW(LAYER_LISTS + 4 * BTN_LAYER, game_list); }
}

/* the sixth button: a copy of EXIT's object, showing our page */
static void clone_on(void) {
    if (clone >= 0 && RB(obj(clone) + 0x5A) && RB(obj(clone) + 0x5B) == 0xC) return;
    clone = -1;
    int exit_k = -1, free_k = -1;
    for (int k = 0; k < OBJ_N; k++) {
        int b = button(k);
        if (b == 0) x_even = (int16_t)RH(obj(k) + 6);
        if (b == 1) x_odd = (int16_t)RH(obj(k) + 6);
        if (b == 4) exit_k = k;
    }
    if (exit_k < 0) return;
    for (int k = 0; k < OBJ_N; k++) if (!RB(obj(k) + 0x5A)) { free_k = k; break; }
    if (free_k < 0) return;
    memcpy(HOST(obj(free_k)), HOST(obj(exit_k)), OBJ_SZ);
    uint32_t o = obj(free_k);
    WW(o + 0x3C, OUR_PAGE); WH(o + 0x40, 0); WH(o + 0x42, 192); WH(o + 0x4C, 0); WH(o + 0x4E, 32);
    WB(OBJ_COUNT, RB(OBJ_COUNT) - 1);
    clone = free_k;
}
static void clone_off(void) {
    if (clone >= 0 && RB(obj(clone) + 0x5A)) { memset(HOST(obj(clone)), 0, OBJ_SZ); WB(OBJ_COUNT, RB(OBJ_COUNT) + 1); }
    clone = -1;
}

/* ---------------------------------------------------------------- our screens */
/* EX. SETTING and P1 - P4 CONFIG are laid out like GAME OPTION (0x088B88F4): names in font 1, a row
 * cursor in the option context's +7, colour 0xB on its row and 0xA elsewhere, and GAME OPTION's green
 * bar (object kind 0xC, type 4; once a frame 0x088BA8F4) under it. PLAYER CONFIG reuses the menu's
 * buttons as five tiles. */
extern int rt_join_in[3], rt_couch_coop, rt_l2[4], rt_r2[4];
extern const uint8_t pcfg_default[20];
uint8_t *pcfg_block(int p);
void settings_save(void);
void f_08823338(Cpu *c);                    /* new screen object: a0 kind, a1 type; v0 = object */
void f_0880A7B0(Cpu *c);                    /* button symbol a0 (font 5) at a1, a2 */
void f_0880A7F4(Cpu *c);                    /* SELECT (font 6) at a1, a2 */
enum { M_MENU, M_EX, M_PLAYERS, M_PEDIT } ;
static int mode, row, bar = -1, bar_y = -1, tile, editing, two[2] = { -1, -1 };
static int new_obj(Cpu *c, int kind, int type) {           /* a screen object of the game's; its index or -1 */
    uint32_t keep[2] = { c->r[4], c->r[5] }, ra = c->r[31];
    c->r[4] = (uint32_t)kind; c->r[5] = (uint32_t)type; f_08823338(c);
    uint32_t o = c->r[2];
    c->r[4] = keep[0]; c->r[5] = keep[1]; c->r[31] = ra;
    return o ? (int)((o - OBJS) / OBJ_SZ) : -1;
}
static void free_obj(int *k) {
    if (*k >= 0 && RB(obj(*k) + 0x5A)) { memset(HOST(obj(*k)), 0, OBJ_SZ); WB(OBJ_COUNT, RB(OBJ_COUNT) + 1); }
    *k = -1;
}
static void bar_on(Cpu *c) { if (bar < 0) bar = new_obj(c, 0xC, 4); }
static void bar_off(void) { free_obj(&bar); }
/* The "2" of L2 and R2, after the L and R symbols: SELECT's S (font 6's picture, 0x09739DC0, at
 * 0,162: white letters 3 wide in a dark outline, 14 high with it) turned round, on our page at
 * TWO_U, TWO_V, shown by an object of ours (kind 0xC, type 4 like the bar, handled in 0x088BA8F4) */
#define FONT6_TEX 0x09739DC0u
#define TWO_U 0
#define TWO_V 224
#define TWO_W 5
#define TWO_H 14
/* L and R are 8 wide (with their outline) and the other symbols are centred on x + 8 at 0x68:
   L2 (8 + 5 - the shared outline = 12 wide) starts at 0x6A, its 2 at 0x6A + 7 */
#define TWO_SYM_X 0x6A
#define TWO_X (TWO_SYM_X + 7)
static void two_build(void) {
    uint32_t t = FONT6_TEX;
    if ((RH(t + (161 * 256) * 2) >> 15) || !(RH(t + (162 * 256) * 2) >> 15) || RH(t + (164 * 256 + 1) * 2) != 0xFFFF) {
        rt_log("exsetting: SELECT not where expected, no 2 for L2 / R2"); return;
    }
    for (int y = 0; y < TWO_H; y++) for (int x = 0; x < TWO_W; x++) {
        uint16_t p = (uint16_t)RH(t + (uint32_t)(((162 + y) * 256 + (TWO_W - 1 - x)) * 2));
        if (x == 0 && !(RH(t + (uint32_t)(((162 + y) * 256 + TWO_W - 2) * 2)) >> 15)) p = 0;   /* E's outline */
        WH(TEX + (uint32_t)(((TWO_V + y) * 256 + TWO_U + x) * 2), p);
    }
    two_ready = 1;
}
static void two_on(Cpu *c) {
    if (!two_ready) two_build();
    if (!two_ready) return;
    for (int i = 0; i < 2; i++) if (two[i] < 0) two[i] = new_obj(c, 0xC, 4);
}
static void two_off(void) { free_obj(&two[0]); free_obj(&two[1]); }
/* An object's +6 x is in the text's units; its +0xA y (top, downwards) and +0x1A height are in halves */
static void two_frame(uint32_t o, int text_y) {           /* place a 2 for a row drawn at text_y */
    WW(o + 0x3C, OUR_PAGE); WH(o + 0x40, TWO_U); WH(o + 0x42, TWO_U + TWO_W); WH(o + 0x4C, TWO_V); WH(o + 0x4E, TWO_V + TWO_H);
    WH(o + 6, TWO_X); WH(o + 0xA, (uint32_t)(2 * (text_y + 2)));
    WH(o + 0xC, 0); WH(o + 0x18, 0); WH(o + 0xE, TWO_W); WH(o + 0x1A, 2 * TWO_H);
    static const uint8_t col[] = { 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x2C, 0x2D, 0x30, 0x31, 0x32, 0x33 };
    for (size_t i = 0; i < sizeof col; i++) WB(o + col[i], 0xFF);
    WB(o + 0x36, 0x12); WB(o + 0x37, 7); WB(o + 1, 1);          /* drawn like the buttons: textured */
    WB(o + 0x58, 1); WB(o + 0x59, 1); WB(o + 0x8A, 1);
    WB(o + 0x88, 0xFF); WB(o + 0x38, 0); WB(o + 0x5D, 1);
}
static void title(Cpu *c, int s) { text(c, STR(s), 2, 0xC0 - 8 * (int)strlen((char *)HOST(STR(s))), 5, 0xB); }
static void symbol(Cpu *c, int sel, int id, int x, int y) {
    uint32_t keep[8], ra = c->r[31]; memcpy(keep, &c->r[4], sizeof keep);
    c->r[4] = (uint32_t)id; c->r[5] = (uint32_t)x; c->r[6] = (uint32_t)y;
    if (sel) f_0880A7F4(c); else f_0880A7B0(c);
    memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra;
}

/* EX. SETTING: SFA3MR's options */
enum { R_P1, R_P2, R_P3, R_COOP, R_DEFAULT, R_EXIT, N_ROWS };
static int *const row_value[N_ROWS] = { &rt_join_in[0], &rt_join_in[1], &rt_join_in[2], &rt_couch_coop, NULL, NULL };
static const int row_label[N_ROWS] = { S_P1, S_P2, S_P3, S_COOP, S_DEFAULT, S_EXIT };
static const int row_names[N_ROWS][2] = { { S_CPU, S_HUMAN }, { S_CPU, S_HUMAN }, { S_CPU, S_HUMAN }, { S_OFF, S_ON }, { 0, 0 }, { 0, 0 } };
static void ex_open(Cpu *c) { mode = M_EX; row = 0; bar_y = -1; WB(OPT + 5, EX_ITEM + 1); WB(OPT + 7, 0); bar_on(c); sound(c, 0x39); }
static void ex_close(Cpu *c, int snd) { mode = M_MENU; WB(OPT + 5, 0); WB(OPT + 7, 0); bar_off(); settings_save(); sound(c, snd); }
#define VERSION_Y 0xD4
#define VERSION_X (0x178 - 8 * (int)(sizeof SFA3MR_VERSION - 1))
static void ex_screen(Cpu *c) {
    uint32_t p = pressed();
    if (p & 8) { row = (row + N_ROWS - 1) % N_ROWS; sound(c, 0x38); }
    else if (p & 4) { row = (row + 1) % N_ROWS; sound(c, 0x38); }
    else if ((p & 3) && row_value[row]) { *row_value[row] = !*row_value[row]; sound(c, 0x38); }
    else if ((p & B_CONFIRM) && row_value[row]) { *row_value[row] = !*row_value[row]; sound(c, 0x39); }
    else if ((p & B_CONFIRM) && row == R_DEFAULT) {          /* DEFAULT: as a fresh sfa3mr.cfg (settings.c) */
        rt_join_in[0] = rt_join_in[1] = rt_join_in[2] = 0; rt_couch_coop = 1; sound(c, 0x39);
    }
    else if ((p & B_CONFIRM) && row == R_EXIT) { ex_close(c, 0x39); return; }   /* EXIT chosen: a confirm */
    else if (p & B_BACK) { ex_close(c, 0x2F); return; }
    WB(OPT + 7, (uint32_t)row);
    title(c, S_TITLE);
    for (int i = 0; i < N_ROWS; i++) {
        int y = 0x20 + 15 * i, colour = i == row ? 0xB : 0xA;
        text(c, STR(row_label[i]), 1, 0x30, y, colour);
        if (row_value[i]) text(c, STR(row_names[i][!!*row_value[i]]), 1, 0xA0, y, 0xB);   /* values: always yellow, as GAME OPTION */
    }
    text(c, STR(S_VERSION), 0, VERSION_X, VERSION_Y, 0xA);   /* SFA3MR's version, bottom right in the small font: just shown */
}

/* PLAYER CONFIG: P1 - P4 CONFIG and EXIT on the menu's buttons */
static void players_open(Cpu *c, int at) { mode = M_PLAYERS; tile = at; WB(OPT + 5, 0); sound(c, 0x39); }
static void players_screen(Cpu *c);
static void pedit_open(Cpu *c, int p);

static void players_screen(Cpu *c) {
    uint32_t p = pressed();
    if (p & 8) { tile = (tile + 4) % 5; sound(c, 0x38); }
    else if (p & 4) { tile = (tile + 1) % 5; sound(c, 0x38); }
    else if ((p & B_CONFIRM) && tile < 4) { pedit_open(c, tile); return; }
    else if (p & B_CONFIRM) { mode = M_MENU; sound(c, 0x39); return; }         /* EXIT */
    else if (p & B_BACK) { mode = M_MENU; sound(c, 0x2F); return; }
    title(c, S_PLAYER);
}

/* P1 - P4 CONFIG: the game's BUTTON CONFIG rows, with L2 and R2, for any player */
enum { E_SQ, E_TR, E_L, E_CR, E_CI, E_R, E_SEL, E_L2, E_R2, E_CMD, E_TYPE, E_RAPID, E_DEFAULT, E_EXIT, N_EROWS };
#define N_ATTACKS 0x37                      /* 0x088B7D08 limit */
#define ATTACK_NAMES 0x08A37B44u
static const uint32_t table_cmd = 0x08A37A5Cu, table_type = 0x08A37A68u, table_rapid = 0x08A37A70u;
static int erow_y(int r) { return 0x18 + 14 * r; }
/* The attack pictures of the game's BUTTON CONFIG (L / M / H with a fist or a foot, "Taunt"): objects
 * of kind 0x93, types 0x24 - 0x2A (0x08822EAC makes one; once a frame 0x088BB268, which shows the
 * value of its button's row in P1's or P2's config). Ours, one per button row, show the value of
 * the player being edited: animation 0x08A38010[value] for values 0 - 14; longer ones are names. */
void f_08822EAC(Cpu *c);                    /* new object: a0 kind, a1 type; v0 = object */
void f_088231C8(Cpu *c);                    /* free an object */
void f_0882196C(Cpu *c);                    /* object a0: animation a1 */
void f_088215C4(Cpu *c);                    /* object a0: step and draw */
#define ICON_ANIMS 0x08A38010u
#define N_ICONS    (E_R2 + 1)
static uint32_t icon[N_ICONS];
static int icon_v[N_ICONS];
static uint32_t call2(Cpu *c, void (*f)(Cpu *), uint32_t a0, uint32_t a1) {
    uint32_t keep[8], ra = c->r[31], v0 = c->r[2]; memcpy(keep, &c->r[4], sizeof keep);
    c->r[4] = a0; c->r[5] = a1; f(c);
    uint32_t r = c->r[2];
    memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra; c->r[2] = v0;
    return r;
}
static int icon_ok(int r) { return icon[r] && RB(icon[r]) && RB(icon[r] + 2) == 0x93; }
static void icons_on(Cpu *c, int from) {
    for (int r = from; r < N_ICONS; r++) {
        if (!icon_ok(r)) icon[r] = call2(c, f_08822EAC, 0x93, (uint32_t)(0x24 + (r < 7 ? r : 6)));
        icon_v[r] = -1;
        if (icon[r]) WB(icon[r] + 1, 0);
    }
}
static void icons_off(Cpu *c) {
    for (int r = 0; r < N_ICONS; r++) { if (c && icon_ok(r)) call2(c, f_088231C8, icon[r], 0); icon[r] = 0; }
}
static int is_icon(uint32_t o) { for (int r = 0; r < N_ICONS; r++) if (icon[r] && icon[r] == o) return 1; return 0; }
/* one button row's value: a picture (1) or not (0, the name is drawn as text) */
static int icon_show(Cpu *c, int r, int v, int y) {
    if (!icon_ok(r)) return 0;
    uint32_t o = icon[r];
    if (v < 0 || v >= 15) { WB(o + 1, 0); return 0; }
    if (icon_v[r] != v) { call2(c, f_0882196C, o, RW(ICON_ANIMS + 4u * (uint32_t)v)); icon_v[r] = v; }
    WH(o + 0x12, 0xB0); WH(o + 0x16, (uint32_t)(240 - y)); WB(o + 1, 1);
    return 1;
}
/* their once-a-frame: just drawn where pedit_screen put them */
void f_088BB268(Cpu *c) {
    uint32_t o = c->r[4];
    if (!is_icon(o)) { orig_f_088BB268(c); return; }
    if (mode != M_PEDIT) WB(o + 1, 0);
    if (RB(o + 1)) call2(c, f_088215C4, o, 0);
}
static void pedit_open(Cpu *c, int p) {
    mode = M_PEDIT; editing = p; row = 0; WB(OPT + 5, EX_ITEM + 1); WB(OPT + 7, 0);
    bar_y = 239 - erow_y(0); bar_on(c); two_on(c); icons_on(c, 0); sound(c, 0x39);
}
static int *attack_of(int r, uint8_t *cfg, int *tmp) {
    if (r <= E_SEL) { *tmp = cfg[2 * r] | cfg[2 * r + 1] << 8; return tmp; }
    return r == E_L2 ? &rt_l2[editing] : &rt_r2[editing];
}
static void pedit_screen(Cpu *c) {
    uint8_t *cfg = pcfg_block(editing);
    uint32_t p = pressed();
    int step = (p & 1) ? 1 : (p & 2) ? -1 : 0;               /* right / left */
    if (p & 8) { row = (row + N_EROWS - 1) % N_EROWS; sound(c, 0x38); }
    else if (p & 4) { row = (row + 1) % N_EROWS; sound(c, 0x38); }
    else if ((p & B_CONFIRM) && row == E_DEFAULT) { memcpy(cfg, pcfg_default, 20); rt_l2[editing] = 6; rt_r2[editing] = 7; sound(c, 0x39); }
    else if (((p & B_CONFIRM) && row == E_EXIT) || (p & B_BACK)) {
        mode = M_PLAYERS; WB(OPT + 5, 0); bar_off(); two_off(); icons_off(c); bar_y = -1; settings_save(); sound(c, (p & B_BACK) ? 0x2F : 0x39); return;
    } else if (step || ((p & B_CONFIRM) && row < E_DEFAULT)) {
        if (!step) step = 1;
        if (row <= E_R2) {
            int tmp, *v = attack_of(row, cfg, &tmp);
            *v = (*v + step + N_ATTACKS) % N_ATTACKS;
            if (row <= E_SEL) { cfg[2 * row] = (uint8_t)*v; cfg[2 * row + 1] = (uint8_t)(*v >> 8); }
        } else if (row == E_CMD) cfg[0x10] = (uint8_t)((cfg[0x10] + step + 3) % 3);
        else if (row == E_TYPE) cfg[0x11] ^= 1;
        else if (row == E_RAPID) cfg[0x12] ^= 1;
        sound(c, 0x38);
    }
    WB(OPT + 7, (uint32_t)row); bar_y = 239 - erow_y(row);
    title(c, S_P1C + editing);
    static const signed char sym[9] = { 2, 1, 8, 3, 0, 9, -1, 8, 9 };   /* button symbols (font 5); -1 SELECT */
    for (int r = 0; r < N_EROWS; r++) {
        int y = erow_y(r), colour = r == row ? 0xB : 0xA;
        if (r <= E_R2) {
            int x = (sym[r] == 8 || sym[r] == 9) ? 0x6C : 0x68;
            if (r == E_L2 || r == E_R2) x = TWO_SYM_X;
            if (sym[r] < 0) symbol(c, 1, 0, x, y); else symbol(c, 0, sym[r], x, y);
            int tmp, v = *attack_of(r, cfg, &tmp);
            if (!icon_show(c, r, v, y)) text(c, v == 14 ? STR(S_TAUNT) : RW(ATTACK_NAMES + 4u * (uint32_t)v), 1, 0xB0, y, 0xB);   /* values: always yellow */
        } else {
            text(c, STR(S_COMMAND + (r - E_CMD)), 1, 0x68, y, colour);
            if (r == E_CMD) text(c, RW(table_cmd + 4u * (cfg[0x10] % 3)), 1, 0xB0, y, 0xB);
            if (r == E_TYPE) text(c, RW(table_type + 4u * (cfg[0x11] & 1)), 1, 0xB0, y, 0xB);
            if (r == E_RAPID) text(c, RW(table_rapid + 4u * (cfg[0x12] & 1)), 1, 0xB0, y, 0xB);
        }
    }
}

static int ensure_mem(void) {
    if (mem) return 1;
    mem = rt_alloc(0x40180 + 0x20 * S_COUNT, 1, 256, "ex. setting");
    if (!mem) return 0;
    memset(HOST(TEX), 0, 256 * 256 * 2);
    for (int i = 0; i < S_COUNT; i++) strcpy((char *)HOST(STR(i)), strings[i]);
    return 1;
}

/* ---------------------------------------------------------------- hooks */
/* the option menu, once a frame */
void f_088B6164(Cpu *c) {
    if (!ensure_mem()) { orig_f_088B6164(c); return; }
    if (seen < 0 && !sub_from) WB(OPT + 1, 0);               /* OPTION MODE just chosen: start on GAME OPTION */
    seen = (long)rt_vblank_count();
    int have_buttons = 0; for (int k = 0; k < OBJ_N; k++) if (button(k) >= 0) have_buttons = 1;
    if (have_buttons) { if (!built) build_picture(); page_on(); clone_on(); }
    if (!RB(OPT + 6) && sub_from && mode == M_MENU) { WB(OPT + 1, sub_from); sub_from = 0; }   /* a submenu ended (LOAD ends SAVE LOAD itself) */
    if (mode == M_EX) { ex_screen(c); return; }
    if (mode == M_PLAYERS) { players_screen(c); return; }
    if (mode == M_PEDIT) { pedit_screen(c); return; }
    if (!RB(OPT + 6)) {                                       /* on the menu: the cursor is our item */
        item_v = RB(OPT + 1);
        uint32_t p = pressed();
        if ((p & B_CONFIRM) && item_v == EX_ITEM) { ex_open(c); return; }
        if ((p & B_CONFIRM) && item_v == PLAYER_ITEM) { players_open(c, 0); return; }
        if ((p & B_CONFIRM) && item_v > EX_ITEM) { if (item_v < N_ITEMS - 1) sub_from = item_v; WB(OPT + 1, item_v - 1); }   /* the game's item (EXIT leaves: nothing to come back to) */
        orig_f_088B6164(c);
        if (!RB(OPT + 6) && sub_from) { WB(OPT + 1, sub_from); sub_from = 0; }
        return;
    }
    orig_f_088B6164(c);                                       /* a submenu of the game's */
    if (RB(OPT + 1) == 2) { void widescreen_bar(void); widescreen_bar(); }   /* SCREEN SETTING */
    if (!RB(OPT + 6) && sub_from) { WB(OPT + 1, sub_from); sub_from = 0; }
}

/* menu cursor: one more item when called from the option menu */
void f_088B7EC0(Cpu *c) {
    if ((c->r[31] == 0x088B61FCu || c->r[31] == 0x088B6210u) && c->r[6] == 4) c->r[6] = N_ITEMS - 1;
    if (c->r[31] == 0x088B7144u || c->r[31] == 0x088B7158u) { c->r[2] = c->r[5]; return; }   /* SAVE LOAD: moved already (f_088B70F8) */
    orig_f_088B7EC0(c);
}

/* a menu button, once a frame: six buttons on the menu, five tiles on PLAYER CONFIG */
#define LAYOUT 0x08A37C60u                  /* per item: size, page, u0, v0, u1, v1 (16 bytes) */
static void picture(uint32_t o, int page, int v0) { WW(o + 0x3C, (uint32_t)page); WH(o + 0x40, 0); WH(o + 0x42, 192); WH(o + 0x4C, (uint32_t)v0); WH(o + 0x4E, (uint32_t)(v0 + 32)); }
void f_088BA82C(Cpu *c) {
    uint32_t o = c->r[4];
    orig_f_088BA82C(c);
    int k = (int)((o - OBJS) / OBJ_SZ), b = button(k);
    if (b < 0) return;
    if (starting && built) { starting = 0; rt_hold_display_set(3); }
    int item, on, y, h, slot;
    if (mode == M_PLAYERS) {                                  /* tiles: P1-P3 on items 0-2, P4 on ours, EXIT */
        item = k == clone ? 3 : b == 3 ? -1 : b;
        WB(o + 0x5D, item >= 0);
        if (item < 0) return;
        if (item < 4) picture(o, OUR_PAGE, 32 * (ROW_P1 + item));
        else if (tiles_ok && page2_ours()) picture(o, OUR_PAGE2, 32 * ROW_EXIT);
        else picture(o, (int)RW(LAYOUT + 16 * 4 + 4), (int)RH(LAYOUT + 16 * 4 + 0xA));
        on = tile == item; slot = item; y = 80 + 64 * item; h = 64;
    } else {
        item = k == clone ? EX_ITEM : b < EX_ITEM ? b : b + 1;
        if (k == clone) picture(o, OUR_PAGE, 32 * ROW_EX);
        else if (b == PLAYER_ITEM) picture(o, OUR_PAGE, 32 * ROW_PLAYER);
        else if (tiles_ok && b == SL_ITEM) picture(o, OUR_PAGE, 32 * ROW_SL);
        else if (tiles_ok && page2_ours() && (b == 0 || b == 2 || b == 4)) picture(o, OUR_PAGE2, 32 * (b == 0 ? ROW_GO : b == 2 ? ROW_SCR : ROW_EXIT));
        else picture(o, (int)RW(LAYOUT + 16u * (uint32_t)b + 4), (int)RH(LAYOUT + 16u * (uint32_t)b + 0xA));
        on = mode == M_MENU && RB(OPT + 1) == item && !RB(OPT + 6);
        if (mode != M_MENU) WB(o + 0x5D, 0);
        slot = item; y = 80 + 53 * item; h = 53;              /* five rows of 64 become six of 53 */
    }
    WH(o + 0xA, (uint32_t)y);
    WH(o + 6, (uint16_t)((slot & 1) ? x_odd : x_even));
    WB(o + 0x38, on ? 0 : 1); WB(o + 0x88, on ? 0xFF : 0x80);
    WH(o + 0xC, on ? (uint16_t)-4 : 0); WH(o + 0xE, on ? 0xC8 : 0xC0);
    WH(o + 0x18, on ? (uint16_t)(h == 64 ? -4 : -3) : 0); WH(o + 0x1A, (uint32_t)(on ? h + h / 8 : h));
    static const uint8_t col[] = { 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x2C, 0x2D, 0x30, 0x31, 0x32, 0x33 };   /* corner colours */
    for (size_t i = 0; i < sizeof col; i++) WB(o + col[i], on ? 0xFF : 0x80);
}

/* The option menu's first frames are drawn with its five buttons as the game made them, before they
 * are laid out here: they are kept off screen (the previous, black frame stays up) from the menu's
 * start until a few frames after the buttons' first lay-out (the drawing is that much behind). */

void f_088B5FB8(Cpu *c) { orig_f_088B5FB8(c); starting = 1; rt_hold_display(30); }

/* once a frame: tidy up when the option menu is gone */
void exsetting_frame(void) {
    if (!mem || seen < 0) return;
    /* (not sub_from: a load or save runs frames of its own inside a submenu, without the menu) */
    if ((long)rt_vblank_count() - seen > 2) { clone_off(); page_off(); bar_off(); two_off(); icons_off(NULL); mode = M_MENU; seen = -1; }
}

/* GAME OPTION's green bar, once a frame: it shows only on GAME OPTION (+5 == 1) and goes away when
 * no submenu is open (+6 == 0); ours is shown on our screens */
void f_088BA8F4(Cpu *c) {
    uint32_t o = c->r[4];
    for (int i = 0; i < 2; i++) if (two[i] >= 0 && o == obj(two[i])) {
        if (mode == M_PEDIT) two_frame(o, erow_y(E_L2 + i)); else WB(o + 0x5D, 0);
        return;
    }
    if (!((mode == M_EX || mode == M_PEDIT) && bar >= 0 && o == obj(bar))) { orig_f_088BA8F4(c); return; }
    uint32_t k5 = RB(OPT + 5), k6 = RB(OPT + 6);
    WB(OPT + 5, 1); WB(OPT + 6, 1);
    orig_f_088BA8F4(c);
    WB(OPT + 5, k5); WB(OPT + 6, k6);
    if (bar_y >= 0) WH(o + 0xA, (uint32_t)bar_y);
}

/* ---------------------------------------------------------------- SAVE LOAD: IMPORT and EXPORT */
/* The game's SAVE LOAD submenu (logic 0x088B70F8, drawing 0x088B7AE8; row in the option context's +7:
 * SAVE, LOAD, DELETE, AUTO SAVE, EXIT) gets two more rows before EXIT:
 *   IMPORT  reads the PSP saves (system data and World Tour) in <work>/import (savestore.c), keeps
 *           them in sfa3mr.dat / sfa3mr.cfg (the previous ones are copied to .bak first) and gives
 *           them to the game at once
 *   EXPORT  writes the saves to <work>/export as a PSP keeps them (encrypted, with PARAM.SFO), to
 *           copy to a PSP's or PPSSPP's PSP/SAVEDATA
 * The game's rows 0-3 work as before; ours are handled here, and the whole list is drawn here. */
void f_0880A39C(Cpu *c);                    /* leaving an option submenu */
void f_088B73B8(Cpu *c);                    /* SAVE LOAD's LOAD row: on confirm, loads the system data and puts it in effect */
int store_import(const char *dir);
int store_export_dir(const char *dir);
void store_path(char *out, size_t n, const char *name);
enum { SL_SAVE, SL_LOAD, SL_DELETE, SL_AUTO, SL_IMPORT, SL_EXPORT, SL_EXIT, SL_ROWS };
#define SL_TASK       0x09523C60u           /* save / load task: +4 message, +5 result (sysdata.c) */
#define MSG_SAVE_DONE 0x0E                  /* "Save completed." */
#define MSG_LOAD_DONE 0x11                  /* "Load completed." */
static int sl_y(int r) { return 0x26 + 0x14 * r; }       /* up a little: room for the status line under EXIT */
static char sl_msg[2][48];
static long sl_msg_until;
static uint32_t sl_strings;                 /* our strings in guest memory: IMPORT, EXPORT, the message */
static void sl_say(const char *a, const char *b) {
    snprintf(sl_msg[0], sizeof sl_msg[0], "%s", a); snprintf(sl_msg[1], sizeof sl_msg[1], "%s", b ? b : "");
    sl_msg_until = (long)rt_vblank_count() + 300;
}
static void backup(const char *name) {
    char from[1100], to[1200]; store_path(from, sizeof from, name); snprintf(to, sizeof to, "%s.bak", from);
    FILE *f = fopen(from, "rb"); if (!f) return;
    FILE *g = fopen(to, "wb"); char buf[4096]; size_t n;
    if (g) { while ((n = fread(buf, 1, sizeof buf, f)) > 0) fwrite(buf, 1, n, g); fclose(g); }
    fclose(f);
}
static void sl_leave(Cpu *c, int snd) {
    sound(c, snd);
    uint32_t keep[8], ra = c->r[31]; memcpy(keep, &c->r[4], sizeof keep);
    f_0880A39C(c);
    memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra;
    WB(OPT + 7, 0); WB(OPT + 6, 0);
}
/* the game's own LOAD (0x088B73B8, as if chosen), off screen: it reads the save (savestore.c) and puts
   the settings in effect (with its confirm sound) */
int store_delete_sys(void);
static void sl_game_load(Cpu *c) {
    uint32_t keep[8], ra = c->r[31], k0 = RW(SLOTS + 0xC); memcpy(keep, &c->r[4], sizeof keep);
    WW(SLOTS + 0xC, k0 | B_CONFIRM);
    rt_hold_display(600);
    WB(OPT + 7, SL_LOAD); f_088B73B8(c);
    rt_hold_display_set(2);
    WW(SLOTS + 0xC, k0); memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra;
}
void f_088B70F8(Cpu *c) {
    /* up / down over all seven rows, by the game's own cursor mover (its press-and-hold repeat too:
       moving rows by hand left its repeat timer stale, so a held press moved twice) */
    int start = (int8_t)RB(OPT + 7);
    {
        uint32_t keep[8], ra = c->r[31]; memcpy(keep, &c->r[4], sizeof keep);
        for (int k = 0; k < 2; k++) {
            c->r[4] = SLOTS + 0x2Cu * (uint32_t)k; c->r[5] = (uint32_t)(int8_t)RB(OPT + 7); c->r[6] = SL_ROWS - 1; c->r[31] = 0;
            orig_f_088B7EC0(c); WB(OPT + 7, c->r[2]);
        }
        memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra;
    }
    int r = (int8_t)RB(OPT + 7);
    uint32_t p = pressed();
    if (r != start) return;
    if ((r == SL_SAVE || r == SL_LOAD) && (p & B_CONFIRM)) {  /* SAVE / LOAD: no question, no screens, a line at the bottom */
        rt_hold_display(600);                                 /* (they run frames of their own) */
        WB(SL_TASK + 4, 0);
        orig_f_088B70F8(c);
        rt_hold_display_set(2);
        int ok = RB(SL_TASK + 5) == 0 && RB(SL_TASK + 4) == (r == SL_SAVE ? MSG_SAVE_DONE : MSG_LOAD_DONE);
        if (ok) sl_say(r == SL_SAVE ? "SYSTEM DATA SAVED" : "SYSTEM DATA LOADED", NULL);
        else rt_hold_display_set(0);                          /* something went wrong: the game's own message shows */
        return;
    }
    if (r == SL_DELETE && (p & B_CONFIRM)) {                  /* DELETE: the system data back to the defaults (World Tour kept) */
        backup("sfa3mr.dat"); backup("sfa3mr.cfg");
        if (store_delete_sys()) { sl_game_load(c); WB(OPT + 7, SL_DELETE); sl_say("SYSTEM DATA DELETED", NULL); }
        else { sl_say("DELETE FAILED", NULL); sound(c, 0x2F); }
        return;
    }
    if (r < SL_IMPORT) { orig_f_088B70F8(c); return; }        /* the game's own rows */
    if (p & B_BACK) { sl_leave(c, 0x2F); return; }
    if (!(p & B_CONFIRM)) return;
    char dir[1100];
    if (r == SL_EXIT) { sl_leave(c, 0x39); return; }
    if (r == SL_IMPORT) {
        store_path(dir, sizeof dir, "import");
        backup("sfa3mr.dat"); backup("sfa3mr.cfg");
        int n = store_import(dir);
        if (n) {
            /* the game takes it in by its own LOAD (0x088B73B8, as if chosen): the load reads the new
               save (savestore.c) and puts the settings in effect */
            sl_game_load(c);
            WB(OPT + 7, SL_IMPORT);
            sl_say(n == 2 ? "IMPORTED SYSTEM AND WORLD TOUR" : "IMPORTED 1 SAVE", "OLD SAVES KEPT AS .BAK");
        }
        else { sl_say("NO PSP SAVE FOUND IN", "THE IMPORT FOLDER"); sound(c, 0x2F); }
    } else {
        store_path(dir, sizeof dir, "export");
        int n = store_export_dir(dir);
        if (n) { sl_say(n == 2 ? "EXPORTED SYSTEM AND WORLD TOUR" : "EXPORTED 1 SAVE", "TO THE EXPORT FOLDER"); sound(c, 0x39); }
        else { sl_say("EXPORT FAILED", NULL); sound(c, 0x2F); }
    }
}
void f_088B7AE8(Cpu *c) {
    if (!sl_strings) {
        sl_strings = rt_alloc(0x100, 1, 16, "save load");
        if (!sl_strings) { orig_f_088B7AE8(c); return; }
        strcpy((char *)HOST(sl_strings), "IMPORT"); strcpy((char *)HOST(sl_strings + 0x10), "EXPORT");
    }
    static const uint32_t game_str[SL_ROWS] = { 0x089FF914u, 0x089FF91Cu, 0x089FF924u, 0x089FF92Cu, 0, 0, 0x089FF8ACu };
    int r = (int8_t)RB(OPT + 7);
    text(c, 0x089FF938u, 2, 0x78, 5, 0xB);                   /* SAVE LOAD */
    for (int i = 0; i < SL_ROWS; i++) {
        uint32_t s = game_str[i] ? game_str[i] : sl_strings + (i == SL_IMPORT ? 0 : 0x10);
        text(c, s, 1, 0x80, sl_y(i), i == r ? 0xB : 0xA);
    }
    text(c, RW(0x08A37AACu + 4u * (uint32_t)((int8_t)RB(0x08A058A0u + 0x4F) & 1)), 1, 0xE0, sl_y(SL_AUTO), 0xB);   /* AUTO SAVE ON / OFF */
    if ((long)rt_vblank_count() < sl_msg_until)
        for (int k = 0; k < 2; k++) if (sl_msg[k][0]) {
            strcpy((char *)HOST(sl_strings + 0x20 + 0x30u * (uint32_t)k), sl_msg[k]);
            text(c, sl_strings + 0x20 + 0x30u * (uint32_t)k, 0, 0xC0 - 4 * (int)strlen(sl_msg[k]), 0xC6 + 10 * k, 9);
        }
}

/* SAVE LOAD's bar (object kind 0xC, type 0x14), once a frame: on our row (its y is 240 - the row's) */
void f_088BAB44(Cpu *c) {
    uint32_t o = c->r[4];
    orig_f_088BAB44(c);
    if (RB(OPT + 6) && RB(OPT + 5) == SL_ITEM + 1 && mode == M_MENU) WH(o + 0xA, (uint32_t)(240 - sl_y((int8_t)RB(OPT + 7))));
}

/* ---------------------------------------------------------------- the pause menu's BUTTON CONFIG (M4)
 * The pause menu's BUTTON CONFIG (a task: 0x088B8ED0 opens it, 0x088BA028 once a frame) edits the
 * config of the player who paused (context 0x08B02E24: +0 - +2 each player's row 0 - 11, +4 that
 * player): input 0x088B8FC4 / 0x088B90E4 / 0x088B9208 for players 1 - 3 (up / down 0x088B9CEC, left /
 * right 0x088B93BC, Circle on DEFAULT 0x088B96B0, on EXIT or Cross: leave), drawing 0x088B9790, the
 * attack pictures objects of kind 0 (types player x 8 + 1 + row, made by 0x08822EE4, placed once) and
 * the green bar an object of kind 3 type 5 (once a frame 0x08888CC0, rows of 16 from the table at
 * 0x08A2620C).
 *
 * Here it gets the L2 and R2 rows of P1 - P4 CONFIG: the same fourteen rows, 14 apart. Our row per
 * player (pex) maps to the game's (L2 / R2 to COMMAND's, which has no picture); up / down, and left
 * / right on L2 / R2, are ours; the rest is the game's. The 2 of L2 / R2 and their pictures are the
 * ones P1 - P4 CONFIG uses (our page 4; kind 0x93 objects). Leaving saves (sfa3mr.cfg). */
void f_088B9CEC(Cpu *c);                    /* up / down with repeat: a0 pad, a1 row, a2 last row; v0 row */
void f_088B9B30(Cpu *c);                    /* left / right with repeat: a0 pad, a1 value, a2 last value; v0 value */
#define PCTX      0x08B02E24u
#define PICONS    0x09514AB0u               /* the pool of the attack pictures: 16 x 0xC0, downwards */
static const uint32_t pause_cmd = 0x08A37C44u, pause_type = 0x08A37C50u, pause_rapid = 0x08A37C58u;
static int pex[3];
static long pause_seen = -100, pause_opened = -100;
static int pause_closing;                   /* left, still closing (pause_end, pause_frame) */
void f_08822EE4(Cpu *c);                    /* new object of the pause's pool: a0 kind, a1 type; v0 = object */
void selectscreen_two(int x, int y);
#define PAUSE_ANIMS 0x08A3CC78u             /* the pause's attack pictures, by value */
static uint32_t picon[2];
static int picon_v[2];
static int pause_live(void) { return (long)rt_vblank_count() - pause_seen <= 2; }
static int game_row(int ex) { return ex <= E_SEL ? ex : ex <= E_R2 ? 7 : ex - 2; }
static uint32_t call3(Cpu *c, void (*f)(Cpu *), uint32_t a0, uint32_t a1, uint32_t a2) {
    uint32_t keep[8], ra = c->r[31], v0 = c->r[2]; memcpy(keep, &c->r[4], sizeof keep);
    c->r[4] = a0; c->r[5] = a1; c->r[6] = a2; f(c);
    uint32_t r = c->r[2];
    memcpy(&c->r[4], keep, sizeof keep); c->r[31] = ra; c->r[2] = v0;
    return r;
}
/* L2's and R2's pictures are made when the menu opens, with the game's own (kind 0 type 0x18, just
 * drawn): an object takes a few frames to show its picture, so made later they came in late */
static void pause_open(Cpu *c) {
    pex[0] = pex[1] = pex[2] = 0; pause_closing = 0;
    ensure_mem();
    for (int i = 0; i < 2; i++) {
        picon[i] = call2(c, f_08822EE4, 0, 0x18); picon_v[i] = -1;
        if (picon[i]) WW(picon[i] + 0x58, PCTX);
    }
    pause_seen = pause_opened = (long)rt_vblank_count();
}
void f_088B8ED0(Cpu *c) { uint32_t keep[4]; memcpy(keep, &c->r[4], sizeof keep); orig_f_088B8ED0(c); uint32_t v0 = c->r[2]; memcpy(&c->r[4], keep, sizeof keep); pause_open(c); c->r[2] = v0; }
static void pause_begin(Cpu *c) {
    if (!pause_live() && !picon[0]) pause_open(c);
    pause_closing = 0;
    pause_seen = (long)rt_vblank_count();
}
/* Leaving: the menu is still drawn while it closes, so our rows stay up until the game's pictures
 * go (pause_frame), and L2's and R2's go with them. */

static void pause_end(Cpu *c) {
    (void)c;
    settings_save();
    pause_closing = 1;
}
static int game_pictures(void) {
    for (int k = 0; k < 16; k++) {
        uint32_t o = PICONS - 0xC0u * (uint32_t)k;
        if (RB(o) && RB(o + 2) == 0 && RB(o + 3) >= 1 && RB(o + 3) < 0x18) return 1;
    }
    return 0;
}
void pause_frame(void) {                    /* once a frame, after the 2D objects have run (selectscreen.c) */
    if (!pause_closing || game_pictures()) return;
    for (int i = 0; i < 2; i++) {
        uint32_t o = picon[i];
        if (o && RB(o) && RB(o + 2) == 0 && RB(o + 3) == 0x18) WH(o + 0x12, (uint16_t)-200);   /* (the game frees its kind 0 objects itself) */
        picon[i] = 0;
    }
    pause_closing = 0; pause_seen = -100;
}
static void pause_input(Cpu *c, int pl, void (*orig)(Cpu *)) {
    uint32_t task = c->r[6], pad = SLOTS + 0x2Cu * (uint32_t)pl;
    pause_begin(c);
    uint32_t now = RW(pad + 0xC) | RW(pad + 4);
    int ex = pex[pl];
    if (now & 0xC) {                                          /* up / down: our rows */
        ex = (int)call3(c, f_088B9CEC, pad, (uint32_t)ex, N_EROWS - 1);
        pex[pl] = ex; WB(PCTX + (uint32_t)pl, (uint32_t)game_row(ex));
        return;
    }
    WB(PCTX + (uint32_t)pl, (uint32_t)game_row(ex));
    if ((ex == E_L2 || ex == E_R2) && (now & 3)) {            /* left / right on L2 / R2 */
        int *v = ex == E_L2 ? &rt_l2[pl] : &rt_r2[pl];
        *v = (int)call3(c, f_088B9B30, pad, (uint32_t)*v, N_ATTACKS - 1);
        return;
    }
    int dflt = (RW(pad + 0xC) & 0x80) && ex == E_DEFAULT;
    uint32_t before = RB(task);
    orig(c);
    if (dflt) { rt_l2[pl] = 6; rt_r2[pl] = 7; }
    if (RB(task) != before) pause_end(c);                      /* leaving */
}
void f_088B8FC4(Cpu *c) { pause_input(c, 0, orig_f_088B8FC4); }
void f_088B90E4(Cpu *c) { pause_input(c, 1, orig_f_088B90E4); }
void f_088B9208(Cpu *c) { pause_input(c, 2, orig_f_088B9208); }

/* the drawing: fourteen rows */
void f_088B9790(Cpu *c) {
    if (picon[0] && (pause_closing || (!pause_live() && (long)rt_vblank_count() - pause_opened <= 30)))
        pause_seen = (long)rt_vblank_count();                 /* opened with no input yet, or closing */
    if (!pause_live() || !mem) { orig_f_088B9790(c); return; }
    uint32_t cfgs = c->r[4];
    int pl = (int8_t)RB(PCTX + 4);
    if (pl < 0 || pl > 2) { orig_f_088B9790(c); return; }
    uint8_t *cfg = HOST(cfgs + 20u * (uint32_t)pl);
    int cur = pex[pl];
    static const signed char sym[9] = { 2, 1, 8, 3, 0, 9, -1, 8, 9 };
    for (int r = 0; r < N_EROWS; r++) {
        int y = erow_y(r), colour = r == cur ? 0xB : 0xA;
        if (r <= E_R2) {
            int x = (sym[r] == 8 || sym[r] == 9) ? 0x6C : 0x68;
            if (r == E_L2 || r == E_R2) x = TWO_SYM_X;
            if (sym[r] < 0) symbol(c, 1, 0, x, y); else symbol(c, 0, sym[r], x, y);
            int v = r <= E_SEL ? (cfg[2 * r] | cfg[2 * r + 1] << 8) : r == E_L2 ? rt_l2[pl] : rt_r2[pl];
            if (r <= E_SEL) {                                 /* the game's picture, moved to our row */
                for (int k = 0; k < 16; k++) {
                    uint32_t o = PICONS - 0xC0u * (uint32_t)k;
                    if (RB(o) && RB(o + 2) == 0 && RB(o + 3) == (uint32_t)(pl * 8 + 1 + r)) WH(o + 0x16, (uint32_t)(240 - y));
                }
                if (v >= 15) text(c, RW(ATTACK_NAMES + 4u * (uint32_t)v), 1, 0xB0, y, 0xB);
            } else {
                selectscreen_two(TWO_X, y + 2);
                uint32_t o = picon[r - E_L2];
                int pic = o && RB(o) && RB(o + 2) == 0 && RB(o + 3) == 0x18 && v < 15;
                if (pic && picon_v[r - E_L2] != v) {            /* (again until its own start has run: +4) */
                    call2(c, f_0882196C, o, RW(PAUSE_ANIMS + 4u * (uint32_t)v));
                    if (RB(o + 4)) picon_v[r - E_L2] = v;
                }
                if (o && RB(o) && RB(o + 2) == 0) { WH(o + 0x12, pic ? 0xB0 : (uint16_t)-200); WH(o + 0x16, (uint32_t)(240 - y)); }
                if (!pic) text(c, RW(ATTACK_NAMES + 4u * (uint32_t)v), 1, 0xB0, y, 0xB);
            }
        } else {
            text(c, STR(S_COMMAND + (r - E_CMD)), 1, 0x68, y, colour);
            if (r == E_CMD) text(c, RW(pause_cmd + 4u * (cfg[0x10] % 3)), 1, 0xB0, y, 0xB);
            if (r == E_TYPE) text(c, RW(pause_type + 4u * (cfg[0x11] & 1)), 1, 0xB0, y, 0xB);
            if (r == E_RAPID) text(c, RW(pause_rapid + 4u * (cfg[0x12] & 1)), 1, 0xB0, y, 0xB);
        }
    }
}
/* the green bar on our row */
void f_08888CC0(Cpu *c) {
    uint32_t o = c->r[4];
    orig_f_08888CC0(c);
    if (!pause_live() || RW(o + 0x7C) != PCTX) return;
    int pl = (int16_t)RH(o + 0x66);
    if (pl >= 0 && pl <= 2) WH(o + 0xA, (uint32_t)(240 - erow_y(pex[pl])));
}
