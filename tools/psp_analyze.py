#!/usr/bin/env python3
"""Find the functions, switch jump tables and code pointers of the PSP program.

Usage: psp_analyze.py BOOT.BIN out.json
 1. Relocate the PRX to the hardware load address.
 2. Seeds: exports, jal targets, la-materialised code addresses, function-pointer tables in data.
 3. Each `jr reg` (not ra) is resolved to its switch table (lui/addu/lw pattern, size from sltiu).
 4. Control flow is walked from each seed; unreached code becomes new seeds until nothing is left.
"""
import sys, struct, json, bisect, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from psp_elf import Elf, relocate

BASE = 0x08804000

def sx16(v): return (v & 0xFFFF) - ((v & 0x8000) << 1)

class Prog:
    """The relocated program. `extra_code` lists [lo, hi) address ranges outside .text that hold
    code (for example the code caves a binary patch such as the widescreen patch adds in .data)."""
    def __init__(self, path, extra_code=None):
        self.e = e = Elf(path)
        self.img, self.rel = relocate(e, BASE)
        self.lo, self.hi = BASE + e.text_lo, BASE + e.text_hi
        self.stub_lo, self.stub_hi = self.hi, BASE + e.exec_hi
        if extra_code is None:
            side = os.path.splitext(path)[0] + '.extra_code.json'
            extra_code = json.load(open(side)) if os.path.exists(side) else []
        self.extra = [tuple(r) for r in extra_code]
        # a patch switched at run time (prepare.py: boot.widescreen.json): .text words with a second
        # meaning (ws_text) and code added in unused .data (caves, read as code here)
        self.ws_text, self.caves = {}, {}
        side = os.path.join(os.path.dirname(os.path.abspath(path)), 'boot.widescreen.json')
        if os.path.exists(side):
            j = json.load(open(side))
            self.ws_text = {int(k, 16): v for k, v in j['text'].items()}
            self.caves = {int(k, 16): v for k, v in j['caves'].items()}
            for a in sorted(self.caves):
                if self.extra and a - self.extra[-1][1] <= 16: self.extra[-1] = (self.extra[-1][0], a + 4)
                else: self.extra.append((a, a + 4))
    def u32(self, a):
        if a in self.caves: return self.caves[a]
        return struct.unpack_from('<I', self.img, a - BASE)[0]
    def ws_targets(self, a):
        """Where the patched instruction at a can also go (a jump into a cave)."""
        w = self.ws_text.get(a)
        if w is None: return []
        br = is_branch(w)
        if br and br[0] in ('j', 'b'): return [target(a, w)]
        return []
    def in_text(self, a):
        if a % 4: return False
        return self.lo <= a < self.hi or any(l <= a < h for l, h in self.extra)

def is_branch(w):
    """Return (kind, target_or_None, likely, link). kind: 'b' conditional, 'j' jump, 'jal', 'jr', 'jalr', None"""
    op = w >> 26
    if op == 0:
        f = w & 0x3F
        if f == 8: return ('jr', None, False, False)
        if f == 9: return ('jalr', None, False, True)
        return None
    if op == 1:
        rt = (w >> 16) & 31
        if rt in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13):
            return ('b', None, rt in (2, 3, 0x12, 0x13), rt >= 0x10)
        return None
    if op in (2, 3): return ('j' if op == 2 else 'jal', None, False, op == 3)
    if op in (4, 5, 6, 7): return ('b', None, False, False)
    if op in (0x14, 0x15, 0x16, 0x17): return ('b', None, True, False)
    if op == 0x11 and ((w >> 21) & 31) == 8: return ('b', None, bool((w >> 17) & 1), False)   # bc1f/t(l)
    return None

def target(a, w):
    op = w >> 26
    if op in (2, 3): return (a & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
    return a + 4 + (sx16(w) << 2)

def find_switch(p, a):
    """jr reg at a: resolve the switch table by a small backward evaluation of
    lw r, off(base) ; base = addu(idx, T) ; T = lui/addiu constant.  Returns (table, count or None)."""
    w = p.u32(a); r = (w >> 21) & 31
    window = [a - 4 * k for k in range(1, 28) if a - 4 * k >= p.lo]
    def defn(reg, before):
        for b in window:
            if b >= before: continue
            x = p.u32(b); op = x >> 26
            rt, rs, rd = (x >> 16) & 31, (x >> 21) & 31, (x >> 11) & 31
            if op in (0x09, 0x0F, 0x23, 0x0D) and rt == reg: return b, x
            if op == 0 and (x & 0x3F) in (0x21, 0x20, 0x25, 0x00, 0x02, 0x03) and rd == reg: return b, x
        return None, None
    def const(reg, before, depth=0):
        if reg == 0: return 0
        b, x = defn(reg, before)
        if x is None or depth > 6: return None
        op = x >> 26
        if op == 0x0F: return (x & 0xFFFF) << 16
        if op == 0x09:
            v = const((x >> 21) & 31, b, depth + 1)
            return None if v is None else (v + sx16(x)) & 0xFFFFFFFF
        if op == 0x0D:
            v = const((x >> 21) & 31, b, depth + 1)
            return None if v is None else v | (x & 0xFFFF)
        return None
    b, x = defn(r, a)
    if x is None or x >> 26 != 0x23: return None
    base, off = (x >> 21) & 31, sx16(x)
    b2, y = defn(base, b)
    t = None
    if y is not None and y >> 26 == 0 and (y & 0x3F) in (0x21, 0x20):
        for cand in ((y >> 21) & 31, (y >> 16) & 31):
            v = const(cand, b2)
            if v is not None: t = (v + off) & 0xFFFFFFFF; break
    if t is None: return None
    count = None
    for c in window:
        z = p.u32(c)
        if z >> 26 in (0x0A, 0x0B): count = z & 0xFFFF; break      # slti / sltiu bound check
    return t, count

def main(path, out):
    p = Prog(path); e = p.e
    seeds = {}
    def seed(a, why):
        if p.in_text(a) and a not in p.caves: seeds.setdefault(a, set()).add(why)   # (caves: reached only by the patch's jumps)

    le = e.by_name['.lib.ent']['addr'] + BASE
    nvar, nfunc = p.img[le - BASE + 9], struct.unpack_from('<H', p.img, le - BASE + 10)[0]
    tab = p.u32(le + 12); exports = {}
    for i in range(nfunc + nvar):
        nid, addr = p.u32(tab + 4 * i), p.u32(tab + 4 * (nfunc + nvar + i))
        exports['0x%08X' % nid] = addr
        if i < nfunc: seed(addr, 'export')

    data_ptr = {}
    for a, typ, v in p.rel:
        if typ == 4: seed(v, 'jal')
        elif typ == 6: seed(v, 'la')
        elif typ == 2 and p.in_text(v) and v not in p.caves: data_ptr[a + BASE] = v     # relocation offsets are 0-based

    # switch tables
    switches = {}
    case_labels = set(); table_slots = set()
    unresolved = []
    for a in range(p.lo, p.hi, 4):
        w = p.u32(a)
        if (w & 0xFC1FFFFF) == 8 and ((w >> 21) & 31) != 31:
            s = find_switch(p, a)
            if not s: unresolved.append(a); continue
            t, n = s
            run = 0                                    # every real case entry is a relocated code pointer
            while t + 4 * (run) in data_ptr: run += 1
            if n is None or n > run: n = run
            ts = [p.u32(t + 4 * i) for i in range(n)]
            switches[a] = {'table': t, 'targets': ts}
            case_labels.update(ts); table_slots.update(t + 4 * i for i in range(n))
    for a, v in data_ptr.items():
        if a not in table_slots: seed(v, 'dataptr')

    # walk
    def walk(start, known):
        seen = set(); todo = [start]
        while todo:
            a = todo.pop()
            while p.in_text(a) and a not in seen:
                if a != start and a in known and a not in case_labels:
                    break                                   # fell into another function
                seen.add(a); w = p.u32(a); br = is_branch(w)
                todo.extend(p.ws_targets(a))                # the switched patch's jumps (caves)
                if br is None: a += 4; continue
                kind, _, likely, link = br
                seen.add(a + 4)                             # delay slot
                if kind == 'b':
                    todo.append(target(a, w))
                    if not link and (w >> 26) == 4 and (w >> 16) & 0x3FF == 0:   # beq zero,zero = always
                        break
                    a += 8; continue
                if kind == 'j':
                    t = target(a, w)
                    if t not in known: todo.append(t)       # local jump; else tail call
                    break
                if kind in ('jal', 'jalr'): a += 8; continue
                if kind == 'jr':
                    if a in switches: todo.extend(switches[a]['targets'])
                    break
        return seen

    owner = {}
    rounds = 0
    while True:
        rounds += 1
        known = set(seeds)
        for s in sorted(seeds):
            if s in owner: continue
            for x in walk(s, known): owner.setdefault(x, s)
        # unreached code -> new seeds (skip nop padding)
        new = 0; a = p.lo
        while a < p.hi:
            if a not in owner and p.u32(a) != 0:
                seed(a, 'unreached'); new += 1
                while a < p.hi and a not in owner: owner[a] = a if a in seeds else owner.get(a - 4, a); a += 4
                continue
            a += 4
        if not new or rounds > 6: break
        owner = {k: v for k, v in owner.items() if p.u32(k) == 0 or v in seeds and 'unreached' not in seeds[v]}
    # function list
    funcs = {}
    for a, s in owner.items():
        f = funcs.setdefault(s, [a, a])
        f[0] = min(f[0], a); f[1] = max(f[1], a)
    fl = [{'addr': s, 'lo': v[0], 'hi': v[1] + 4, 'why': sorted(seeds.get(s, ['?']))} for s, v in sorted(funcs.items())]
    overlap = sum(1 for i in range(len(fl) - 1) if fl[i]['hi'] > fl[i + 1]['lo'])

    res = dict(base=BASE, text=[p.lo, p.hi], stubs=[p.stub_lo, p.stub_hi], exports=exports,
               functions=fl, switches={hex(k): v for k, v in switches.items()},
               unresolved_jr=unresolved,
               data_code_pointers=sorted(set(v for a, v in data_ptr.items() if a not in table_slots)))
    json.dump(res, open(out, 'w'))
    open(os.path.splitext(out)[0] + '.image.bin', 'wb').write(p.img)
    from collections import Counter
    print('functions %d (%d overlapping ranges)  seed reasons %s' % (len(fl), overlap, dict(Counter(w for f in fl for w in f['why']))))
    print('switches %d (%d case labels), unresolved jr %d' % (len(switches), len(case_labels), len(unresolved)))
    print('code pointers in data %d' % len(res['data_code_pointers']))
    cov = len([a for a in owner if p.u32(a) != 0]); tot = len([a for a in range(p.lo, p.hi, 4) if p.u32(a) != 0])
    print('non-zero words covered %d / %d' % (cov, tot))

if __name__ == '__main__':
    main(*sys.argv[1:3])
