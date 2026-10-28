# Known and possible issues

**Known** = seen; **possible** = suspected or untested. An issue is removed when it is fixed (the commit
says what changed).

## Network modes played locally

| # | Status | Issue | Notes |
|---|---|---|---|
| N2 | known | A CPU player chooses its side at random on Dramatic Battle's SELECT PLAY SIDE. | netplay.c `cpu_step`; could steer it to the side with room. |
| N3 | known | A CPU player wanders the select screens with random presses; it cannot tell which screen it is on. | netplay.c. Fine so far because the after-match menu is the host's. |
| N4 | possible | LEAGUE and TOURNAMENT (VS and Team) are untested; two virtual players are started for them. | netplay.c `on_group`. |
| N5 | possible | VARIABLE BATTLE's host / guest choice is not picked automatically (the cursor starts on HOST). | Only Dramatic Battle's 0x088D8790 menu is automated. |
| N6 | possible | The virtual players' "sync when the host goes quiet" rule is a heuristic; a stray sync word could arrive at a bad moment. | vpeer.c. The game skips words it does not expect; no problem seen yet. |
| N7 | possible | The game's "wait for other PSPs" counter (0x09523984) is cut short in the lobby and the battle group; other uses of that counter are not known. | vpeer.c `shorten_waits`. |

## Menus and presentation

| # | Status | Issue | Notes |
|---|---|---|---|
| M1 | parked | A second MODE MENU page was tried and dropped; menus may be rethought, console style, better as part of a decompilation. | If a runtime-drawn layer comes back: the GPU present shader outputs alpha 1, so blending a layer over the frame needs its own shader or a premultiplied path. |
| M7 | known | WIDE FORCED: a black margin where a stage's art runs out (camera at a stage edge) and on the left of the menu screens. The original patch does the same. | widescreen.c; could be improved later. |

## Elsewhere

| # | Status | Issue | Notes |
|---|---|---|---|
| E5 | possible | **EXPORT not tried in PPSSPP or on a real PSP.** Exports use PPSSPP's save encryption and PARAM.SFO hashing (checked against PPSSPP's code, both ways) with the game's key. One PARAM.SFO hash needs a PSP's own key; PPSSPP writes a stand-in there too. | psp_savecrypt.c; SAVES.md |
| E6 | possible | **IMPORT not tried with a save from a PSP or PPSSPP** (only with SFA3R's own exports). | savestore.c `import_one` |
| E7 | known | **Pad 4 cannot pause, or edit P4 CONFIG from the pause menu.** The game has three player slots, and pad 4 plays no fighter yet; P4 CONFIG is set in OPTION MODE > PLAYER CONFIG. To come with a 2v2 / X.Dramatic mode. | hooks.c `f_08828AF4`, exsetting.c (pause BUTTON CONFIG) |
