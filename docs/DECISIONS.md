# Decisions

| # | Decision | Why |
|---|---|---|
| 1 | **The PSP version (Street Fighter Alpha 3 MAX, US ULUS-10062) is the one recompiled.** | It has the full roster and every mode, Dramatic and Variable modes included. Other versions are references only (VERSION_DIFFERENCES.md). |
| 2 | **Local players come from feeding more controllers into the PSP game**, not from another version. | The missing second player is a hardware limit (one PSP, one pad); the game already runs several fighters and has versus logic behind its wireless layer. |
| 3 | **Western names and English throughout.** | The US disc already uses them; nothing to patch. |
| 4 | **Everything unlocked from the start**, by holding the game's own unlock flags on. | Every character and mode available without grinding. |
| 5 | **Target: Windows x64, Visual Studio 2026, CMake.** | |
| 6 | **Our own translator**, written for this game. | The program needs plain MIPS plus a few Allegrex extras, so a small translator is enough, and other recompilers' licences would bind the project. |
| 7 | **Widescreen is the SFA3MAX 16:9 patch (Rickow) as a setting.** `prepare.py` fetches it and records what it changes; the translation keeps both meanings and SCREEN SIZE > WIDE FORCED picks one at run time. | Proven in PPSSPP; no separate build; the patch itself is never committed. |
| 8 | **Notes on the other versions' differences are kept** (VERSION_DIFFERENCES.md). | For possible later play modes (other versions' rules). |
| 9 | **Licence: GPL-3.0-or-later.** | Allows reusing GPL code such as PPSSPP's (GPLv2 or later); copied code keeps its notices. |
| 10 | **Branches: `main` plus `feature/`, `fix/`, `docs/` or `chore/` + a short name**, merged by pull request after the maintainer's review ([CONTRIBUTING.md](../CONTRIBUTING.md)). | |
| 11 | **Graphics: a software renderer as reference, a GPU renderer (OpenGL 3.3 core) for play.** | The software renderer was quick to get right and checks the GPU one; OpenGL runs on any Windows GPU and on Linux. |
| 12 | **SDL3** (fetched by CMake) for window, input and sound. | One dependency for all three; keeps Linux / Steam Deck possible. |
| 13 | **PPSSPP's ATRAC3+ decoder vendored unchanged; SAS written in C** following PPSSPP. | The decoder is self-contained; PPSSPP's SAS is tied to its own memory and save-state layers. |
| 14 | **Local versus uses the game's own network modes, with the other PSPs simulated in the same process** ("virtual players", vpeer.c). | The network battles are lockstep: only buttons travel, so one process can answer the traffic, and every wireless mode works unchanged with the real select screens. |
| 15 | **One MODE MENU, the game's own.** Local versus is entered through NETWORK, with player 1 the host. A native menu layer may come later. | A runtime-drawn second menu page was tried and dropped. |
| 16 | **Network modes: pads decide who plays.** Gamepad 2 / 3 plugged in = PLAYER2 / PLAYER3; otherwise that player is the computer (CPU2 / CPU3). | Gives two humans against a CPU in Dramatic Battle, and 1P vs CPU in every network mode. |
| 17 | **sceFont is served by the runtime** with its own pixel font. | No PSP system font is available; the network screens' names need one. |
| 18 | **SFA3R's own screens follow the game's option screens:** names in font 1, yellow (0xB) on the cursor's row and cyan (0xA) elsewhere, values always yellow, the green bar under the cursor; Cross on EXIT confirms (sound 0x39), Circle goes back (0x2F), moving or changing a value 0x38. | They look and sound like the rest of the game. |
| 19 | **Who plays which fighter (offline):** whoever started the mode or pressed Start to join plays their own fighter; every other fighter, in order, goes to the next pad nobody started with, played by that pad when its JOIN-IN (EX. SETTING) is HUMAN, by the computer otherwise. JOIN-IN defaults to CPU. | One rule for every mode; the side you stand on comes from who pressed Start. hooks.c `apply_local_players`; PLAYERS.md. |
| 20 | **Everyone picks their own character.** Where a select screen has a box another player could take, it says **JOIN** (blinking) while a pad that could join is plugged in; Start joins it, the box says **WAIT** until its turn, and that pad picks and plays the character. Dramatic and Variable Battle keep the PSP's turn order with player 2's turn on pad 2. JOIN and WAIT use a small pixel font of our own. A computer partner is still picked by the starter. | On the PSP the starter picked every box. selectscreen.c; PLAYERS.md. |
