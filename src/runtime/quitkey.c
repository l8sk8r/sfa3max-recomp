/* F2: back to the MODE MENU at once, without saving (keyboard only; no pad has it).
 *
 * The game's tasks are threads; the record of the one running is at *0x08E75C40 (+0 state, +4
 * entry), task 0's at 0x08AD8FE8 runs the game's screens (the MODE MENU is its entry 0x0888C268, a
 * mode replaces it). F2 only asks; task 0 acts the next time it waits for a frame (0x088054F0), the
 * way the game itself leaves:
 *   - a mode: as quitting from the pause menu (0x08828148): stop the sound (0x08814320), then
 *     0x088325DC (a0 = 0x08E75910), which stops the other tasks, resets the game state and restarts
 *     task 0 at the MODE MENU;
 *   - NETWORK (its menus, the lobby, a network battle: 0x08A3E8F8 is set from entering it until its
 *     exit): first the network is closed if it is up (0x088D7EDC, the exit callback's shutdown:
 *     sockets, matching, AdhocTerm, NetTerm), then what NETWORK's own exit does (0x088D7A08): the
 *     players present back from 0x09525DE4, the network task's state cleared with 0x08A3E8F8 (or
 *     entering NETWORK again resumes where it was), the local pad slot back (0x088051A0), the settings
 *     it set aside (0x08815374, 0x088152B0), its memory freed (0x088DD8F4, 0x088DC080), its flag
 *     cleared (0x08804A74). The mode (GAME+0x72) goes back to NETWORK too: a network battle sets
 *     it to 1, and the MODE MENU then waits forever for a sprite cache it cannot fill.
 * The MODE MENU's auto-save is then skipped once (sysdata.c). Nothing happens before the MODE MENU has
 * first been reached, or on the MODE MENU.
 *
 * Restarting a task frees its thread's stack and fiber (kernel.c), so repeated F2 leaks nothing. */
#include "rt.h"

#define CUR_TASK   0x08E75C40u
#define TASK0      0x08AD8FE8u
#define MODE_MENU  0x0888C268u
#define GAME       0x08E75910u
void f_088325DC(Cpu *c);
void f_08814320(Cpu *c); void f_08815374(Cpu *c); void f_088152B0(Cpu *c); void f_088DD8F4(Cpu *c);
void f_088DC080(Cpu *c); void f_08804A74(Cpu *c); void f_088051A0(Cpu *c); void f_088D7EDC(Cpu *c);
void f_0880B57C(Cpu *c); void f_0882134C(Cpu *c);
#define NET_TASK   0x088D8128u

static int requested, menu_seen;
extern int rt_net_session;                  /* hle_net.c */
static void call(Cpu *c, void (*f)(Cpu *), uint32_t a0) { c->r[4] = a0; c->r[31] = 0; f(c); }
#define NET       0x09523980u              /* the network task's state */
#define NET_ON    0x08A3E8F8u              /* set while in NETWORK (its task resumes rather than starts) */
static void leave_network(Cpu *c) {
    if (rt_net_session) call(c, f_088D7EDC, 0);           /* close the network, as the exit callback does */
    call(c, f_0880B57C, 0);
    WH(GAME + 0, 0); WH(GAME + 2, 0); WH(GAME + 4, 0); WH(GAME + 6, 0);
    WB(GAME + 0x89, 1); WB(GAME + 0x92, 0); WB(GAME + 0x39, 0);
    WB(GAME + 0x29, RB(0x09525DE4u)); WB(GAME + 0x35, RB(0x09525DE4u));
    WB(NET + 0x22, 0); WW(NET + 8, 0); WB(NET + 0x3B, 0);
    WB(NET_ON, 0);                                         /* entering NETWORK starts afresh */
    WB(GAME + 0x72, 12);                                   /* the mode: NETWORK */
    call(c, f_0882134C, (uint32_t)((int16_t)RH(NET + 0x32) + RB(NET + 0x37)));
    call(c, f_088051A0, RB(0x08A058A0u + 0x1FF));
    WB(0x08E75B98u, 0);
    call(c, f_08815374, 0); call(c, f_088152B0, 0);
    call(c, f_088DD8F4, 0); call(c, f_088DC080, 0);
    call(c, f_08804A74, 0);
}
int quitkey_nosave;                         /* sysdata.c: skip the next auto-save */

void quitkey_request(void) { requested = 1; }

void f_088054F0(Cpu *c) {
    uint32_t t = RW(CUR_TASK);
    if (t == TASK0) {
        if (RW(TASK0 + 4) == MODE_MENU) { menu_seen = 1; requested = 0; }
        else if (requested && menu_seen) {
            uint32_t task = RW(TASK0 + 4);
            int net = RB(NET_ON) || RB(GAME + 0x72) == 12 || task == NET_TASK || rt_net_session;
            requested = 0; quitkey_nosave = 1;
            rt_log("F2: back to the MODE MENU (no save)%s", net ? ", leaving NETWORK" : "");
            if (net) leave_network(c);
            call(c, f_08814320, 0);
            c->r[5] = 0x092E1228u; call(c, f_088325DC, GAME);   /* restarts task 0: does not come back */
            return;
        }
    } else if (requested && !menu_seen) requested = 0;
    orig_f_088054F0(c);
}
