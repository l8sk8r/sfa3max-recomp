#!/usr/bin/env python3
"""Prepare a build from your own Street Fighter Alpha 3 MAX disc image.

Usage:  python tools/prepare.py [path/to/game.iso] [--no-widescreen]
        The ISO defaults to SFA3MR_ISO if set, else imports/psp/Street Fighter Alpha 3 Max.iso.

Writes everything to work/ and build/generated/ (both git-ignored):
  work/iso/...                 the disc's files
  work/boot.elf                the game program (BOOT.BIN)
  work/boot.widescreen.json    the SFA3MAX 16:9 patch as an overlay, switched at run time (not with --no-widescreen)
  work/imports.json            the OS functions it calls
  work/analysis.json           functions, switch tables, code pointers
  build/generated/*.c          the translated program
Needs Python 3.9+ only (build.ps1 downloads a copy if needed).
"""
import sys, os, hashlib, shutil, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ISO = os.path.join(ROOT, 'imports', 'psp', 'Street Fighter Alpha 3 Max.iso')
KNOWN_BOOT = {'3fd0abf7cc65742233e1b7787997fa3e20152d98': 'ULUS-10062 v1.00 (USA)'}

def extract(iso_path, out):
    """Copy every file of the disc image (plain ISO 9660, as a UMD image is) to OUT."""
    S = 2048
    with open(iso_path, 'rb') as f:
        def sector(n, count=1):
            f.seek(n * S); return f.read(S * count)
        pvd = sector(16)
        if pvd[1:6] != b'CD001': raise SystemExit('prepare.py: %s is not an ISO 9660 disc image' % iso_path)
        def records(lba, size):
            data = sector(lba, (size + S - 1) // S); pos = 0
            while pos < size:
                n = data[pos]
                if n == 0: pos = (pos // S + 1) * S; continue   # records do not cross sectors
                r = data[pos:pos + n]; pos += n
                name = r[33:33 + r[32]]
                if name in (b'\0', b'\1'): continue
                yield name.decode('ascii').split(';')[0], int.from_bytes(r[2:6], 'little'), int.from_bytes(r[10:14], 'little'), r[25] & 2
        count = 0
        def walk(lba, size, dst):
            nonlocal count
            for name, at, length, is_dir in records(lba, size):
                path = os.path.join(dst, name)
                if is_dir: walk(at, length, path); continue
                os.makedirs(dst, exist_ok=True)
                with open(path, 'wb') as o:
                    f.seek(at * S); left = length
                    while left:
                        chunk = f.read(min(left, 1 << 20)); o.write(chunk); left -= len(chunk)
                count += 1
        root = pvd[156:190]
        walk(int.from_bytes(root[2:6], 'little'), int.from_bytes(root[10:14], 'little'), out)
    return count

WS_REPO = 'https://github.com/Rickow/SFA3max-PSP-widescreen'
WS_ZIP = WS_REPO + '/archive/HEAD.zip'

def fetch_widescreen(dst):
    """Download the patch's repository as a zip (no git needed) into DST."""
    import io, urllib.request, zipfile
    data = urllib.request.urlopen(WS_ZIP, timeout=60).read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for m in z.infolist():
            rel = m.filename.split('/', 1)[1] if '/' in m.filename else ''
            if not rel or m.is_dir(): continue
            path = os.path.join(dst, *rel.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as o: o.write(z.read(m))

def widescreen_overlay(boot, work):
    """The SFA3MAX 16:9 patch (Rickow, MIT) as an overlay on the unpatched program, so SFA3MR can
    switch it on and off while running (SCREEN SETTING > SCREEN SIZE > WIDE FORCED). The patch is
    downloaded here and applied to a copy; work/boot.widescreen.json records, by load address, the
    words it changes in .text ("text": translated both ways, chosen at run time) and the code it adds
    in unused .data ("caves": translated as code). Nothing of the patch is kept in the repository."""
    import json
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from psp_elf import Elf, relocate
    from psp_analyze import BASE
    out = os.path.join(work, 'boot.widescreen.json')
    if os.path.exists(out): os.remove(out)
    patcher = os.path.join(work, 'SFA3max-PSP-widescreen', 'sfa3_ws_patternpatcher.py')
    try:
        if not os.path.exists(patcher):
            try: fetch_widescreen(os.path.dirname(patcher))
            except Exception as ex:                          # (a network that only lets git through)
                print('  zip download failed (%s): trying git' % ex)
                subprocess.check_call(['git', 'clone', '--depth', '1', WS_REPO + '.git', os.path.dirname(patcher)])
        tmp = os.path.join(work, 'boot.widescreen.elf')
        run(patcher, boot, tmp)
    except Exception as ex:
        print('  widescreen patch not available (%s): WIDE FORCED will be off' % ex); return
    a_e, b_e = Elf(boot), Elf(tmp)
    a, _ = relocate(a_e, BASE); b, _ = relocate(b_e, BASE)
    text = a_e.by_name['.text']; tlo, thi = text['addr'], text['addr'] + text['size']
    t, caves = {}, {}
    for off in range(0, min(len(a), len(b)) - 3, 4):
        if a[off:off + 4] == b[off:off + 4]: continue
        w = int.from_bytes(b[off:off + 4], 'little')
        if tlo <= off < thi: t['0x%08X' % (BASE + off)] = w
        else:
            if a[off:off + 4] != b'\0\0\0\0': print('  widescreen patch changes data at %08X: not used' % (BASE + off)); return
            caves['0x%08X' % (BASE + off)] = w
    json.dump({'patch': WS_REPO, 'text': t, 'caves': caves}, open(out, 'w'), indent=0)
    os.remove(tmp)
    print('  widescreen overlay: %d words in .text, %d words of added code' % (len(t), len(caves)))

def run(*args):
    print('>', ' '.join(os.path.relpath(a, ROOT) if os.path.isabs(a) else a for a in args)); sys.stdout.flush()
    subprocess.check_call([sys.executable, '-I'] + list(args), stderr=subprocess.STDOUT)   # (one stream: notes are not errors)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    iso = args[0] if args else os.environ.get('SFA3MR_ISO') or DEFAULT_ISO
    if not os.path.isfile(iso):
        sys.exit('prepare.py: %s not found. Put your Street Fighter Alpha 3 MAX ISO (US, ULUS-10062) there, '
                 'or give its path (or set SFA3MR_ISO).' % iso)
    work = os.path.join(ROOT, 'work'); gen = os.path.join(ROOT, 'build', 'generated')
    os.makedirs(work, exist_ok=True)
    print('extracting', iso); print('  %d files' % extract(iso, os.path.join(work, 'iso')))
    boot = os.path.join(work, 'iso', 'PSP_GAME', 'SYSDIR', 'BOOT.BIN')
    sha = hashlib.sha1(open(boot, 'rb').read()).hexdigest()
    print('BOOT.BIN sha1', sha, '->', KNOWN_BOOT.get(sha, 'UNKNOWN version: analysis may not match'))
    elf = os.path.join(work, 'boot.elf')
    shutil.copyfile(boot, elf)
    for side in ('.extra_code.json',):
        if os.path.exists(os.path.splitext(elf)[0] + side): os.remove(os.path.splitext(elf)[0] + side)
    if '--no-widescreen' in sys.argv:
        if os.path.exists(os.path.join(work, 'boot.widescreen.json')): os.remove(os.path.join(work, 'boot.widescreen.json'))
    else:
        widescreen_overlay(elf, work)
    T = os.path.join(ROOT, 'tools')
    run(os.path.join(T, 'psp_imports.py'), elf, '--json', os.path.join(work, 'imports.json'))
    run(os.path.join(T, 'psp_analyze.py'), elf, os.path.join(work, 'analysis.json'))
    run(os.path.join(T, 'psp_recomp.py'), elf, os.path.join(work, 'analysis.json'), os.path.join(work, 'imports.json'), gen)
    print('done: build/generated is ready')

main()
