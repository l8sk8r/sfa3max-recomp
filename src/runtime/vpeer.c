/* Virtual players: the other PSPs of a network battle, simulated inside this process, so the
 * network modes are played locally. Player 2 is node 1, player 3 node 2; their buttons come from
 * netplay_buttons() (gamepads 2 and 3, or the CPU stand-in when a pad is missing).
 *
 * The game's network battles are lockstep: every PSP runs the whole game and only buttons travel.
 * What a real second PSP sends was recorded between two linked instances (SFA3R_NETCAP=1) and is
 * reproduced here, answering the player's instance message by message:
 *
 *   lobby      beacons and matching HELLO / REQUEST, as a guest that wants to play
 *   streams    PTP on port 1025. VS: the guest connects to the host. Dramatic Battle: a ring,
 *              node 1 connects to the host and the host connects to node 2.
 *   0x1AC3C501 sent once by the side that accepted a stream
 *   228 bytes  each side's settings block, passed round the ring; we answer with the host's own
 *   0xE198DE71 / 0x53201336 / 0x7F035D72   sync request / answer / go. Who asks alternates, so a
 *              silent stream gets a request from us (the game ignores one it did not expect)
 *   0x1CE14355 a marker both sides send; echoed
 *   other words lockstep input: high half = frame counter, low half = the PSP buttons. We answer
 *              each word with the same counter and this player's gamepad
 *   PDP 1026   Dramatic Battle roll call: [AC 00 sum 94 type 00 step 00]; we report the host's step,
 *              then the next one, as guests do.
 */
#include "rt.h"
#include "net_link.h"
#include <string.h>
#include <stdlib.h>

uint32_t netplay_buttons(int k);                /* netplay.c */

#define W_HELLO 0x1AC3C501u
#define W_SYNC  0xE198DE71u
#define W_ACK   0x53201336u
#define W_GO    0x7F035D72u
#define W_MARK  0x1CE14355u
enum { M_HELLO = 1, M_REQUEST, M_ACCEPT, M_CANCEL, M_DENY, M_BYE };
typedef struct { int16_t sock, peer_sock; uint16_t sport, dport; } PtpHdr;

typedef struct {
    int on;
    char group[8];
    int heard_host, matched; long request_at; int match_port;
    int host_sock, my_sock, linked, host_is_client; long syn_at;
    long last_rx, sync_at; int asked;                       /* frame of the host's last data; sync asked since */
    int pdp_step, pdp_seen, pdp_hport;
} VP;
static VP vp[3];
static long host_rx;                           /* frame of the host's last data on any stream */
static int nvp, blocks, parent;            /* parent: Dramatic Battle (the host is the parent) */                        /* blocks: settings exchanged, streams in use */
static long now_f(void) { return (long)rt_vblank_count(); }

static void put_ptp(VP *v, int k, const uint8_t *p, size_t n) {
    uint8_t b[512]; PtpHdr h = { (int16_t)v->my_sock, (int16_t)v->host_sock, 1025, 0 };
    if (n > sizeof b - sizeof h) return;
    memcpy(b, &h, sizeof h); memcpy(b + sizeof h, p, n);
    net_link_inject(k, NL_PTP_DATA, b, sizeof h + n);
}
static void put_word(VP *v, int k, uint32_t w) { put_ptp(v, k, (const uint8_t *)&w, 4); }
static void put_match(VP *v, int k, int kind) {
    uint8_t b[16]; memcpy(b, v->group, 8);
    b[8] = (uint8_t)kind; b[9] = (uint8_t)v->match_port; b[10] = (uint8_t)(v->match_port >> 8); b[11] = 4;
    memset(b + 12, 0, 4);
    net_link_inject(k, NL_MATCH, b, 16);
}

static void on_data(int k, const uint8_t *p, size_t n) {
    VP *v = &vp[k];
    v->last_rx = host_rx = now_f(); v->asked = 0;
    if (n == 228) {                                /* settings block: the answer comes round the ring */
        int r = k;
        blocks++;
        for (int j = 1; j <= nvp; j++) if (vp[j].linked && vp[j].host_is_client) r = j;
        put_ptp(&vp[r], r, p, n);
        return;
    }
    uint8_t out[512]; size_t m = 0;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        uint32_t w; memcpy(&w, p + i, 4);
        uint32_t a;
        if (w == W_SYNC) a = W_ACK;
        else if (w == W_ACK) a = W_GO;
        else if (w == W_GO || w == W_HELLO) continue;
        else if (w == W_MARK) { a = W_MARK; v->sync_at = now_f() + 3; }   /* after the marker the guests ask first */
        else a = (w & 0xFFFF0000u) | (netplay_buttons(k) & 0xFFFFu);
        if (m + 4 <= sizeof out) { memcpy(out + m, &a, 4); m += 4; }
    }
    if (m) put_ptp(v, k, out, m);
}

static void on_pdp(int k, const uint8_t *p, size_t n) {
    VP *v = &vp[k];
    if (n < 4 + 8) return;
    int hport = p[2] | p[3] << 8;                  /* the host's source port */
    const uint8_t *d = p + 4;
    if (n - 4 != 8 || d[0] != 0xAC || d[3] != 0x94) return;   /* the member list needs no answer */
    int type = d[4], step = d[6];
    if (type != 7) return;
    if (step > v->pdp_step) v->pdp_step = step;
    if (++v->pdp_seen >= 4 && v->pdp_step == step && step < 1) v->pdp_step = step + 1;   /* guests move on first */
    uint8_t r[12] = { (uint8_t)hport, (uint8_t)(hport >> 8), 0x02, 0x04, 0xAC, 0x00, 0, 0x94, (uint8_t)type, 0, (uint8_t)v->pdp_step, 0 };
    r[6] = (uint8_t)(0xA3 + type + v->pdp_step);
    v->pdp_hport = hport;
    net_link_inject(k, NL_PDP, r, sizeof r);
}

/* frames the player's instance sends to virtual node k */
static void vsend(int k, int type, const uint8_t *p, size_t n) {
    if (k < 1 || k > nvp) return;
    VP *v = &vp[k];
    switch (type) {
    case NL_BEACON:
        if (n >= 8) {
            if (memcmp(v->group, p, 8)) memcpy(v->group, p, 8);     /* lobby, then the battle's own group */
            net_link_inject(k, NL_BEACON, v->group, 8);
        }
        break;
    case NL_MATCH: {
        if (n < 12) break;
        int kind = p[8];
        v->match_port = p[9] | p[10] << 8;
        if (memcmp(v->group, p, 8)) memcpy(v->group, p, 8);
        if (kind == M_HELLO) {
            if (!parent) put_match(v, k, M_HELLO);   /* VS lobbies list each other; Dramatic guests only ask to join */
            if (!v->heard_host) { v->heard_host = 1; v->request_at = now_f() + 30 + 45 * (k - 1); }
        } else if (kind == M_ACCEPT) { v->matched = 1; v->request_at = -1; rt_log("vpeer %d: matched", k); }
        else if (kind == M_DENY || kind == M_CANCEL) { v->matched = 0; v->request_at = now_f() + 60 + 30 * k; }
        /* M_BYE: the lobby closes once the battle is agreed; we stay paired */
        break;
    }
    case NL_PTP_SYN: {                             /* the host connects to us */
        PtpHdr h; if (n < sizeof h) break; memcpy(&h, p, sizeof h);
        v->host_sock = h.sock; v->linked = 1; v->host_is_client = 1; v->last_rx = now_f(); v->asked = 0;
        PtpHdr a = { (int16_t)v->my_sock, h.sock, h.dport, h.sport };
        net_link_inject(k, NL_PTP_SYNACK, &a, sizeof a);
        put_word(v, k, W_HELLO);
        rt_log("vpeer %d: host connected to us", k);
        break;
    }
    case NL_PTP_SYNACK: {
        PtpHdr h; if (n < sizeof h) break; memcpy(&h, p, sizeof h);
        v->host_sock = h.sock; v->linked = 1; v->host_is_client = 0; v->last_rx = now_f(); v->asked = 0;
        rt_log("vpeer %d: connected to the host", k);
        break;
    }
    case NL_PTP_RST: v->syn_at = now_f() + 20; break;
    case NL_PTP_FIN: v->linked = 0; break;
    case NL_PTP_DATA: if (n > sizeof(PtpHdr)) on_data(k, p + sizeof(PtpHdr), n - sizeof(PtpHdr)); break;
    case NL_PDP: on_pdp(k, p, n); break;
    default: break;
    }
}

/* After joining the battle's own group the game waits about 10 seconds (15 in Dramatic Battle)
 * before opening its streams, to give other PSPs time to arrive. Ours are already there: the wait
 * (a frame counter at 0x09523984, in the network menus' context) is cut short until the settings
 * have been exchanged. */
#define NET_WAIT 0x09523984u
static void shorten_waits(void) {
    if (blocks) return;
    for (int k = 1; k <= nvp; k++) if (!vp[k].matched) return;
    if (!memcmp(vp[1].group, "VERSUS", 6)) return;      /* (the VS lobby moves on by itself) */
    int16_t t = (int16_t)RH(NET_WAIT);
    if (t > 20 && t < 3000) WH(NET_WAIT, 20);
}

/* once a frame */
void vpeer_frame(void) {
    long f = now_f();
    if (nvp) shorten_waits();
    for (int k = 1; k <= nvp; k++) {
        VP *v = &vp[k];
        if (v->group[0] && f % 18 == k) net_link_inject(k, NL_BEACON, v->group, 8);   /* we are in that group too */
        if (v->heard_host && !v->matched && v->request_at >= 0 && f >= v->request_at) {
            put_match(v, k, M_REQUEST); v->request_at = f + 240;     /* asks again if nothing happens */
        }
        /* node 1 connects to the host's stream port once matched (node 2 is connected to) */
        if (v->matched && !v->linked && k == 1 && f >= v->syn_at) {
            PtpHdr h = { (int16_t)v->my_sock, 0, 50000, 1025 };
            net_link_inject(k, NL_PTP_SYN, &h, sizeof h);
            v->syn_at = f + 30;
        }
        if (v->linked && v->sync_at && f >= v->sync_at) { v->sync_at = 0; put_word(v, k, W_SYNC); v->asked = 1; }
        if (v->linked && blocks && !v->asked && f - v->last_rx > 60 && f - host_rx > 60) { put_word(v, k, W_SYNC); v->asked = 1; }
    }
}

void vpeer_start(int players, int as_children) {   /* players: 1 or 2 virtual players */
    memset(vp, 0, sizeof vp);
    nvp = players; blocks = 0; parent = as_children;
    for (int k = 1; k <= nvp; k++) { vp[k].my_sock = 20 + k; vp[k].request_at = -1; vp[k].on = 1; }
    net_link_virtual(nvp, vsend);
    rt_log("vpeer: %d virtual player(s)", nvp);
}
void vpeer_stop(void) { nvp = 0; net_link_virtual(0, NULL); memset(vp, 0, sizeof vp); }
int vpeer_count(void) { return nvp; }
