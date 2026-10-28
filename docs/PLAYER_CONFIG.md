# PLAYER CONFIG

OPTION MODE > PLAYER CONFIG (the PSP's BUTTON CONFIG) sets what each button does in a fight, for each
player. Choose P1, P2, P3 or P4 CONFIG, move to a row with up / down and change it with left / right
(or Cross, which steps forward). DEFAULT puts that player back to the defaults; EXIT or Circle goes back
and saves.

All of it is kept in `work/sfa3r.cfg` ([SAVES.md](SAVES.md)), by name, so it can also be edited there.

## The buttons

| Row | Default | Keyboard (player 1) | Gamepad |
|---|---|---|---|
| Square | L PUNCH | A | West |
| Triangle | M PUNCH | S | North |
| L | H PUNCH | Q | Left shoulder |
| Cross | L KICK | Z | South |
| Circle | M KICK | X | East |
| R | H KICK | W | Right shoulder |
| SELECT | TAUNT | Backspace | Back |
| L2 | PPP | E | Left trigger |
| R2 | KKK | R | Right trigger |

A PSP has no L2 or R2; SFA3R adds them. Attacks are shown as the game's pictures (L / M / H with a fist
or a foot, Taunt, PPP / KKK) and the longer ones by name, as on the original BUTTON CONFIG.

The BUTTON CONFIG in the pause menu has the same rows, L2 and R2 included, for the player who paused
(players 1 - 3); what is set there is kept and saved like P1 - P4 CONFIG.

## What a button can do

Each of the nine buttons can be given any of these 55 settings. Left / right step through them in this
order and wrap round at either end.

### Attacks

| Setting | What the button does |
|---|---|
| L PUNCH, M PUNCH, H PUNCH | light, medium or heavy punch |
| L KICK, M KICK, H KICK | light, medium or heavy kick |
| PPP | all three punches at once |
| KKK | all three kicks at once |
| PP | light and medium punch together |
| KK | light and medium kick together |
| LP + LK | light punch and light kick together |
| MP + MK | medium punch and medium kick together |
| HP + HK | heavy punch and heavy kick together |
| NO USE | nothing: the button is switched off |
| TAUNT | the character's taunt (the game calls it CHOUHATU) |

The pairs and triples are a short cut: one button press is the same as pressing those buttons together,
which makes the moves that need several buttons at once (Variable Combos, Alpha Counters, rolls and so on)
easier to do.

### Special moves on one button

| Setting | What the button does |
|---|---|
| SPECIAL1 L / M / H ... SPECIAL8 L / M / H | the character's special move 1 - 8, done with a light, medium or heavy attack (24 settings) |
| S.COMBO1 LV1 / LV2 / LV3 ... S.COMBO5 LV1 / LV2 / LV3 | the character's super combo 1 - 5 at level 1, 2 or 3 (15 settings) |
| VARIABLE 1 | the V-ISM Variable Combo |

These are the PSP version's one-button moves: the button does the whole move without the joystick
motion. The numbers follow each character's own list of moves (the game's tables at 0x08A1B3F8 and
0x08A1B4F4), so SPECIAL2 is a different move for Ryu than for Chun-Li; for Ryu, SPECIAL1 L is a light
Hadoken and S.COMBO1 LV1 a level 1 Shinku Hadoken. A number past the end of a character's list does
nothing (the game skips it). Super combos use the super meter as usual.

## The other three rows

| Row | Choices | |
|---|---|---|
| COMMAND | ARCADE, NORMAL (default), LONG | how the game reads joystick motions for special moves |
| TYPE | NORMAL, EASY (default) | the game's control type |
| RAPID | OFF (default), ON | ON: holding a button repeats it (rapid fire) |

The game gives no further description of COMMAND and TYPE. Our reading of them, not yet confirmed: ARCADE
reads motions as strictly as the arcade game; NORMAL and LONG give more time to finish a motion.

## How L2 and R2 work

L2 and R2 are buttons of their own: each does whatever it is set to, any of the 55 settings, whatever the
other buttons are set to. The game knows only the PSP's buttons, so for the moment the fighter's buttons
are read, L2 (or R2) takes the place of START, which has no attack (`src/runtime/playercfg.c`); START
itself still pauses.

Holding L2 and R2 together: L2 does its move, and R2 adds its attack only when that is one of the
basic attacks (L PUNCH to TAUNT), not a special or super combo.
