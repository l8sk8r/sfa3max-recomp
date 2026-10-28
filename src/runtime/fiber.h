/* Minimal fibers: one per game thread. Windows fibers, or ucontext elsewhere (for Linux test builds). */
#pragma once
#include <stddef.h>

typedef struct Fiber Fiber;
typedef void (*FiberFn)(void *arg);

Fiber *fiber_main(void);                                  /* turn the calling host thread into a fiber */
Fiber *fiber_create(FiberFn fn, void *arg, size_t stack_bytes);
void   fiber_switch(Fiber *to);
void   fiber_delete(Fiber *f);                            /* never on the running fiber */
