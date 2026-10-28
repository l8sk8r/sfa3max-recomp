/* PSP kernel threads re-implemented on fibers.
 *
 * The PSP runs one thread at a time: the highest-priority ready thread (lowest number) runs until it
 * blocks or a higher-priority thread becomes ready. Each game thread here owns a fiber and its own Cpu.
 * Time is virtual: it only moves forward when every thread is blocked, jumping to the next event
 * (a vblank or a timeout). In real-time mode the jump also waits for the wall clock.
 */
#include "rt.h"
#include "fiber.h"
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#define VBLANK_US_NUM 1001000ull   /* 59.94 Hz: 1001000/60 us per frame */
#define VBLANK_US_DEN 60ull

enum { T_DORMANT, T_READY, T_RUNNING, T_WAITING, T_DEAD };
enum { W_NONE, W_DELAY, W_VBLANK, W_SEMA, W_EVF, W_SLEEP, W_THREADEND };

struct Thread {
    uint32_t id;
    char name[32];
    uint32_t entry, attr, stack, stack_size;
    int prio, init_prio;
    int state;
    uint64_t ready_seq;
    Cpu cpu;
    Fiber *fiber;
    /* waiting */
    int wait;
    uint32_t wait_obj;
    uint64_t wake_at;           /* 0: no timeout */
    uint32_t timeout_ptr;
    uint32_t wait_result;
    uint32_t wait_arg0, wait_arg1, wait_arg2;   /* sema: count; evf: bits, mode, out ptr */
    uint32_t exit_status;
    int wakeup_count;
    uint32_t start_argsize, start_argp;
    void (*native)(Cpu *c);       /* native thread body (runtime services), or NULL for game code */
};

#define MAX_THREADS 128
static Thread *threads[MAX_THREADS];
static Thread *current;
static Fiber *sched_fiber;
static Fiber *graveyard[MAX_THREADS];
static int ngrave;
static uint64_t now_us, next_vblank_us, vblank_count, seq_counter;
static uint32_t uid_counter = 0x100;
volatile int rt_quit;
long rt_max_frames;
int rt_realtime = 1;
int rt_display_held(void);                  /* main.c */

uint32_t rt_new_uid(void) { return uid_counter++; }
Thread *rt_cur(void) { return current; }
uint32_t rt_cur_id(void) { return current ? current->id : 0; }
uint64_t rt_now_us(void) { return now_us; }
uint64_t rt_vblank_count(void) { return vblank_count; }

static Thread *find_thread(uint32_t id) {
    if (id == 0) return current;
    for (int i = 0; i < MAX_THREADS; i++) if (threads[i] && threads[i]->id == id) return threads[i];
    return NULL;
}

/* ---------------------------------------------------------------- wall clock */
static uint64_t host_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER f; LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (uint64_t)(t.QuadPart * 1000000.0 / f.QuadPart);
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
#endif
}
static void host_sleep_us(uint64_t us) {
#ifdef _WIN32
    if (us > 1500) Sleep((DWORD)((us - 1000) / 1000));
#else
    struct timespec ts = { (time_t)(us / 1000000), (long)(us % 1000000) * 1000 }; nanosleep(&ts, NULL);
#endif
}
static uint64_t host_base;

/* ---------------------------------------------------------------- scheduling */
static void make_ready(Thread *t, int keep_seq) {
    t->state = T_READY; t->wait = W_NONE;
    if (!keep_seq) t->ready_seq = ++seq_counter;
}

static void to_scheduler(void) { fiber_switch(sched_fiber); }

/* Block the running thread until woken; returns the wait result. */
static uint32_t block(Cpu *c, int wait, uint32_t obj, uint32_t timeout_ptr) {
    (void)c;
    Thread *t = current;
    t->state = T_WAITING; t->wait = wait; t->wait_obj = obj; t->wait_result = 0;
    t->timeout_ptr = timeout_ptr; t->wake_at = 0;
    if (timeout_ptr) {
        uint32_t us = RW(timeout_ptr);
        t->wake_at = now_us + (us ? us : 1);
    }
    to_scheduler();
    return t->wait_result;
}

static void wake(Thread *t, uint32_t result) {
    if (t->timeout_ptr && t->wake_at) {
        uint64_t left = t->wake_at > now_us ? t->wake_at - now_us : 0;
        WW(t->timeout_ptr, (uint32_t)left);
    }
    t->wait_result = result;
    make_ready(t, 0);
}

void rt_yield_if_preempted(void) {
    Thread *t = current;
    if (!t || t->state != T_RUNNING) return;
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *o = threads[i];
        if (o && o != t && o->state == T_READY && o->prio < t->prio) {
            make_ready(t, 1);            /* stays at the head of its priority */
            t->ready_seq = 0;
            to_scheduler();
            return;
        }
    }
}

static void thread_main(void *arg) {
    Thread *t = (Thread *)arg;
    if (t->native) t->native(&t->cpu); else rt_call_indirect(&t->cpu, t->entry);
    /* returning from the entry function ends the thread with v0 as its exit status */
    t->exit_status = t->cpu.r[2];
    t->state = T_DORMANT;
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *o = threads[i];
        if (o && o->state == T_WAITING && o->wait == W_THREADEND && o->wait_obj == t->id) wake(o, 0);
    }
    graveyard[ngrave++] = t->fiber; t->fiber = NULL;
    to_scheduler();
}

static void timers(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = threads[i];
        if (!t || t->state != T_WAITING) continue;
        if (t->wait == W_DELAY && t->wake_at <= now_us) { t->timeout_ptr = 0; wake(t, 0); }
        else if (t->wait != W_DELAY && t->wake_at && t->wake_at <= now_us) {
            if (t->timeout_ptr) WW(t->timeout_ptr, 0);
            t->timeout_ptr = 0; t->wait_result = ERR_WAIT_TIMEOUT; make_ready(t, 0);
        }
    }
}

static void do_vblank(void) {
    vblank_count++;
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = threads[i];
        if (t && t->state == T_WAITING && t->wait == W_VBLANK) wake(t, 0);
    }
    rt_display_vblank();
    if (rt_max_frames && (long)vblank_count >= rt_max_frames) rt_quit = 1;
}

void rt_run(void) {
    sched_fiber = fiber_main();
    host_base = host_us();
    next_vblank_us = VBLANK_US_NUM / VBLANK_US_DEN;
    while (!rt_quit) {
        while (ngrave) fiber_delete(graveyard[--ngrave]);
        timers();
        Thread *best = NULL;
        for (int i = 0; i < MAX_THREADS; i++) {
            Thread *t = threads[i];
            if (t && t->state == T_READY &&
                (!best || t->prio < best->prio || (t->prio == best->prio && t->ready_seq < best->ready_seq))) best = t;
        }
        if (best) {
            current = best; best->state = T_RUNNING;
            if (!best->fiber) best->fiber = fiber_create(thread_main, best, 4u << 20);
            fiber_switch(best->fiber);
            current = NULL;
            continue;
        }
        /* everyone is blocked: jump to the next event */
        uint64_t next = next_vblank_us;
        for (int i = 0; i < MAX_THREADS; i++) {
            Thread *t = threads[i];
            if (t && t->state == T_WAITING && t->wake_at && t->wake_at < next) next = t->wake_at;
        }
        if (next > now_us) {
            if (rt_realtime && rt_display_held()) {
                host_base = host_us() - next;        /* a load or save off screen (rt_hold_display): run it at full speed, owing no time */
            } else if (rt_realtime) {
                uint64_t h = host_us() - host_base;
                if (next > h + 1000000 || h > next + 250000) host_base = host_us() - next;  /* just switched to paced, or fell far behind: resync */
                else if (next > h + 200) host_sleep_us(next - h);
            }
            now_us = next;
        }
        if (now_us >= next_vblank_us) {
            do_vblank();
            next_vblank_us = (vblank_count + 1) * VBLANK_US_NUM / VBLANK_US_DEN;
        }
        int any = 0;
        for (int i = 0; i < MAX_THREADS; i++)
            if (threads[i] && (threads[i]->state == T_READY || threads[i]->state == T_WAITING)) any = 1;
        if (!any) { rt_log("all threads finished"); break; }
    }
}

/* ---------------------------------------------------------------- threads */
uint32_t rt_thread_create(const char *name, uint32_t entry, int prio, uint32_t stack, uint32_t attr) {
    int slot = -1;
    for (int i = 0; i < MAX_THREADS; i++) if (!threads[i]) { slot = i; break; }
    if (slot < 0) return ERR_NO_MEMORY;
    if (stack < 0x200) stack = 0x200;
    stack = (stack + 0xFF) & ~0xFFu;
    uint32_t base = rt_alloc(stack, 1, 256, name);
    if (!base) return ERR_NO_MEMORY;
    Thread *t = (Thread *)calloc(1, sizeof *t);
    t->id = rt_new_uid();
    strncpy(t->name, name ? name : "", sizeof t->name - 1);
    t->entry = entry; t->prio = t->init_prio = prio; t->stack = base; t->stack_size = stack; t->attr = attr;
    t->state = T_DORMANT;
    threads[slot] = t;
    TRACE("thread create %s id=%x entry=%08X prio=%d stack=%08X+%X", t->name, t->id, entry, prio, base, stack);
    return t->id;
}

/* A thread whose body is native code, with its own guest stack and Cpu, so it can call game
 * callbacks exactly as a PSP system thread would (ad-hoc handlers, for example). */
uint32_t rt_thread_create_native(const char *name, void (*fn)(Cpu *c), int prio) {
    uint32_t id = rt_thread_create(name, 0, prio, 0x4000, 0);
    Thread *t = find_thread(id);
    if (t) { t->native = fn; rt_thread_start(id, 0, 0); }
    return id;
}

int rt_thread_start(uint32_t id, uint32_t argsize, uint32_t argp) {
    Thread *t = find_thread(id);
    if (!t) return (int)ERR_UNKNOWN_UID;
    if (t->state != T_DORMANT) return (int)ERR_NOT_DORMANT;
    memset(&t->cpu, 0, sizeof t->cpu);
    if (!(t->attr & 0x00100000)) memset(HOST(t->stack), 0xFF, t->stack_size);   /* fill stack, like the PSP */
    uint32_t sp = t->stack + t->stack_size;
    sp -= 256;                                     /* the k0 block at the top of the stack */
    memset(HOST(sp), 0, 256);
    WW(sp + 0xC0, t->id); WW(sp + 0xC8, t->stack); WW(sp + 0xF8, 0xFFFFFFFF); WW(sp + 0xFC, 0xFFFFFFFF);
    WW(t->stack, t->id);
    t->cpu.r[26] = sp;                             /* k0 */
    if (argp && argsize) {
        sp -= (argsize + 15) & ~15u;
        memcpy(HOST(sp), HOST(argp), argsize);
        t->cpu.r[4] = argsize; t->cpu.r[5] = sp;
    }
    sp -= 64;
    t->cpu.r[29] = sp; t->cpu.r[30] = sp; t->cpu.r[31] = 0;
    t->cpu.fcr31 = 0;
    t->prio = t->init_prio; t->wakeup_count = 0;
    make_ready(t, 0);
    TRACE("thread start %s", t->name);
    return 0;
}

/* call guest code as an interrupt/callback would: on the current thread, registers preserved */
void rt_call_guest(Cpu *c, uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2) {
    Cpu save = *c;
    c->r[4] = a0; c->r[5] = a1; c->r[6] = a2;
    c->r[29] = (c->r[29] - 0x400) & ~15u;
    c->r[31] = 0;
    rt_call_indirect(c, addr);
    *c = save;
}

void rt_wait_vblank(Cpu *c) { block(c, W_VBLANK, 0, 0); }

void rt_delay(Cpu *c, uint64_t us) {
    Thread *t = current;
    (void)c;
    t->state = T_WAITING; t->wait = W_DELAY; t->timeout_ptr = 0;
    t->wake_at = now_us + (us < 200 ? 200 : us);
    to_scheduler();
}

void rt_kernel_init(void) {}

static int run_callbacks(Cpu *c);

/* ---------------------------------------------------------------- ThreadManForUser */
void hle_sceKernelCreateThread(Cpu *c) {
    RET(rt_thread_create(rt_str(ARG(0)), ARG(1), (int)ARG(2), ARG(3), ARG(4)));
}
void hle_sceKernelStartThread(Cpu *c) {
    int r = rt_thread_start(ARG(0), ARG(1), ARG(2));
    c->r[2] = (uint32_t)r;
    rt_yield_if_preempted();
}
static void exit_current(uint32_t status, int del) {
    Thread *t = current;
    t->exit_status = status; t->state = del ? T_DEAD : T_DORMANT;
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *o = threads[i];
        if (o && o->state == T_WAITING && o->wait == W_THREADEND && o->wait_obj == t->id) wake(o, 0);
    }
    graveyard[ngrave++] = t->fiber; t->fiber = NULL;
    if (del) { for (int i = 0; i < MAX_THREADS; i++) if (threads[i] == t) threads[i] = NULL; rt_free(t->stack); }
    TRACE("thread exit %s (%x)", t->name, status);
    to_scheduler();     /* never returns */
}
void hle_sceKernelExitThread(Cpu *c) { exit_current(ARG(0), 0); }
void hle_sceKernelExitDeleteThread(Cpu *c) { exit_current(ARG(0), 1); }
static int kill_thread(uint32_t id, int del) {
    Thread *t = find_thread(id);
    if (!t) return (int)ERR_UNKNOWN_UID;
    if (t == current) { exit_current(0x800201AC, del); return 0; }
    if (t->fiber) { graveyard[ngrave++] = t->fiber; t->fiber = NULL; }
    t->state = T_DORMANT;
    if (del) { for (int i = 0; i < MAX_THREADS; i++) if (threads[i] == t) threads[i] = NULL; rt_free(t->stack); }
    return 0;
}
void hle_sceKernelTerminateThread(Cpu *c) { RET(kill_thread(ARG(0), 0)); }
void hle_sceKernelTerminateDeleteThread(Cpu *c) { RET(kill_thread(ARG(0), 1)); }
void hle_sceKernelDeleteThread(Cpu *c) {
    Thread *t = find_thread(ARG(0));
    if (!t) RET(ERR_UNKNOWN_UID);
    if (t->state != T_DORMANT) RET(ERR_NOT_DORMANT);
    for (int i = 0; i < MAX_THREADS; i++) if (threads[i] == t) threads[i] = NULL;
    rt_free(t->stack);
    RET(0);
}
void hle_sceKernelGetThreadId(Cpu *c) { RET(current->id); }
void hle_sceKernelSleepThread(Cpu *c) {
    if (current->wakeup_count > 0) { current->wakeup_count--; RET(0); }
    RET(block(c, W_SLEEP, 0, 0));
}
void hle_sceKernelWakeupThread(Cpu *c) {
    Thread *t = find_thread(ARG(0));
    if (!t) RET(ERR_UNKNOWN_UID);
    if (t->state == T_WAITING && t->wait == W_SLEEP) wake(t, 0); else t->wakeup_count++;
    c->r[2] = 0;
    rt_yield_if_preempted();
}
void hle_sceKernelCancelWakeupThread(Cpu *c) {
    Thread *t = find_thread(ARG(0));
    if (!t) RET(ERR_UNKNOWN_UID);
    int n = t->wakeup_count; t->wakeup_count = 0; RET(n);
}
void hle_sceKernelWaitThreadEnd(Cpu *c) {
    Thread *t = find_thread(ARG(0));
    if (!t) RET(ERR_UNKNOWN_UID);
    if (t->state == T_DORMANT || t->state == T_DEAD) RET(t->exit_status);
    uint32_t r = block(c, W_THREADEND, t->id, ARG(1));
    RET(r ? r : t->exit_status);
}
void hle_sceKernelDelayThread(Cpu *c) { rt_delay(c, ARG(0)); RET(0); }
void hle_sceKernelDelayThreadCB(Cpu *c) { uint32_t us = ARG(0); run_callbacks(c); rt_delay(c, us); run_callbacks(c); RET(0); }

/* ---------------------------------------------------------------- semaphores */
typedef struct { uint32_t id; char name[32]; int count, max; uint32_t attr; } Sema;
static Sema semas[256];
static Sema *find_sema(uint32_t id) { for (int i = 0; i < 256; i++) if (semas[i].id == id) return &semas[i]; return NULL; }

static void sema_wake(Sema *s) {
    for (;;) {
        Thread *pick = NULL;
        for (int i = 0; i < MAX_THREADS; i++) {
            Thread *t = threads[i];
            if (!t || t->state != T_WAITING || t->wait != W_SEMA || t->wait_obj != s->id) continue;
            if ((int)t->wait_arg0 > s->count) continue;
            if (!pick) pick = t;
            else if (s->attr & 0x100 ? t->prio < pick->prio : t->ready_seq < pick->ready_seq) pick = t;
        }
        if (!pick) return;
        s->count -= (int)pick->wait_arg0;
        wake(pick, 0);
    }
}
void hle_sceKernelCreateSema(Cpu *c) {
    for (int i = 0; i < 256; i++) if (!semas[i].id) {
        Sema *s = &semas[i]; s->id = rt_new_uid();
        strncpy(s->name, rt_str(ARG(0)) ? rt_str(ARG(0)) : "", 31);
        s->attr = ARG(1); s->count = (int)ARG(2); s->max = (int)ARG(3);
        TRACE("sema create %s id=%x count=%d max=%d", s->name, s->id, s->count, s->max);
        RET(s->id);
    }
    RET(ERR_NO_MEMORY);
}
void hle_sceKernelDeleteSema(Cpu *c) {
    Sema *s = find_sema(ARG(0)); if (!s) RET(ERR_UNKNOWN_UID);
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = threads[i];
        if (t && t->state == T_WAITING && t->wait == W_SEMA && t->wait_obj == s->id) wake(t, ERR_WAIT_DELETE);
    }
    s->id = 0; RET(0);
}
void hle_sceKernelSignalSema(Cpu *c) {
    Sema *s = find_sema(ARG(0)); if (!s) RET(ERR_UNKNOWN_UID);
    int n = (int)ARG(1);
    if (s->count + n > s->max) RET(0x800201BC);   /* semaphore overflow */
    s->count += n;
    sema_wake(s);
    c->r[2] = 0;
    rt_yield_if_preempted();
}
void hle_sceKernelWaitSema(Cpu *c) {
    Sema *s = find_sema(ARG(0)); if (!s) RET(ERR_UNKNOWN_UID);
    int n = (int)ARG(1);
    if (n <= 0) RET(ERR_ILLEGAL_COUNT);
    if (s->count >= n) { s->count -= n; RET(0); }
    current->wait_arg0 = (uint32_t)n;
    RET(block(c, W_SEMA, s->id, ARG(2)));
}

/* ---------------------------------------------------------------- event flags */
typedef struct { uint32_t id; char name[32]; uint32_t attr, bits; } Evf;
static Evf evfs[256];
static Evf *find_evf(uint32_t id) { for (int i = 0; i < 256; i++) if (evfs[i].id == id) return &evfs[i]; return NULL; }

static int evf_match(Evf *e, uint32_t bits, uint32_t mode) {
    return (mode & 1) ? (e->bits & bits) != 0 : (e->bits & bits) == bits;   /* bit0: OR, else AND */
}
static void evf_take(Evf *e, uint32_t bits, uint32_t mode, uint32_t outp) {
    if (outp) WW(outp, e->bits);
    if (mode & 0x20) e->bits = 0;            /* CLEARALL */
    else if (mode & 0x10) e->bits &= ~bits;  /* CLEAR */
}
void hle_sceKernelCreateEventFlag(Cpu *c) {
    for (int i = 0; i < 256; i++) if (!evfs[i].id) {
        Evf *e = &evfs[i]; e->id = rt_new_uid();
        strncpy(e->name, rt_str(ARG(0)) ? rt_str(ARG(0)) : "", 31);
        e->attr = ARG(1); e->bits = ARG(2);
        TRACE("evf create %s id=%x bits=%x", e->name, e->id, e->bits);
        RET(e->id);
    }
    RET(ERR_NO_MEMORY);
}
void hle_sceKernelDeleteEventFlag(Cpu *c) {
    Evf *e = find_evf(ARG(0)); if (!e) RET(ERR_UNKNOWN_UID);
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = threads[i];
        if (t && t->state == T_WAITING && t->wait == W_EVF && t->wait_obj == e->id) wake(t, ERR_WAIT_DELETE);
    }
    e->id = 0; RET(0);
}
void hle_sceKernelSetEventFlag(Cpu *c) {
    Evf *e = find_evf(ARG(0)); if (!e) RET(ERR_UNKNOWN_UID);
    e->bits |= ARG(1);
    for (int i = 0; i < MAX_THREADS; i++) {
        Thread *t = threads[i];
        if (t && t->state == T_WAITING && t->wait == W_EVF && t->wait_obj == e->id && evf_match(e, t->wait_arg0, t->wait_arg1)) {
            evf_take(e, t->wait_arg0, t->wait_arg1, t->wait_arg2);
            wake(t, 0);
        }
    }
    c->r[2] = 0;
    rt_yield_if_preempted();
}
void hle_sceKernelClearEventFlag(Cpu *c) {
    Evf *e = find_evf(ARG(0)); if (!e) RET(ERR_UNKNOWN_UID);
    e->bits &= ARG(1); RET(0);
}
void hle_sceKernelWaitEventFlag(Cpu *c) {
    Evf *e = find_evf(ARG(0)); if (!e) RET(ERR_UNKNOWN_UID);
    uint32_t bits = ARG(1), mode = ARG(2), outp = ARG(3);
    if (evf_match(e, bits, mode)) { evf_take(e, bits, mode, outp); RET(0); }
    current->wait_arg0 = bits; current->wait_arg1 = mode; current->wait_arg2 = outp;
    RET(block(c, W_EVF, e->id, ARG(4)));
}
void hle_sceKernelPollEventFlag(Cpu *c) {
    Evf *e = find_evf(ARG(0)); if (!e) RET(ERR_UNKNOWN_UID);
    uint32_t bits = ARG(1), mode = ARG(2), outp = ARG(3);
    if (evf_match(e, bits, mode)) { evf_take(e, bits, mode, outp); RET(0); }
    if (outp) WW(outp, e->bits);
    RET(ERR_EVF_COND);
}

/* ---------------------------------------------------------------- callbacks
 * A callback belongs to the thread that created it and only runs when that thread is in a
 * "CB" wait (DelayThreadCB here). rt_notify_callback() is what devices use to fire one. */
typedef struct { uint32_t id, func, arg, owner, notify_count, notify_arg; char name[32]; } Callback;
static Callback cbs[64];
void hle_sceKernelCreateCallback(Cpu *c) {
    for (int i = 0; i < 64; i++) if (!cbs[i].id) {
        Callback *b = &cbs[i]; memset(b, 0, sizeof *b);
        b->id = rt_new_uid(); b->func = ARG(1); b->arg = ARG(2); b->owner = current->id;
        strncpy(b->name, rt_str(ARG(0)) ? rt_str(ARG(0)) : "", 31);
        TRACE("callback %s -> %08X id=%x", b->name, b->func, b->id);
        RET(b->id);
    }
    RET(ERR_NO_MEMORY);
}
void hle_sceKernelDeleteCallback(Cpu *c) {
    for (int i = 0; i < 64; i++) if (cbs[i].id == ARG(0)) { cbs[i].id = 0; RET(0); }
    RET(ERR_UNKNOWN_UID);
}
int rt_notify_callback(uint32_t id, uint32_t arg) {
    for (int i = 0; i < 64; i++) if (cbs[i].id == id) { cbs[i].notify_count++; cbs[i].notify_arg = arg; return 1; }
    return 0;
}
static int run_callbacks(Cpu *c) {
    int ran = 0;
    for (int i = 0; i < 64; i++) {
        Callback *b = &cbs[i];
        if (!b->id || b->owner != current->id || !b->notify_count) continue;
        uint32_t count = b->notify_count, arg = b->notify_arg;
        b->notify_count = 0;
        TRACE("run callback %s (%u, %x)", b->name, count, arg);
        rt_call_guest(c, b->func, count, arg, b->arg);
        ran = 1;
    }
    return ran;
}

/* ---------------------------------------------------------------- interrupts */
void hle_sceKernelCpuSuspendIntr(Cpu *c) { RET(1); }
void hle_sceKernelCpuResumeIntr(Cpu *c) { (void)c; }
