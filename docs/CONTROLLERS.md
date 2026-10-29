# Controllers

SFA3MR reads the keyboard and up to four gamepads through SDL3. Any controller SDL3 knows as a
gamepad should work: Xbox / XInput pads, DualSense and DualShock 4, Switch Pro, and the many others in
SDL's controller database. So far it has been tested with four DualSense pads on Windows 11; reports
for other controllers are welcome.

## Who is which player

| Device | Player |
|---|---|
| Keyboard | 1 |
| Gamepad 1 | 1 (together with the keyboard) |
| Gamepad 2 | 2 |
| Gamepad 3 | 3 |
| Gamepad 4 | 4 (see [below](#player-4)) |

Gamepads are numbered by the player number each controller reports (its XInput slot, or the lit
player LED), then by when it was connected, with no gaps. Unplugging a pad moves the ones after it up:
take out gamepad 1 and gamepad 2 becomes player 1.

Pads can be plugged in and out at any time. The MODE MENU's bar shows an icon for each gamepad that is
plugged in, with the player it is: **1P** red and **2P** blue on the left, **3P** green and **4P**
yellow on the right.

## Buttons

| PSP | Keyboard | Gamepad | Xbox | PlayStation |
|---|---|---|---|---|
| D-pad | Arrow keys | D-pad or left stick | D-pad / left stick | D-pad / left stick |
| Cross | Z | South | A | Cross |
| Circle | X | East | B | Circle |
| Square | A | West | X | Square |
| Triangle | S | North | Y | Triangle |
| L / R | Q / W | Shoulders | LB / RB | L1 / R1 |
| L2 / R2 (SFA3MR's own) | E / R | Triggers | LT / RT | L2 / R2 |
| Start | Enter | Start | Menu | Options |
| Select | Backspace | Back | View | Create |

The left stick and the triggers count as pressed past about half way. What each button does in a
fight is set per player in OPTION MODE > PLAYER CONFIG ([PLAYER_CONFIG.md](PLAYER_CONFIG.md)), and from
the pause menu's BUTTON CONFIG.

Keyboard only: **F2** goes back to the MODE MENU at once without saving, **F11** or **Alt+Enter**
switches full screen, **Esc** quits. No gamepad button does these.

## What a plugged-in pad does

- **Offline modes:** whoever starts a mode plays it. Gamepads 2 and 3 come in by pressing Start where a
  select screen shows JOIN, or through OPTION MODE > EX. SETTING > JOIN-IN (HUMAN). Mode by mode:
  [PLAYERS.md](PLAYERS.md).
- **Network modes on one PC** (COUCH CO-OP): gamepads 2 and 3 are PLAYER2 and PLAYER3; a pad that is
  not plugged in is played by the computer (CPU2 / CPU3).
- **Pause:** a player who plays a fighter pauses with their own Start, and the pause menu's BUTTON
  CONFIG then edits that player's buttons.

## Player 4

The game has three player slots, so gamepad 4 does not play yet: it shows its 4P icon, and its buttons
can be set in OPTION MODE > PLAYER CONFIG > P4 CONFIG, ready for a 2v2 / X.Dramatic mode. It cannot
pause a fight ([KNOWN_ISSUES.md](KNOWN_ISSUES.md), E7).

## Tests

`--p2` / `--p3 human|cpu|auto` treat gamepad 2 / 3 as plugged in or not, and `--pads 1234` sets which
pad icons the MODE MENU shows, for headless runs without controllers.
