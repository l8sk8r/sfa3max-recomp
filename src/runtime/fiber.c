#include "fiber.h"
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Fiber { LPVOID h; FiberFn fn; void *arg; int is_main; };

static void WINAPI tramp(LPVOID p) { Fiber *f = (Fiber *)p; f->fn(f->arg); }

Fiber *fiber_main(void) {
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    f->h = ConvertThreadToFiber(NULL);
    if (!f->h) f->h = GetCurrentFiber();
    f->is_main = 1;
    return f;
}
Fiber *fiber_create(FiberFn fn, void *arg, size_t stack) {
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    f->fn = fn; f->arg = arg;
    f->h = CreateFiberEx(64 * 1024, stack, FIBER_FLAG_FLOAT_SWITCH, tramp, f);
    if (!f->h) { free(f); return NULL; }
    return f;
}
void fiber_switch(Fiber *to) { SwitchToFiber(to->h); }
void fiber_delete(Fiber *f) { if (f && !f->is_main) { DeleteFiber(f->h); free(f); } }

#else
#include <ucontext.h>

struct Fiber { ucontext_t ctx; FiberFn fn; void *arg; void *stack; };
static Fiber *current;

static void tramp(unsigned lo, unsigned hi) {
    Fiber *f = (Fiber *)(((uintptr_t)hi << 32) | (uintptr_t)lo);
    f->fn(f->arg);
    abort();   /* a fiber function must never return */
}
Fiber *fiber_main(void) {
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    current = f;
    return f;
}
Fiber *fiber_create(FiberFn fn, void *arg, size_t stack) {
    Fiber *f = (Fiber *)calloc(1, sizeof *f);
    f->fn = fn; f->arg = arg; f->stack = malloc(stack);
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack; f->ctx.uc_stack.ss_size = stack; f->ctx.uc_link = NULL;
    uintptr_t p = (uintptr_t)f;
    makecontext(&f->ctx, (void (*)(void))tramp, 2, (unsigned)(p & 0xFFFFFFFFu), (unsigned)(p >> 32));
    return f;
}
void fiber_switch(Fiber *to) {
    Fiber *from = current; current = to;
    swapcontext(&from->ctx, &to->ctx);
}
void fiber_delete(Fiber *f) { if (f) { free(f->stack); free(f); } }
#endif
