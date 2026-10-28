# Contributing

Issues and pull requests are welcome. The maintainer reviews every pull request and is the only one
who merges into `main`.

## Before you start

- **Never commit game data:** no disc images, extracted files, `work/`, `build/generated/` or anything
  built from them. The `.gitignore` covers the usual ones; check `git status` before you commit.
- For anything bigger than a small fix, open an issue first so we can agree on the approach.
- Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the pieces fit, and
  [docs/DECISIONS.md](docs/DECISIONS.md) for why things are the way they are.

## Branches

`main` is always buildable and is only changed through pull requests. Work on a branch named for the
kind of change, in lower case with hyphens:

| Prefix | For | Example |
|---|---|---|
| `feature/` | something new | `feature/pad-icons` |
| `fix/` | a bug fix | `fix/pause-l2r2` |
| `docs/` | documentation only | `docs/controller-page` |
| `chore/` | build, tooling, tidying | `chore/build-script` |

Keep a branch to one change, and keep it short-lived: branch from the latest `main`, and rebase or
merge `main` in if it moves on.

## Commits

- Small commits with a short summary line (under about 72 characters), then a blank line and a few
  lines on *why*, if it is not obvious.
- No generated code, logs, screenshots of copyrighted material or personal paths in commits.

## Pull requests

1. Push your branch (to your fork, if you do not have write access) and open a pull request into
   `main`.
2. Say what it changes and how you tested it: which mode, which controller, and screenshots where it
   helps.
3. Check the rest of the game still behaves the same. The headless runs (`--headless --fast --input
   ... --dump ...`, README > Command line) make before / after screenshots easy to compare.
4. Update the docs the change touches (README, `docs/`, `tools/data/hooks.txt` for a new hook).

The maintainer reviews, may ask for changes, and merges.

## Releases (maintainer)

A release is made by merging a version: in the pull request, raise `VERSION` (for example `1.0.2` to
`1.0.3`) and add a section for it at the top of [CHANGELOG.md](CHANGELOG.md). Once it is merged into
`main`, the release workflow tags it `v1.0.3` and publishes the release with that section as its notes. The game shows the same version on EX. SETTING.

## Code

- C for the runtime (`src/runtime/`), Python for the tools (`tools/`).
- A game function replaced by native code is listed in `tools/data/hooks.txt` and named
  `f_XXXXXXXX`, calling `orig_f_XXXXXXXX` around its own changes.
- Comments say what the game's addresses are and why a change is made; new findings about the program
  go in [docs/PSP_PROGRAM_MAP.md](docs/PSP_PROGRAM_MAP.md).
- Known problems go in [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## Reporting a bug

Use the bug report form. Include the version (bottom corner of OPTION MODE > EX. SETTING), the mode,
your controllers, the steps to make it happen, and `work/sfa3r.log` if you can.

## Licence

By contributing you agree that your contribution is licensed under the project's licence,
GPL-3.0-or-later ([LICENSE](LICENSE)).
