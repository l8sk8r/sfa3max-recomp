/* Shared declarations for the native runtime (the PSP's OS, re-implemented). */
#pragma once
#include "cpu.h"
#include "funcs.h"     /* generated: every translated function and every OS function (hle_*) */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* ---- arguments and results of OS functions (PSP EABI: a0-a3, t0-t3; result in v0/v1) ---- */
#define ARG(n)    (c->r[4 + (n)])
#define ARG64(n)  ((uint64_t)c->r[4 + (n)] | ((uint64_t)c->r[5 + (n)] << 32))   /* n must be even */
#define RET(v)    do { c->r[2] = (uint32_t)(v); return; } while (0)
#define RET64(v)  do { uint64_t r_ = (uint64_t)(v); c->r[2] = (uint32_t)r_; c->r[3] = (uint32_t)(r_ >> 32); return; } while (0)

/* PSP error codes used here */
#define ERR_WAIT_TIMEOUT      0x800201A8u
#define ERR_WAIT_DELETE       0x800201B5u
#define ERR_WAIT_CANCEL       0x800201B7u
#define ERR_UNKNOWN_UID       0x800200CBu
#define ERR_NOT_DORMANT       0x800201A4u
#define ERR_DORMANT           0x800201A2u
#define ERR_ILLEGAL_COUNT     0x800201BDu
#define ERR_SEMA_ZERO         0x800201ADu
#define ERR_EVF_COND          0x800201AFu
#define ERR_NO_MEMORY         0x80020190u
#define ERR_FILE_NOT_FOUND    0x80010002u
#define ERR_BAD_FD            0x80020323u

/* ---- logging ---- */
extern int rt_trace;                 /* 1: log every OS call */
void rt_log(const char *fmt, ...);
void rt_fatal(const char *fmt, ...);
#define TRACE(...) do { if (rt_trace) rt_log(__VA_ARGS__); } while (0)

/* ---- memory ---- */
#define USER_LO 0x08800000u
#define USER_HI 0x0A000000u
int      rt_mem_init(void);
uint32_t rt_alloc(uint32_t size, int high, uint32_t align, const char *name);   /* 0 on failure */
uint32_t rt_alloc_at(uint32_t addr, uint32_t size, const char *name);
int      rt_free(uint32_t addr);
void     rt_reserve(uint32_t lo, uint32_t hi, const char *name);
static inline char *rt_str(uint32_t a) { return a ? (char *)HOST(a) : NULL; }

/* ---- config ---- */
extern char rt_disc_root[1024];      /* folder holding PSP_GAME/ (the extracted disc) */
extern char rt_save_root[1024];      /* folder for ms0: (saves) */
extern int  rt_realtime;             /* 1: pace to 59.94 Hz wall clock, 0: run as fast as possible */
extern long rt_max_frames;           /* stop after this many vblanks (0 = never) */
extern volatile int rt_quit;

/* ---- kernel ---- */
typedef struct Thread Thread;
Thread  *rt_cur(void);
uint32_t rt_cur_id(void);
uint64_t rt_now_us(void);
uint64_t rt_vblank_count(void);
void     rt_kernel_init(void);
void     rt_run(void);                                      /* the scheduler: returns when the game quits */
uint32_t rt_thread_create(const char *name, uint32_t entry, int prio, uint32_t stack, uint32_t attr);
int      rt_thread_start(uint32_t id, uint32_t argsize, uint32_t argp);
uint32_t rt_thread_create_native(const char *name, void (*fn)(Cpu *c), int prio);
void     rt_yield_if_preempted(void);
void     rt_wait_vblank(Cpu *c);                            /* block the current thread until the next vblank */
void     rt_delay(Cpu *c, uint64_t us);                     /* block the current thread for us microseconds */
void     rt_call_guest(Cpu *c, uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2);  /* interrupt-style call */
uint32_t rt_new_uid(void);
int      rt_notify_callback(uint32_t id, uint32_t arg);    /* queue a callback for its thread */

/* subsystem hooks the scheduler calls */
void rt_display_vblank(void);       /* once per vblank */
void rt_ge_init(void);
void rt_io_init(void);
void rt_report(void);               /* summary at exit */
