# PSP program map (Street Fighter Alpha 3 MAX, ULUS-10062 v1.00)

Everything here was measured with the tools in `tools/`; re-run `tools/prepare.py` to reproduce it.

## The disc

| File | Notes |
|---|---|
| `PSP_GAME/SYSDIR/BOOT.BIN` | **Unencrypted** relocatable program (ELF type 0xFFA0). SHA-1 `3fd0abf7cc65742233e1b7787997fa3e20152d98`. Used as the source; no decryption needed. |
| `PSP_GAME/SYSDIR/EBOOT.BIN` | Encrypted `~PSP` copy of the same program. Not needed. |
| `PSP_GAME/USRDIR/usa/module/*.prx` | Sony's encrypted libraries (ATRAC3+, SAS, MPEG, font, codecs). Replaced by native code (HLE); never loaded. |
| `PSP_GAME/USRDIR/usa/data/*.pac` | 313 game data archives (format still to be decoded). Names show the layout: `pl00`–`pl29` (42 fighter slots, plus `pl29r`), `st*` stages, `op*`/`end*`/`win*`/`bd*` per-character intros, endings, win screens and bios, `rk*`, `select_u`, `title_u`, `world_u`/`wr_scr` (World Tour), `netd_u`/`nets_u` (wireless), `msel_u` (mode select), `opt_u`, `memory_u`. |
| `PSP_GAME/USRDIR/usa/bgm/p0000.at3`… | 51 ATRAC3+ music tracks. |
| `PSP_GAME/USRDIR/dummy.bin` | 10 MB padding. |
| `PARAM.SFO` | `ULUS10062`, title `STREET FIGHTER ALPHA3 MAX`, disc version 1.00, system 2.50. Module name `ZERO3`. |

## Program layout (loaded at 0x08804000, the hardware address)

| Region | Address | Size |
|---|---|---|
| `.text` | 0x08804000 – 0x089FC354 | 2.06 MB, 516,309 instructions |
| import stubs | 0x089FC354 – 0x089FC8FC | 181 OS functions |
| `.rodata` | 0x089FCFF0 – 0x08A02DE0 | 24 KB |
| `.data` | 0x08A02DE0 – 0x08AD8F18 | 857 KB, incl. switch tables and function-pointer tables |
| `.bss` | 0x08AD8FC0 | 10.3 MB |

- Entry: `module_start` 0x08804000, `module_stop` 0x08804360.
- **Relocations are complete**: about 90,000 relocation records. Because the program had to be relocatable,
  every code address stored anywhere carries one, so the analysis sees every function pointer.
- **No VFPU (vector unit) instructions at all**, about 2,500 single-precision FPU instructions (incl. loads/stores), and a
  handful of Allegrex extras (`ext`, `ins`, `seb`, `seh`, `min`, `max`, `clz`). Branch-likely is used
  (about 6,500).
- 14,932 functions: 4,839 called directly, 9,555 only through pointer tables (Capcom-style state
  machines), the rest reached by fall-through or never called.
- 74 `switch` jump tables (578 case labels), all resolved. No unresolved indirect jumps.

## Where things are

| What | Where |
|---|---|
| Controller read (`sceCtrlReadBufferPositive`) | one function, **0x088051BC**. Raw pad buffer at 0x08AD8FD0; buttons copied to 0x08AD8FE0 and 0x08E75900; analogue stick folded into the d-pad (threshold ±64). |
| Wireless (ad-hoc) VS code | one block, **0x088DE4A0 – 0x088E1EC4** (sceNet, sceNetAdhoc, Adhocctl, Matching). PTP send at 0x088DF910 / 0x088DFEF4, receive at 0x088DFA48 / 0x088E02C0. |
| Save data (`sceUtilitySavedata*`) | 0x089E347C – 0x089E3E78 |
| Graphics (`sceGe*`) | 14 functions, 0x089E4110 – 0x089EAAD4 |
| Sound (SAS voices) | 23 functions, 0x089EC8E4 – 0x089ED018; ATRAC3+ music 0x089E5BC4 – 0x089E68A4 |
| Compiler library (libc etc.) | about 0x089E6000 onwards |

## Modes named in the program

`SURVIVAL MODE`, `VS100 KUMITE MODE`, `VARIABLE BATTLE`, `DRAMATIC BATTLE`, `R.DRAMATIC BATTLE`,
`DRAMA SURVIVAL`, `RECOVERY BATTLE`, `FREE BATTLE MODE`, `TEAM BATTLE`, `TRAINING MODE MENU`, `ISM EDIT MODE`,
`ENDING MODE`. Wireless menu: `VS MODE`, `TRAINING MODE`, `TEAM VS MODE`, `SURVIVAL MODE`,
`DRAMATIC MODE`, `VS DRAMATIC BATTLE`, `VS VARIABLE MODE`, `TOURNAMENT MODE`, `LEARGUE MODE` (sic).
The wireless modes are the ones local 2P/3P should open up.

## Widescreen patch (Rickow SFA3MAX-PSP-widescreen v1.1)

Applies cleanly to this `BOOT.BIN`. Changes 35 words in `.text` (viewport 384 -> 480 and the camera
-192 -> -240 as immediates; 0x0880F390, the pillarbox, returns at once; six stores in the background
tile drawers 0x0881AB28 / 0x0881B268 / 0x0881B7C8 become jumps into caves) and adds code in caves in
unused `.data` (0x08A02ED0, 0x08A02F38, 0x08A031B4; zero in the original, though 0x08A02ED0 is also
an address the program computes). `prepare.py` records the overlay (`work/boot.widescreen.json`) and the
translation switches it with `rt_widescreen` (SCREEN SIZE > WIDE FORCED). The game's own display option
must be **Normal** for it, which WIDE FORCED sees to.

## What boot does (from the runtime's trace)

1. `module_start` creates **user_main** (prio 32, 256 KB stack), which loads Sony's libraries
   (`audiocodec`, `sc_sascore`, `libatrac3plus`, `libfont`: handled natively) and starts **GameMain**
   (0x08804B84, prio 111).
2. GameMain sets 480×272 display mode, creates the `SceGuSignal` event flag, sets the GE callbacks
   (signal 0x089EA550, finish 0x089EA5D0) and submits the first display list (0x08A020E8).
3. Double-buffered frame buffers at **0x04000000 and 0x04044000** (VRAM), width 512, pixel format 1
   (16-bit 5551).
4. Grabs one big heap ("UserSbrk", 0x8E7000 bytes at 0x09531000), reserves audio channels 0–3
   (448/256 samples), starts **sound_thread** (0x089E5BEC, prio 16) which mixes SAS voices in
   256-sample grains and outputs 4 grains at a time on channel 0.
5. Loader threads (**task_ctrl_thread**, prio 107) read the data packs: `ascii`, `vram`, `efct`,
   `sign`, `memory_u`, `pit_u`, `zett`, `ake`, `opt_u`, `se_mid`, `select_u`, `titlesnd`.
6. At about 3.6 s it checks the memory stick (`fatms0:` devctl), then settles into one display list
   per frame: presumably the title or save-check screen.

Volume setting byte: 0x08AD8040 (written by 0x089E688C; read by the sound thread).

## Save data

| Save | File | Contents |
|---|---|---|
| `ULUS100620000` | `STFALPH3.SYS` | system data (options, unlocks, records) |
| `ULUS100620001` | `STFALPH3.WTR` | World Tour |

Both files carry 0x3399 at +0x280 and have no checksum. Loading (0x08816FC4) copies them in pieces:
SYS +0x284 (0x208 bytes) → 0x08A058A0, +0x48C (0x17C0) → 0x08A05AA8, +0x1C4C (0x3C0) → 0x08A07268,
+0x200C (0x140) → 0x08A07628, +0x214C (0x13F8) → 0x09519588, +0x3544 (4) → 0x08A05894;
WTR +0x284 (0x174C) → 0x09517E38. SFA3MR keeps these files in sfa3mr.dat / sfa3mr.cfg (savestore.c,
settings.c), not on a memory stick; a PSP keeps them encrypted (see "save key" below, docs/SAVES.md).

| Address | Meaning |
|---|---|
| 0x08A058A0 + 0x4F | AUTO SAVE (OPTION > SAVE LOAD); 0x08A077CC is the session copy (cleared without a memory stick) |
| 0x08A058A0 + 0x50 | bits 0-2: modes cleared in their alternative (L + R) version |
| 0x08A058A0 + 0x52 | unlock bits (16-bit), set by 0x0882AE7C when modes are cleared; bits 0x0001 / 0x4000 gate MODE MENU alternatives (0x0888CEC4) and the select screens' extras (0x08897434: SELECT held when choosing turns Akuma (2) into Shin Akuma (0x24) and the second Balrog (0x25) into 0x15, and makes M. Bison (0xA) Final Bison (fighter + 0x1B7) in Arcade, VS and modes 9, 0x11, 0x12) |
| 0x08A058A0 + 0x68 | game time in frames |
| 0x08A058A0 (+20 per player, 3) | button config: 8 halfwords (attack for Square, Triangle, L, Cross, Circle, R, Select, unused), +0x10 COMMAND, +0x11 TYPE, +0x12 RAPID. A fighter reads its entry when the button is pressed. Attacks 0-0x36, names at 0x08A37B44 (L PUNCH ... HP + HK, NO USE, CHOUHATU = taunt, SPECIAL1 L ...); COMMAND / TYPE / RAPID names at 0x08A37A5C / 68 / 70 |
| 0x08864DFC | a fighter's buttons, once a frame (a0 = fighter): held game bits of its slot (0x08E75BA0 + 0x2C n + 4; TRAINING: fighter +362) → a held flag per button (0x08B0264E + 16 × player: 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x4000 SELECT, 0x8000 START); each held button's config attack → game bits (attacks 0-14: 0x08A19782; specials / super combos: per character lists at 0x08A1B3F8 / 0x08A1B4F4, counts at 0x08A1B4A0 / 0x08A1B59C, 8 bytes per entry: bits, move id) and the attack kept at fighter +1150 + 2 × button; bits to fighter +752 / +764. Some modes read configs at 0x09523438 (0xD0 each) instead. Hooked: L2 / R2 (playercfg.c) |
| 0x08867794 | a fighter's newly pressed buttons as a byte (bit 7 START), a0 = fighter: slot +0xC by fighter +775, or TRAINING +362 & ~+364. Read with +1150 by 0x08867A60 (one-button specials / super combos) and 0x088679BC. Hooked: L2 / R2 |
| 0x08E75BA0 slot bits | game bits: 0x10 Square, 0x20 Triangle, 0x40 L, 0x80 Cross, 0x100 Circle, 0x200 R, 0x4000 SELECT, 0x8000 START; 1/2/4/8 directions |
| 0x08A058A0 + 0x200 | ISM-Pluses earned (one bit each in bits 0-27, 26 in all; bit 0 Alpha Combo, bit 4 ∞ Guard, bit 5 Auto Guard; the mode rewards at 0x0883B884 give bits 15-27 by mode). EDIT MODE lists the rest as ???. All held on (sysdata.c) |
| 0x08A058A0 + 0x204 | GAME OPTION > MODE SELECT (1 on; file +0x488). On in the save made on first start |
| 0x08A058A0 + 0x3C.. | GAME OPTION: +0x3C difficulty 0-7, +0x3D time (30 60 99 ∞), +0x3E rounds (1 3 5), +0x40 damage 0-3 (and +0x42), +0x43 speed TURBO1-7 (0: +0x44 NORMAL / FREE1 / FREE2), +0x45 S.C. gauge (0 MAX), +0x1FC / +0x1FD BGM / S.E. volume 0-15, +0x1FF player side. Value names at 0x08A379C0 on. Kept in sfa3mr.cfg (settings.c) |
| 0x08A058A0 + 0x58 | SCREEN SETTING > SCREEN SIZE (NORMAL / WIDE, and +0x62); +0x1FE WALLPAPER (NONE, 01-10) |
| 0x08AEF75C | OPTION MODE's copy of the options block (0x208 bytes) |
| 0x08E75910 + 0x2D | the difficulty in effect (GAME OPTION edits it and writes 0x08A058A0 + 0x3C, 0x088B6318); the other settings in effect are near it, filled from the options block after a load (LOAD row: 0x088B73B8 → 0x088B6108) |
| 0x088B70F8 / 0x088B7AE8 | SAVE LOAD submenu: logic (rows SAVE, LOAD, DELETE, AUTO SAVE, EXIT in +7; per-row handlers 0x08A37A34) / drawing (rows at y 0x38 + 0x18 n). Hooked: IMPORT and EXPORT rows (exsetting.c) |
| 0x088BAB44 | SAVE LOAD's green bar (object kind 0xC, type 0x14), once a frame: y = 240 - the row's text y |
| 0x08AD51D0 | the save icon (ICON0.PNG, 11005 bytes) the game passes with its system data save |
| save key | the game saves with SceUtilitySavedataParam size 0x600, secure version 0 and key 006FAF01D5700002C712242EBA83FFE1 (SDK 2.5: crypt mode 3) |
| 0x088B6164 | OPTION MODE menu, once a frame. Context 0x08E75912: +0 state, +1 cursor, +5 submenu shown, +6 in a submenu. Submenus by cursor: 0x08A37A10 (start) and 0x08A37A48 (each frame) |
| 0x088B7EC0 | menu cursor up / down (a0 pad slot, a1 cursor, a2 last item) |
| 0x088BA82C | OPTION MODE button (object kind 0xC, type 0x0F + item), once a frame: bright and bigger under the cursor, dim otherwise. Layout tables 0x08A37DBA (x, y) and 0x08A37C60 (size, page, u, v) |
| 0x088BA8F4 | GAME OPTION's green bar (object kind 0xC, type 4), once a frame: y from 0x08A37D28 by the row (+7 of the option context); shown only while +5 == 1, removed when +6 == 0 |
| 0x08B0C334 | texture pages, 0x18 each (+0 address, +4 RAM copy, +0x14 description); an object's +0x3C page n is entry 0x30 + n |
| 0x0880A7B0 / 0x0880A7F4 | draw a PSP button symbol (a0: 0 Circle, 1 Triangle, 2 Square, 3 Cross, 8 L, 9 R; font 5) / SELECT (font 6) at a1, a2 |
| 0x088D2B64 | unpacks one 16x16 tile (128 bytes, 4-bit) of a sprite into a 64x64 texture; save and boot messages are such sprites (pictures of the text), drawn tile by tile. The boot screen's "Auto-load completed." lands at 0x095FF600 |
| 0x08818BA4 | Yes / No question (a0 context: +4 message, +9 cursor 1 Yes / -1 No) |
| 0x09523C60 | save / load task context: +2 state (4 = result shown, waiting for a button; 0x08817C10), +4 message (0x41 load World Tour?, 0x18 load SYSTEM DATA?, 0x11 Load completed.), +5 result |
| 0x0888D2B0 | leaving a mode for the MODE MENU: +4 state (1 runs the auto-save 0x0881725C, 2 shows the result for 120 frames, 4 fades out), result in 0x09525880 (5 saved). The only caller of the auto-save |
| 0x088335B8 | entering NETWORK: "Save SYSTEM DATA now?" (state at task +6: 1 asking, 2 yes, 3 no, 4 back) |

## Graphics usage

- Each scene is drawn at **480×224** into a 512-wide buffer at **0x04088000** (RGB5551), then drawn as one
  bilinear-filtered sprite onto the visible buffer (0x04000000 / 0x04044000), stretched to 480×272.
- Textures live in RAM and in VRAM from 0x040CC000; formats seen: 5551, CLUT4, CLUT8 (palettes 565/5551).
- Only one blend mode (src alpha, 1 − src alpha, add), alpha test "greater than 0", texture function
  modulate (mostly with texture alpha).

Measured over 1,800 frames:

- All primitives are **through mode** (2D screen coordinates): triangle strips (≈98%) and sprites.
- Two vertex formats: `0x80011E` (16-bit UV, 32-bit colour, 16-bit position) and `0x80011C` (no texture).
- No 3D transforms, lighting, depth test, splines or DXT textures.

## Input path and players (found with --watch / --snap / --poke)

1. `0x088051BC` reads the pad (`sceCtrlReadBufferPositive`), folds the stick into the d-pad, runs the
   button-config remap `0x088E3660`, stores the result at **0x08AD8FE0**.
2. `0x0880509C` (now hooked) builds three records `{slot, present, buttons}` at **0x08E75C28** and passes
   each to `0x08804F50`, which keeps per-slot controller state at **0x08E75BA0 + 0x2C × slot**
   (+4 held, +8 previous, +0xC pressed, +0x10 released, +0x14 repeat). The local pad goes only to the
   slot named by the byte at **0x08AD8FE4** (0 host, 1 guest); other slots normally come over ad-hoc.
3. **Fighter objects** at **0x092E3EA0 + 0x498 × k** (k = 0, 1, 2):

   | Offset | Meaning |
   |---|---|
   | +0x00 | active |
   | +0x02 | player number (selects the controller slot in most modes) |
   | +0x12 | X position (s16) |
   | +0x16A / +0x16C | copy of the slot's held buttons (current / previous) |
   | **+0x175** | **computer-controlled flag**: clear it and the fighter obeys its controller slot |
   | +0x2F0 / +0x2FC | decoded game input (after the per-player button config) |
   | +0x307 | controller slot index |

4. `0x08864DFC` decodes a fighter's buttons; it chooses the source by the **game-mode byte at
   0x08E75982** (seen: 0x0D/0x13 use +0x307, 2 uses +0x16A, 7/0x0F/0x10 special cases, otherwise +2).
5. CPU fighters run AI routines from a pointer table at about **0x08A1914C** instead of reading input.

Roles seen: Arcade: fighter 0 = player 1, fighter 1 = opponent. **Dramatic Battle: fighter 0 = player 1,
fighter 1 = partner, fighter 2 = the boss.** P2's Start opens the game's own "2P PAUSE" menu.

## Menus

| Address | What |
|---|---|
| 0x08A26C90 | MODE MENU state table: 0x0888C3A0 title wait, 0x0888C498 sub-state dispatch via 0x08A26CA0 (0x0888C4CC init, **0x0888C5B0 running**) |
| 0x088325DC | back to the MODE MENU (a0 = 0x08E75910): stops the other tasks, resets the game state and restarts task 0 at 0x0888C268 (used by the pause menu's quit and many more). Tasks are threads; the running one's record is at *0x08E75C40 (+0 state, +4 entry; task 0 at 0x08AD8FE8, 12 bytes each); 0x088054F0 waits for the next frame, 0x08805614 restarts the running task at a0 |
| 0x0888C5B0 | MODE MENU running state, a0 = menu object (+0x72 selected mode id, +0x35 how it was chosen). Reads slots 0-2; game bits 1/2/4/8 = directions, 0x8080 confirm, 0x100 back. L + R held on confirm (game bits 0x240, pad 1 or 2) picks a mode's alternative: Dramatic (8), Variable (15) and R.Dramatic (17) with unlock bit 0x4000, Final Battle (9) with bit 0x0001 (0x0888CE80; sound 0x3A, + 0xBF = 1) |
| **0x08B02711** | MODE MENU cursor = column × 5 + row (0 ARCADE ... 9 NETWORK ... 14 SCORE RANKING); 0x08B02710 = key-repeat timer |
| 0x08A26C68 | cursor → mode id (15 bytes: 0,15,2,16,5,3,14,4,18,12,7,8,17,9,11); 0x09525DD0 = which mode ids are enabled (copied from 0x08A26C77) |
| 0x088D8128 | network task (task 0's entry while in NETWORK); state at 0x09523980 (+0x14 step table, +0x18 step). 0x08A3E8F8 is set while in NETWORK, so the task resumes rather than starts afresh after a battle. Its exit to the MODE MENU is 0x088D7A08; 0x088D7EDC shuts the network down (sockets, matching, AdhocTerm, NetTerm), as the exit callback 0x08804A88 does. A network battle sets the mode (0x08E75910+0x72) to 1 |
| 0x0888CD94 | MODE MENU dispatch (a2 = 0 chosen, 1 by player 2, -1 back); jump tables at 0x089FF0D0 (alternatives) and 0x089FF0F8 (mode id 0-19). Mode 12 (NETWORK) backs up 0x9FC bytes of settings and starts the network task 0x08837A44 |
| 0x0888C85C / C90C / C9B0 / CA5C | MODE MENU cursor down / right / up / left (skip disabled modes) |
| 0x0887C4CC | sound effect (a0 = 0, a1 = id, a2 = 0xC0): 0x38 cursor, 0x39 choose, 0x3A, 0x2F back |
| 0x088DD5D0 | vertical list menu (a0 = context, cursor at +0x2E; a1 = items; a2 = slot mask): returns 1 chosen, -1 back. Network menus share context 0x09523980: called from 0x088D85C4 (network MODE MENU: VS MODE, TEAM BATTLE, VARIABLE BATTLE, DRAMATIC BATTLE), 0x088D86D8 (SINGLE, LEAGUE, TOURNAMENT), 0x088D8790 (Dramatic: HOST, GUEST) |
| 0x088DD734 | horizontal value adjust (a0 = value, a1 = max): option screens |
| 0x09524A28 | network task's yes/no dialog ("Save SYSTEM DATA now?"): +0 = 0xFF and +2 = 2 while up, +1 cursor (0 Yes, 1 No). Handled at 0x08833700; the task state (s0 + 6) becomes cursor + 2 when chosen |
| 0x088331E4 | "confirm pressed" check for a slot (0x80, or 0x280 in two-button mode) |
| 0x092E2B88 | menu/screen objects: 32 × 0x94 bytes, handler table 0x08A25AA0 by +0x5B (run from 0x08887AFC) |
| 0x08E75A18 | texture list per object layer (+0x36), 4 bytes each: {entries, count}, an entry 12 bytes with +8 the page table entry. An object's page (+0x3C) is an index into its layer's list (0x0881036C selects it with 0x089E0EA4), not 0x30 + page as such. The OPTION MODE buttons' layer 0x12 lists 0x30 - 0x34 |
| 0x08B0C334 | texture page table, 0x18 per entry: +0 texture, +4 RAM copy, +0x14 description (+0 index, +0x18 format, +0x24 / +0x28 width / height). OPTION MODE tile pictures: page 0x32 (GAME OPTION 0, BUTTON CONFIG 32, SCREEN SETTING 64, EXIT 96), 0x33 row 128 (SAVE/LOAD); MODE MENU tiles 0x30, 0x31, 0x33 (176 × 31 at 32-pixel steps) |
| 0x09512770 | objects of another system: pools of 0xC0-byte entries (+0 active, +1 shown, +2 kind, +3 type, +0x12 x, +0x16 y up from the bottom), made from the top down: 0x08822EAC (32, kind in a0), 0x08822EE4 (pool from 0x09514AB0, 16); 0x0882196C sets an animation, 0x088215C4 steps and draws. The option BUTTON CONFIG's attack pictures are kind 0x93 types 0x24 - 0x2A (handlers 0x08A380EC: 0x088BB104, 0x088BB268), animation 0x08A38010[value] |
| 0x088B8ED0 | pause menu BUTTON CONFIG (task: 0x088BA028 once a frame, context 0x08B02E24: +0 - +2 each player's row 0 - 11, +4 the player from 0x08E75910 + 0x59). Input 0x088B8FC4 / 0x088B90E4 / 0x088B9208 (players 1 - 3; up / down 0x088B9CEC, left / right 0x088B93BC through 0x088B9B30), drawing 0x088B9790 (rows 16 apart from 0x18), attack pictures kind 0 types player × 8 + 1 + row (0x088D0D64, animation 0x08A3CC78[value]), green bar kind 3 type 5 (0x08888CC0, rows from 0x08A2620C) |

The yes/no prompt at boot ("Do you wish to begin the game without auto-loading?") is a different dialog.

## Network battle protocol (recorded with SFA3MR_NETCAP=1)

Lobby groups: VERSUS, LEAGUEVS, TOURNAVS, TEAM, LEAGUETM, TOURNATM, DRAMATIC, VARIABLE.
Group "VERSUS" (P2P matching, mode 3) or "DRAMATIC" (parent mode 1 with children mode 2), matching
port 555, hello/request data 4 zero bytes. After matching the game leaves the lobby group, joins a
group named from the players ("9B413301") and, after a wait counter at **0x09523984** (about 600
frames, 940 in Dramatic Battle), opens PTP streams on port 1025. VS: the guest connects to the host.
Dramatic Battle: every PSP listens and connects to the next one in a ring 0 → 2 → 1 → 0; data that
must reach everyone is passed round the ring. All PTP traffic goes through 0x088DF988 (send) and
0x088DFAC0 (receive); the message layer is 0x088E0CC8 / 0x088E11C0 and the sync state machine is
around 0x088E3860 (words at 0x08A3EB98).

| Message | Meaning |
|---|---|
| 0x1AC3C501 | sent once by the side that accepted the stream |
| 228 bytes | settings block (checksum word, button layout, member list); the host's has byte 30 different |
| 0xE198DE71 / 0x53201336 / 0x7F035D72 | sync request / answer / go; who asks alternates; a receiver skips words it does not expect |
| 0x1CE14355 | phase marker, sent by both sides; afterwards the guests ask for the sync |
| other words | lockstep input: low 16 bits the raw PSP buttons, high 16 bits the frame counter (+2 per frame); each side sends its own input to every neighbour, several frames per packet |

Dramatic Battle also uses PDP port 1026: an 84-byte member list from the host (MACs and roles), then
a roll call `AC 00 sum 94 type 00 step 00` (sum = 0xA3 + type + step) that everyone repeats about
every 21 frames until all report the next step.

Names in the network screens are drawn with sceFont: the host's from the system nickname
(sceUtilityGetSystemParamString 1), the others' from sceNetAdhocctlGetNameByAddr, into a
256x32 4-bit buffer one character at a time (GetCharInfo, then GetCharGlyphImage_Clip).

After a network match, CONTINUE / SELECT / EXIT is run by 0x088E9AE0 (context 0x08E75910: +0x16
cursor, +0x14 timer from 1200, then EXIT). Only the host takes input, from the local pad; a guest
(0x09523980 + 0x2B = 2 or 4, see 0x088D5E7C) waits for the host's choice.

## Select screen players (offline)

Each player object (the fighter objects above) runs its select cursor from 0x088945C0 only while
**+0x338** is set; 0x08894140 sets it from the players present (0x08E75910 + 0x35, bit per side)
when the screen starts, and Dramatic (mode 8) / Variable (15) hand it back and forth (0x08894950,
0x08895530). A player who presses Start later joins through 0x0883465C (+0x154 = 3, +0x35 |= side)
but its +0x338 stays 0, so on the PSP the joiner was frozen: hooked to switch it on. Cursor input
comes from 0x08896AA4 (slot by side +0x151). In offline Dramatic Battle the boss (fighter 3) has
player number 0.

The Survival and Free Battle select screen (code 0x088A2xxx - 0x088A6xxx, context 0x092E1228) reads a box's
buttons from fighter +0x334's +0x16A (held) / +0x16C (last frame's): 0x088A5174 finds that fighter,
0x088A507C / 0x088A5100 read it. +0x334 is set when the screen starts (0x088A6DF4 ...) to the player who
started, so one player picks every box; hooked for the Dramatic kinds (decision 20). Each fighter 0-2's
+0x16A is the held word of slot +0x307 (= +775), copied each frame by 0x08820E50. The objects' states
(+4): 1 character, 2 chosen, 3 ISM, 7 speed; Survival Dramatic goes 1P character, 2P character, ISM,
speed. 0x08E75910 + 2 is the game's phase: 0 MODE MENU, 1 select screen, 2 VS screen and battle.
Free Battle's kinds, fighters on the select screen (player numbers 0 - 3 = fighter number there): Arcade
0, 1 (2 and 3 off); Team 0, 1, starting in state 0 (team size), then 1 (characters); Variable and Dramatic
0, 1 (the team) and 2 (the opponent; 3 unused); R.Dramatic the starter (0 or 1) and 2, 3 (the opponents).
In R.Dramatic the boxes' +775 (the slot whose word is in +0x16A) is 0, 255, 1, 2: the box that reads pad k
is the one whose +775 is k, among all four. Turn order (player 1 started): starter's character and ISM,
then the partner's character (Variable / Dramatic), then the opponents' character and ISM, then the
starter's speed (state 7 / 8). Boxes measured on the screen: Arcade / Team 1P 99.5 and 2P 379.5 at 43
(fixed); R.Dramatic's opponents 379.5 and 429.5 (player 1 started) or 49.5 and 99.5, at 72.
0x088A1548 (a0 = 0x08E75910) runs once a frame there and draws SPEED: with 0x0880A6CC in font 3 (x 0x78,
y 0xC2, colour from 0x08B02716). Text coordinates are 384 x 224, scaled to the 480 x 272 screen; font 3 is
about 10.6 units a letter. Box centres (screen): Survival 1P (50, 72), 2P (100, 72); Free Battle 4 to the
left. In Free Battle the opponent's box is fighter 2, with player number 2 on the select screen (0 in the
fight).

Dramatic Battle's select screen (mode 8): per player, display tasks with a1 = the player: box 0x0889961C,
portrait 0x0889B0A4 (its set-up 0x0889B15C), sprite 0x0889A818, name strip 0x0889BFA4, and more (0x08899744,
0x08899AC8, 0x08899D28, 0x0889AA6C, 0x0889B2AC, 0x0889C23C); they run only while the player's bit is in
0x08E75910 + 0x29 | + 0x39. Their side (task +0xB: 1 left, 0 right) is `side ^ 1`, except in modes 8 and 15
where it is (0x08E75910 + 0x35) & 1 for everyone: the team's side (player 1 present: left), so a player 2
joining player 1 was drawn over player 1. The tasks of a kind run through one entry each (a0 = the task, its player
at +0x5C, which calls the task's state handler from a table): 0x08899558, 0x08899A80, 0x0889A7C8 (sprite;
also the ISM banner), 0x0889B048 (portrait; also the ISM column), 0x0889BAB0, 0x0889BF30 (name strip;
also the ISM banner), 0x0889E434, 0x088A712C, 0x088A7694, 0x088A78E0, 0x088A7CB4, 0x088A8038, 0x088A81FC,
0x088A8404. The ISM panel's parts take their sides when they start, some from the team rule and some
from the player's side: with player 2 started and player 1 joined, the banner went left of its column.
In Dramatic Battle the player's state after choosing a character is 0.3 (+4 = 0, +5 = 3), then 2 (ISM). 0x08896AA4 (new presses) / 0x08896B18 (held) give a box its
buttons: in modes 8 and 15 both boxes read the starter's slot. The cursor goes to the other box at 0x0889494C
(modes 8 and 15). A joiner's box (+0x154 = 3) waits in state 0.0 with +0x338 = 0 until then; the waiting box
sits at (149, 47) on the screen whichever player started. The select screen runs once a frame at 0x08894214
(a0 = 0x08E75910). Free Battle's boxes when player 2 starts: the team on the right (1P 379, 2P 429), ENEMY on
the left (96); Survival's: 1P 379, 2P 430.

The 2D library (used by the text and many screens): 0x089E0CF4(a0 = texture list {descriptors, count}),
0x089E0EA4(n) picks descriptor n (12 bytes: ?, ?, page index into 0x08B0C334), 0x089E1618(a0 = {xy, colours,
uv, n}, a1 = n, a2 = flags (0x80000000 textured), f12 = layer) adds a primitive to the frame's list
(0x089E44EC; 0x08E55C40 + 0x8000 per buffer, count at 0x08E65C40), which 0x089E4408 draws layer by layer, 0
to 23 (from 0x089E47C4, the frame's render). Vertex x, y are in the text's 384 x 224 space (+0x30, -8 added).
Text is queued by 0x0880A6CC into 0x092DF240 (96 entries of 16 bytes) and drawn by 0x08812520 (or 0x0880B098):
fonts' advance / height at 0x08A03CE6 (font 0: 8 x 8, 1: 8 x 16, 2: 16 x 16, 3: 12 x 12), colours (top,
bottom vertex colour) at 0x08A03D04.

Battle setup after a select screen is 0x0882E380 (a0 = 0x08E75910). +0x39 holds a bit per player who
finished choosing (set by 0x088954E0, but only for the player who finishes second). Arcade (mode 0)
always takes the opponent from the ladder (0x0882FEB8: ladder table 0x09517988, writes the opponent's
+0x152 character, +0x173, +0x182, +0x56); other modes branch on +0x39 == 3 for player against player.
After a two-human Arcade match won by a player on a different character, the game shows that character's
profile and reason for fighting (input polled by 0x0882BB68 with mask 0x83F0).

Mode identification: game-mode byte 0x08E75982 (= 0x08E75910 + 0x72; MODE MENU ids: 0 Arcade, 2 Training,
3 World Tour, 4 Entry, 7 Survival, 8 Dramatic, 9 Final Battle, 12 Network, 14 Edit, 15 Variable,
16 Free Battle, 17 R.Dramatic, 18 VS 100 Kumite). Survival's kind: 0x092E1228 + 0x4F (1 Arcade,
2 Original, 3 Boss, 4 Dramatic, 5 Variable, 6 R.Dramatic, 7 R.Dramatic with lives, 8 Infinity).
Free Battle's kind: 0x092E1228 + 0x9F (1 Arcade, 2 Team, 3 Variable, 4 Dramatic, 5 R.Dramatic).
Fighter layout seen: 1-on-1 modes fighters 0-1 (player numbers 0, 1); Dramatic fighters 0-2 with the
boss as fighter 2 (player number 0); Variable fighters 0-2 with fighter 0 tagged out as CPU.

The fighter button reader 0x08864DFC picks the held word by mode: TRAINING the fighter's +362; modes 0xD / 0x13 slot +0x307; the Variable modes (15, Survival kind 5, Free Battle kind 3) slot 0 if player 1 is present, else slot 1, for every fighter (tag: one player plays whoever is in); otherwise slot 0 for player number 0 and slot 1 for any other.

Pause: 0x08828AF4 (a0 = the battle, a1 = a fighter on screen, called for each) pauses when Start is newly pressed on the slot in the fighter's +775; the pausing player is 1 << +775 in a0 + 0x59. A tagged-out fighter is not checked. A computer fighter's slot (its player number) also pauses, so an unused pad can pause.
