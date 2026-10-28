#!/usr/bin/env python3
"""List the system-library imports (module + NID + name) of a PSP ELF/PRX.

Usage: psp_imports.py BOOT.BIN [ppsspp_Core_HLE_dir] [--json out.json]
NID names come from tools/data/nids.json, plus PPSSPP's HLE tables when that directory is given.
"""
import re, struct, sys, json, glob, os

def nid_names(hle_dir):
    names = {}
    table = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'nids.json')
    if os.path.exists(table):
        names.update({int(k, 16): v for k, v in json.load(open(table)).items()})
    if not hle_dir: return names
    pat = re.compile(r'\{\s*0[xX]([0-9A-Fa-f]{8})\s*,[^"]*"([A-Za-z_0-9]+)"')
    for f in glob.glob(os.path.join(hle_dir, '*.cpp')):
        for m in pat.finditer(open(f, encoding='utf-8', errors='ignore').read()):
            names.setdefault(int(m.group(1), 16), m.group(2))
    return names

def sections(d):
    shoff, = struct.unpack_from('<I', d, 0x20)
    shnum, shstr = struct.unpack_from('<HH', d, 0x30)
    hdrs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i*40) for i in range(shnum)]
    so = hdrs[shstr][4]
    out = {}
    for h in hdrs:
        n = d[so+h[0]:d.index(b'\0', so+h[0])].decode()
        out[n] = h  # name, type, flags, addr, off, size, ...
    return out

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    jout = sys.argv[sys.argv.index('--json')+1] if '--json' in sys.argv else None
    if jout in args: args.remove(jout)
    d = open(args[0], 'rb').read()
    names = nid_names(args[1] if len(args) > 1 else None)
    s = sections(d)
    base_off = s['.text'][4] - s['.text'][3]          # file offset = addr + base_off (addr 0-based PRX)
    rd = lambda a, n: d[a+base_off:a+base_off+n]
    stub = s['.lib.stub']
    result = []
    a = stub[3]
    while a < stub[3] + stub[5]:
        libname_p, ver, attr, entlen, varcnt, funccnt, nidtab, functab = struct.unpack('<IHHBBHII', rd(a, 20))
        lib = rd(libname_p, 64).split(b'\0')[0].decode()
        nids = struct.unpack('<%dI' % funccnt, rd(nidtab, 4*funccnt))
        funcs = [{'nid': '0x%08X' % n, 'name': names.get(n, '?'), 'stub': '0x%06X' % (functab + 8*i)} for i, n in enumerate(nids)]
        result.append({'module': lib, 'version': ver, 'functions': funcs})
        a += entlen * 4
    tot = sum(len(m['functions']) for m in result); unk = sum(f['name'] == '?' for m in result for f in m['functions'])
    for m in result:
        print('%-28s %3d' % (m['module'], len(m['functions'])))
        for f in m['functions']: print('    %s %s %s' % (f['stub'], f['nid'], f['name']))
    print('total %d imports, %d unnamed' % (tot, unk), file=sys.stderr)
    if jout: json.dump(result, open(jout, 'w'), indent=1)

main()
