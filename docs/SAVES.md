# Saves and settings

SFA3R does not use a PSP memory stick. Everything the game saves, and every setting, is kept in two
files in the work folder:

| File | What |
|---|---|
| `sfa3r.cfg` | every setting, by name: EX. SETTING, GAME OPTION, SCREEN SETTING, each player's buttons (PLAYER CONFIG, with L2 / R2 and player 4), unlocks |
| `sfa3r.dat` | everything else the game saves (rankings, records, Entry and Edit characters, World Tour), as blocks of hex |

The settings in `sfa3r.cfg` win over the same bytes in `sfa3r.dat`, so `sfa3r.cfg` can be edited by
hand (with the program closed). Button settings take the names shown on the P1 - P4 CONFIG screens
(see [PLAYER_CONFIG.md](PLAYER_CONFIG.md)) or their numbers.

Both files are written whenever the game saves (AUTO SAVE is always on: leaving a mode or OPTION
MODE saves). `sfa3r.cfg` is also written when you leave EX. SETTING or a P1 - P4 CONFIG screen, and
both are written when the program closes, so a setting is never lost.

## First run

- With `sfa3r.dat` missing, a save on the memory stick of an earlier SFA3R version
  (`work/ms0/PSP/SAVEDATA`) is imported once.
- With nothing at all, the game's defaults are used (with MODE SELECT on and everything unlocked).
- A `sfa3r.cfg` on its own (no `sfa3r.dat`) is applied on top of the defaults.

## OPTION MODE > SAVE LOAD

| Row | What it does |
|---|---|
| SAVE / LOAD | save or load at once (no questions, no screens): SYSTEM DATA SAVED / LOADED shows at the bottom |
| DELETE | puts the system data (options, button configs, rankings, Entry characters ...) and SFA3R's own settings back to the defaults; the World Tour data is kept. The old `sfa3r.dat` / `sfa3r.cfg` are copied to `.bak` first. SYSTEM DATA DELETED shows at the bottom |
| IMPORT | reads the PSP saves in `work/import` and uses them from now on. The current `sfa3r.dat` and `sfa3r.cfg` are copied to `sfa3r.dat.bak` / `sfa3r.cfg.bak` first |
| EXPORT | writes the saves to `work/export`, as a PSP keeps them |

A PSP save is a folder: `ULUS100620000` (system data: options, rankings, Entry characters ...) and
`ULUS100620001` (World Tour and Edit Mode characters). To import, copy one or both folders from a
PSP's or PPSSPP's `PSP/SAVEDATA` into `work/import` (putting the whole `SAVEDATA` or `PSP` folder there
works too). To use an export on a PSP or in PPSSPP, copy the folders from `work/export` into its
`PSP/SAVEDATA`.

Exports are encrypted with the game's own key and carry a PARAM.SFO and the game's save icon, as the
PSP makes them (the method is PPSSPP's; neither PPSSPP nor a real PSP has been tried with them yet:
known issues E5 / E6).
Imports read encrypted saves (from a PSP or PPSSPP) and the unencrypted ones of earlier SFA3R versions.

SFA3R's own settings (EX. SETTING, L2 / R2, player 4) are not part of a PSP save: IMPORT keeps them as
they are, EXPORT leaves them out.
