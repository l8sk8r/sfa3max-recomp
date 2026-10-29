# Changelog

Each version's section here becomes its release notes. To release: in the pull request, raise
`VERSION` and add its section below; merging into `main` tags it and publishes the release
(`.github/workflows/release.yml`).

## 1.1.2

- Fixes the build in Windows PowerShell 5.1, which stopped at the first note a build step printed.

## 1.1.1

- Laptops with two GPUs (NVIDIA Optimus, AMD switchable graphics) run the game on the fast one.

## 1.1.0

- Nothing to install to build: `build.ps1` uses the build tools you have (Visual Studio, CMake,
  Python) and downloads the ones you don't (LLVM-MinGW, CMake, Ninja, Python). Git and pip are no
  longer needed.
- A simpler build guide for players in the README, and a [developers' guide](docs/BUILDING.md)
  (git clone, VS Code tasks, every option).
- The project is now called **SFA3MR** (Street Fighter Alpha 3 MAX Recomp): the game is
  `sfa3mr.exe`, and its saves `sfa3mr.dat` / `sfa3mr.cfg`. Settings and saves from earlier
  versions are not carried over.

## 1.0.3

- Fixes to the build documentation and steps.

## 1.0.2

- Clearer building steps in the README, from download to playing.
- Each release now has **sfa3max-recomp.zip**, which unzips to a plain `sfa3max-recomp` folder (no
  version in the name), so shortcuts keep working after an update.
- Releases are made automatically when a new version is merged into `main`, and the version on
  EX. SETTING always matches it (both come from the `VERSION` file).

## 1.0.1

- `build.ps1` builds a release's source zip: it skips its git steps when there is no `.git` folder.

## 1.0.0

- The first public release: every mode and character of the PSP release, up to three local players,
  L2 / R2 and PLAYER CONFIG, WIDE FORCED, everything unlocked, silent saves, COUCH CO-OP, MODE MENU
  pad icons and F2 back to the MODE MENU.
