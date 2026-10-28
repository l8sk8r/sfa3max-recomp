# SFA3R: Street Fighter Alpha 3: Recomp

A native Windows static recompile of **Street Fighter Alpha 3 MAX** (PSP). The aim is the most complete
version of the game: every character and mode of the PSP release, plus local multiplayer for up to three
players, widescreen, everything unlocked and new options.

No game data is in this repository: you build it from your own copy of the game.

## Why I'm doing this

I spent my teenage years messing around with Game Genies and Action Replays and sending codes in to
gaming magazines; some of them even got published. It was one of the many things that got me into
gaming, and later into computers.

I've worked as a developer for many years, and like most people in the profession I'm expected to
stay curious and keep skilling up. Companies are asking us to look at AI tooling more and more, so
when I saw the hype train around static recomps (and the stigma that comes with it), I was intrigued
by what I could do, and whether I could learn some C along the way (honestly, it's not my main
language).

### Why the Street Fighter Alpha series?

I remember going to Hong Kong in the 90s and seeing Street Fighter Zero in the arcades there. It blew
my mind how much of a step up it was from Street Fighter II. When the Japanese PlayStation version of
Zero 3 came out, my brother and I played hours of Dramatic Battle, and it's a mode we still play to
this day.

### Why this version?

Like most retro games and their re-releases, every version of Street Fighter Alpha 3 has something
extra that makes it good, and then some limitation that stops it from being truly great. I've played
all of them and own most, and in my opinion the PSP version is the best place to start. Here's why I
didn't pick the others:

| Version | Why not |
|---|---|
| PlayStation | Cut frames. |
| Dreamcast / NAOMI | Nasty hit box implementation. |
| Saturn | Probably my first choice: full frames, and it has Survival Dramatic, my favourite mode. But no English translation, and it's missing characters. |
| Game Boy Advance | This port shouldn't even exist. It's technically very good, but obviously a downgrade in a lot of areas. |
| PS2 (*Street Fighter Alpha Anthology*) | I did consider this one. I like that its Hyper version lets you play in SFA and SFA2 styles; a bit more care went into it. |
| PC (*Capcom Fighting Collection 2*) | Interestingly they added the extra characters, but the modes suck. I don't want to hold loads of buttons to get into modes. |
| Arcade | Obviously the source, and I thought about it, but it has no modes or menus, and building all of that out would be a pain. |

### The PSP version

It isn't perfect either: it's one device = one player, which isn't a great pick for Dramatic Battle.

Why I picked it:

- The modes and roster are comprehensive, and I think I can lean into extending it well.
- Widescreen (sort of). It messes up the pixel ratio, but there are patches around that help. SFA3R
  uses the [SFA3MAX PSP widescreen patch by Rickow](https://github.com/Rickow/SFA3max-PSP-widescreen).

Why I wish I hadn't:

- The stupid memory stick, and the menu confirmation nightmare.
- No L2 and R2 buttons.
- The menus, modes and character select screens are inconsistent and seem to have different hooks.

### How far I've got

- Up to three players on gamepads.
- Join options in the modes that support them; a box changes to WAIT until it's your turn to pick a
  fighter.
- In the Variable modes, a second player can take control of the fighter on the same side, and
  tagging works.
- L2 and R2 restored, with custom mapping like the console versions, for every player; they can be
  edited from within fights too. A new PLAYER CONFIG menu manages the settings for four controllers.
- 2P support restored in Arcade mode (interestingly Arcade mode works like the original game, which
  suggests it was always in there and stubbed out).
- Extra settings (EX. SETTING) to hold feature flags.
- CPU overrides to HUMAN for the modes that don't easily allow joining in (R.Dramatic Battle is one
  example, and World Tour, if you're crazy enough to punish someone by stopping them completing it).
- Ripped out the memory stick load / save nightmare: the game auto-saves silently to its own files.
- Import and export of PSP save data (untested).
- Controller icons on the MODE MENU, showing which pads are plugged in and which player each one is.
- WIDE FORCED in SCREEN SETTING, using the PSP widescreen patch (tested OK).
- Everything unlocked, all the -ISMs included.
- F2 exits to the MODE MENU (it should be thread and memory safe).
- A COUCH CO-OP setting so three people can play Dramatic Battle through the network mode on one PC.
  It works, and you can't pause, which is correct, but Free Battle has the better implementation.

### What I've tested with so far

- Four DualSense pads ([controller support](docs/CONTROLLERS.md)).
- A laptop with two GPUs (there's no GPU switching yet).
- Windows 11 only; the Windows build is the only one so far.

### What's broken

- Network mode is stubbed: there's no real online play, the network modes only run on one PC. If you
  hit an error in there, press F2.
- More in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

### What I'd like to look at

- Some optimisation, with care not to mess up the timings.
- Checking other controllers.
- Builds for other systems.

### Pipe dreams

- A player 4 controller. The original game was coded to handle three sets of inputs; can it do more?
- X.DRAMATIC BATTLE: 2v2, like Street Fighter EX3. This could be possible if I can get Dramatic
  Battle mirrored.
- Optimisation, optimisation, optimisation.

## Status

| Part | State |
|---|---|
| Translation | every function of the PSP program translated to C and compiled |
| Runtime | threads, files, timing, saves, the PSP's OS functions |
| Graphics | GPU renderer (OpenGL 3.3) up to 8× the PSP's resolution; software renderer as reference |
| Sound | music (ATRAC3+) and effects / voices (SAS, with its reverb) |
| Players | up to three local players in the offline modes and in the network modes (played on one PC) |
| Extras | widescreen (SCREEN SIZE > WIDE FORCED), everything unlocked, L2 / R2 buttons, silent saves |

Open problems: [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## Prerequisites

- Windows 10 / 11, x64
- Visual Studio 2026 with C/C++ (includes CMake)
- Python 3.9+ with `pip install pycdlib`
- Git (also needed when building from a release zip: it fetches SDL3 and the widescreen patch)
- Your own **Street Fighter Alpha 3 MAX** PSP disc image (ISO), US release `ULUS-10062`, placed at
  `imports\psp\Street Fighter Alpha 3 Max.iso` (git-ignored)

CMake downloads and builds SDL3 the first time (a few minutes). `prepare.py` also downloads the SFA3MAX
16:9 patch (Rickow, MIT) for WIDE FORCED; `--no-widescreen` leaves it out.

## Building

### Windows

1. Install the [prerequisites](#prerequisites) above.
2. Download the **Source code (zip)** of the [latest release](https://github.com/l8sk8r/sfa3max-recomp/releases/latest).
3. Open the zip and copy the folder from inside it. Rename it without the version number, so it
   becomes `sfa3max-recomp`, and move it where you want to run it. In this example it is
   `C:\sfa3max-recomp`.
4. Copy your ISO into it as `C:\sfa3max-recomp\imports\psp\Street Fighter Alpha 3 Max.iso` (make the
   `imports\psp` folders if they are not there).
5. Search for and launch **Windows PowerShell**.
6. Change to the folder:
   ```
   cd C:\sfa3max-recomp
   ```
7. Build, and start the game when it is done:
   ```
   .\build.ps1 -Run
   ```
   The first build takes a while: it extracts the disc, translates the game and downloads SDL3. If
   PowerShell says running scripts is disabled, run `Set-ExecutionPolicy -Scope Process Bypass` in the
   same window first. Other options are [below](#buildps1-options).

**Playing after that:** in PowerShell,
```
cd C:\sfa3max-recomp
.\out\Release\sfa3r.exe --work work
```
or make a shortcut to `C:\sfa3max-recomp\out\Release\sfa3r.exe` with `--work C:\sfa3max-recomp\work`
after it in the shortcut's Target.

**Updating:** download the new release's Source code (zip) and copy what is in its folder over your
`sfa3max-recomp` folder (your ISO, `work\` and saves stay), then run `.\build.ps1 -Run` again.

### build.ps1 options

| Option | |
|---|---|
| `-Run` | start the game after a successful build |
| `-Iso path` | the ISO, if it is not at `imports\psp\Street Fighter Alpha 3 Max.iso` (or set `SFA3R_ISO`) |
| `-ForceWideScreen:$false` | build without the 16:9 patch (no WIDE FORCED) |
| `-SkipPrepare` | reuse the extracted disc and translated code from the last build |
| `-Config Debug` | a debug build (`out\Debug\sfa3r.exe`) |
| `-Here`, `-NoPull` | in a git clone: build the current branch / do not pull first (by default it checks out and pulls `main`) |

### From a git clone (developers)

The same script works in a clone; by default it checks out and pulls `main` first. In VS Code it is the
default build task (**Ctrl+Shift+B**). By hand:

```
python tools/prepare.py
cmake -B out -G "Visual Studio 18 2026" -A x64
cmake --build out --config Release
out\Release\sfa3r.exe --work work
```

`prepare.py` writes `work/` (the disc's files and the analysis) and `build/generated/` (the translated
C). Both are git-ignored and must never be committed. Re-run it after pulling changes to `tools/`.

## Playing

| Keyboard | PSP | Gamepad |
|---|---|---|
| Arrow keys | D-pad | D-pad or left stick |
| Z / X / A / S | Cross / Circle / Square / Triangle | South / East / West / North |
| Q / W | L / R | Shoulders |
| E / R | L2 / R2 (SFA3R's own) | Triggers |
| Enter / Backspace | Start / Select | Start / Back |
| F2 | Back to the MODE MENU at once, without saving (keyboard only) | |
| F11 or Alt+Enter | Full screen | |
| Esc | Quit | |

The keyboard and the first gamepad are player 1; gamepads 2 and 3 are players 2 and 3 (in the order the
controllers report), gamepad 4 player 4. The MODE MENU's bar shows a pad icon for each gamepad plugged
in, with the player it is: 1P red and 2P blue on the left, 3P green and 4P yellow on the right. More in
[docs/CONTROLLERS.md](docs/CONTROLLERS.md).

- **Offline modes:** whoever starts a mode plays it. Other players come in through OPTION MODE >
  EX. SETTING > JOIN-IN (HUMAN), or by pressing Start where a select screen shows JOIN. Mode by mode:
  [docs/PLAYERS.md](docs/PLAYERS.md).
- **Local versus:** choose NETWORK on the MODE MENU and play VS MODE, TEAM BATTLE, VARIABLE BATTLE or
  DRAMATIC BATTLE as on a PSP. You are the host; players 2 and 3 are gamepads 2 and 3, or the computer
  (CPU2 / CPU3) when not plugged in.
- **Buttons:** OPTION MODE > PLAYER CONFIG, for players 1 to 4 ([docs/PLAYER_CONFIG.md](docs/PLAYER_CONFIG.md)).
- **Saves and settings:** `work/sfa3r.cfg` (every setting, by name) and `work/sfa3r.dat` (the rest);
  AUTO SAVE is always on, and loads and saves happen without screens. PSP / PPSSPP saves can be
  imported and exported ([docs/SAVES.md](docs/SAVES.md)).
- **Hidden extras** (all unlocked): hold **L + R** while confirming Dramatic Battle, Variable Battle,
  R.Dramatic Battle or Final Battle on the MODE MENU for its alternative version; hold **SELECT** while
  choosing Akuma (Shin Akuma), M. Bison (Final Bison) or Balrog's second slot.
- **EX. SETTING:** 1P / 2P / 3P JOIN-IN, COUCH CO-OP (network modes on this PC), DEFAULT; the version is
  shown in its corner.

A log is written to `work/sfa3r.log`.

### Command line

| Option | |
|---|---|
| `--work DIR` | the work folder made by `prepare.py` |
| `--renderer gl\|soft` | GPU renderer (default) or the software reference renderer |
| `--scale N` | GPU internal resolution, N × 480×272 (1–8; default: enough for your monitor) |
| `--p2` / `--p3 human\|cpu\|auto` | treat gamepad 2 / 3 as plugged in or not (tests) |
| `--pads 1234` | the gamepads shown as plugged in on the MODE MENU's pad icons (tests) |
| `--headless`, `--frames N`, `--fast` | no window or sound; quit after N frames; do not wait for the clock |
| `--input "200=up;206=;220=cross;226="` | scripted buttons by frame (`p2:` / `p3:` for the other pads) |
| `--dump DIR`, `--dump-every N`, `--dump-from F` | save the screen as PNG every N frames (from frame F) |
| `--record-audio DIR`, `--trace`, `--log FILE` | audio channels as WAV; log every OS call; log elsewhere |
| `--net-hub PORT` / `--net-join PORT N` | link separate instances over TCP (tests) |

## Layout

| Path | What |
|---|---|
| `tools/` | `prepare.py` (the whole pipeline), `psp_elf.py`, `psp_imports.py`, `psp_analyze.py`, `psp_recomp.py`; `data/hooks.txt` lists the game functions replaced by native code |
| `src/runtime/` | the native runtime: memory, threads, files, graphics, sound, input, and SFA3R's changes to the game |
| `third_party/` | ATRAC3/ATRAC3+ decoder from PPSSPP / FFmpeg (LGPL 2.1+), libkirk |
| `docs/` | [architecture](docs/ARCHITECTURE.md), [decisions](docs/DECISIONS.md), [program map](docs/PSP_PROGRAM_MAP.md), [players](docs/PLAYERS.md), [controllers](docs/CONTROLLERS.md), [buttons](docs/PLAYER_CONFIG.md), [saves](docs/SAVES.md), [other versions](docs/VERSION_DIFFERENCES.md), [known issues](docs/KNOWN_ISSUES.md) |

## Contributing

Issues and pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md) for branch names and how
pull requests are reviewed.

## Licence

The project's own code is licensed under the **GNU General Public License v3.0 or later** (`LICENSE`).
It covers the tools, runtime and documentation only and grants nothing over the game itself.

## Legal

You must own the game. Disc images, extracted files and generated code are never committed.
Not affiliated with Capcom. *Street Fighter* is a trademark of Capcom Co., Ltd.
