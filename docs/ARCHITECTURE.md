# Architecture

```
 your ISO ──prepare.py──► BOOT.BIN ──analyse──► functions / switch tables / code pointers
                                        └─translate──► build/generated/*.c  (one C function per game function)
                                                              │
                     ┌────────────────────────────────────────┴───────────────┐
                     │                 sfa3r.exe (Windows x64)                 │
                     │  translated game code (+ hooks: native replacements)    │
                     │      │ calls 181 OS functions                           │
                     │  HLE layer: threads · files · save data · display ·     │
                     │             controller · SAS audio · ATRAC3+ · GE · net │
                     │      │                                                  │
                     │  host: SDL3 window / input / audio, OpenGL renderer     │
                     └─────────────────────────────────────────────────────────┘
```

## 1. Translator (`tools/`)

`prepare.py` extracts the disc, then `psp_analyze.py` finds every function, switch table and code
pointer, and `psp_recomp.py` turns each function into `void f_XXXXXXXX(Cpu *c)`.

- Registers live in a `Cpu` struct; memory is one host block mirroring the PSP's address window
  (`0x08000000` user RAM, `0x04000000` VRAM, `0x00010000` scratchpad).
- Delay slots, branch-likely and the Allegrex extras are handled; divide-by-zero matches the hardware.
- `jal` becomes a direct C call, `jr ra` a return, switch tables a C `switch`; calls through pointers go
  through a generated address-to-function table (`dispatch.c`). Calls into import stubs go straight to
  `hle_<name>()`.
- **Hooks:** a function listed in `tools/data/hooks.txt` is translated as `orig_f_XXXXXXXX` and replaced
  by native code in `src/runtime/` that usually calls the original around its own changes.
- **A patch switched at run time** (the widescreen patch, `work/boot.widescreen.json`): each word it
  changes in `.text` is translated both ways (`if (rt_widescreen) ... else ...`, a jump it adds taken only
  when set), and the code it adds in unused `.data` is translated too.

## 2. Runtime (`src/runtime/`)

- **Memory**: the relocated program is loaded at 0x08804000.
- **Threads**: the PSP kernel is single-core and priority-scheduled; each game thread is a fiber and a
  small scheduler runs the highest-priority ready thread. Frames kept off screen (loads and saves) are
  not paced, so they take no time.
- **Time**: vblank at 59.94 Hz drives the frame pacing.
- **Files**: `disc0:` / `umd0:` read from the extracted disc. Saves go to `sfa3r.dat` / `sfa3r.cfg`
  (SAVES.md).
- **Sound**: `hle_atrac.c` decodes music with the vendored ATRAC3+ decoder; `hle_sas.c` is the PSP's
  voice synthesiser (VAG, ADSR, pitch, and the reverb in `sas_reverb.c`) in C, following PPSSPP. The game
  sets a reverb (Hall) but keeps the wet mix off and sends nothing to it, so it is silent, as on a PSP. Each PSP channel gets its own SDL stream.

## 3. Graphics

The game only uses 2D "through mode": triangle strips and sprites, 4/8-bit palette and 16/32-bit
textures, alpha test, blending and block transfers.

- **Software renderer** (`ge_soft.c`): the reference. Pixels land in guest VRAM as on the PSP.
- **GPU renderer** (`ge_gl.c`, OpenGL 3.3 core through SDL3): every frame-buffer address the game draws
  to is an off-screen render target at up to 8× resolution, so the game's own stretches sample the
  upscaled image. Textures are decoded once and cached by a hash of their bytes and palette.
  Presentation letterboxes to 480:272.

Possible next steps: texture upscaling, integer-scaled presentation of the 480×224 scene, CRT shaders.

## 4. Players

- **Offline:** pads 2 and 3 feed the game's player 2 and 3 controller slots (the PSP had one pad), and a
  fighter given to a pad reads that pad (hooks.c, playercfg.c). Who plays which fighter: decision 19 and
  PLAYERS.md. Select screens where a player joins: selectscreen.c.
- **Network modes on one PC** (decision 14): `hle_net.c` emulates the PSP's ad-hoc networking;
  `net_link.c` carries its traffic to **virtual players** in the process (or to other instances over
  TCP, for tests). `vpeer.c` plays those players: lobby, streams, settings block, sync words and one
  word of buttons per frame. `netplay.c` starts them, picks HOST in Dramatic Battle, names the players
  and plays the computer for a missing pad. `SFA3R_NETCAP=1` logs every packet.

## 5. SFA3R's own changes

- Menus: EX. SETTING, PLAYER CONFIG with P1 - P4 CONFIG, SAVE LOAD's extra rows, OPTION MODE tiles in
  one font (exsetting.c, tiles.c); L2 / R2 buttons (playercfg.c).
- Saves: always present, AUTO SAVE on, everything unlocked, silent loads and saves (sysdata.c,
  savestore.c, settings.c).
- Widescreen: SCREEN SIZE > WIDE FORCED (widescreen.c).
- Pad icons on the MODE MENU, one per gamepad plugged in (padicons.c).
- F2 (keyboard only): back to the MODE MENU without saving, the way quitting from the pause menu goes;
  from NETWORK (menus, lobby, battles) the network is shut down and left the way its own exit does
  (quitkey.c).

## 6. Build and test

- CMake, Visual Studio 2026 generator, x64. The generated C is about 19 MB in 38 files.
- Scripted headless runs (`--headless --fast --input ... --dump ...`) of every mode, compared screen by
  screen, check that changes leave the rest of the game alone.
- PPSSPP is the reference for the PSP's behaviour.
