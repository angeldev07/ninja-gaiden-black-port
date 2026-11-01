---
tags: [ngb, tools, achievements]
updated: 2026-10-31
status: current
---

# Remote session: driving a game that is already running

**What it is:** `ngb-loader --remote` opens a local named pipe, `\\.\pipe\ngb-loader`, which takes one command per
line. A client then works on the game the player has in front of them: presses, the achievement trace, infinite
health, captures and the game state. It exists for measurements that need a scene nobody can reach from a save, such as
a fight with enemies around Ryu. Every save sits at a save statue with no enemy in sight, so [[tools]] §
`ngb_tools.checkpoint` cannot load one.

## How to use it

1. Add `--remote` to the session. From the launcher, put it in `build\port\ngb.ini`, which the launcher reads and
   never rewrites:
   ```ini
   [advanced]
   extra_args=--remote
   ```
2. Play to the scene, then leave the pad alone.
3. From a terminal:
   ```sh
   python -I -m ngb_tools.remote status
   python -I -m ngb_tools.remote "god on" "trace on"
   python -I -m ngb_tools.remote "input t=0p:LS_LEFT@60p,t=80p:A,t=95p:Y" status shot
   ```

| Command | What it does |
| --- | --- |
| `input <script>` | Presses for the game now, in the `--input-script` syntax, with times counted from now. They add up with the player's pad. |
| `trace on\|off` | `--ach-trace`: events for the pad (what the game read), the action byte, defeats, hits and ki. It also writes `runs\ach-trace-player-NNN.bin`: per game frame, the poll, play time, defeats, ki and pad, then player 1's object. A new file starts every 20,000 frames (about 60 MB) and at every `trace on`. It also takes one capture the first time each action id shows. |
| `events` | The trace events not read yet, oldest first, as many as fit in one line: `N left: @poll pad X+A \| @poll state 0x56 \| …`. The log itself is locked while the game runs. |
| `god on\|off` | Keeps player 1's health at its max (below). |
| `shot` | Captures the next frame to `runs\shots`, as F12 does. |
| `status` | Polls, mode, chapter, difficulty, play time, health, ki, action, melee weapon, defeats, and the trace and god switches. |

## Infinite health and the game's anti-tamper check

- The game keeps 24 sums of its own variables: inventory, defeats, karma, essence, scarabs, health, ki and so on
  (`FUN_00164e70`). It checks them now and then (`FUN_00164db0`). A sum that changed without its "changed on purpose"
  flag sends the player back to the main menu.
- The flags are 24 bytes at `0x0081CCB4` (PAL). The port finds them in the routine that hands Golden Scarabs to
  Muramasa (FUN_00187400), which sets group 7's.
- The first `god on` wrote the health alone and the game went back to the main menu. It now does what the game does
  when an item revives Ryu (`FUN_002e3f60`):
  - set the flags of groups 0xF (health) and 0x11 (the copy in the first health segment);
  - then write both values.
- `VERIFIED` on 2026-10-31 (PAL, chapter 1 checkpoint):
  - health 370/1000 rose to 1000/1000;
  - the game stayed in play for minutes;
  - an injected `LS_LEFT` made Ryu run (action 0x09).
- Nothing in the game files changes; only the running game's memory.

## Gotchas

- One client at a time. The pipe rejects remote clients and keeps Windows' default security (the user who started the
  loader).
- Polls run at about 70 a second in gameplay, slower while loading. A script that must wait for the game should poll
  `status`.

## Code

`port/src/remote.c`, `ngb_input_inject` (`port/src/input/xapi_hle.c`, merging through `input_script_append`),
`ngb_ach_status` and `god_tick` in `port/src/ach/ach.c`, the flags in `port/src/ach/sig.c`, and the client
`src/ngb_tools/remote`.
