/* sceGe_user: display lists are walked so that the game's signal/finish callbacks run as on hardware.
 * Drawing itself comes in M2; for now the lists are counted and the first ones logged. */
#include "rt.h"
#include <string.h>
#include "ge.h"

#define EDRAM 0x04000000u

typedef struct { uint32_t id, start, pc, stall, base, ret[8]; int depth, done, cb; uint32_t arg; } GeList;
static GeList lists[64];
static uint32_t cb_signal, cb_signal_arg, cb_finish, cb_finish_arg;
static int cb_set;
static uint64_t lists_total, cmds_total, lists_this_frame;
static int logged;
static uint64_t op_count[256], prim_count[8], vtype_seen[64]; static uint32_t vtypes[64]; static int nvt;
static uint32_t cur_vtype;

void rt_ge_init(void) {}

/* Run a list until END, a stall, or a hard limit. Returns 1 when the list completed. */
static int ge_run(Cpu *c, GeList *l) {
    int steps = 0;
    if (ge_use_gl) ge_gl_list_start();
    while (!l->done) {
        if (l->stall && (l->pc & 0x0FFFFFFF) == (l->stall & 0x0FFFFFFF)) return 0;
        if (++steps > 2000000) { rt_log("GE: list %u ran away at %08X", l->id, l->pc); l->done = 1; break; }
        uint32_t w = RW(l->pc), op = w >> 24, arg = w & 0xFFFFFF;
        l->pc += 4; cmds_total++; op_count[op]++;
        if (op == 0x12) { cur_vtype = arg; int k; for (k = 0; k < nvt && vtypes[k] != arg; k++) {} if (k == nvt && nvt < 64) vtypes[nvt++] = arg; if (k < 64) vtype_seen[k]++; }
        if (op == 0x04) prim_count[(arg >> 16) & 7]++;
        switch (op) {
        case 0x08: l->pc = ge_soft_reladdr(arg) & 0x0FFFFFFC; break;                /* JUMP */
        case 0x0A: if (l->depth < 8) l->ret[l->depth++] = l->pc; l->pc = ge_soft_reladdr(arg) & 0x0FFFFFFC; break; /* CALL */
        case 0x0B: if (l->depth) l->pc = l->ret[--l->depth]; break;                 /* RET */
        case 0x0E: {                                                                 /* SIGNAL */
            uint32_t next = RW(l->pc);
            if ((next >> 24) == 0x0C) {
                if (cb_set && cb_signal) rt_call_guest(c, cb_signal, arg & 0xFFFF, cb_signal_arg, l->pc);
            }
            break;
        }
        case 0x0F: break;                                                            /* FINISH */
        case 0x0C: {                                                                 /* END */
            uint32_t prev = RW(l->pc - 8);
            l->done = 1;
            if ((prev >> 24) == 0x0F && cb_set && cb_finish) rt_call_guest(c, cb_finish, prev & 0xFFFF, cb_finish_arg, l->pc);
            break;
        }
        default: ge_soft_cmd(l->pc - 4, op, arg); break;
        }
    }
    return 1;
}

void hle_sceGeEdramGetAddr(Cpu *c) { RET(EDRAM); }
static void enqueue(Cpu *c, int head) {
    (void)head;
    for (int i = 0; i < 64; i++) if (!lists[i].id || lists[i].done) {
        GeList *l = &lists[i];
        memset(l, 0, sizeof *l);
        l->id = rt_new_uid(); l->start = l->pc = ARG(0) & 0x0FFFFFFF; l->stall = ARG(1) & 0x0FFFFFFF; l->cb = (int)ARG(2);
        lists_total++; lists_this_frame++;
        if (lists_total == 1) rt_log("GE: first display list submitted at %08X (stall %08X), frame %llu",
                                     l->start, l->stall, (unsigned long long)rt_vblank_count());
        uint32_t id = l->id;
        ge_run(c, l);
        RET(id);
    }
    RET(0x80000021);
}
void hle_sceGeListEnQueue(Cpu *c) { enqueue(c, 0); }
void hle_sceGeListEnQueueHead(Cpu *c) { enqueue(c, 1); }
void hle_sceGeListUpdateStallAddr(Cpu *c) {
    for (int i = 0; i < 64; i++) if (lists[i].id == ARG(0)) { lists[i].stall = ARG(1) & 0x0FFFFFFF; ge_run(c, &lists[i]); RET(0); }
    RET(0x80000100);
}
void hle_sceGeListSync(Cpu *c) { RET(0); }      /* 0 = completed */
void hle_sceGeDrawSync(Cpu *c) { RET(0); }
void hle_sceGeBreak(Cpu *c) { RET(0); }
void hle_sceGeContinue(Cpu *c) { RET(0); }
void hle_sceGeSetCallback(Cpu *c) {
    uint32_t p = ARG(0);
    cb_signal = RW(p); cb_signal_arg = RW(p + 4); cb_finish = RW(p + 8); cb_finish_arg = RW(p + 12); cb_set = 1;
    TRACE("GeSetCallback signal=%08X finish=%08X", cb_signal, cb_finish);
    RET(1);
}
void hle_sceGeUnsetCallback(Cpu *c) { cb_set = 0; RET(0); }

uint64_t rt_ge_lists(void) { return lists_total; }
void rt_ge_census(void) {
    for (int i = 0; i < 256; i++) if (op_count[i]) rt_log("GE op %02X: %llu", i, (unsigned long long)op_count[i]);
    for (int i = 0; i < 8; i++) if (prim_count[i]) rt_log("GE prim type %d: %llu", i, (unsigned long long)prim_count[i]);
    for (int i = 0; i < nvt; i++) rt_log("GE vtype %06X: %llu", vtypes[i], (unsigned long long)vtype_seen[i]);
}
uint64_t rt_ge_cmds(void) { return cmds_total; }
uint64_t rt_ge_take_frame_lists(void) { uint64_t n = lists_this_frame; lists_this_frame = 0; return n; }
