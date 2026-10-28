/* Local players in the game's network modes.
 *
 * Choose NETWORK on the MODE MENU and any network battle is played on this PC: the other PSPs
 * are virtual players in this process (vpeer.c). Which mode decides how many: VS / TEAM /
 * VARIABLE BATTLE one (player 2), DRAMATIC BATTLE two (players 2 and 3), LEAGUE and TOURNAMENT two.
 * Player 1 is always the host; in Dramatic Battle HOST is chosen by itself.
 *
 * Gamepad 2 plays player 2 and gamepad 3 player 3 when they are plugged in (--p2 / --p3 human|cpu
 * for tests; EX. SETTING's JOIN-IN is for the offline modes only). Without one, that player is the computer: it moves round the select screens by
 * itself and its fighter is computer controlled. Names: PLAYER1, PLAYER2 / CPU2, PLAYER3 / CPU3.
 *
 * After a match the CONTINUE / SELECT / EXIT menu (0x088E9AE0) is the host's: it reads only the
 * local pad, and guests wait for the host's choice (0x088D5E7C checks the network role). So player 1
 * always decides there, whoever won; left alone for 1200 frames it picks EXIT.
 */
#include "rt.h"
#include "net_link.h"
#include <string.h>
#include <stdio.h>

extern uint32_t rt_pad_buttons[4];
extern int rt_human[3];                    /* hooks.c: 1 human, 0 CPU, -1 auto (human if the pad is there) */
int host_pad_connected(int i);
void vpeer_start(int players, int as_children);
void vpeer_stop(void);
void vpeer_frame(void);
int vpeer_count(void);

#define RA_DRAMA_ROLE 0x088D8790u          /* 0x088DD5D0 called from here: Dramatic Battle HOST / GUEST */
#define SLOTS         0x08E75BA0u          /* per-slot input state, 0x2C per slot; +0xC new presses */
#define G_CONFIRM     0x80u                /* the game's own "confirm" bit */
#define FIGHTER(j)    (0x092E3EA0u + 0x498u * (uint32_t)(j))

int netplay_human(int k) {                 /* k = 1, 2: players 2 and 3 */
    if (k < 1 || k > 2) return 0;
    return rt_human[k] >= 0 ? rt_human[k] : host_pad_connected(k);
}
const char *netplay_name(int node) {
    static char b[4][12];
    if (node <= 0) return "PLAYER1";
    if (node > 3) return "";
    snprintf(b[node], sizeof b[node], "%s%d", netplay_human(node) ? "PLAYER" : "CPU", node + 1);
    return b[node];
}

/* ---------------------------------------------------------------- the computer's buttons */
/* On the select screens a computer player wanders the grid and picks: a d-pad tap or Cross every
 * 16 frames, Cross about one time in four. In a fight its fighter is computer controlled instead. */
static uint32_t rng = 0x5FA3C0DEu;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t cpu_held[3];
static long cpu_next[3];
static void cpu_step(int k, long f) {
    if (f < cpu_next[k]) { if (f >= cpu_next[k] - 10) cpu_held[k] = 0; return; }
    static const uint32_t dirs[4] = { 0x10, 0x20, 0x40, 0x80 };     /* up right down left */
    uint32_t r = rnd();
    cpu_held[k] = (r & 3) == 0 ? 0x4000u : dirs[(r >> 2) & 3];
    cpu_next[k] = f + 16;
}
uint32_t netplay_buttons(int k) {
    if (k < 1 || k > 2) return 0;
    return netplay_human(k) ? rt_pad_buttons[k] : cpu_held[k];
}
/* fighters belonging to a computer player: +2 is the player number, +0x175 the computer flag */
static void cpu_fighters(void) {
    if (RB(FIGHTER(0)) != 1) return;
    for (int j = 0; j < 3; j++) {
        uint32_t F = FIGHTER(j);
        int who = RB(F + 2);
        if (RB(F) == 1 && who >= 1 && who <= vpeer_count() && !netplay_human(who) && RB(F + 0x175) == 0) WB(F + 0x175, 1);
    }
}

/* ---------------------------------------------------------------- following the network modes */
static int active;
static void on_group(const char *g) {
    if (!g) { if (active) { vpeer_stop(); active = 0; rt_log("netplay: left the network modes"); } return; }
    int n = 0, children = 0;
    if (!strncmp(g, "DRAMATIC", 8)) { n = 2; children = 1; }
    else if (!strncmp(g, "VERSUS", 6) || !strncmp(g, "TEAM", 4) || !strncmp(g, "VARIABLE", 8)) n = 1;
    else if (!strncmp(g, "LEAGUE", 6) || !strncmp(g, "TOURNA", 6)) n = 2;
    if (!n) return;                        /* the battle's own group: same players */
    vpeer_start(n, children); active = 1;
    rt_log("netplay: lobby %.8s, players 2%s: %s%s%s", g, n > 1 ? " and 3" : "", netplay_name(1),
           n > 1 ? ", " : "", n > 1 ? netplay_name(2) : "");
}

/* Dramatic Battle's HOST / GUEST menu: player 1 hosts (chosen after a moment on screen) */
void f_088DD5D0(Cpu *c) {
    static int wait;
    if (c->r[31] != RA_DRAMA_ROLE) { wait = 0; orig_f_088DD5D0(c); return; }
    if (++wait < 12) { orig_f_088DD5D0(c); return; }
    wait = 0;
    WB(c->r[4] + 0x2E, 0);
    uint32_t keep[3];
    for (int k = 0; k < 3; k++) { keep[k] = RW(SLOTS + 0x2Cu * k + 0xC); WW(SLOTS + 0x2Cu * k + 0xC, k ? 0 : G_CONFIRM); }
    orig_f_088DD5D0(c);
    for (int k = 0; k < 3; k++) WW(SLOTS + 0x2Cu * k + 0xC, keep[k]);
    rt_log("netplay: Dramatic Battle as HOST");
}

void netplay_init(void) { extern void (*rt_net_on_group)(const char *); rt_net_on_group = on_group; }
void netplay_frame(void) {
    if (!active) return;
    long f = (long)rt_vblank_count();
    for (int k = 1; k <= vpeer_count(); k++) if (!netplay_human(k)) cpu_step(k, f);
    vpeer_frame();
    cpu_fighters();
}
