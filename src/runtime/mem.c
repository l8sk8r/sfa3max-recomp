/* Guest memory and the user-partition allocator (sceKernelAllocPartitionMemory, thread stacks). */
#include "rt.h"
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

uint8_t *rt_mem;

typedef struct { uint32_t lo, hi; char name[32]; } Block;
static Block blocks[1024];
static int nblocks;

int rt_mem_init(void) {
    size_t size = 0x10000000u;   /* the 256 MB physical window; untouched pages cost nothing */
#ifdef _WIN32
    rt_mem = (uint8_t *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    rt_mem = (uint8_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (rt_mem == MAP_FAILED) rt_mem = NULL;
#endif
    return rt_mem != NULL;
}

static void insert(uint32_t lo, uint32_t hi, const char *name) {
    int i = nblocks;
    if (nblocks >= (int)(sizeof blocks / sizeof blocks[0])) rt_fatal("allocator: too many blocks");
    while (i > 0 && blocks[i - 1].lo > lo) { blocks[i] = blocks[i - 1]; i--; }
    blocks[i].lo = lo; blocks[i].hi = hi;
    strncpy(blocks[i].name, name ? name : "", sizeof blocks[i].name - 1);
    blocks[i].name[sizeof blocks[i].name - 1] = 0;
    nblocks++;
}

void rt_reserve(uint32_t lo, uint32_t hi, const char *name) { insert(lo, hi, name); }

uint32_t rt_alloc(uint32_t size, int high, uint32_t align, const char *name) {
    if (align < 256) align = 256;
    size = (size + 255) & ~255u;
    if (!high) {
        uint32_t cand = USER_LO;
        for (int i = 0; i <= nblocks; i++) {
            uint32_t a = (cand + align - 1) & ~(align - 1);
            uint32_t limit = i < nblocks ? blocks[i].lo : USER_HI;
            if (a + size <= limit && a + size > a) { insert(a, a + size, name); return a; }
            if (i < nblocks && blocks[i].hi > cand) cand = blocks[i].hi;
        }
    } else {
        uint32_t cand = USER_HI;
        for (int i = nblocks - 1; i >= -1; i--) {
            uint32_t limit = i >= 0 ? blocks[i].hi : USER_LO;
            if (cand >= size) {
                uint32_t a = (cand - size) & ~(align - 1);
                if (a >= limit) { insert(a, a + size, name); return a; }
            }
            if (i >= 0 && blocks[i].lo < cand) cand = blocks[i].lo;
        }
    }
    return 0;
}

uint32_t rt_alloc_at(uint32_t addr, uint32_t size, const char *name) {
    size = (size + 255) & ~255u;
    for (int i = 0; i < nblocks; i++)
        if (addr < blocks[i].hi && addr + size > blocks[i].lo) return 0;
    insert(addr, addr + size, name);
    return addr;
}

int rt_free(uint32_t addr) {
    for (int i = 0; i < nblocks; i++)
        if (blocks[i].lo == addr) {
            memmove(&blocks[i], &blocks[i + 1], (size_t)(nblocks - i - 1) * sizeof(Block));
            nblocks--;
            return 1;
        }
    return 0;
}
