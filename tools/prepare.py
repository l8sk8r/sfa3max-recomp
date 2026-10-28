#!/usr/bin/env python3
"""Prepare a build from your own Street Fighter Alpha 3 MAX disc image.

Usage:  python tools/prepare.py [path/to/game.iso] [--no-widescreen]
        The ISO defaults to SFA3R_ISO if set, else imports/psp/Street Fighter Alpha 3 Max.iso.

Writes everything to work/ and build/generated/ (both git-ignored):
  work/iso/...                 the disc's files
  work/boot.elf                the game program (BOOT.BIN)
  work/boot.widescreen.json    the SFA3MAX 16:9 patch as an overlay, switched at run time (not with --no-widescreen)
  work/imports.json            the OS functions it calls
  work/analysis.json           functions, switch tables, code pointers
  build/generated/*.c          the translated program
Needs Python 3.9+ and `pip install pycdlib`.
"""
import sys, os, hashlib, shutil, subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ISO = os.path.join(ROOT, 'imports', 'psp', 'Street Fighter Alpha 3 Max.iso')
KNOWN_BOOT = {'3fd0abf7cc65742233e1b7787997fa3e20152d98': 'ULUS-10062 v1.00 (USA)'}

def extract(iso_path, out):
    import pycdlib
    iso = pycdlib.PyCdlib(); iso.open(iso_path)
    n = 0
    for root, dirs, files in iso.walk(iso_path='/'):
        for f in files:
            src = root.rstrip('/') + '/' + f
            dst = os.path.join(out, *src.replace(';1', '').strip('/').split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            iso.get_file_from_iso(dst, iso_path=src); n += 1
    iso.close()
    return n

WS_REPO = 'https://github.com/Rickow/SFA3max-PSP-widescreen.git'

def widescreen_overlay(boot, work):
    """The SFA3MAX 16:9 patch (Rickow, MIT) as an overlay on the unpatched program, so SFA3R can
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
            subprocess.check_call(['git', 'clone', '--depth', '1', WS_REPO, os.path.dirname(patcher)])
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
    subprocess.check_call([sys.executable, '-I'] + list(args))

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    iso = args[0] if args else os.environ.get('SFA3R_ISO') or DEFAULT_ISO
    if not os.path.isfile(iso):
        sys.exit('prepare.py: %s not found. Put your Street Fighter Alpha 3 MAX ISO (US, ULUS-10062) there, '
                 'or give its path (or set SFA3R_ISO).' % iso)
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
