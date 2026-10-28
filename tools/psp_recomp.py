#!/usr/bin/env python3
"""Translate the PSP program's MIPS (Allegrex) code into C.

Usage: psp_recomp.py BOOT.BIN analysis.json imports.json OUTDIR [--per-file N]

Every function found by psp_analyze.py becomes `void f_XXXXXXXX(Cpu *c)`.
 * jal X        -> delay slot, ra = return address, call f_X (or the HLE stub for an import)
 * jalr / data pointers -> rt_call_indirect(c, addr) (dispatch table generated here)
 * jr ra        -> delay slot, return
 * jr reg (switch) -> delay slot, switch over the case labels
 * j outside the function -> tail call
 * branch-likely  -> the delay slot runs only when the branch is taken
No game code or data is written except the translated logic itself, which stays in the
build folder (never committed).
"""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psp_analyze import Prog, is_branch, target, sx16

R = ['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
     's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']

class Unsupported(Exception): pass

def gpr(n): return '0' if n == 0 else 'c->r[%d]' % n
def setr(n, expr): return '' if n == 0 else 'c->r[%d] = %s;' % (n, expr)

def translate_op(a, w):
    """C statement(s) for one non-branch instruction."""
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; sa = (w >> 6) & 31
    imm = w & 0xFFFF; simm = sx16(w)
    S, T = gpr(rs), gpr(rt)
    ea = '(%s + %d)' % (S, simm) if simm else S
    if op == 0:
        f = w & 0x3F
        if f == 0x00: return setr(rd, '%s << %d' % (T, sa)) if w else ''
        if f == 0x02: return setr(rd, '%s >> %d' % (T, sa)) if not rs & 1 else setr(rd, 'ROTR(%s, %d)' % (T, sa))
        if f == 0x03: return setr(rd, '(uint32_t)((int32_t)%s >> %d)' % (T, sa))
        if f == 0x04: return setr(rd, '%s << (%s & 31)' % (T, S))
        if f == 0x06: return setr(rd, '%s >> (%s & 31)' % (T, S)) if not sa & 1 else setr(rd, 'ROTR(%s, %s & 31)' % (T, S))
        if f == 0x07: return setr(rd, '(uint32_t)((int32_t)%s >> (%s & 31))' % (T, S))
        if f == 0x0A: return 'if (%s == 0) %s' % (T, setr(rd, S) or ';')
        if f == 0x0B: return 'if (%s != 0) %s' % (T, setr(rd, S) or ';')
        if f == 0x0C: return 'rt_syscall(c, 0x%X, 0x%08X);' % ((w >> 6) & 0xFFFFF, a)
        if f == 0x0D: return 'rt_break(c, 0x%08X);' % a
        if f == 0x0F: return ''                                          # sync
        if f == 0x10: return setr(rd, 'c->hi')
        if f == 0x11: return 'c->hi = %s;' % S
        if f == 0x12: return setr(rd, 'c->lo')
        if f == 0x13: return 'c->lo = %s;' % S
        if f == 0x16: return setr(rd, 'CLZ(%s)' % S)
        if f == 0x17: return setr(rd, 'CLZ(~%s)' % S)
        if f == 0x18: return '{ int64_t m = (int64_t)(int32_t)%s * (int32_t)%s; c->lo = (uint32_t)m; c->hi = (uint32_t)(m >> 32); }' % (S, T)
        if f == 0x19: return '{ uint64_t m = (uint64_t)%s * %s; c->lo = (uint32_t)m; c->hi = (uint32_t)(m >> 32); }' % (S, T)
        if f == 0x1A: return 'DIV(c, %s, %s);' % (S, T)
        if f == 0x1B: return 'DIVU(c, %s, %s);' % (S, T)
        if f == 0x1C: return '{ int64_t m = ((int64_t)c->hi << 32 | c->lo) + (int64_t)(int32_t)%s * (int32_t)%s; c->lo = (uint32_t)m; c->hi = (uint32_t)((uint64_t)m >> 32); }' % (S, T)
        if f == 0x1D: return '{ uint64_t m = ((uint64_t)c->hi << 32 | c->lo) + (uint64_t)%s * %s; c->lo = (uint32_t)m; c->hi = (uint32_t)(m >> 32); }' % (S, T)
        if f in (0x20, 0x21): return setr(rd, '%s + %s' % (S, T))
        if f in (0x22, 0x23): return setr(rd, '%s - %s' % (S, T))
        if f == 0x24: return setr(rd, '%s & %s' % (S, T))
        if f == 0x25: return setr(rd, '%s | %s' % (S, T))
        if f == 0x26: return setr(rd, '%s ^ %s' % (S, T))
        if f == 0x27: return setr(rd, '~(%s | %s)' % (S, T))
        if f == 0x2A: return setr(rd, '(int32_t)%s < (int32_t)%s' % (S, T))
        if f == 0x2B: return setr(rd, '%s < %s' % (S, T))
        if f == 0x2C: return setr(rd, '(uint32_t)MAX((int32_t)%s, (int32_t)%s)' % (S, T))
        if f == 0x2D: return setr(rd, '(uint32_t)MIN((int32_t)%s, (int32_t)%s)' % (S, T))
        if f == 0x2E: return '{ int64_t m = ((int64_t)c->hi << 32 | c->lo) - (int64_t)(int32_t)%s * (int32_t)%s; c->lo = (uint32_t)m; c->hi = (uint32_t)((uint64_t)m >> 32); }' % (S, T)
        if f == 0x2F: return '{ uint64_t m = ((uint64_t)c->hi << 32 | c->lo) - (uint64_t)%s * %s; c->lo = (uint32_t)m; c->hi = (uint32_t)(m >> 32); }' % (S, T)
    if op == 0x08 or op == 0x09: return setr(rt, '%s + 0x%X' % (S, simm & 0xFFFFFFFF)) if simm else setr(rt, S)
    if op == 0x0A: return setr(rt, '(int32_t)%s < %d' % (S, simm))
    if op == 0x0B: return setr(rt, '%s < 0x%XU' % (S, simm & 0xFFFFFFFF))
    if op == 0x0C: return setr(rt, '%s & 0x%X' % (S, imm))
    if op == 0x0D: return setr(rt, '%s | 0x%X' % (S, imm))
    if op == 0x0E: return setr(rt, '%s ^ 0x%X' % (S, imm))
    if op == 0x0F: return setr(rt, '0x%X' % (imm << 16))
    if op == 0x20: return setr(rt, '(uint32_t)(int8_t)MRB(%s, 0x%08X)' % (ea, a))
    if op == 0x21: return setr(rt, '(uint32_t)(int16_t)MRH(%s, 0x%08X)' % (ea, a))
    if op == 0x22: return setr(rt, 'LWL(%s, %s)' % (ea, T))
    if op == 0x23: return setr(rt, 'MRW(%s, 0x%08X)' % (ea, a))
    if op == 0x24: return setr(rt, 'MRB(%s, 0x%08X)' % (ea, a))
    if op == 0x25: return setr(rt, 'MRH(%s, 0x%08X)' % (ea, a))
    if op == 0x26: return setr(rt, 'LWR(%s, %s)' % (ea, T))
    if op == 0x28: return 'MWB(%s, %s, 0x%08X);' % (ea, T, a)
    if op == 0x29: return 'MWH(%s, %s, 0x%08X);' % (ea, T, a)
    if op == 0x2A: return 'SWL(%s, %s);' % (ea, T)
    if op == 0x2B: return 'MWW(%s, %s, 0x%08X);' % (ea, T, a)
    if op == 0x2E: return 'SWR(%s, %s);' % (ea, T)
    if op == 0x2F: return ''                                              # cache
    if op == 0x31: return 'c->f[%d] = RW(%s);' % (rt, ea)                 # lwc1 (FPU regs held as raw bits)
    if op == 0x39: return 'WW(%s, c->f[%d]);' % (ea, rt)                  # swc1
    if op == 0x1F:
        f = w & 0x3F
        if f == 0x00:  # ext rt, rs, pos, size
            pos, size = sa, rd + 1
            return setr(rt, '(%s >> %d) & 0x%XU' % (S, pos, (1 << size) - 1 if size < 32 else 0xFFFFFFFF))
        if f == 0x04:  # ins rt, rs, pos, size
            pos, size = sa, rd - sa + 1
            m = ((1 << size) - 1) << pos & 0xFFFFFFFF
            return setr(rt, '(%s & 0x%XU) | ((%s << %d) & 0x%XU)' % (T, ~m & 0xFFFFFFFF, S, pos, m))
        if f == 0x20:
            if sa == 0x10: return setr(rd, '(uint32_t)(int8_t)%s' % T)
            if sa == 0x18: return setr(rd, '(uint32_t)(int16_t)%s' % T)
            if sa == 0x02: return setr(rd, '((%s & 0x00FF00FFU) << 8) | ((%s >> 8) & 0x00FF00FFU)' % (T, T))
            if sa == 0x03: return setr(rd, 'BSWAP32(%s)' % T)
            if sa == 0x14: return setr(rd, 'BITREV(%s)' % T)
    if op == 0x11:
        fmt = (w >> 21) & 31; ft, fs, fd = rt, rd, sa
        if fmt == 0x00: return setr(rt, 'c->f[%d]' % fs)                 # mfc1
        if fmt == 0x04: return 'c->f[%d] = %s;' % (fs, T)                # mtc1
        if fmt == 0x02: return setr(rt, 'c->fcr31') if fs == 31 else setr(rt, '0x3351')   # cfc1
        if fmt == 0x06: return 'c->fcr31 = %s;' % T if fs == 31 else ''  # ctc1
        if fmt == 0x10:
            f = w & 0x3F
            F = lambda n: 'FS(c,%d)' % n
            if f == 0x00: return 'FSET(c,%d, %s + %s);' % (fd, F(fs), F(ft))
            if f == 0x01: return 'FSET(c,%d, %s - %s);' % (fd, F(fs), F(ft))
            if f == 0x02: return 'FSET(c,%d, %s * %s);' % (fd, F(fs), F(ft))
            if f == 0x03: return 'FSET(c,%d, %s / %s);' % (fd, F(fs), F(ft))
            if f == 0x04: return 'FSET(c,%d, sqrtf(%s));' % (fd, F(fs))
            if f == 0x05: return 'FSET(c,%d, fabsf(%s));' % (fd, F(fs))
            if f == 0x06: return 'c->f[%d] = c->f[%d];' % (fd, fs)
            if f == 0x07: return 'c->f[%d] = c->f[%d] ^ 0x80000000U;' % (fd, fs)
            if f in (0x0C, 0x0D, 0x0E, 0x0F, 0x24):
                fn = {0x0C: 'CVT_ROUND', 0x0D: 'CVT_TRUNC', 0x0E: 'CVT_CEIL', 0x0F: 'CVT_FLOOR', 0x24: 'CVT_W'}[f]
                return 'c->f[%d] = (uint32_t)%s(c, %s);' % (fd, fn, F(fs))
            if f >= 0x30:
                cond = f & 0xF
                return 'c->fcc = FCMP(%s, %s, %d);' % (F(fs), F(ft), cond)
        if fmt == 0x14 and (w & 0x3F) == 0x20: return 'FSET(c,%d, (float)(int32_t)c->f[%d]);' % (fd, fs)
    raise Unsupported('%08X: %08X' % (a, w))

def branch_cond(a, w):
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31
    S, T = gpr(rs), gpr(rt)
    if op in (4, 0x14): return '%s == %s' % (S, T)
    if op in (5, 0x15): return '%s != %s' % (S, T)
    if op in (6, 0x16): return '(int32_t)%s <= 0' % S
    if op in (7, 0x17): return '(int32_t)%s > 0' % S
    if op == 1:
        return '(int32_t)%s %s 0' % (S, '<' if rt & 1 == 0 else '>=')
    if op == 0x11: return 'c->fcc' if (w >> 16) & 1 else '!c->fcc'
    raise Unsupported('%08X: branch %08X' % (a, w))

def load_hooks():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'hooks.txt')
    hooks = set()
    if os.path.exists(path):
        for line in open(path):
            line = line.split('#')[0].strip()
            if line: hooks.add(int(line.split()[0], 16))
    return hooks

class Gen:
    def __init__(self, p, A, imps):
        self.p = p; self.A = A
        self.hooks = load_hooks()
        self.funcs = sorted(f['addr'] for f in A['functions'])
        self.known = set(self.funcs)
        self.switches = {int(k, 16): v for k, v in A['switches'].items()}
        self.case_labels = {t for v in self.switches.values() for t in v['targets']}
        B = A['base']
        self.stubs = {}
        for m in imps:
            for f in m['functions']:
                self.stubs[int(f['stub'], 16) + B] = (m['module'], f['name'], f['nid'])
        self.unsupported = []

    def walk(self, start):
        p = self.p; seen = set(); todo = [start]; labels = set()
        while todo:
            a = todo.pop()
            if a != start: labels.add(a)
            while p.in_text(a) and a not in seen:
                if a != start and a in self.known and a not in self.case_labels:
                    labels.add(a); seen.add(('fall', a)); break
                seen.add(a); w = p.u32(a); br = is_branch(w)
                todo.extend(p.ws_targets(a))                # the switched patch's jumps (caves)
                if br is None: a += 4; continue
                kind = br[0]
                seen.add(a + 4)
                if kind == 'b':
                    todo.append(target(a, w))
                    if (w >> 26) == 4 and ((w >> 16) & 0x3FF) == 0: break
                    a += 8; continue
                if kind == 'j':
                    t = target(a, w)
                    if t not in self.known or t == start: todo.append(t)
                    break
                if kind in ('jal', 'jalr'): a += 8; continue
                if kind == 'jr':
                    if a in self.switches: todo.extend(self.switches[a]['targets'])
                    break
        return seen, labels

    def call(self, t, ret):
        if t in self.stubs:
            m, n, nid = self.stubs[t]
            return 'c->r[31] = 0x%08X; hle_%s(c);' % (ret, n)
        if t in self.known:
            return 'c->r[31] = 0x%08X; f_%08X(c);' % (ret, t)
        return 'c->r[31] = 0x%08X; rt_call_indirect(c, 0x%08X);' % (ret, t)

    def op(self, a):
        """One non-branch instruction; where the switched patch (widescreen) gives it a second meaning,
        both, chosen by rt_widescreen at run time."""
        o = translate_op(a, self.p.u32(a))
        w = self.p.ws_text.get(a)
        if w is None or is_branch(w): return o
        n = translate_op(a, w)
        if n == o: return o
        return 'if (rt_widescreen) { %s } else { %s }' % (n or ';', o or ';')

    def ws_branch(self, a, start):
        """The switched patch puts a jump at a (into a cave, or an early return) where the program has
        a plain instruction: taken only with rt_widescreen; the delay slot is the patched one."""
        w = self.p.ws_text.get(a)
        if w is None: return ''
        br = is_branch(w)
        if not br: return ''
        if is_branch(self.p.u32(a)): raise Unsupported('%08X: patch changes a branch' % a)
        dw = self.p.ws_text.get(a + 4, self.p.u32(a + 4))
        if is_branch(dw): raise Unsupported('%08X: patch puts a branch in a delay slot' % a)
        d = translate_op(a + 4, dw)
        kind = br[0]
        if kind == 'j':
            t = target(a, w)
            if t in self.known and t != start:
                go = self.call(t, 0).split('; ', 1)[1] + ' return;'
            else:
                go = 'goto L_%08X;' % t
        elif kind == 'jr' and ((w >> 21) & 31) == 31:
            go = 'return;'
        else:
            raise Unsupported('%08X: patch jump of kind %s' % (a, kind))
        return '    if (rt_widescreen) { %s %s }   /* widescreen patch */' % (d, go)

    def func(self, start):
        p = self.p
        seen, labels = self.walk(start)
        addrs = sorted(x for x in seen if isinstance(x, int))
        falls = {x[1] for x in seen if isinstance(x, tuple)}
        out = ['void %s_%08X(Cpu *c) {' % ('orig_f' if start in self.hooks else 'f', start)]
        if addrs and addrs[0] != start:
            out.append('    goto L_%08X;   /* entry is not the lowest address of the function */' % start)
        skip = set(); prev = None
        for a in addrs:
            if a in skip: continue
            if prev is not None and a != prev + 4 and (prev + 4) in falls:
                out.append('    f_%08X(c); return;  /* falls into next function */' % (prev + 4))
            if a in labels or a == start: out.append('L_%08X:;' % a)
            w = p.u32(a); br = is_branch(w)
            try:
                wb = self.ws_branch(a, start)
                if wb: out.append(wb)
                if br is None:
                    s = self.op(a)
                    if s: out.append('    ' + s)
                    prev = a; continue
                kind, _, likely, link = br
                d = self.op(a + 4) if (a + 4) in seen else ''
                if d: d = '{ %s }' % d
                skip.add(a + 4); prev = a + 4
                if kind == 'b':
                    t = target(a, w)
                    cond = branch_cond(a, w)
                    always = (w >> 26) == 4 and ((w >> 16) & 0x3FF) == 0
                    lnk = 'c->r[31] = 0x%08X; ' % (a + 8) if link else ''
                    if link:   # bal / bltzal etc: a call
                        callexpr = self.call(t, a + 8)
                        if always or (w >> 26 == 1 and (w >> 21) & 31 == 0 and (w >> 16) & 31 == 0x11):
                            out.append('    %s %s' % (d, callexpr)); continue
                        out.append('    { int t_ = %s; %s if (t_) { %s } }' % (cond, d, callexpr)); continue
                    if always:
                        out.append('    %s goto L_%08X;' % (d, t)); continue
                    if likely:
                        out.append('    if (%s) { %s goto L_%08X; }' % (cond, d, t))
                    else:
                        out.append('    { int t_ = %s; %s if (t_) goto L_%08X; }' % (cond, d, t))
                elif kind == 'j':
                    t = target(a, w)
                    if t in self.known and t != start:
                        tc = self.call(t, 0).split('; ', 1)[1]       # tail call keeps ra
                        out.append('    %s %s return;' % (d, tc))
                    else:
                        out.append('    %s goto L_%08X;' % (d, t))
                elif kind == 'jal':
                    t = target(a, w)
                    out.append('    %s %s' % (d, self.call(t, a + 8)))
                elif kind == 'jalr':
                    rs, rd = (w >> 21) & 31, (w >> 11) & 31
                    out.append('    { uint32_t t_ = %s; %s %s rt_call_indirect(c, t_); }' % (gpr(rs), d, setr(rd, '0x%08X' % (a + 8))))
                elif kind == 'jr':
                    rs = (w >> 21) & 31
                    if rs == 31:
                        out.append('    %s return;' % d)
                    elif a in self.switches:
                        cases = ' '.join('case 0x%08X: goto L_%08X;' % (t, t) for t in sorted(set(self.switches[a]['targets'])))
                        out.append('    { uint32_t t_ = %s; %s switch (t_) { %s default: rt_bad_jump(c, t_, 0x%08X); return; } }' % (gpr(rs), d, cases, a))
                    else:
                        out.append('    { uint32_t t_ = %s; %s rt_jump_indirect(c, t_); return; }' % (gpr(rs), d))
            except Unsupported as ex:
                self.unsupported.append(str(ex)); out.append('    rt_unsupported(c, 0x%08X);' % a)
        last = addrs[-1] if addrs else start
        if (last + 4) in falls:
            out.append('    f_%08X(c); return;' % (last + 4))
        # branches that land on another function's first instruction: continue there as a tail call
        for t in sorted(falls):
            out.append('L_%08X:; f_%08X(c); return;' % (t, t))
        out.append('    return;')
        out.append('}')
        # labels never emitted would break compilation: make sure every goto target exists
        emitted = {int(l[2:10], 16) for l in out if l.startswith('L_')}
        missing = [l for l in labels if l not in emitted]
        return '\n'.join(out), missing

def main(binp, anap, impp, outdir, per_file=400):
    p = Prog(binp); A = json.load(open(anap)); imps = json.load(open(impp))
    g = Gen(p, A, imps)
    os.makedirs(outdir, exist_ok=True)
    # clear output from earlier runs: a stray cpu.h here would shadow src/runtime/cpu.h,
    # and old code_NNN.c files would be compiled alongside the new ones
    for name in os.listdir(outdir):
        if name.endswith(('.c', '.h')):
            os.remove(os.path.join(outdir, name))
    hdr = ['#pragma once', '#include "cpu.h"', 'extern int rt_widescreen;   /* the widescreen patch, switched at run time */']
    hdr += ['void f_%08X(Cpu *c);' % a for a in g.funcs]
    hdr += ['void orig_f_%08X(Cpu *c);   /* replaced by a native hook */' % a for a in sorted(g.hooks)]
    hle = sorted({n for m in imps for n in [f['name'] for f in m['functions']]})
    hdr += ['void hle_%s(Cpu *c);' % n for n in hle]
    open(os.path.join(outdir, 'funcs.h'), 'w').write('\n'.join(hdr) + '\n')
    files = 0; nmiss = 0
    for i in range(0, len(g.funcs), per_file):
        body = ['#include "funcs.h"', '']
        for a in g.funcs[i:i + per_file]:
            src, missing = g.func(a); nmiss += len(missing)
            body.append(src); body.append('')
        open(os.path.join(outdir, 'code_%03d.c' % files), 'w').write('\n'.join(body)); files += 1
    # dispatch table for indirect calls/jumps: every function start
    disp = ['#include "funcs.h"', 'const FuncEntry rt_func_table[] = {']
    disp += ['    {0x%08X, f_%08X},' % (a, a) for a in g.funcs]
    disp += ['};', 'const unsigned rt_func_count = %d;' % len(g.funcs)]
    stubs = ['const StubEntry rt_stub_table[] = {']
    stubs += ['    {0x%08X, hle_%s, "%s", "%s"},' % (a, n, m, n) for a, (m, n, nid) in sorted(g.stubs.items())]
    stubs += ['};', 'const unsigned rt_stub_count = %d;' % len(g.stubs)]
    stubs += ['const int rt_widescreen_available = %d;   /* the widescreen patch is translated in (boot.widescreen.json) */' % (1 if p.ws_text else 0)]
    open(os.path.join(outdir, 'dispatch.c'), 'w').write('\n'.join(disp + stubs) + '\n')
    print('functions %d in %d files; unsupported instructions %d; missing labels %d; hooks %d' % (len(g.funcs), files, len(g.unsupported), nmiss, len(g.hooks)))
    for u in g.unsupported[:20]: print('  unsupported', u)

if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0], a[1], a[2], a[3])
