/* Local links between game instances (one process per player) for the emulated ad-hoc network.
 *
 * Node 0 (the player's own instance) is the hub: it listens on 127.0.0.1 and every other instance
 * connects to it. Frames are [u32 length][u8 src][u8 dst][u8 type][u8 0][payload]; the hub delivers
 * frames addressed to itself and forwards the rest (dst 0xFF = everyone but the sender).
 */
#include "net_link.h"
#include "rt.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BADSOCK INVALID_SOCKET
#define closesock closesocket
static int would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void set_nonblock(sock_t s) { u_long one = 1; ioctlsocket(s, FIONBIO, &one); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int sock_t;
#define BADSOCK (-1)
#define closesock close
static int would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void set_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK); }
#endif

#define MAXN 4
typedef struct { sock_t s; uint8_t *rx; size_t rxn, rxcap; int node; } Conn;
static int self_node = -1;
static sock_t listener = BADSOCK;
static Conn conns[MAXN];          /* hub: index = node; guest: conns[0] = hub */
static Conn pending[MAXN];        /* hub: accepted, not yet named */
static int nconns;

/* virtual nodes (vpeer.c): players simulated inside this process */
static int nvirtual;
static void (*vsend)(int node, int type, const uint8_t *p, size_t n);
typedef struct { int src, type; size_t n; uint8_t *p; } Inj;
static Inj inj[256]; static int ninj;
static int is_virtual(int node) { return node >= 1 && node <= nvirtual; }
void net_link_virtual(int n, void (*fn)(int node, int type, const uint8_t *p, size_t n)) {
    if (self_node < 0) { self_node = 0; for (int i = 0; i < MAXN; i++) { conns[i].s = BADSOCK; pending[i].s = BADSOCK; } }
    nvirtual = n; vsend = fn;
}
void net_link_inject(int src, int type, const void *p, size_t n) {
    if (ninj >= 256) return;
    inj[ninj].src = src; inj[ninj].type = type; inj[ninj].n = n;
    inj[ninj].p = (uint8_t *)malloc(n ? n : 1); if (n) memcpy(inj[ninj].p, p, n);
    ninj++;
}

int net_link_self(void) { return self_node; }
int net_link_active(void) { return self_node >= 0; }

static void sock_init(void) {
#ifdef _WIN32
    static int done; if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = 1; }
#endif
}
static void tune(sock_t s) { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one); set_nonblock(s); }

int net_link_hub(int want_port) {
    sock_init();
    listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((uint16_t)want_port);
    { int one = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one); }
    if (bind(listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(listener, 8) != 0) return 0;
    socklen_t al = sizeof a; getsockname(listener, (struct sockaddr *)&a, &al);
    set_nonblock(listener);
    self_node = 0;
    for (int i = 0; i < MAXN; i++) { conns[i].s = BADSOCK; pending[i].s = BADSOCK; }
    rt_log("net link: hub listening on 127.0.0.1:%d", ntohs(a.sin_port));
    return ntohs(a.sin_port);
}

int net_link_join(int port, int node) {
    sock_init();
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons((uint16_t)port);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { rt_log("net link: cannot reach hub on port %d", port); return 0; }
    tune(s);
    for (int i = 0; i < MAXN; i++) conns[i].s = BADSOCK;
    conns[0].s = s; conns[0].node = 0; nconns = 1;
    self_node = node;
    uint8_t hello = (uint8_t)node;
    net_link_send(0, NL_JOIN, &hello, 1);
    rt_log("net link: node %d joined hub on port %d", node, port);
    return 1;
}

static void raw_send(sock_t s, const uint8_t *buf, size_t n) {
    while (n) {
        int k = (int)send(s, (const char *)buf, (int)n, 0);
        if (k > 0) { buf += k; n -= (size_t)k; continue; }
        if (k < 0 && would_block()) continue;            /* loopback: drains immediately */
        return;
    }
}
static void frame_to(sock_t s, int src, int dst, int type, const void *p, size_t n) {
    uint8_t h[8]; uint32_t len = (uint32_t)n;
    memcpy(h, &len, 4); h[4] = (uint8_t)src; h[5] = (uint8_t)dst; h[6] = (uint8_t)type; h[7] = 0;
    raw_send(s, h, 8);
    if (n) raw_send(s, (const uint8_t *)p, n);
}

static void (*on_frame)(int src, int type, const uint8_t *p, size_t n);
void net_link_set_handler(void (*fn)(int src, int type, const uint8_t *p, size_t n)) { on_frame = fn; }

void net_link_send(int dst, int type, const void *p, size_t n) {
    if (self_node < 0) return;
    if (self_node == 0 && vsend)
        for (int k = 1; k <= nvirtual; k++) if (dst == 0xFF || dst == k) vsend(k, type, (const uint8_t *)p, n);
    if (self_node == 0) {
        for (int i = 1; i < MAXN; i++)
            if (conns[i].s != BADSOCK && (dst == 0xFF || dst == i)) frame_to(conns[i].s, 0, dst, type, p, n);
    } else if (conns[0].s != BADSOCK) {
        frame_to(conns[0].s, self_node, dst, type, p, n);
    }
}

static void deliver_or_forward(int src, int dst, int type, const uint8_t *p, size_t n) {
    if (self_node == 0) {
        if (dst == 0xFF || dst != 0)
            for (int i = 1; i < MAXN; i++)
                if (i != src && conns[i].s != BADSOCK && (dst == 0xFF || dst == i)) frame_to(conns[i].s, src, dst, type, p, n);
        if (dst != 0xFF && dst != 0) return;
    }
    if (on_frame) on_frame(src, type, p, n);
}

static int pump(Conn *c, int hub_side) {
    uint8_t buf[65536];
    for (;;) {
        int k = (int)recv(c->s, (char *)buf, sizeof buf, 0);
        if (k == 0 || (k < 0 && !would_block())) { closesock(c->s); c->s = BADSOCK; return 0; }
        if (k < 0) break;
        if (c->rxn + (size_t)k > c->rxcap) { c->rxcap = (c->rxn + (size_t)k) * 2; c->rx = (uint8_t *)realloc(c->rx, c->rxcap); }
        memcpy(c->rx + c->rxn, buf, (size_t)k); c->rxn += (size_t)k;
    }
    size_t off = 0;
    while (c->rxn - off >= 8) {
        uint32_t len; memcpy(&len, c->rx + off, 4);
        if (c->rxn - off < 8 + (size_t)len) break;
        int src = c->rx[off + 4], dst = c->rx[off + 5], type = c->rx[off + 6];
        const uint8_t *p = c->rx + off + 8;
        if (hub_side && type == NL_JOIN && len >= 1) {   /* a guest names itself: move it to its slot */
            int node = p[0];
            if (node > 0 && node < MAXN && c != &conns[node]) {
                conns[node] = *c; c->s = BADSOCK; c->rx = NULL; c->rxn = c->rxcap = 0;
                memmove(conns[node].rx, conns[node].rx + off + 8 + len, conns[node].rxn - off - 8 - len);
                conns[node].rxn -= off + 8 + len;
                rt_log("net link: node %d connected", node);
                if (on_frame) on_frame(node, NL_JOIN, p, len);
                return 1;   /* buffer moved; continue next poll */
            }
        }
        deliver_or_forward(src, dst, type, p, len);
        off += 8 + len;
    }
    memmove(c->rx, c->rx + off, c->rxn - off); c->rxn -= off;
    return 1;
}

void net_link_poll(void) {
    if (self_node < 0) return;
    if (ninj) {                                   /* what the virtual players said since the last poll */
        static Inj q[256]; int n = ninj; memcpy(q, inj, sizeof q[0] * (size_t)n); ninj = 0;
        for (int i = 0; i < n; i++) { if (on_frame) on_frame(q[i].src, q[i].type, q[i].p, q[i].n); free(q[i].p); }
    }
    if (self_node == 0 && listener != BADSOCK) {
        for (;;) {
            sock_t s = accept(listener, NULL, NULL);
            if (s == BADSOCK) break;
            tune(s);
            for (int i = 0; i < MAXN; i++) if (pending[i].s == BADSOCK) { memset(&pending[i], 0, sizeof pending[i]); pending[i].s = s; break; }
        }
        for (int i = 0; i < MAXN; i++) if (pending[i].s != BADSOCK) pump(&pending[i], 1);
        for (int i = 1; i < MAXN; i++) if (conns[i].s != BADSOCK) pump(&conns[i], 1);
    } else if (conns[0].s != BADSOCK) {
        pump(&conns[0], 0);
    }
}

int net_link_connected(int node) {
    if (self_node == 0) return is_virtual(node) || (node > 0 && node < MAXN && conns[node].s != BADSOCK);
    return conns[0].s != BADSOCK;
}
