# Who plays which fighter

How SFA3R decides whether a fighter is played by a pad or by the computer, mode by mode, and what is
unchanged from the PSP game. Code: `src/runtime/hooks.c` (`apply_local_players`), with
`src/runtime/playercfg.c` for the buttons. Decision 19.

## The rule (offline modes)

1. **Whoever started the mode, or pressed Start to join, plays their own fighter**, on the side the
   game gives them: the side comes from who pressed Start (P1 on the left, P2 on the right), exactly
   as on the PSP. Joining is the game's own (Arcade's HERE COMES A NEW CHALLENGER, Dramatic Battle's
   partner on the select screen); a Start pressed during a fight is the pause, never a join.
2. **Every other fighter, in the game's order, goes to the next pad that did not start** (pads 1, 2,
   3 in turn, skipping the starters).
3. That pad plays it if its **JOIN-IN** (OPTION MODE > EX. SETTING) is **HUMAN** and the pad is plugged
   in (pad 1 also has the keyboard). If its JOIN-IN is **CPU** (the default for all three), the fighter
   stays exactly as the game has it: the computer.

So with **all three JOIN-IN on CPU, nothing is changed**: every mode plays as on a PSP (see the check
below). JOIN-IN only matters for a pad that did not start the mode. EX. SETTING's DEFAULT puts all
three back to CPU and COUCH CO-OP to ON.

The game keeps each fighter's player number at +2 (which pad slot and button config it reads). A
side can have more fighters than players (R.Dramatic Battle's two opponents and Dramatic Battle's boss
all say player 1), so a fighter given to another pad gets that pad's number for as long as it is
given, and its new presses come from that pad (playercfg.c); it gets its own number back when the
pad lets it go.

## Mode by mode

What a mode's fighters are (fighters in the game's order) and where they go with JOIN-IN HUMAN. "Free
pads" are the pads that did not start, in order (P1 started: pads 2 then 3; P2 started: pads 1
then 3).

| Mode | Fighters (game's order) | JOIN-IN HUMAN gives |
|---|---|---|
| Arcade | starter, opponent | the opponent to the first free pad. A pad can still join the game's own way (Start: new challenger) |
| World Tour | starter, opponent | the opponent to the first free pad |
| VS 100 Kumite, Final Battle | starter, opponent | the opponent to the first free pad |
| Survival (Arcade / Original / Boss / Infinity) | starter, opponent | the opponent to the first free pad |
| Dramatic Battle | starter, partner, boss | the partner to the first free pad, the boss to the second. Player 2 can also join the game's own way (Start on the select screen; until then an empty box says JOIN): it is drawn on its own side, its box says WAIT until the starter has chosen, then **player 2 picks the partner** on pad 2 (the cursors take turns as on the PSP) |
| Survival Dramatic, Free Battle Dramatic | starter, partner, boss | the partner to the first free pad, the boss to the second. A box that can be joined says JOIN (blinking); Start joins it: **the partner's box** goes to the first other pad to press Start (pad 1, 2 or 3), and in Free Battle **the opponent's box (ENEMY)** is pad 3's. A joined box says WAIT until its turn, then that pad picks its character and plays it. A computer partner is picked by the starter. ISM and speed are the starter's |
| R.Dramatic Battle, Survival R.Dramatic | starter, two opponents | the two opponents to the free pads, in order. R.Dramatic Battle (MODE MENU): only the starter plays unless JOIN-IN; another pad's Start does not join (the game made it the opponent) but pauses the fight |
| Free Battle R.Dramatic | starter, two opponents | the two opponents to the free pads, in order. The opponents' boxes say JOIN: Start joins the one labelled for that pad (pad 3: the 3P box; pad 1 or 2: the other), WAIT until its turn, then that pad picks and plays it |
| Variable Battle | starter, tag partner, opponents | the partner to the first free pad, the next opponent to the second. As Dramatic Battle: player 2 joins with Start (JOIN on an empty box until then), WAIT until its turn, then picks the partner on pad 2; drawn on its own side |
| Survival Variable | starter, tag partner, opponents | the partner to the first free pad, the next opponent to the second. As Survival Dramatic: JOIN / WAIT on the 2P box, the first other pad to press Start picks and plays the partner |
| Free Battle Variable | starter, tag partner, opponent | the partner to the first free pad, the opponent to the second. As Free Battle Dramatic: JOIN on the partner's box (pad 1 or 2) and the ENEMY box (pad 3), WAIT until its turn |
| Free Battle Arcade, Team | starter, opponent (Team: the opponent team) | the opponent to the first free pad. The opponent's box (Team: its team-size icons, under an empty box of ours) says JOIN: the first other pad to press Start picks the opponent (Team: its team size and characters) on its turn and plays it |
| Training | player, dummy | nothing changes: the dummy is TRAINING's own (its options decide how it moves) |
| Edit, Entry | (menus; Edit's test fight: player, opponent) | the opponent to the first free pad |
| Network modes | (the game's network players) | not used: with COUCH CO-OP on, every plugged-in pad is a human player, otherwise the computer (decision 16) |

Examples:
- P1 starts R.Dramatic Battle, 2P and 3P JOIN-IN HUMAN: the two on the other side are pads 2 and 3.
- P2 starts R.Dramatic Battle, 1P CPU, 3P HUMAN: P2 alone; of the other two, pad 1's is the computer,
  pad 3's is pad 3.
- P2 starts Arcade, 1P HUMAN: the opponent is pad 1.
- P1 starts Dramatic Battle, 2P CPU, 3P HUMAN: the partner is the computer, the boss is pad 3.

## What else SFA3R changes in these modes (whatever JOIN-IN says)

- Pads 2 and 3 feed the game's player 2 and 3 controller slots (the PSP had one pad), so a second
  pad can press Start to join wherever the game lets player 2 join.
- A player who joins late on a select screen can move their cursor (the PSP never had a second
  player there), except in Dramatic and Variable Battle where the cursors take turns.
- Arcade with two humans: both keep the character they chose (the PSP took the ladder's).
- L2 / R2 are extra buttons (PLAYER_CONFIG.md); START keeps pausing.
- Survival, Free Battle and Dramatic Battle: a human partner (or Free Battle opponent) picks their own
  character; boxes say JOIN / WAIT (decision 20; on the PSP the starter picked every box).

## The check: unchanged with JOIN-IN on CPU

23 scripted runs, one per mode and sub-mode (pad 1 boots, chooses the mode, then presses Cross every
100 frames), give every fighter the same player number and computer flag as the game without the player
rules, and the same screen. With all three JOIN-IN on HUMAN the same runs give the non-starting fighters
to pads 2 and 3 in order, as the table above says, and leave Training's dummy alone.

Not covered: what happens after a match where a pad played an opponent (results, continue, the next
fight), and modes reached through a menu choice other than the first.
