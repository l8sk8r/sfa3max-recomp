/* PSP ad-hoc wireless (sceNetAdhoc, sceNetAdhocctl, sceNetAdhocMatching), emulated between game
 * instances on this PC: one process per player, joined by net_link (node 0 is the player's own window).
 *
 * The semantics follow PPSSPP's implementation (GPL-2.0-or-later, https://github.com/hrydgard/ppsspp):
 *  - adhocctl: groups are announced by periodic beacons; Scan lists the groups heard recently.
 *  - matching (P2P): HELLO broadcast with the game's hello data; SelectTarget sends a REQUEST, or
 *    accepts one; the requester then gets ACCEPT and ESTABLISHED, the accepter ESTABLISHED.
 *  - PTP: reliable streams (SYN / SYNACK / DATA / FIN) over the links; PDP: datagrams.
 * Handlers run on a native "ad-hoc library" thread, as on the PSP.
 */
#include "rt.h"
#include "net_link.h"
#include <string.h>

#define WOULD_BLOCK      0x80410709u
#define ERR_DISCONNECTED 0x8041070Cu
#define ERR_TIMEOUT      0x80410715u
#define ERR_REFUSED      0x80410718u
#define ERR_BAD_ID       0x80410701u
#define ERR_NOT_CONNECTED 0x8041070Bu

int rt_net_log = 1;
#define NLOG(...) do { if (rt_net_log) rt_log("net: " __VA_ARGS__); } while (0)

static int me(void) { return net_link_active() ? net_link_self() : 0; }
static void mac_of(int node, uint8_t m[6]) { m[0] = 0x02; m[1] = 0x53; m[2] = 0x46; m[3] = 0x41; m[4] = 0x33; m[5] = (uint8_t)(0x01 + node); }
static int node_of(const uint8_t *m) {
    if (m[0] == 0xFF && m[1] == 0xFF) return 0xFF;
    if (m[0] != 0x02 || m[1] != 0x53 || m[2] != 0x46 || m[3] != 0x41 || m[4] != 0x33) return -1;
    return m[5] - 1;
}

/* guest-memory scratch for handler arguments (a ring, so a handler's buffers stay valid) */
static uint32_t scratch;
static int scratch_pos;
static uint32_t scratch_put(const void *p, size_t n) {
    if (!scratch) scratch = rt_alloc(0x4000, 1, 64, "adhoc");
    if (scratch_pos + n + 16 > 0x4000) scratch_pos = 0;
    uint32_t a = scratch + (uint32_t)scratch_pos;
    if (n) memcpy(HOST(a), p, n);
    scratch_pos += (int)((n + 15) & ~15u);
    return a;
}

static void netcap(const char *what, int sock, uint32_t ra, const uint8_t *p, uint32_t n);
void (*rt_net_on_group)(const char *name);         /* joined a group (NULL: network mode left) */
/* ---------------------------------------------------------------- handler events */
typedef struct { uint32_t func, a0, a1, a2, a3, a4; } Event;
static Event events[64];
static int nevents;
static void queue(uint32_t func, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    if (func && nevents < 64) { Event *e = &events[nevents++]; e->func = func; e->a0 = a0; e->a1 = a1; e->a2 = a2; e->a3 = a3; e->a4 = a4; }
}

/* ---------------------------------------------------------------- adhocctl */
static uint32_t ctl_handler, ctl_handler_arg;
static int ctl_state;                              /* 0 disconnected, 1 connected */
static char group[9];
typedef struct { char name[9]; int node; uint64_t heard; } Heard;
static Heard heard[8];
static uint64_t last_beacon;

static void ctl_event(int ev) { queue(ctl_handler, (uint32_t)ev, 0, ctl_handler_arg, 0, 0); }

/* ---------------------------------------------------------------- matching */
enum { P_NONE, P_KNOWN, P_IN_REQ, P_OUT_REQ, P_EST };
typedef struct { int state; uint64_t last_hello; } MPeer;
typedef struct {
    uint32_t id; int mode, maxnum, port; uint32_t handler; int running;
    uint8_t hello[256]; int hello_len; uint64_t hello_int, last_hello;
    MPeer peers[4];
} Matching;
static Matching mt[4];
static int nmt;
static uint32_t next_id = 1;
enum { M_HELLO = 1, M_REQUEST, M_ACCEPT, M_CANCEL, M_DENY, M_BYE };
static Matching *mt_get(uint32_t id) { for (int i = 0; i < nmt; i++) if (mt[i].id == id) return &mt[i]; return NULL; }
static Matching *mt_port(int port) { for (int i = 0; i < nmt; i++) if (mt[i].port == port && mt[i].running) return &mt[i]; return NULL; }

static void m_send(Matching *m, int dst, int kind, const uint8_t *opt, int optlen) {
    uint8_t b[300]; int n = 0;
    memcpy(b, group, 8); n = 8;
    b[n++] = (uint8_t)kind; b[n++] = (uint8_t)(m->port & 0xFF); b[n++] = (uint8_t)(m->port >> 8);
    b[n++] = (uint8_t)(optlen & 0xFF);
    if (optlen > 0 && optlen <= 256) { memcpy(b + n, opt, (size_t)optlen); n += optlen; }
    net_link_send(dst, NL_MATCH, b, (size_t)n);
}
static void m_event(Matching *m, int ev, int node, const uint8_t *opt, int optlen) {
    uint8_t mac[6]; mac_of(node, mac);
    uint32_t ma = scratch_put(mac, 8);
    uint32_t oa = optlen > 0 ? scratch_put(opt, (size_t)optlen) : 0;
    if (optlen > 0) netcap("match-opt", ev, (uint32_t)node, opt, (uint32_t)optlen);
    queue(m->handler, m->id, (uint32_t)ev, ma, (uint32_t)optlen, oa);
}
void (*rt_net_on_request)(void);                   /* automation: a battle request arrived (node 0 accepts it) */
void (*rt_net_on_hello)(int node);                 /* automation: a peer appeared in the lobby */
void (*rt_net_on_denied)(void);                    /* automation: our request was refused or cancelled */
int rt_net_established;                            /* matching connections made */
int rt_net_requested;                              /* we sent a battle request */
int rt_net_parent;                                 /* a PARENT matching context (Dramatic Battle host) is running */

static void on_match(int src, const uint8_t *p, size_t n) {
    int kind = n > 8 ? p[8] : -1, port = n > 10 ? (p[9] | p[10] << 8) : -1, optlen = n > 11 ? p[11] : 0;
    const uint8_t *opt = p + 12;
    Matching *m = mt_port(port);
    if (n < 12 || memcmp(p, group, 8) != 0 || ctl_state != 1 || !m || src < 0 || src > 3) {
        static int dropped; if (dropped++ < 10) NLOG("dropped match packet from %d: kind %d port %d group \"%.8s\" (ours \"%s\", state %d, matching %s)", src, kind, port, p, group, ctl_state, m ? "yes" : "no");
        return;
    }
    MPeer *pe = &m->peers[src];
    switch (kind) {
    case M_HELLO:
        {   /* like the PSP, every hello raises an event, unless a request is in progress or we are matched */
            int any_est = 0;
            for (int i = 0; i < 4; i++) if (m->peers[i].state == P_EST) any_est = 1;
            int first = pe->state == P_NONE;
            if (first) pe->state = P_KNOWN;
            if (pe->state == P_KNOWN && !any_est) m_event(m, 1, src, opt, optlen);
            if (first && rt_net_on_hello) rt_net_on_hello(src);
        }
        pe->last_hello = rt_now_us();
        break;
    case M_REQUEST:
        if (pe->state == P_EST) break;
        if (pe->state == P_NONE) m_event(m, 1, src, NULL, 0);
        pe->state = P_IN_REQ; m_event(m, 2, src, opt, optlen);
        if (rt_net_on_request) rt_net_on_request();
        break;
    case M_ACCEPT:
        if (pe->state == P_OUT_REQ) { pe->state = P_EST; rt_net_established++; m_event(m, 6, src, opt, optlen); m_event(m, 7, src, NULL, 0); }
        break;
    case M_CANCEL: if (pe->state == P_IN_REQ) { pe->state = P_KNOWN; m_event(m, 5, src, opt, optlen); } break;
    case M_DENY:   if (pe->state == P_OUT_REQ) { pe->state = P_KNOWN; rt_net_requested = 0; m_event(m, 4, src, opt, optlen); if (rt_net_on_denied) rt_net_on_denied(); } break;
    case M_BYE:    if (pe->state != P_NONE) { pe->state = P_NONE; m_event(m, 10, src, NULL, 0); } break;
    }
}

/* ---------------------------------------------------------------- PTP / PDP sockets */
typedef struct {
    int used, kind;                                /* 1 PTP connection, 2 PTP listener, 3 PDP */
    int lport, peer, pport, peer_sock;
    int state;                                     /* PTP: 0 closed, 1 SYN sent, 2 established, 3 peer closed, 4 refused */
    uint8_t *rx; size_t rxn, rxcap;                /* PTP stream / PDP datagrams ([node][port lo][port hi][len lo][len hi] data) */
    uint8_t *tx; size_t txn;                       /* PTP bytes sent before the SYNACK */
    int pend[8][3], npend;                         /* listener: pending (node, port, sock) */
} Sock;
#define MAXSOCK 32
static Sock socks[MAXSOCK];
static Sock *sock_get(uint32_t id) { return id >= 1 && id < MAXSOCK && socks[id].used ? &socks[id] : NULL; }
static int sock_new(int kind) { for (int i = 1; i < MAXSOCK; i++) if (!socks[i].used) { memset(&socks[i], 0, sizeof socks[i]); socks[i].used = 1; socks[i].kind = kind; return i; } return 0; }
static void rx_add(Sock *s, const void *p, size_t n) {
    if (s->rxn + n > s->rxcap) { s->rxcap = (s->rxn + n) * 2 + 256; s->rx = (uint8_t *)realloc(s->rx, s->rxcap); }
    memcpy(s->rx + s->rxn, p, n); s->rxn += n;
}
static void rx_take(Sock *s, size_t n) { memmove(s->rx, s->rx + n, s->rxn - n); s->rxn -= n; }
static int next_eport = 50000;

typedef struct { int16_t sock, peer_sock; uint16_t sport, dport; } PtpHdr;
static void ptp_send_ctl(int node, int type, int sock, int peer_sock, int sport, int dport) {
    PtpHdr h = { (int16_t)sock, (int16_t)peer_sock, (uint16_t)sport, (uint16_t)dport };
    net_link_send(node, type, &h, sizeof h);
}

static void on_ptp(int src, int type, const uint8_t *p, size_t n) {
    if (n < sizeof(PtpHdr)) return;
    PtpHdr h; memcpy(&h, p, sizeof h);
    switch (type) {
    case NL_PTP_SYN:
        for (int i = 1; i < MAXSOCK; i++) {
            Sock *l = &socks[i];
            if (l->used && l->kind == 2 && l->lport == h.dport && l->npend < 8) {
                l->pend[l->npend][0] = src; l->pend[l->npend][1] = h.sport; l->pend[l->npend][2] = h.sock; l->npend++;
                return;
            }
        }
        ptp_send_ctl(src, NL_PTP_RST, 0, h.sock, h.dport, h.sport);
        break;
    case NL_PTP_SYNACK: {
        Sock *s = sock_get((uint32_t)h.peer_sock);
        if (s && s->kind == 1 && s->state == 1) {
            s->peer_sock = h.sock; s->state = 2;
            if (s->txn) {
                uint8_t *b = (uint8_t *)malloc(sizeof(PtpHdr) + s->txn);
                PtpHdr d = { (int16_t)(s - socks), (int16_t)s->peer_sock, (uint16_t)s->lport, (uint16_t)s->pport };
                memcpy(b, &d, sizeof d); memcpy(b + sizeof d, s->tx, s->txn);
                net_link_send(s->peer, NL_PTP_DATA, b, sizeof d + s->txn);
                free(b); free(s->tx); s->tx = NULL; s->txn = 0;
            }
        }
        break;
    }
    case NL_PTP_RST: { Sock *s = sock_get((uint32_t)h.peer_sock); if (s && s->kind == 1) s->state = 4; break; }
    case NL_PTP_DATA: { Sock *s = sock_get((uint32_t)h.peer_sock); if (s && s->kind == 1) rx_add(s, p + sizeof h, n - sizeof h); break; }
    case NL_PTP_FIN: { Sock *s = sock_get((uint32_t)h.peer_sock); if (s && s->kind == 1) s->state = 3; break; }
    }
}
static void on_pdp(int src, const uint8_t *p, size_t n) {
    if (n < 4) return;
    int dport = p[0] | p[1] << 8, sport = p[2] | p[3] << 8;
    for (int i = 1; i < MAXSOCK; i++) {
        Sock *s = &socks[i];
        if (s->used && s->kind == 3 && s->lport == dport) {
            uint8_t hd[5] = { (uint8_t)src, (uint8_t)sport, (uint8_t)(sport >> 8), (uint8_t)((n - 4) & 0xFF), (uint8_t)((n - 4) >> 8) };
            if (s->rxn < 64 * 1024) { rx_add(s, hd, 5); rx_add(s, p + 4, n - 4); }
            return;
        }
    }
}

/* ---------------------------------------------------------------- link traffic */
void (*rt_net_on_pad)(const uint8_t *p, size_t n);
void (*rt_net_on_cmd)(const uint8_t *p, size_t n);
static int rx_count[16];
void rt_net_report(void) { rt_log("net: received by type: beacon %d match %d syn %d synack %d data %d pdp %d pad %d", rx_count[NL_BEACON], rx_count[NL_MATCH], rx_count[NL_PTP_SYN], rx_count[NL_PTP_SYNACK], rx_count[NL_PTP_DATA], rx_count[NL_PDP], rx_count[NL_PAD]); }
static void on_frame(int src, int type, const uint8_t *p, size_t n) {
    if (type < 16) rx_count[type]++;
    switch (type) {
    case NL_BEACON:
        if (n >= 8) {
            int slot = -1;
            for (int i = 0; i < 8; i++) if (heard[i].node == src && heard[i].heard) slot = i;
            if (slot < 0) for (int i = 0; i < 8; i++) if (!heard[i].heard) { slot = i; break; }
            if (slot >= 0) { memset(heard[slot].name, 0, 9); memcpy(heard[slot].name, p, 8); heard[slot].node = src; heard[slot].heard = rt_now_us() | 1; }
        }
        break;
    case NL_MATCH: on_match(src, p, n); break;
    case NL_PTP_SYN: case NL_PTP_SYNACK: case NL_PTP_RST: case NL_PTP_DATA: case NL_PTP_FIN: on_ptp(src, type, p, n); break;
    case NL_PDP: on_pdp(src, p, n); break;
    case NL_PAD: if (rt_net_on_pad) rt_net_on_pad(p, n); break;
    case NL_CMD: if (rt_net_on_cmd) rt_net_on_cmd(p, n); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- the library thread */
static void tick(void) {
    net_link_poll();
    uint64_t now = rt_now_us();
    if (ctl_state == 1 && now - last_beacon > 300000) { last_beacon = now; net_link_send(0xFF, NL_BEACON, group, 8); }
    for (int i = 0; i < nmt; i++) {
        Matching *m = &mt[i];
        if (!m->running) continue;
        uint64_t iv = m->hello_int < 200000 ? 200000 : m->hello_int > 1000000 ? 1000000 : m->hello_int;
        if (m->mode == 2) continue;                      /* children never announce themselves */
        if (now - m->last_hello > iv) { m->last_hello = now; m_send(m, 0xFF, M_HELLO, m->hello, m->hello_len); }
    }
}
static void deliver(Cpu *c) {
    while (nevents) {
        Event e = events[0];
        memmove(events, events + 1, (size_t)(--nevents) * sizeof(Event));
        NLOG("-> handler %08X(%X, %X, %X, %X, %X)", e.func, e.a0, e.a1, e.a2, e.a3, e.a4);
        Cpu save = *c;
        c->r[7] = e.a3; c->r[8] = e.a4;
        rt_call_guest(c, e.func, e.a0, e.a1, e.a2);
        *c = save;
    }
}
static void library_thread(Cpu *c) { for (;;) { tick(); deliver(c); rt_delay(c, 2000); } }
static void start_thread(int prio) {
    static int started;
    if (!started) { started = 1; rt_thread_create_native("SceNetAdhocctl", library_thread, prio ? prio : 48); }
}
void rt_net_init(void) { net_link_set_handler(on_frame); }
/* poll the links from the frame loop too, so pads and commands arrive before the game needs them */
void rt_net_frame(void) { if (net_link_active()) net_link_poll(); }

/* wait helper for blocking calls: returns 1 when cond() became true */
static int wait_until(Cpu *c, int (*cond)(void *), void *arg, uint32_t timeout_us) {
    uint64_t start = rt_now_us();
    for (;;) {
        tick();
        if (cond(arg)) return 1;
        if (timeout_us && rt_now_us() - start >= timeout_us) return 0;
        rt_delay(c, 1000);
    }
}

/* ---------------------------------------------------------------- sceNet / sceNetAdhoc */
/* OPTION MODE > EX. SETTING > COUCH CO-OP off: the network is a stub, as in the first recompile:
 * every ad hoc call fails (0x80410001), so the game finds no wireless network. */
extern int rt_couch_coop;                    /* settings.c */
#define STUB_UNLESS_COOP(n) do { if (!rt_couch_coop) { TRACE(#n " (couch co-op off)"); RET(0x80410001); } } while (0)
void hle_sceNetInit(Cpu *c) { RET(0); }
void hle_sceNetTerm(Cpu *c) { RET(0); }
void hle_sceNetFreeThreadinfo(Cpu *c) { RET(0); }
void hle_sceNetGetLocalEtherAddr(Cpu *c) { uint8_t m[6]; mac_of(me(), m); memcpy(HOST(ARG(0)), m, 6); RET(0); }
int rt_net_session;                         /* the game's network is up (AdhocInit .. AdhocTerm): quitkey.c */
void hle_sceNetAdhocInit(Cpu *c) { rt_net_session = 1; STUB_UNLESS_COOP(sceNetAdhocInit); NLOG("AdhocInit"); RET(0); }
void hle_sceNetAdhocTerm(Cpu *c) { rt_net_session = 0; STUB_UNLESS_COOP(sceNetAdhocTerm); NLOG("AdhocTerm"); if (rt_net_on_group) rt_net_on_group(NULL); RET(0); }

/* protocol capture (SFA3MR_NETCAP=1): every packet the game sends or takes, with the caller */
static void netcap(const char *what, int sock, uint32_t ra, const uint8_t *p, uint32_t n) {
    static int on = -1; if (on < 0) on = getenv("SFA3MR_NETCAP") != NULL;
    if (!on) return;
    char hx[3 * 256 + 1]; uint32_t k = n < 256 ? n : 256;
    for (uint32_t i = 0; i < k; i++) snprintf(hx + 3 * i, 4, "%02X ", p[i]);
    hx[3 * k] = 0;
    rt_log("cap %s s%d ra=%08X f=%llu n=%u: %s", what, sock, ra, (unsigned long long)rt_vblank_count(), n, hx);
}
void hle_sceNetAdhocPdpCreate(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPdpCreate);
    int id = sock_new(3); if (!id) RET(0x8041070F);
    socks[id].lport = (int)ARG(1);
    NLOG("PdpCreate(port=%u) -> %d", ARG(1), id);
    RET(id);
}
void hle_sceNetAdhocPdpSend(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPdpSend);
    Sock *s = sock_get(ARG(0)); if (!s) RET(ERR_BAD_ID);
    int dst = node_of(HOST(ARG(1))); uint32_t len = ARG(4);
    uint8_t *b = (uint8_t *)malloc(len + 4);
    b[0] = (uint8_t)ARG(2); b[1] = (uint8_t)(ARG(2) >> 8); b[2] = (uint8_t)s->lport; b[3] = (uint8_t)(s->lport >> 8);
    memcpy(b + 4, HOST(ARG(3)), len);
    netcap("pdp-tx", (int)ARG(0), c->r[31], b + 4, len);
    if (dst >= 0) net_link_send(dst, NL_PDP, b, len + 4);
    free(b);
    RET(0);
}
static int pdp_ready(void *a) { return ((Sock *)a)->rxn > 0; }
void hle_sceNetAdhocPdpRecv(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPdpRecv);
    Sock *s = sock_get(ARG(0)); if (!s) RET(ERR_BAD_ID);
    uint32_t macp = ARG(1), portp = ARG(2), buf = ARG(3), lenp = ARG(4), timeout = ARG(5), nb = ARG(6);
    tick();
    if (!s->rxn) { if (nb || !wait_until(c, pdp_ready, s, timeout)) RET(nb ? WOULD_BLOCK : ERR_TIMEOUT); }
    int src = s->rx[0], sport = s->rx[1] | s->rx[2] << 8, n = s->rx[3] | s->rx[4] << 8;
    uint32_t cap = lenp ? RW(lenp) : 0;
    if (macp) { uint8_t m[6]; mac_of(src, m); memcpy(HOST(macp), m, 6); }
    if (portp) WH(portp, (uint32_t)sport);
    uint32_t k = (uint32_t)n < cap ? (uint32_t)n : cap;
    if (buf && k) memcpy(HOST(buf), s->rx + 5, k);
    netcap("pdp-rx", (int)ARG(0), c->r[31], s->rx + 5, k);
    if (lenp) WW(lenp, k);
    rx_take(s, 5 + (size_t)n);
    RET(0);
}
void hle_sceNetAdhocPdpDelete(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPdpDelete); Sock *s = sock_get(ARG(0)); if (s) { free(s->rx); s->used = 0; } RET(0); }

void hle_sceNetAdhocPtpOpen(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpOpen);
    int id = sock_new(1); if (!id) RET(0x8041070F);
    Sock *s = &socks[id];
    s->lport = ARG(1) ? (int)ARG(1) : next_eport++;
    s->peer = node_of(HOST(ARG(2))); s->pport = (int)ARG(3);
    NLOG("PtpOpen(sport=%d, peer node %d, dport=%d) -> %d", s->lport, s->peer, s->pport, id);
    RET(id);
}
static int ptp_connected(void *a) { Sock *s = (Sock *)a; return s->state >= 2; }
void hle_sceNetAdhocPtpConnect(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpConnect);
    Sock *s = sock_get(ARG(0)); if (!s || s->kind != 1) RET(ERR_BAD_ID);
    if (s->state == 0) { s->state = 1; ptp_send_ctl(s->peer, NL_PTP_SYN, (int)ARG(0), 0, s->lport, s->pport); }
    if (s->state == 2) RET(0);
    if (s->state == 4) RET(ERR_REFUSED);
    if (ARG(2)) RET(0);                            /* non-blocking: treated as connected; data waits for the SYNACK */
    if (!wait_until(c, ptp_connected, s, ARG(1))) RET(ERR_TIMEOUT);
    RET(s->state == 2 ? 0 : ERR_REFUSED);
}
void hle_sceNetAdhocPtpListen(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpListen);
    int id = sock_new(2); if (!id) RET(0x8041070F);
    socks[id].lport = (int)ARG(1);
    NLOG("PtpListen(port=%u) -> %d", ARG(1), id);
    RET(id);
}
static int has_pending(void *a) { return ((Sock *)a)->npend > 0; }
void hle_sceNetAdhocPtpAccept(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpAccept);
    Sock *l = sock_get(ARG(0)); if (!l || l->kind != 2) RET(ERR_BAD_ID);
    uint32_t macp = ARG(1), portp = ARG(2), timeout = ARG(3), nb = ARG(4);
    tick();
    if (!l->npend) { if (nb || !wait_until(c, has_pending, l, timeout)) RET(nb ? WOULD_BLOCK : ERR_TIMEOUT); }
    int node = l->pend[0][0], pport = l->pend[0][1], psock = l->pend[0][2];
    memmove(l->pend, l->pend + 1, sizeof l->pend[0] * (size_t)(--l->npend));
    int id = sock_new(1); if (!id) RET(0x8041070F);
    Sock *s = &socks[id];
    s->lport = l->lport; s->peer = node; s->pport = pport; s->peer_sock = psock; s->state = 2;
    ptp_send_ctl(node, NL_PTP_SYNACK, id, psock, s->lport, pport);
    if (macp) { uint8_t m[6]; mac_of(node, m); memcpy(HOST(macp), m, 6); }
    if (portp) WH(portp, (uint32_t)pport);
    NLOG("PtpAccept -> %d (node %d port %d)", id, node, pport);
    RET(id);
}
void hle_sceNetAdhocPtpSend(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpSend);
    Sock *s = sock_get(ARG(0)); if (!s || s->kind != 1) RET(ERR_BAD_ID);
    uint32_t len = ARG(2) ? RW(ARG(2)) : 0;
    netcap("ptp-tx", (int)ARG(0), c->r[31], HOST(ARG(1)), len);
    if (s->state == 3 || s->state == 4) RET(ERR_DISCONNECTED);
    if (s->state < 2) {                            /* before the SYNACK: keep the bytes */
        s->tx = (uint8_t *)realloc(s->tx, s->txn + len); memcpy(s->tx + s->txn, HOST(ARG(1)), len); s->txn += len;
        RET(0);
    }
    uint8_t *b = (uint8_t *)malloc(sizeof(PtpHdr) + len);
    PtpHdr h = { (int16_t)ARG(0), (int16_t)s->peer_sock, (uint16_t)s->lport, (uint16_t)s->pport };
    memcpy(b, &h, sizeof h); memcpy(b + sizeof h, HOST(ARG(1)), len);
    net_link_send(s->peer, NL_PTP_DATA, b, sizeof h + len);
    free(b);
    RET(0);
}
static int ptp_readable(void *a) { Sock *s = (Sock *)a; return s->rxn > 0 || s->state >= 3; }
void hle_sceNetAdhocPtpRecv(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpRecv);
    Sock *s = sock_get(ARG(0)); if (!s || s->kind != 1) RET(ERR_BAD_ID);
    uint32_t buf = ARG(1), lenp = ARG(2), timeout = ARG(3), nb = ARG(4);
    tick();
    if (!s->rxn) {
        if (s->state >= 3) RET(ERR_DISCONNECTED);
        if (nb || !wait_until(c, ptp_readable, s, timeout)) RET(nb ? WOULD_BLOCK : ERR_TIMEOUT);
        if (!s->rxn) RET(ERR_DISCONNECTED);
    }
    uint32_t cap = lenp ? RW(lenp) : 0, k = (uint32_t)s->rxn < cap ? (uint32_t)s->rxn : cap;
    memcpy(HOST(buf), s->rx, k); netcap("ptp-rx", (int)ARG(0), c->r[31], s->rx, k); rx_take(s, k);
    if (lenp) WW(lenp, k);
    RET(0);
}
void hle_sceNetAdhocPtpFlush(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpFlush); RET(0); }
void hle_sceNetAdhocPtpClose(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocPtpClose);
    Sock *s = sock_get(ARG(0)); if (!s) RET(ERR_BAD_ID);
    if (s->kind == 1 && s->state == 2) ptp_send_ctl(s->peer, NL_PTP_FIN, (int)ARG(0), s->peer_sock, s->lport, s->pport);
    free(s->rx); free(s->tx); memset(s, 0, sizeof *s);
    RET(0);
}

/* ---------------------------------------------------------------- sceNetAdhocctl */
void hle_sceNetAdhocctlInit(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlInit); NLOG("AdhocctlInit(prio=%u)", ARG(1)); start_thread((int)ARG(1)); RET(0); }
void hle_sceNetAdhocctlTerm(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlTerm); ctl_state = 0; RET(0); }
void hle_sceNetAdhocctlAddHandler(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlAddHandler); ctl_handler = ARG(0); ctl_handler_arg = ARG(1); RET(1); }
void hle_sceNetAdhocctlDelHandler(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlDelHandler); ctl_handler = 0; RET(0); }
void hle_sceNetAdhocctlConnect(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlConnect);
    memset(group, 0, sizeof group);
    if (ARG(0)) memcpy(group, HOST(ARG(0)), 8);
    NLOG("AdhocctlConnect(\"%s\")", group);
    if (rt_net_on_group) rt_net_on_group(group);
    ctl_state = 1; last_beacon = 0;
    ctl_event(1);
    RET(0);
}
void hle_sceNetAdhocctlScan(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlScan); NLOG("AdhocctlScan"); ctl_event(3); RET(0); }
void hle_sceNetAdhocctlDisconnect(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlDisconnect); NLOG("AdhocctlDisconnect"); ctl_state = 0; memset(group, 0, sizeof group); ctl_event(2); RET(0); }
void hle_sceNetAdhocctlGetState(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlGetState); if (ARG(0)) WW(ARG(0), (uint32_t)ctl_state); RET(0); }
const char *netplay_name(int node);
void hle_sceNetAdhocctlGetNameByAddr(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlGetNameByAddr);
    int n = node_of(HOST(ARG(0)));
    if (ARG(1)) { memset(HOST(ARG(1)), 0, 128); snprintf((char *)HOST(ARG(1)), 128, "%s", netplay_name(n)); }
    RET(0);
}
void hle_sceNetAdhocctlGetParameter(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlGetParameter);
    uint32_t p = ARG(0);
    if (p) {
        memset(HOST(p), 0, 4 + 8 + 128 + 8);
        WW(p, 1); memcpy(HOST(p + 4), group, 8);
        snprintf((char *)HOST(p + 12), 128, "%s", netplay_name(me()));
        uint8_t m[6]; mac_of(me(), m); memcpy(HOST(p + 140), m, 6);
    }
    RET(0);
}
void hle_sceNetAdhocctlGetScanInfo(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocctlGetScanInfo);
    uint32_t sizep = ARG(0), buf = ARG(1);
    tick();
    uint64_t now = rt_now_us();
    int n = 0; Heard list[8];
    for (int i = 0; i < 8; i++) {
        if (!heard[i].heard || now - heard[i].heard > 2000000 || !heard[i].name[0]) continue;
        int dup = 0; for (int k = 0; k < n; k++) if (!strcmp(list[k].name, heard[i].name)) dup = 1;
        if (!dup) list[n++] = heard[i];
    }
    uint32_t need = (uint32_t)n * 28u;
    if (!buf) { if (sizep) WW(sizep, need); RET(0); }
    uint32_t cap = sizep ? RW(sizep) : 0;
    int fit = (int)(cap / 28u); if (fit > n) fit = n;
    for (int i = 0; i < fit; i++) {
        uint32_t e = buf + 28u * (uint32_t)i;
        memset(HOST(e), 0, 28);
        WW(e, i + 1 < fit ? e + 28 : 0); WW(e + 4, 1);
        memcpy(HOST(e + 8), list[i].name, 8);
        uint8_t m[6]; mac_of(list[i].node, m); memcpy(HOST(e + 16), m, 6);
    }
    if (sizep) WW(sizep, (uint32_t)fit * 28u);
    NLOG("AdhocctlGetScanInfo: %d group(s)%s%s", fit, fit ? " first " : "", fit ? list[0].name : "");
    RET(0);
}

/* ---------------------------------------------------------------- sceNetAdhocMatching */
void hle_sceNetAdhocMatchingInit(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingInit); RET(0); }
void hle_sceNetAdhocMatchingTerm(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingTerm); nmt = 0; RET(0); }
void hle_sceNetAdhocMatchingCreate(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingCreate);
    if (nmt >= 4) RET(0x80410807);
    Matching *m = &mt[nmt++];
    memset(m, 0, sizeof *m);
    m->id = next_id++; m->mode = (int)ARG(0); if (m->mode == 1) rt_net_parent = 1; m->maxnum = (int)ARG(1); m->port = (int)ARG(2);
    m->hello_int = ARG(4); m->handler = RW(c->r[29] + 0);
    NLOG("MatchingCreate(mode=%d, max=%d, port=%d) -> %u", m->mode, m->maxnum, m->port, m->id);
    RET(m->id);
}
void hle_sceNetAdhocMatchingStart(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingStart);
    Matching *m = mt_get(ARG(0)); if (!m) RET(0x80410802);
    int optlen = (int)ARG(5); uint32_t opt = ARG(6);
    m->hello_len = optlen > 0 && optlen <= 256 && opt ? optlen : 0;
    if (m->hello_len) memcpy(m->hello, HOST(opt), (size_t)m->hello_len);
    m->running = 1; m->last_hello = 0;
    NLOG("MatchingStart(%u, hello %d bytes)", m->id, m->hello_len);
    RET(0);
}
void hle_sceNetAdhocMatchingStop(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingStop);
    Matching *m = mt_get(ARG(0)); if (!m) RET(0x80410802);
    for (int i = 0; i < 4; i++) if (m->peers[i].state == P_EST) m_send(m, i, M_BYE, NULL, 0);
    m->running = 0; memset(m->peers, 0, sizeof m->peers);
    RET(0);
}
void hle_sceNetAdhocMatchingDelete(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingDelete);
    for (int i = 0; i < nmt; i++) if (mt[i].id == ARG(0)) { mt[i] = mt[--nmt]; break; }
    RET(0);
}
void hle_sceNetAdhocMatchingSelectTarget(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingSelectTarget);
    Matching *m = mt_get(ARG(0)); if (!m) RET(0x80410802);
    int node = node_of(HOST(ARG(1)));
    int optlen = (int)ARG(2); const uint8_t *opt = optlen > 0 ? HOST(ARG(3)) : NULL;
    if (node < 0 || node > 3 || node == me()) RET(0x80410804);
    MPeer *pe = &m->peers[node];
    if (pe->state == P_IN_REQ) {
        NLOG("MatchingSelectTarget: accept node %d", node);
        pe->state = P_EST; rt_net_established++; m_send(m, node, M_ACCEPT, opt, optlen); m_event(m, 7, node, NULL, 0);
    } else if (pe->state == P_KNOWN || pe->state == P_NONE) {
        NLOG("MatchingSelectTarget: request node %d", node);
        pe->state = P_OUT_REQ; rt_net_requested++; m_send(m, node, M_REQUEST, opt, optlen);
    }
    RET(0);
}
void hle_sceNetAdhocMatchingCancelTarget(Cpu *c) { STUB_UNLESS_COOP(sceNetAdhocMatchingCancelTarget);
    Matching *m = mt_get(ARG(0)); if (!m) RET(0x80410802);
    int node = node_of(HOST(ARG(1)));
    NLOG("MatchingCancelTarget(node %d, state %d)", node, node >= 0 && node < 4 ? m->peers[node].state : -1);
    if (node < 0 || node > 3) RET(0x80410804);
    MPeer *pe = &m->peers[node];
    if (pe->state == P_OUT_REQ) m_send(m, node, M_CANCEL, NULL, 0);
    else if (pe->state == P_IN_REQ) m_send(m, node, M_DENY, NULL, 0);
    else if (pe->state == P_EST) m_send(m, node, M_BYE, NULL, 0);
    pe->state = P_KNOWN;
    RET(0);
}
