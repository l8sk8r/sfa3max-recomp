# Building

The quick way is in the [README](../README.md#building): put your ISO in place and run
`.\build.ps1 -Run`. This page covers what the script does, its options, and building by hand.

## What `build.ps1` does

1. **In a git clone only:** checks out `main` and pulls it (see `-Here`, `-NoPull`). In a release's
   source zip there is no `.git` folder, so these steps are skipped.
2. **Finds the build tools** you have installed, and downloads the ones you don't into `.tools\` inside
   the folder (once; later builds reuse them):

   | Tool | Used if installed | Otherwise downloaded |
   |---|---|---|
   | Compiler | Visual Studio 2026 or 2022 with *Desktop development with C++* (found with `vswhere`) | LLVM-MinGW 20251104 (clang), about 180 MB |
   | CMake | Visual Studio's own CMake, else a CMake 3.20+ on `PATH` | CMake 4.1.2, about 50 MB |
   | Ninja | one on `PATH` (only with LLVM-MinGW) | Ninja 1.13.1 |
   | Python | Python 3.9+ on `PATH` (`python` or `py`; not the Microsoft Store stand-in) | Python 3.12.10 (embeddable), about 10 MB |

   Each download is a fixed release and is checked against its SHA-256 (Python's against HTTPS
   only). The script prints which tools it uses.
3. **`python tools/prepare.py`:** reads your ISO, extracts the disc's files into `work\`, downloads and
   applies the widescreen patch as an overlay (unless `-ForceWideScreen:$false`), analyses the game
   program and translates it to C in `build\generated\` (about 19 MB).
4. **CMake configure and build** in `out\`: downloads SDL3 (once) and compiles everything into
   `out\Release\sfa3r.exe` (or `out\Debug\...` with `-Config Debug`). If `out\` was made by the other
   toolchain, it is started again.
5. **`-Run`:** starts the game with `--work work`.

It stops at the first step that fails, and says which.

## Options

| Option | What it does |
|---|---|
| `-Run` | Start the game after a successful build. |
| `-Iso path` | Your ISO, if it is not at `imports\psp\Street Fighter Alpha 3 Max.iso`. The `SFA3R_ISO` environment variable does the same. |
| `-ForceWideScreen:$false` | Leave out the SFA3MAX 16:9 patch: no WIDE FORCED in SCREEN SETTING, and nothing downloaded for it. |
| `-Toolchain Auto` | The default: Visual Studio if it is installed, otherwise LLVM-MinGW. |
| `-Toolchain VisualStudio` | Visual Studio only; stops if it is not installed. |
| `-Toolchain LLVM` | LLVM-MinGW even if Visual Studio is installed. |
| `-SkipPrepare` | Reuse `work\` and `build\generated\` from the last build: only compiles. Quicker after a change to the runtime (`src\runtime\`); not after a change to `tools\` or a different ISO. |
| `-Config Release` | The default. `Debug` and `RelWithDebInfo` build into `out\Debug\` / `out\RelWithDebInfo\`. |
| `-Here` | Git clone: build the branch you are on instead of checking out `main` (still pulls). |
| `-Branch name` | Git clone: the branch to check out (default `main`). |
| `-NoPull` | Git clone: do not pull before building. |

Examples:

```
.\build.ps1 -Run                          # build and play
.\build.ps1 -SkipPrepare -Run             # after editing src\runtime: compile and play
.\build.ps1 -Toolchain LLVM               # the downloaded compiler, even with Visual Studio installed
.\build.ps1 -Iso D:\isos\sfa3max.iso      # an ISO elsewhere
.\build.ps1 -Here -NoPull -Config Debug   # git clone: this branch as it is, debug build
```

In VS Code the default build task (**Ctrl+Shift+B**) runs `build.ps1`.

## What is where

| Folder | What | Safe to delete? |
|---|---|---|
| `imports\psp\` | your ISO | yours |
| `.tools\` | downloaded build tools | yes: downloaded again when needed |
| `work\` | the disc's files, the analysis, **and your saves and settings** (`sfa3r.dat`, `sfa3r.cfg`) | keep the two save files |
| `build\generated\` | the translated C | yes: made again by `prepare.py` |
| `out\` | the CMake build and `sfa3r.exe` | yes: built again |

All of these are git-ignored and must never be committed.

## Troubleshooting

- **"running scripts is disabled on this system":** run `Set-ExecutionPolicy -Scope Process Bypass` in
  the same PowerShell window (it lasts until the window is closed), then `.\build.ps1` again.
- **A download fails:** check the internet connection (or proxy) and run the script again; an unfinished
  download is started again. A download that does not match its SHA-256 is deleted and the build stops.
- **Strange build errors after switching toolchain or updating:** delete `out\` and build again.
- **The ISO is not found:** check the name and place (`imports\psp\Street Fighter Alpha 3 Max.iso`) or
  use `-Iso`.
- The log of the game itself is `work\sfa3r.log`.

## By hand

With Visual Studio 2026 and Python 3.9+:

```
python tools/prepare.py
cmake -B out -G "Visual Studio 18 2026" -A x64
cmake --build out --config Release
out\Release\sfa3r.exe --work work
```

With LLVM-MinGW, CMake and Ninja on `PATH`:

```
python tools/prepare.py
cmake -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build out
out\Release\sfa3r.exe --work work
```

`prepare.py` takes the ISO's path as its argument, and `--no-widescreen`. Re-run it after pulling
changes to `tools\`.

## Linux (developers)

The same code builds with gcc or clang (this is how the automated tests run): `python3 tools/prepare.py`,
then `cmake -B out && cmake --build out -j`. SDL3 needs its usual Linux development packages
([SDL's list](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies)). The game is
`out/sfa3r --work work`. Linux is not a supported target yet.
