/* Player button configs (OPTION MODE > PLAYER CONFIG > P1 - P4 CONFIG) and the L2 / R2 buttons.
 *
 * The game keeps one button config per player in the system data, 20 bytes each from 0x08A058A0
 * (players 1, 2, 3): eight halfwords, the attack given to Square, Triangle, L, Cross, Circle, R,
 * Select and Start (NO USE), then COMMAND (+0x10), TYPE (+0x11) and RAPID (+0x12). An attack is 0-0x36:
 *   0-5 L/M/H punch, L/M/H kick   6 PPP   7 KKK   8 PP   9 KK   10 LP+LK   11 MP+MK   12 HP+HK
 *   13 no use   14 taunt   15-38 SPECIAL1-8 L/M/H   39-53 S.COMBO1-5 LV1-3   54 VARIABLE 1
 * Player 4 is not in the game; its config and every player's L2 and R2 are kept in sfa3mr.cfg
 * (settings.c).
 *
 * How a fighter's buttons are read, once a frame, by 0x08864DFC (a0 = fighter): the held game bits of
 * its slot (0x08E75BA0 + 0x2C * slot + 4; in TRAINING the fighter's +362) go into a held flag per
 * button (0x08B0264E + 16 * player, 8 halfwords for the bits 0x10, 0x20, 0x40, 0x80, 0x100, 0x200,
 * 0x4000 SELECT, 0x8000 START). For each held button the attack in the player's config is turned into
 * game bits (attacks 0-14: the table at 0x08A19782; specials and super combos: the character's lists
 * at 0x08A1B3F8 / 0x08A1B4F4, 8 bytes per entry: bits, move) and kept per button at fighter +1150, for
 * the move to be done; the bits go to fighter +752 (and +764).
 *
 * A PSP has no L2 / R2; here they are two more buttons, each doing the attack set for it, any of the
 * 55. They borrow START's place for that one call: START's bit is set in the held word and START's
 * config entry holds L2's (or R2's) attack; both are put back straight after, so START (pause) is not
 * touched. The fighter's newly pressed buttons (0x08867794: a bit per button, from the slot's new
 * presses, +0xC) get START's bit when L2 / R2 has just been pressed, for the moves that start on a press
 * (one-button specials, read from +1150). With L2 and R2 held together, L2 has that place and R2 adds
 * its game bits (attacks 0-14).
 */
#include "rt.h"
#include "host.h"
#include <string.h>

#define CONFIGS 0x08A058A0u
#define CFG_SZ  20u
uint8_t rt_p4_config[CFG_SZ] = { 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 14, 0, 13, 0, 1, 1, 0, 0 };
const uint8_t pcfg_default[CFG_SZ] = { 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 14, 0, 13, 0, 1, 1, 0, 0 };
int rt_l2[4] = { 6, 6, 6, 6 }, rt_r2[4] = { 7, 7, 7, 7 };   /* PPP, KKK */

/* the config of player p (0-3), in host memory */
uint8_t *pcfg_block(int p) { return p < 3 ? HOST(CONFIGS + CFG_SZ * (uint32_t)p) : rt_p4_config; }

/* the pads: L2 / R2 are taken off (the PSP has no such buttons) and kept here, per pad */
static uint32_t held_l2r2[4];
uint32_t pcfg_pad_to_psp(int pad, uint32_t raw) {
    if (pad >= 0 && pad < 4) held_l2r2[pad] = raw & (PAD_L2 | PAD_R2);
    return raw & ~(PAD_L2 | PAD_R2);
}

#define SLOTS      0x08E75BA0u                /* per-slot input, 0x2C each; +4 held game bits */
#define ALT_CONFIG 0x09523438u                /* the configs 0x08864DFC reads instead in some modes, 0xD0 each */
#define START_CFG  14u                        /* START's entry in a config */
#define BIT_START  0x8000u
#define MODE       0x08E75982u                /* the game mode (2 TRAINING) */
#define HELD       0x08B0264Eu                /* per player: a held flag per button */
#define BASIC_BITS 0x08A19782u                /* game bits of attacks 0-14 */
/* per fighter: L2 / R2 held last time, and newly pressed this frame (for 0x08867794) */
static struct { uint32_t f, prev, fresh; } seen[8];
static int fighter_slot(uint32_t f) {
    for (int i = 0; i < 8; i++) if (seen[i].f == f) return i;
    for (int i = 0; i < 8; i++) if (!seen[i].f) { seen[i].f = f; return i; }
    return 0;
}
int hooks_taken_slot(uint32_t f);
int hooks_pad_of(uint32_t f);
static int fighter_player(uint32_t f) {           /* whose pad: the pad playing it (hooks.c), else 0x08867794's choice */
    int t = hooks_pad_of(f);
    if (t >= 0) return t;
    t = hooks_taken_slot(f);
    if (t >= 0) return t;
    int p = (int8_t)RB(RB(MODE) == 2 ? f + 2 : f + 775);
    return p >= 0 && p < 3 ? p : -1;
}
int hooks_taken_slot(uint32_t f);              /* hooks.c: a fighter handed to another pad */
int hooks_pad_of(uint32_t f);                  /* hooks.c: the pad playing a fighter (offline), or -1 */
static int variable_slot(void) {               /* the slot every fighter reads in the Variable modes, or -1 */
    uint32_t m = RB(MODE);
    int var = m == 15 || (m == 7 && RB(0x092E1228u + 0x4F) == 5) || (m == 16 && RB(0x092E1228u + 0x9F) == 3);
    return var ? ((RB(0x08E75910u + 0x35) & 1) ? 0 : 1) : -1;
}
void f_08864DFC(Cpu *c) {
    uint32_t f = c->r[4];
    { int k = fighter_slot(f), p = fighter_player(f); uint32_t h = p >= 0 ? held_l2r2[p] : 0;
      seen[k].fresh = h & ~seen[k].prev; seen[k].prev = h; }
    uint32_t keep_slot[3]; uint16_t keep_cfg[3], keep_alt[3], keep_own = (uint16_t)RH(f + 362);
    int extra[3] = { -1, -1, -1 };
    for (int p = 0; p < 3; p++) {
        uint32_t sl = SLOTS + 0x2Cu * (uint32_t)p + 4, cf = CONFIGS + CFG_SZ * (uint32_t)p + START_CFG, al = ALT_CONFIG + 0xD0u * (uint32_t)p + START_CFG;
        keep_slot[p] = RW(sl); keep_cfg[p] = (uint16_t)RH(cf); keep_alt[p] = (uint16_t)RH(al);
        if (!held_l2r2[p]) continue;
        int a = (held_l2r2[p] & PAD_L2) ? rt_l2[p] : rt_r2[p];
        if ((held_l2r2[p] & PAD_L2) && (held_l2r2[p] & PAD_R2)) extra[p] = rt_r2[p];
        WW(sl, keep_slot[p] | BIT_START); WH(cf, (uint32_t)a); WH(al, (uint32_t)a);
    }
    int own = (int8_t)RB(f + 2);                  /* TRAINING reads the fighter's own held word */
    if (RB(MODE) == 2 && own >= 0 && own < 3 && held_l2r2[own]) WH(f + 362, keep_own | BIT_START);
    /* a fighter handed to pad 3 (hooks.c): offline the game reads slot 0 for player number 0 and slot 1
       for any other, so pad 3's word goes in slot 1's place for this call */
    int taken = hooks_taken_slot(f), m = RB(MODE);
    /* in the Variable modes (Variable Battle 15, Survival Variable, Free Battle Variable) every fighter reads
       the starter's slot (0 if player 1 is present, else 1: one player plays whoever is in): a fighter a pad
       plays (hooks.c) gets that pad's word in that place instead */
    int vs = variable_slot();
    if (vs >= 0) {
        int p = hooks_pad_of(f);
        if (p >= 0 && p != vs) {
            WW(SLOTS + 0x2Cu * (uint32_t)vs + 4, RW(SLOTS + 0x2Cu * (uint32_t)p + 4));
            /* the button config is the starter's place's too: L2 / R2's attack (in START's entry) goes there */
            WH(CONFIGS + CFG_SZ * (uint32_t)vs + START_CFG, RH(CONFIGS + CFG_SZ * (uint32_t)p + START_CFG));
            WH(ALT_CONFIG + 0xD0u * (uint32_t)vs + START_CFG, RH(ALT_CONFIG + 0xD0u * (uint32_t)p + START_CFG));
            extra[vs] = extra[p];
        }
    }
    else if (taken == 2 && m != 0xD && m != 0x13 && m != 2) WW(SLOTS + 0x2Cu + 4, RW(SLOTS + 0x2Cu * 2 + 4));
    orig_f_08864DFC(c);
    for (int p = 0; p < 3; p++) {
        WW(SLOTS + 0x2Cu * (uint32_t)p + 4, keep_slot[p]);
        WH(CONFIGS + CFG_SZ * (uint32_t)p + START_CFG, keep_cfg[p]); WH(ALT_CONFIG + 0xD0u * (uint32_t)p + START_CFG, keep_alt[p]);
    }
    WH(f + 362, keep_own);
    for (int p = 0; p < 3; p++)                   /* L2 and R2 together: R2's bits too, on the fighter that took L2 */
        if (extra[p] >= 0 && extra[p] < 15 && RH(HELD + 16u * (uint32_t)p + 14) && RH(f + 1150 + 14) == (uint32_t)rt_l2[p]) {
            uint32_t bits = RH(BASIC_BITS + 2u * (uint32_t)extra[p]);
            WH(f + 752, RH(f + 752) | bits); WH(f + 764, RH(f + 764) | bits);
        }
}

/* a fighter's newly pressed buttons, one bit each (bit 7: START), a0 = fighter: L2 / R2 just pressed
   count as START, whose config entry 0x08864DFC filled with their attack (at +1150 + 14) */
int hooks_taken_slot(uint32_t f);              /* hooks.c: a fighter handed to another pad */
void f_08867794(Cpu *c) {
    uint32_t f = c->r[4];
    int taken = hooks_taken_slot(f);
    if (taken < 0 && variable_slot() >= 0) taken = hooks_pad_of(f);   /* (Variable modes: its own pad, not the starter's) */
    if (taken >= 0) {                             /* it reads the new presses of the pad it was given */
        uint32_t b = RW(SLOTS + 0x2Cu * (uint32_t)taken + 0xC) & 0xFFFF, r = 0;
        static const uint16_t bit[8] = { 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x4000, 0x8000 };
        for (int i = 0; i < 8; i++) if (b & bit[i]) r |= 1u << i;
        c->r[2] = r;
    } else orig_f_08867794(c);
    for (int i = 0; i < 8; i++) if (seen[i].f == f && seen[i].fresh) { c->r[2] |= 0x80; break; }
}
