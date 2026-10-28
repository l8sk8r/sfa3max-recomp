"""Minimal reader for a relocatable PSP PRX/ELF (type 0xFFA0): sections and relocations."""
import struct

R_NAMES = {0: 'NONE', 2: 'R_MIPS_32', 4: 'R_MIPS_26', 5: 'R_MIPS_HI16', 6: 'R_MIPS_LO16', 7: 'R_MIPS_GPREL16'}

class Elf:
    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        shoff, = struct.unpack_from('<I', d, 0x20)
        shnum, shstr = struct.unpack_from('<HH', d, 0x30)
        hdrs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * 40) for i in range(shnum)]
        so = hdrs[shstr][4]
        self.sections = []
        for h in hdrs:
            name = d[so + h[0]:d.index(b'\0', so + h[0])].decode()
            self.sections.append(dict(name=name, type=h[1], flags=h[2], addr=h[3], off=h[4], size=h[5], info=h[7]))
        self.by_name = {s['name']: s for s in self.sections}
        t = self.by_name['.text']
        self.text_lo, self.text_hi = t['addr'], t['addr'] + t['size']
        # end of all executable sections (includes the import stubs)
        self.exec_hi = max(s['addr'] + s['size'] for s in self.sections if s['flags'] & 4)
        self.mem_hi = max(s['addr'] + s['size'] for s in self.sections if s['flags'] & 2)

    def section_at(self, addr):
        for s in self.sections:
            if s['flags'] & 2 and s['addr'] <= addr < s['addr'] + s['size']:
                return s

    def read(self, addr, n):
        s = self.section_at(addr)
        if s is None or s['type'] == 8:      # NOBITS
            return b'\0' * n
        o = s['off'] + addr - s['addr']
        return self.d[o:o + n]

    def u32(self, addr):
        return struct.unpack('<I', self.read(addr, 4))[0]

    def image(self):
        """Flat little-endian memory image 0..mem_hi (bss zero)."""
        img = bytearray(self.mem_hi)
        for s in self.sections:
            if s['flags'] & 2 and s['type'] != 8:
                img[s['addr']:s['addr'] + s['size']] = self.d[s['off']:s['off'] + s['size']]
        return img

    def relocs(self):
        """Yield (section_name_patched, offset_addr, type, ofsbase_section_index, addrbase_section_index)."""
        for s in self.sections:
            if s['type'] != 0x700000A0:
                continue
            tgt = self.sections[s['info']]
            for i in range(0, s['size'], 8):
                off, info = struct.unpack_from('<II', self.d, s['off'] + i)
                yield tgt['name'], off, info & 0xFF, (info >> 8) & 0xFF, (info >> 16) & 0xFF

def relocate(elf, base):
    """Apply the PRX relocations for a load at `base`, exactly as the PSP loader does.

    Relocations are processed in file order. A HI16 is paired with the next relocation in the list
    that is not a HI16, using that instruction's *original* low half to compute the carry. A LO16
    just adds the base to its low half. Returns (image, list of (addr, type, target)).
    """
    import struct
    img = elf.image()
    orig = bytes(img)
    ph = [struct.unpack_from('<IIIIIIII', elf.d, 52 + i * 32) for i in range(struct.unpack_from('<H', elf.d, 0x2C)[0])]
    segv = [p[2] for p in ph]
    rels = list(elf.relocs())                          # file order
    out = []
    ow = lambda a: struct.unpack_from('<I', orig, a)[0]
    for r, (sec, off, typ, ob, ab) in enumerate(rels):
        a = segv[ob] + off
        w = ow(a)
        delta = base + segv[ab]
        if typ == 2:                                   # R_MIPS_32
            v = (w + delta) & 0xFFFFFFFF
            struct.pack_into('<I', img, a, v); out.append((a, typ, v))
        elif typ == 4:                                 # R_MIPS_26
            v = ((w & 0x03FFFFFF) << 2) + delta
            struct.pack_into('<I', img, a, (w & 0xFC000000) | ((v >> 2) & 0x03FFFFFF)); out.append((a, typ, v))
        elif typ == 5:                                 # R_MIPS_HI16
            for t in range(r + 1, len(rels)):
                if rels[t][2] == 5: continue
                lo_a = segv[rels[t][3]] + rels[t][1]
                lo = ow(lo_a) & 0xFFFF; lo -= (lo & 0x8000) << 1
                v = (((w & 0xFFFF) << 16) + lo + delta) & 0xFFFFFFFF
                hi = ((v >> 16) + ((v >> 15) & 1)) & 0xFFFF
                struct.pack_into('<I', img, a, (w & 0xFFFF0000) | hi)
                out.append((a, 6, v))                  # full address of the pair
                break
        elif typ == 6:                                 # R_MIPS_LO16
            struct.pack_into('<I', img, a, (w & 0xFFFF0000) | (((w & 0xFFFF) + delta) & 0xFFFF))
        elif typ == 0:
            pass
        else:
            raise ValueError('unhandled reloc type %d at %x' % (typ, a))
    return img, out
