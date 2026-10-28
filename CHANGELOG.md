# Changelog

Each version's section here becomes its release notes. To release: in the pull request, raise
`VERSION` and add its section below; merging into `main` tags it and publishes the release
(`.github/workflows/release.yml`).

## 1.1.0

- Nothing to install to build: `build.ps1` downloads its own compiler (LLVM-MinGW), CMake, Ninja and,
  if needed, Python. Visual Studio, Git and pip are no longer needed (`-VisualStudio` still uses
  Visual Studio).

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
