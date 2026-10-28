/* Native replacements for game functions (listed in tools/data/hooks.txt). */
#include "rt.h"
#include <string.h>
#include <stdio.h>

extern uint32_t rt_pad_buttons[4];
int rt_local_players = 3;                 /* how many local pads feed player slots (1-3) */
int rt_human[3] = { 1, -1, -1 };          /* per pad: 1 plugged in, 0 not, -1 ask the host (--p2 / --p3 for tests) */
int host_pad_connected(int i);
extern int rt_join_in[3];                 /* settings.c: OPTION MODE > EX. SETTING, 1P / 2P / 3P HUMAN or CPU */

/* Who plays which fighter, offline (decision 19).
 *
 * The fighter objects live at fixed addresses (0x092E3EA0 + 0x498 n). +2 is the player number: the
 * controller slot (and config) the fighter reads; +775 the slot for its newly pressed buttons; +0x175
 * the "computer controlled" flag (+775 the game keeps writing; 0x08867794 is told instead: playercfg.c).
 * A side can have more fighters than players: R.Dramatic Battle's two
 * opponents and Dramatic Battle's boss all say player 1.
 *
 * The rule:
 *   - a player who started the mode or pressed Start to join (the game's players-present byte
 *     0x08E75910 + 0x35, or our own record for select screens that keep none) plays their own fighter
 *     (the first with their number), as the game has it;
 *   - every other fighter, in order, goes to the next pad nobody started with (1, 2, 3 in turn):
 *     HUMAN if that pad's JOIN-IN (OPTION MODE > EX. SETTING) is HUMAN and the pad is plugged in
 *     (pad 1 also has the keyboard), otherwise it stays as the game has it (the computer). To make
 *     it read that pad, its +2 is set to the pad's slot (and put back if it goes back).
 * P1 starts R.Dramatic Battle: the two on the right are pads 2 and 3. P2 starts Arcade: the opponent
 * is pad 1. P1 starts Dramatic Battle: the partner is pad 2, the boss pad 3. */
#define FIGHTER(k) (0x092E3EA0u + 0x498u * (uint32_t)(k))
#define N_FIGHTERS 4
static struct { int8_t slot, orig2, freed; } take[N_FIGHTERS] = { { -1 }, { -1 }, { -1 }, { -1 } };   /* what we did to fighter n */
static int8_t pad_of[N_FIGHTERS] = { -1, -1, -1, -1 };   /* the pad playing fighter n by the rule, -1 the computer */
static int offline_now;                   /* set each frame by f_0880509C: not a network game */
int hooks_pad_of(uint32_t f) {            /* the pad playing fighter object f (offline), or -1 */
    if (!offline_now || f < FIGHTER(0) || (f - FIGHTER(0)) % 0x498u) return -1;
    uint32_t i = (f - FIGHTER(0)) / 0x498u;
    return i < N_FIGHTERS ? pad_of[i] : -1;
}
static void give_back(int i) {
    uint32_t F = FIGHTER(i);
    if (take[i].slot >= 0 && RB(F + 2) == (uint32_t)take[i].slot) WB(F + 2, (uint32_t)take[i].orig2);
    if (take[i].freed && RB(F + 0x175) == 0) WB(F + 0x175, 1);
    take[i].slot = -1; take[i].freed = 0;
}
/* the slot we gave fighter F (its +775 is kept by the game), or -1 */
int hooks_taken_slot(uint32_t f) {
    for (int i = 0; i < N_FIGHTERS; i++) if (FIGHTER(i) == f && take[i].slot >= 0 && RB(f + 2) == (uint32_t)take[i].slot) return take[i].slot;
    return -1;
}
#define SURVIVAL_CTX 0x092E1228u
/* The select screens where a pad joins with Start (decision 20): Survival Dramatic / Variable and every Free Battle
 * kind (one select screen, code 0x088A2xxx - 0x088A6xxx, context 0x092E1228; kinds at + 0x4F / + 0x9F).
 * None of them keeps a record of a second player. The boxes a pad can join, by kind (s = the starter's
 * box, fighter 0 when player 1 started, 1 when player 2 did):
 *   Survival Dramatic and Variable, Free Battle Arcade and Team: the other box, 1 - s, the first other pad to press Start
 *   Free Battle Variable and Dramatic: the partner (1 - s, pads 1 / 2) and the opponent (fighter 2, pad 3)
 *   Free Battle R.Dramatic: the two opponents (fighter 2, pads 1 / 2; fighter 3, pad 3)
 * as the boxes' labels say (2P, 3P ...). A box's turn starts in state 1 (character), in Team in state 0
 * (team size). The pad keeps that fighter until a mode is chosen again on the MODE MENU. */
enum { JK_NONE, JK_SURVIVAL, JK_ARCADE, JK_TEAM, JK_VARIABLE, JK_DRAMATIC, JK_RDRAMATIC };
int hooks_join_kind(void) {
    uint32_t mode = RB(0x08E75910u + 0x72);
    if (mode == 7) { uint32_t k = RB(SURVIVAL_CTX + 0x4F); return k == 4 || k == 5 ? JK_SURVIVAL : JK_NONE; }   /* Dramatic, Variable */
    if (mode != 16) return JK_NONE;
    switch (RB(SURVIVAL_CTX + 0x9F)) {
    case 1: return JK_ARCADE; case 2: return JK_TEAM; case 3: return JK_VARIABLE;
    case 4: return JK_DRAMATIC; case 5: return JK_RDRAMATIC; default: return JK_NONE;
    }
}
int dramatic_select(void) { return hooks_join_kind() != JK_NONE; }   /* (a select screen with joining) */
int hooks_join_first_state(void) { return hooks_join_kind() == JK_TEAM ? 0 : 1; }
static int8_t join_to[3] = { -1, -1, -1 };     /* the fighter pad k joined, or -1 */
static int starter_box(void) {                  /* fighter 0 or 1: the one whose player started */
    uint32_t gp = RB(0x08E75910u + 0x35) & 7;
    return (gp & 1) ? 0 : 1;
}
/* the boxes that can be joined (fighter numbers), and which of them is pad 3's (or -1: any pad's) */
int hooks_join_boxes(int out[4], int *pad3_box) {
    int s = starter_box(), n = 0;
    *pad3_box = -1;
    switch (hooks_join_kind()) {
    case JK_SURVIVAL: case JK_ARCADE: case JK_TEAM: out[n++] = 1 - s; break;
    case JK_VARIABLE: case JK_DRAMATIC: out[n++] = 1 - s; out[n++] = 2; *pad3_box = 2; break;
    case JK_RDRAMATIC: out[n++] = 2; out[n++] = 3; *pad3_box = 3; break;
    default: break;
    }
    return n;
}
int hooks_join_box(int k) { return k >= 0 && k < 3 ? join_to[k] : -1; }
int hooks_join_target(int k) {                  /* the box pad k would join by pressing Start now, or -1 */
    uint32_t gp = RB(0x08E75910u + 0x35) & 7;
    if (k < 0 || k > 2 || (gp & (1u << k)) || join_to[k] >= 0) return -1;
    int box[4], p3, n = hooks_join_boxes(box, &p3), t = -1;
    for (int i = 0; i < n && t < 0; i++) {
        if (p3 >= 0 && (box[i] == p3) != (k == 2)) continue;   /* pad 3's box, or the others' */
        t = box[i];
    }
    if (t < 0) return -1;
    for (int j = 0; j < 3; j++) if (join_to[j] == t) return -1;
    return t;
}
static void give_to(int i, int j) {             /* fighter i is played by pad j */
    uint32_t F = FIGHTER(i);
    if (take[i].slot >= 0 && take[i].slot != j) give_back(i);
    /* (recorded even when its number is already the pad's: Free Battle Variable's opponent is player 3 on
       the select screen, and offline the game reads slot 1 for any number but 0, so pad 3's word has to be
       put in its place: playercfg.c, for a fighter handed to pad 3) */
    if (take[i].slot < 0) {
        take[i].orig2 = (int8_t)RB(F + 2); take[i].slot = (int8_t)j;
        if (RB(F + 2) != (uint32_t)j) WB(F + 2, (uint32_t)j);
    }
    if (RB(F + 0x175) != 0) { WB(F + 0x175, 0); take[i].freed = 1; }
    pad_of[i] = (int8_t)j;
}
static void apply_local_players(void) {
    static uint32_t prev[3];
    /* Start joins only on the select screen (0x08E75910 + 2 == 1) of the Survival / Free Battle Dramatic
       kinds; elsewhere the game has its own join (Arcade's new challenger, Dramatic Battle's 0x0883465C),
       and Start in a fight is the pause */
    int select_join = RB(0x08E75910u + 2) == 1 && dramatic_select();
    for (int k = 0; k < 3; k++) {
        uint32_t b = rt_pad_buttons[k];
        if (((b & ~prev[k]) & 0x8u) && select_join) {
            int t = hooks_join_target(k);
            if (t >= 0 && RB(FIGHTER(t) + 4) == (uint32_t)hooks_join_first_state()) {   /* (only while that box has not chosen) */
                join_to[k] = (int8_t)t;
                rt_log("select: pad %d joined (Start), box %d", k + 1, t);
            }
        }
        prev[k] = b;
    }
    uint32_t present = RB(0x08E75910u + 0x35) & 7;
    for (int i = 0; i < N_FIGHTERS; i++) pad_of[i] = -1;
    if (!present) { for (int i = 0; i < N_FIGHTERS; i++) take[i].slot = -1, take[i].freed = 0; return; }   /* attract demo */
    for (int k = 0; k < 3; k++) if (join_to[k] >= 0) present |= 1u << k;
    int free_pads[3], nfree = 0;
    for (int k = 0; k < 3; k++) if (!(present & (1u << k))) free_pads[nfree++] = k;
    int owned = 0, next = 0;
    for (int i = 0; i < N_FIGHTERS; i++) {
        uint32_t F = FIGHTER(i);
        if (take[i].slot >= 0 && RB(F + 2) != (uint32_t)take[i].slot) { take[i].slot = -1; take[i].freed = 0; }   /* set up afresh */
        if (RB(F) != 1) { take[i].slot = -1; take[i].freed = 0; continue; }
        int joined = -1;
        for (int k = 0; k < 3; k++) if (join_to[k] == i) joined = k;
        if (joined >= 0) { owned |= 1 << joined; give_to(i, joined); continue; }
        int k = take[i].slot >= 0 ? take[i].orig2 : (int8_t)RB(F + 2);   /* the player number the game gave it */
        if (k >= 0 && k < 3 && (present & (1u << k)) && join_to[k] < 0 && !(owned & (1 << k))) {
            owned |= 1 << k; pad_of[i] = (int8_t)k;
            if (take[i].slot >= 0 || take[i].freed) give_back(i);
            continue;
        }
        if (next >= nfree) { if (take[i].slot >= 0 || take[i].freed) give_back(i); continue; }
        int j = free_pads[next++];
        int human = rt_join_in[j] && (j == 0 || (rt_human[j] >= 0 ? rt_human[j] : host_pad_connected(j)));
        if (!human) { if (take[i].slot >= 0 || take[i].freed) give_back(i); continue; }
        give_to(i, j);
    }
}

/* A player joins on a select screen (Start pressed): 0x0883465C sets the player up (a2 = its object)
 * and adds it to the players present (0x08E75910 + 0x35). Whose cursor moves is +0x338 of the
 * object, which the select screen only sets when it starts, so on the PSP a late joiner (always the
 * other side, which a PSP never had) was frozen. Switch it on, except in Dramatic Battle (mode 8) and
 * Variable Battle (15), where the cursors take turns. */
void f_0883465C(Cpu *c) {
    uint32_t obj = c->r[6];
    orig_f_0883465C(c);
    uint32_t mode = RB(0x08E75910u + 0x72);
    if (mode != 8 && mode != 15 && RW(obj + 0x338) == 0) {
        WW(obj + 0x338, 1);
        rt_log("select: player %u joined, cursor switched on", RB(obj + 0x151) + 1);
    }
}

/* A player who is not playing presses Start (0x08833DDC, a0 = 0x08E75910, a1 = 0x092E1228, a2 = its
 * fighter, +0x19C the request): on a select screen the game lets it join (0x0883465C), in a fight it
 * comes in as a new challenger. Not in R.DRAMATIC BATTLE from the MODE MENU (mode 17): whoever started
 * it plays it, and the others play only through JOIN-IN (EX. SETTING, docs/PLAYERS.md). Their Start
 * pauses the fight instead (f_08828AF4). (a1 + 6 or + 0x46 set: the continue path, left alone.) */
#define MODE_RDRAMATIC 17
void f_08833DDC(Cpu *c) {
    uint32_t pl = c->r[5], f = c->r[6];
    if (RB(0x08E75910u + 0x72) == MODE_RDRAMATIC && !(RB(pl + 6) | RB(pl + 0x46))) {
        WB(f + 0x19C, 0);
        return;
    }
    orig_f_08833DDC(c);
}

/* Every frame the game fills three input records {slot, present, buttons} at 0x08E75C28 and hands
 * each to the per-player input processor (state at 0x08E75BA0 + 0x2C * slot). On a PSP only the
 * slot named by the byte at 0x08AD8FE4 gets the local pad; the others come over the wireless link.
 * Offline, we feed local pads 2 and 3 into the other slots, through the same button-config remap
 * (0x088E3660) that player 1's buttons went through. */
void f_0880509C(Cpu *c) {
    uint32_t ra = c->r[31];
    c->r[31] = 0x088050BC; f_088D7F18(c);
    int net = c->r[2] != 0;
    offline_now = !net;
    if (!net) {
        c->r[31] = 0x088050CC; f_088E3550(c);
        if (c->r[2] != 0) { c->r[31] = ra; return; }
    }
    const uint32_t rec = 0x08E75C28, state = 0x08E75BA0;
    uint32_t local = RB(0x08AD8FE4), pad = RW(0x08AD8FE0);
    for (uint32_t k = 0; k < 3; k++) {
        uint32_t b = 0;
        if (k == local) b = pad;
        else if (!net && (int)k < rt_local_players) {
            int other = (int)(k > local ? k : k + 1);           /* the next local pad: 2, then 3 */
            if (other < 4 && rt_pad_buttons[other]) {
                c->r[4] = rt_pad_buttons[other]; c->r[31] = 0x088052F4; f_088E3660(c);
                b = c->r[2];
            }
        }
        WH(rec + 8 * k, k); WH(rec + 8 * k + 2, 1); WW(rec + 8 * k + 4, b);
        c->r[4] = state + 0x2C * k; c->r[5] = rec + 8 * k;
        c->r[31] = 0x08805118; f_08804F50(c);
    }
    if (!net) apply_local_players();
    c->r[31] = ra;
}

/* The next battle is set up (0x0882E380, a0 = game state 0x08E75910). +0x39 has a bit for each player
 * who finished choosing; only the one who finishes second sets it (0x088954E0), so with two players
 * present (+0x35) both are marked here. In Arcade (mode 0) the PSP then always takes the opponent from
 * the Arcade ladder (0x0882FEB8 writes its character +0x152, +0x173, +0x182, +0x56): the challenger
 * path was cut. With two humans, each fighter keeps its own choice. Other modes have a two-player path
 * of their own; Dramatic and Variable Battle (8, 15) manage these bits themselves. */
void f_0882E380(Cpu *c) {
    uint32_t gs = c->r[4];
    uint32_t mode = RB(gs + 0x72);
    int two = (RB(gs + 0x35) & 3) == 3 && mode != 8 && mode != 15;
    if (two && (RB(gs + 0x39) & 3) != 3) WB(gs + 0x39, RB(gs + 0x39) | 3);
    static const uint32_t keep_off[1] = { 0x152 };
    uint8_t keep[2][1];
    for (int k = 0; k < 2; k++) for (int i = 0; i < 1; i++) keep[k][i] = (uint8_t)RB(FIGHTER(k) + keep_off[i]);
    orig_f_0882E380(c);
    if (two && mode == 0) {
        for (int k = 0; k < 2; k++) for (int i = 0; i < 1; i++) WB(FIGHTER(k) + keep_off[i], keep[k][i]);
        rt_log("arcade: player against player (characters %u and %u)", keep[0][0], keep[1][0]);
    }
}

/* MODE MENU: a mode was chosen (0x0888CD94): players 2 and 3 have to press Start again to join */
void f_0888CD94(Cpu *c) {
    memset(join_to, -1, sizeof join_to);
    orig_f_0888CD94(c);
}

/* Pause (0x08828AF4, a0 = the battle, a1 = a fighter on screen, once a frame for each): Start pressed on
 * the slot in the fighter's +775 pauses, for that player (a0 + 0x59 = the player's bit). Offline, a fighter
 * a pad plays is paused from that pad; in the Variable modes the team (fighters 0 and 1) has one fighter
 * on screen, so its tagged-out teammate's pad can pause too. */
static int variable_mode(void) {
    uint32_t m = RB(0x08E75910u + 0x72);
    return m == 15 || (m == 7 && RB(SURVIVAL_CTX + 0x4F) == 5) || (m == 16 && RB(SURVIVAL_CTX + 0x9F) == 3);
}
static void pause_from(Cpu *c, uint32_t f, int pad) {
    int keep = (int8_t)RB(f + 775);
    WB(f + 775, (uint32_t)pad);
    orig_f_08828AF4(c);
    WB(f + 775, (uint32_t)keep);
}
void f_08828AF4(Cpu *c) {
    uint32_t game = c->r[4], f = c->r[5];
    uint32_t keep[3] = { c->r[4], c->r[5], c->r[6] };
    int pad = hooks_pad_of(f);
    if (pad >= 0 && pad != (int8_t)RB(f + 775)) pause_from(c, f, pad); else orig_f_08828AF4(c);
    if (RB(game + 0x72) == MODE_RDRAMATIC && pad >= 0 && !RB(game + 0x59)) {   /* R.DRAMATIC: a pad playing no fighter pauses too */
        for (int k = 0; k < 3 && !RB(game + 0x59); k++) {
            int used = 0;
            for (int i = 0; i < N_FIGHTERS; i++) if (RB(FIGHTER(i)) == 1 && hooks_pad_of(FIGHTER(i)) == k) used = 1;
            if (!used) { c->r[4] = keep[0]; c->r[5] = keep[1]; c->r[6] = keep[2]; pause_from(c, f, k); }
        }
        c->r[4] = keep[0]; c->r[5] = keep[1]; c->r[6] = keep[2];
    }
    if (!variable_mode() || RB(game + 0x59) || (f != FIGHTER(0) && f != FIGHTER(1))) return;
    uint32_t mate = f == FIGHTER(0) ? FIGHTER(1) : FIGHTER(0);
    int mp = hooks_pad_of(mate);
    if (mp < 0 || mp == pad) return;
    c->r[4] = keep[0]; c->r[5] = keep[1]; c->r[6] = keep[2];
    pause_from(c, f, mp);
}
