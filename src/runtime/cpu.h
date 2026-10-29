/* CPU state and memory access used by the translated PSP code. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef struct Cpu {
    uint32_t r[32];
    uint32_t hi, lo;
    uint32_t f[32];        /* FPU registers kept as raw bits */
    uint32_t fcr31;
    int fcc;               /* FPU condition flag */
} Cpu;

typedef void (*GuestFn)(Cpu *c);
typedef struct { uint32_t addr; GuestFn fn; } FuncEntry;
typedef struct { uint32_t addr; GuestFn fn; const char *module, *name; } StubEntry;

/* Guest memory: one host block mirrors the PSP's 256 MB physical window.
   Cached/uncached/kernel mirrors (0x4..., 0x8...) fold onto it. */
extern uint8_t *rt_mem;
#define HOST(a) (rt_mem + ((uint32_t)(a) & 0x0FFFFFFFu))

static inline uint32_t RW(uint32_t a) { uint32_t v; memcpy(&v, HOST(a), 4); return v; }
static inline uint32_t RH(uint32_t a) { uint16_t v; memcpy(&v, HOST(a), 2); return v; }
static inline uint32_t RB(uint32_t a) { return *HOST(a); }
static inline void WW(uint32_t a, uint32_t v) { memcpy(HOST(a), &v, 4); }
static inline void WH(uint32_t a, uint32_t v) { uint16_t h = (uint16_t)v; memcpy(HOST(a), &h, 2); }
static inline void WB(uint32_t a, uint32_t v) { *HOST(a) = (uint8_t)v; }

/* Loads and stores from translated code carry the game address of the instruction. Normally they
   compile to plain accesses; build with SFA3MR_WATCH to log accesses to a watched range (--watch). */
#ifdef SFA3MR_WATCH
extern uint32_t rt_watch_lo, rt_watch_hi;
void rt_watch_hit(uint32_t addr, uint32_t pc, uint32_t value, int size, int write);
static inline uint32_t MRW(uint32_t a, uint32_t pc) { uint32_t v = RW(a); if ((a & 0x0FFFFFFF) - rt_watch_lo < rt_watch_hi - rt_watch_lo) rt_watch_hit(a, pc, v, 4, 0); return v; }
static inline uint32_t MRH(uint32_t a, uint32_t pc) { uint32_t v = RH(a); if ((a & 0x0FFFFFFF) - rt_watch_lo < rt_watch_hi - rt_watch_lo) rt_watch_hit(a, pc, v, 2, 0); return v; }
static inline uint32_t MRB(uint32_t a, uint32_t pc) { uint32_t v = RB(a); if ((a & 0x0FFFFFFF) - rt_watch_lo < rt_watch_hi - rt_watch_lo) rt_watch_hit(a, pc, v, 1, 0); return v; }
static inline void MWW(uint32_t a, uint32_t v, uint32_t pc) { if ((a & 0x0FFFFFFF) - rt_watch_lo < rt_watch_hi - rt_watch_lo) rt_watch_hit(a, pc, v, 4, 1); WW(a, v); }
static inline void MWH(uint32_t a, uint32_t v, uint32_t pc) { if ((a & 0x0FFFFFFF) - rt_watch_lo < rt_watch_hi - rt_watch_lo) rt_watch_hit(a, pc, v & 0xFFFF, 2, 1); WH(a, v); }
static inline void MWB(uint32_t a, uint32_t v, uint32_t pc) { if ((a & 0x0FFFFFFF) - rt_watch_lo < rt_watch_hi - rt_watch_lo) rt_watch_hit(a, pc, v & 0xFF, 1, 1); WB(a, v); }
#else
#define MRW(a, pc) RW(a)
#define MRH(a, pc) RH(a)
#define MRB(a, pc) RB(a)
#define MWW(a, v, pc) WW(a, v)
#define MWH(a, v, pc) WH(a, v)
#define MWB(a, v, pc) WB(a, v)
#endif

/* unaligned word pairs (little endian) */
static inline uint32_t LWL(uint32_t a, uint32_t rt) {
    unsigned s = (a & 3) * 8; uint32_t w = RW(a & ~3u);
    return (rt & (0x00FFFFFFu >> s)) | (w << (24 - s));
}
static inline uint32_t LWR(uint32_t a, uint32_t rt) {
    unsigned s = (a & 3) * 8; uint32_t w = RW(a & ~3u);
    return (rt & ~(0xFFFFFFFFu >> s)) | (w >> s);
}
static inline void SWL(uint32_t a, uint32_t rt) {
    unsigned s = (a & 3) * 8; uint32_t w = RW(a & ~3u);
    WW(a & ~3u, (w & ~(0xFFFFFFFFu >> (24 - s))) | (rt >> (24 - s)));
}
static inline void SWR(uint32_t a, uint32_t rt) {
    unsigned s = (a & 3) * 8; uint32_t w = RW(a & ~3u);
    WW(a & ~3u, (w & ~(0xFFFFFFFFu << s)) | (rt << s));
}

#define ROTR(x, n) (((x) >> (n)) | ((x) << ((32 - (n)) & 31)))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#if defined(_MSC_VER)
#include <intrin.h>
static inline uint32_t CLZ(uint32_t x) { unsigned long i; return _BitScanReverse(&i, x) ? 31 - i : 32; }
#define BSWAP32(x) _byteswap_ulong(x)
#else
static inline uint32_t CLZ(uint32_t x) { return x ? (uint32_t)__builtin_clz(x) : 32; }
#define BSWAP32(x) __builtin_bswap32(x)
#endif
static inline uint32_t BITREV(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1); x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4); return BSWAP32(x);
}

/* Allegrex divide: no exception; divide by zero gives the hardware's values */
static inline void DIV(Cpu *c, uint32_t s, uint32_t t) {
    int32_t a = (int32_t)s, b = (int32_t)t;
    if (b == 0) { c->hi = s; c->lo = a < 0 ? 1 : 0xFFFFFFFFu; }
    else if (a == INT32_MIN && b == -1) { c->lo = 0x80000000u; c->hi = 0; }
    else { c->lo = (uint32_t)(a / b); c->hi = (uint32_t)(a % b); }
}
static inline void DIVU(Cpu *c, uint32_t s, uint32_t t) {
    if (t == 0) { c->hi = s; c->lo = 0xFFFFFFFFu; } else { c->lo = s / t; c->hi = s % t; }
}

static inline float FS(Cpu *c, int n) { float f; memcpy(&f, &c->f[n], 4); return f; }
static inline void FSET(Cpu *c, int n, float v) { memcpy(&c->f[n], &v, 4); }
static inline int32_t CVT_TRUNC(Cpu *c, float v) { (void)c; if (v != v) return 0x7FFFFFFF; if (v >= 2147483648.0f) return 0x7FFFFFFF; if (v < -2147483648.0f) return INT32_MIN; return (int32_t)v; }
static inline int32_t CVT_ROUND(Cpu *c, float v) { return CVT_TRUNC(c, rintf(v)); }
static inline int32_t CVT_CEIL(Cpu *c, float v) { return CVT_TRUNC(c, ceilf(v)); }
static inline int32_t CVT_FLOOR(Cpu *c, float v) { return CVT_TRUNC(c, floorf(v)); }
static inline int32_t CVT_W(Cpu *c, float v) {
    switch (c->fcr31 & 3) { case 0: return CVT_ROUND(c, v); case 1: return CVT_TRUNC(c, v); case 2: return CVT_CEIL(c, v); default: return CVT_FLOOR(c, v); }
}
/* c.cond.s: bit0 unordered, bit1 equal, bit2 less */
static inline int FCMP(float a, float b, int cond) {
    int un = (a != a) || (b != b);
    return ((cond & 1) && un) || ((cond & 2) && !un && a == b) || ((cond & 4) && !un && a < b);
}

/* runtime services the translated code calls */
void rt_call_indirect(Cpu *c, uint32_t addr);
void rt_jump_indirect(Cpu *c, uint32_t addr);
void rt_bad_jump(Cpu *c, uint32_t addr, uint32_t from);
void rt_syscall(Cpu *c, uint32_t code, uint32_t pc);
void rt_break(Cpu *c, uint32_t pc);
void rt_unsupported(Cpu *c, uint32_t pc);
