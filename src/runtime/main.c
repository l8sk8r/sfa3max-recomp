/* Boot: load the relocated program, start module_start on a root thread, run the scheduler.
 *
 * sfa3mr [--work DIR] [--frames N] [--fast] [--trace]
 *   --work DIR   folder written by tools/prepare.py (default: work)
 *   --frames N   quit after N frames (for tests)
 *   --fast       do not wait for the wall clock
 *   --trace      log every OS call
 *   --headless   no window or sound (tests)
 *   --renderer gl|soft   GPU renderer (default) or the software reference renderer
 *   --scale N    GPU internal resolution, N x 480x272 (1-8; default: covers the monitor)
 *   --p2 / --p3 human|cpu|auto   who controls fighters 2 and 3 (auto: human if gamepad 2/3 is connected)
 *   --pads LIST                  the gamepads (1-4) shown as plugged in on the MODE MENU's pad icons (tests)
 *   --pad-trace  log every change of a gamepad's buttons and left stick (controller problems)
 *   --watch LO-HI  log loads/stores to that range by game address (build with -DSFA3MR_WATCH=ON)
 *   --watch-from N start watching at frame N
 *   --watch-pc PC  only report accesses made by the instruction at PC (repeatable, up to 16)
 *   --dump-from N   no screens saved before frame N
 *   --snap F:FILE  save user RAM (0x08800000-0x0A000000) at frame F
 *   --poke F:ADDR=VAL[/SIZE]  force a byte (or /2, /4) every frame from frame F
 *   --net-hub PORT   be node 0 of a local multi-instance network (PORT 0: any)
 *   --net-join PORT N  be node N (1-3) of that network (tests)
 *   --renderer none|lazy  draw nothing / only the frames being dumped (tests)
 *   --log FILE   write the log there instead of work/sfa3mr.log
 *   --record-audio DIR   write each PSP audio channel to DIR/chN.wav
 *   --input S    scripted buttons, "FRAME=BUTTONS;..." e.g. "200=cross;206=" (names: cross circle square
 *                triangle start select up down left right l r, joined with +; "p2:" before them for pad 2)
 *   --dump DIR   save the displayed frame as DIR/frame_NNNNN.png every --dump-every N frames (default 60)
 */
#include "rt.h"
#include <string.h>
#include <sys/stat.h>
#include "ge.h"
#include "host.h"
#include "net_link.h"

#ifdef _WIN32
/* Laptops with two GPUs: these exports ask the NVIDIA (Optimus) and AMD (PowerXpress) drivers to run
 * the game on the fast GPU rather than the power-saving one. */
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

int rt_trace;
static FILE *log_file;

void rt_log(const char *fmt, ...) {
    va_list ap; char buf[2048];
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    fprintf(stdout, "[%7.3f] %s\n", rt_now_us() / 1e6, buf);
    if (log_file) { fprintf(log_file, "[%7.3f] %s\n", rt_now_us() / 1e6, buf); fflush(log_file); }
}
void rt_fatal(const char *fmt, ...) {
    va_list ap; char buf[2048];
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    rt_log("FATAL: %s", buf);
    rt_report();
    fflush(stdout); if (log_file) fflush(log_file);
    exit(1);
}

/* ---------------------------------------------------------------- translated-code services */
extern const FuncEntry rt_func_table[];
extern const unsigned rt_func_count;
extern const StubEntry rt_stub_table[];
extern const unsigned rt_stub_count;

static GuestFn lookup(uint32_t addr) {
    unsigned lo = 0, hi = rt_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (rt_func_table[mid].addr < addr) lo = mid + 1; else hi = mid;
    }
    if (lo < rt_func_count && rt_func_table[lo].addr == addr) return rt_func_table[lo].fn;
    for (unsigned i = 0; i < rt_stub_count; i++) if (rt_stub_table[i].addr == addr) return rt_stub_table[i].fn;
    return NULL;
}
void rt_call_indirect(Cpu *c, uint32_t addr) {
    GuestFn f = lookup(addr & 0x0FFFFFFF ? addr : addr);
    if (!f) rt_fatal("call to unknown address %08X (ra=%08X)", addr, c->r[31]);
    f(c);
}
void rt_jump_indirect(Cpu *c, uint32_t addr) { rt_call_indirect(c, addr); }
void rt_bad_jump(Cpu *c, uint32_t addr, uint32_t from) { (void)c; rt_fatal("switch at %08X jumped to unknown case %08X", from, addr); }
void rt_syscall(Cpu *c, uint32_t code, uint32_t pc) { (void)c; rt_fatal("syscall %X at %08X", code, pc); }
void rt_break(Cpu *c, uint32_t pc) { (void)c; rt_fatal("break at %08X", pc); }
void rt_unsupported(Cpu *c, uint32_t pc) { (void)c; rt_fatal("unsupported instruction at %08X", pc); }

/* ---------------------------------------------------------------- memory watch (SFA3MR_WATCH builds) */
uint32_t rt_watch_lo = 1, rt_watch_hi = 0;      /* empty range */
static long watch_from;
static uint32_t watch_pc[16]; static int nwatch_pc;
typedef struct { uint32_t pc, addr; int write; uint64_t n; uint32_t last; } WatchHit;
static WatchHit whits[4096];
static int nwhits;
void rt_watch_hit(uint32_t addr, uint32_t pc, uint32_t value, int size, int write) {
    (void)size;
    if ((long)rt_vblank_count() < watch_from) return;
    if (nwatch_pc) { int ok = 0; for (int i = 0; i < nwatch_pc; i++) if (watch_pc[i] == pc) ok = 1; if (!ok) return; }
    for (int i = 0; i < nwhits; i++)
        if (whits[i].pc == pc && whits[i].addr == (addr & 0x0FFFFFFF) && whits[i].write == write) { whits[i].n++; whits[i].last = value; return; }
    if (nwhits < 4096) {
        WatchHit *h = &whits[nwhits++]; h->pc = pc; h->addr = addr & 0x0FFFFFFF; h->write = write; h->n = 1; h->last = value;
        rt_log("watch: %s %08X at pc %08X value %X", write ? "write" : "read ", h->addr, pc, value);
    }
}
static void watch_report(void) {
    for (int i = 0; i < nwhits; i++)
        rt_log("watch summary: %s %08X pc %08X x%llu last %X", whits[i].write ? "W" : "R", whits[i].addr, whits[i].pc,
               (unsigned long long)whits[i].n, whits[i].last);
}

/* ---------------------------------------------------------------- snapshots and pokes (reverse-engineering aids) */
static struct { long frame; char path[600]; } snaps[16];
static int nsnaps;
static struct { long frame; uint32_t addr; uint32_t value; int size; } pokes[64];
static int npokes;
static void snaps_and_pokes(long f) {
    for (int i = 0; i < nsnaps; i++) if (snaps[i].frame == f) {
        FILE *o = fopen(snaps[i].path, "wb");
        if (o) { fwrite(HOST(USER_LO), 1, USER_HI - USER_LO, o); fclose(o); rt_log("snapshot of user RAM -> %s", snaps[i].path); }
    }
    for (int i = 0; i < npokes; i++) if (pokes[i].frame <= f && pokes[i].frame >= 0) {
        if (pokes[i].size == 1) WB(pokes[i].addr, pokes[i].value); else if (pokes[i].size == 2) WH(pokes[i].addr, pokes[i].value); else WW(pokes[i].addr, pokes[i].value);
    }
}

/* ---------------------------------------------------------------- per-frame hook and report */
uint64_t rt_ge_lists(void);
uint64_t rt_ge_cmds(void);
uint64_t rt_ge_take_frame_lists(void);
uint32_t rt_fb_addr(void);
uint32_t rt_fb_width(void);
uint32_t rt_fb_format(void);
static const char *dump_dir;
static int headless;
static int lazy_draw;                       /* --renderer lazy: rasterise only the frames being dumped */
/* ---- linked instances (local multiplayer through the game's network modes) ---- */
static int net_hub_port = -1, net_join_port, net_node;
static uint32_t net_pad;                         /* background instance: our player's buttons, from the hub */
static long auto_accept_at = -1, auto_request_at = -1;
extern int rt_net_established, rt_net_requested, rt_net_parent;
extern void (*rt_net_on_hello)(int node);
void rt_net_init(void);
void netplay_frame(void);
void sysdata_frame(void);
void rt_net_frame(void);
extern void (*rt_net_on_pad)(const uint8_t *p, size_t n);
extern void (*rt_net_on_request)(void);
static void on_pad(const uint8_t *p, size_t n) { if (n >= 4) memcpy(&net_pad, p, 4); }
/* Lobby automation. The instance that accepts a battle request becomes 1P, so background instances
 * send the requests (to node 0) and the player's own instance accepts them. */
static void on_request(void) { if (net_node == 0) auto_accept_at = (long)rt_vblank_count() + 20; }
static void on_hello(int node) { if (net_node > 0 && node == 0 && auto_request_at < 0 && !rt_net_requested) auto_request_at = (long)rt_vblank_count() + 60 + 90 * (net_node - 1); }
static void on_denied(void) { if (net_node > 0) auto_request_at = (long)rt_vblank_count() + 90 + 30 * net_node; }
extern void (*rt_net_on_denied)(void);
static int renderer_gl_ready, want_soft, render_scale;
/* scripted input: "FRAME=BUTTONS;FRAME=BUTTONS..." e.g. "200=cross;206=;300=start;306=" */
extern uint32_t rt_pad_buttons[4];
static struct { long frame; int player; uint32_t buttons; } script[512];
static int nscript, script_pos;
static uint32_t button_bits(const char *s) {
    static const struct { const char *n; uint32_t b; } names[] = {
        {"select",0x1},{"start",0x8},{"up",0x10},{"right",0x20},{"down",0x40},{"left",0x80},{"l",0x100},{"r",0x200},
        {"triangle",0x1000},{"circle",0x2000},{"cross",0x4000},{"square",0x8000},{"l2",0x40000000u},{"r2",0x80000000u},{"menukey",0x20000000u} };   /* menukey: F2 (tests) */
    uint32_t b = 0; char tok[32]; int n = 0;
    for (;; s++) {
        if (*s && *s != '+' && *s != ',') { if (n < 31) tok[n++] = *s; continue; }
        tok[n] = 0;
        for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) if (!strcmp(tok, names[i].n)) b |= names[i].b;
        n = 0;
        if (!*s) break;
    }
    return b;
}
static void parse_script(const char *s) {
    while (*s && nscript < 512) {
        char item[128]; int n = 0;
        while (*s && *s != ';' && n < 127) item[n++] = *s++;
        item[n] = 0; if (*s == ';') s++;
        char *eq = strchr(item, '='); if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1; int pl = 0;
        if (v[0] == 'p' && v[1] >= '1' && v[1] <= '4' && v[2] == ':') { pl = v[1] - '1'; v += 3; }
        script[nscript].frame = atol(item); script[nscript].player = pl; script[nscript].buttons = button_bits(v); nscript++;
    }
}
static long dump_every = 60, dump_from = 0;
/* Screens the runtime answers by itself (sysdata.c) are kept off screen: while held, the window keeps
 * showing the last frame presented. */
static long display_hold_until = -1;
void rt_hold_display(int frames) { long u = (long)rt_vblank_count() + frames; if (u > display_hold_until) display_hold_until = u; }
void rt_hold_display_set(int frames) { display_hold_until = (long)rt_vblank_count() + frames; }   /* also shortens a hold */
int rt_display_held(void) { return (long)rt_vblank_count() < display_hold_until; }   /* kernel.c: held frames are not paced */
static void lazy_update(uint64_t f) { if (lazy_draw) ge_no_draw = !(dump_dir && (long)f + 3 >= dump_from && ((f + 1) % (uint64_t)dump_every == 0 || (f + 2) % (uint64_t)dump_every == 0 || (f + 3) % (uint64_t)dump_every == 0)); }
void quitkey_request(void);                     /* quitkey.c */
void rt_display_vblank(void) {
    uint64_t n = rt_ge_take_frame_lists(), f = rt_vblank_count();
    snaps_and_pokes((long)f);
    static uint32_t scripted[4];
    while (script_pos < nscript && script[script_pos].frame <= (long)f) { scripted[script[script_pos].player] = script[script_pos].buttons; script_pos++; }
    {
        uint32_t pads[4] = { 0 }; uint8_t sticks[4][2];
        host_poll(pads, sticks);
        uint32_t pcfg_pad_to_psp(int player, uint32_t raw);   /* playercfg.c: L2 / R2 by the player's config */
        static int menu_prev;
        int menu_now = (scripted[0] & 0x20000000u) != 0;
        if (host_menu_requested() || (menu_now && !menu_prev)) quitkey_request();
        menu_prev = menu_now;
        for (int i = 0; i < 4; i++) rt_pad_buttons[i] = pcfg_pad_to_psp(i, pads[i] | (scripted[i] & ~0x20000000u));
        if (host_quit_requested()) rt_quit = 1;
    }
    rt_net_frame();
    netplay_frame();
    sysdata_frame();
    if (net_link_active()) {
        static int est_seen;
        if (net_link_self() == 0) {                     /* hub: send each background instance its player's pad */
            for (int k = 1; k < 4; k++) if (net_link_connected(k)) net_link_send(k, NL_PAD, &rt_pad_buttons[k], 4);
        } else {
            rt_pad_buttons[0] |= net_pad;
        }
        /* press Cross every 40 frames until the request is sent / the connection is made */
        if (auto_request_at >= 0) {
            long d = (long)f - auto_request_at;
            if (rt_net_requested || d > 1200) auto_request_at = -1;
            else if (d >= 0 && d % 40 < 6) rt_pad_buttons[0] |= 0x4000;
        }
        if (auto_accept_at >= 0) {
            long d = (long)f - auto_accept_at;
            if (rt_net_established > est_seen || d > 1200) { auto_accept_at = -1; est_seen = rt_net_established; }
            else if (d >= 0 && d % 40 < 6) rt_pad_buttons[0] |= 0x4000;
        }
        /* Dramatic Battle host: once every linked instance has joined, press Cross to start */
        if (net_link_self() == 0 && rt_net_parent) {
            static long full_since = -1; static int started;
            int want = 0; for (int k = 1; k < 4; k++) want += net_link_connected(k);
            if (!started && want && rt_net_established >= want && auto_accept_at < 0) {
                if (full_since < 0) full_since = (long)f;
                long d = (long)f - full_since;
                if (d >= 60 && d < 66) rt_pad_buttons[0] |= 0x4000;
                if (d >= 66) started = 1;
            } else if (!started) full_since = -1;
        }
    }
    int have_gl = renderer_gl_ready;
    int held = (long)f < display_hold_until;          /* the last frame stays on screen */
    if (!headless && !held) {
        static uint8_t rgba[480 * 272 * 4];
        int ww, wh;
        host_window_size(&ww, &wh);
        if (have_gl && ge_use_gl && ge_gl_present(rt_fb_addr(), ww, wh)) {
            host_gl_swap();
        } else {
            if (rt_fb_addr()) ge_fb_to_rgba(rt_fb_addr(), (int)rt_fb_width(), (int)rt_fb_format(), 480, 272, rgba);
            if (have_gl) { ge_gl_present_rgba(rgba, 480, 272, ww, wh); host_gl_swap(); }
            else host_present(rgba, 480, 272);
        }
    }
    if (have_gl) ge_gl_frame_end();
    if (f <= 5 || f % 60 == 0)
        rt_log("frame %llu: %llu display lists, framebuffer %08X, %llu prims so far", (unsigned long long)f,
               (unsigned long long)n, rt_fb_addr(), (unsigned long long)ge_prims_drawn);
    if (dump_dir && !held && rt_fb_addr() && (long)f >= dump_from && f % (uint64_t)dump_every == 0) {
        static uint8_t *big;
        char p[1200];
        int w = 480, h = 272;
        if (!big) big = (uint8_t *)malloc((size_t)480 * 8 * 272 * 8 * 4);
        snprintf(p, sizeof p, "%s/frame_%05llu.png", dump_dir, (unsigned long long)f);
        if (!(ge_use_gl && ge_gl_read(rt_fb_addr(), big, &w, &h)))
            ge_fb_to_rgba(rt_fb_addr(), (int)rt_fb_width(), (int)rt_fb_format(), 480, 272, big);
        png_write(p, big, w, h);
    }
    lazy_update(f);
}
void rt_ge_census(void);
void rt_net_report(void);
void rt_report(void) {
    rt_net_report();
    watch_report();
    rt_ge_census();
    rt_log("report: %llu frames, %llu display lists, %llu GE commands",
           (unsigned long long)rt_vblank_count(), (unsigned long long)rt_ge_lists(), (unsigned long long)rt_ge_cmds());
}

static long file_size(const char *p) { struct stat s; return stat(p, &s) == 0 ? (long)s.st_size : -1; }

#ifdef SFA3MR_SDL
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#endif
int main(int argc, char **argv) {
    const char *work = "work", *log_path = NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--work") && i + 1 < argc) work = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) rt_max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--fast")) rt_realtime = 0;
        else if (!strcmp(argv[i], "--trace")) rt_trace = 1;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) dump_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--dump-from") && i + 1 < argc) dump_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) parse_script(argv[++i]);
        else if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--net-hub") && i + 1 < argc) net_hub_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-join") && i + 2 < argc) { net_join_port = atoi(argv[++i]); net_node = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_path = argv[++i];
        else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) {
            const char *r = argv[++i];
            want_soft = !strcmp(r, "soft") || !strcmp(r, "none") || !strcmp(r, "lazy");
            ge_no_draw = !strcmp(r, "none");
            if (!strcmp(r, "lazy")) { lazy_draw = 1; ge_no_draw = 1; }
        }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) render_scale = atoi(argv[++i]);
        else if ((!strcmp(argv[i], "--p2") || !strcmp(argv[i], "--p3")) && i + 1 < argc) {
            extern int rt_human[3]; int k = argv[i][3] - '1'; const char *v = argv[++i];
            rt_human[k] = !strcmp(v, "human") ? 1 : !strcmp(v, "cpu") ? 0 : -1;
        }
        else if (!strcmp(argv[i], "--pad-trace")) host_set_pad_trace(1);
        else if (!strcmp(argv[i], "--pads") && i + 1 < argc) {       /* e.g. 124: gamepads taken as plugged in for the pad icons */
            extern int rt_pads_forced; rt_pads_forced = 0;
            for (const char *p = argv[++i]; *p; p++) if (*p >= '1' && *p <= '4') rt_pads_forced |= 1 << (*p - '1');
        }
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) {      /* LO-HI (hex), SFA3MR_WATCH builds only */
            const char *w = argv[++i]; rt_watch_lo = (uint32_t)strtoul(w, NULL, 16) & 0x0FFFFFFF;
            const char *d = strchr(w, '-'); rt_watch_hi = d ? (uint32_t)strtoul(d + 1, NULL, 16) & 0x0FFFFFFF : rt_watch_lo + 4;
        }
        else if (!strcmp(argv[i], "--watch-from") && i + 1 < argc) watch_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--snap") && i + 1 < argc && nsnaps < 16) {        /* FRAME:FILE */
            char *a = argv[++i], *colon = strchr(a, ':');
            if (colon) { snaps[nsnaps].frame = atol(a); snprintf(snaps[nsnaps].path, sizeof snaps[0].path, "%s", colon + 1); nsnaps++; }
        }
        else if (!strcmp(argv[i], "--poke") && i + 1 < argc && npokes < 64) {        /* FRAME:ADDR=VALUE[/SIZE], held every frame from FRAME */
            char *a = argv[++i]; char *c1 = strchr(a, ':'), *eq = strchr(a, '='), *sl = strchr(a, '/');
            if (c1 && eq) { pokes[npokes].frame = atol(a); pokes[npokes].addr = (uint32_t)strtoul(c1 + 1, NULL, 16);
                pokes[npokes].value = (uint32_t)strtoul(eq + 1, NULL, 16); pokes[npokes].size = sl ? atoi(sl + 1) : 1; npokes++; }
        }
        else if (!strcmp(argv[i], "--watch-pc") && i + 1 < argc && nwatch_pc < 16) watch_pc[nwatch_pc++] = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--record-audio") && i + 1 < argc) { extern const char *rt_record_audio_dir; rt_record_audio_dir = argv[++i]; }
    }
    char path[1200];
    if (log_path) snprintf(path, sizeof path, "%s", log_path);
    else snprintf(path, sizeof path, "%s/sfa3mr.log", work);
    log_file = fopen(path, "w");
    snprintf(rt_disc_root, sizeof rt_disc_root, "%s/iso", work);
    snprintf(rt_save_root, sizeof rt_save_root, "%s/ms0", work);
    { void settings_load(void); settings_load(); }

    if (!rt_mem_init()) rt_fatal("could not reserve guest memory");
    snprintf(path, sizeof path, "%s/analysis.image.bin", work);
    long n = file_size(path);
    FILE *f = fopen(path, "rb");
    if (!f || n <= 0) rt_fatal("%s not found: run tools/prepare.py first", path);
    if (fread(HOST(0x08804000), 1, (size_t)n, f) != (size_t)n) rt_fatal("short read on %s", path);
    fclose(f);
    rt_log("loaded program image: %ld bytes at 08804000", n);
    rt_reserve(USER_LO, (0x08804000u + (uint32_t)n + 0xFFFF) & ~0xFFFFu, "program");

    host_set_work(work);
    if (!host_init(headless, !want_soft)) rt_fatal("could not open the window (SDL)");
    if (!want_soft) {
        HostGetProc gp = headless ? host_headless_gl() : host_gl_getproc();
        int scale = render_scale;
        if (scale <= 0) {                       /* auto: enough to cover the monitor without upscaling the result */
            int dh = host_display_height();
            scale = dh ? (dh + 271) / 272 : 4;
        }
        if (gp && ge_gl_init(gp, scale)) { renderer_gl_ready = 1; ge_use_gl = 1; }
        else rt_log("GPU renderer unavailable: using the software renderer");
    }
    rt_kernel_init(); rt_io_init(); rt_ge_init();
    rt_net_init();
    { void netplay_init(void); netplay_init(); }
    rt_net_on_pad = on_pad; rt_net_on_request = on_request; rt_net_on_hello = on_hello; rt_net_on_denied = on_denied;
    if (net_hub_port >= 0 && !net_link_hub(net_hub_port)) rt_fatal("could not start the local network hub");
    if (net_join_port && !net_link_join(net_join_port, net_node)) rt_fatal("could not join the local network hub");

    /* module_start(argsize, argp) runs on a root thread, as the PSP's loader does */
    const char *bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN";
    uint32_t argp = rt_alloc(256, 0, 16, "args");
    strcpy((char *)HOST(argp), bootpath);
    uint32_t root = rt_thread_create("root", 0x08804000, 0x20, 0x4000, 0x80000000u);
    rt_thread_start(root, (uint32_t)strlen(bootpath) + 1, argp);

    rt_run();
    { void store_shutdown(void); store_shutdown(); }   /* keep the settings as they are now */
    rt_report();
    host_shutdown();
    if (log_file) fclose(log_file);
    return 0;
}
