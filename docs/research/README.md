# Research notes (branch `research/vs-battle-and-saves`)

Findings from mapping the game for multiplayer, and the first multiplayer code. Nothing here changes the normal build: remote-player battles, hot-seat and turn files are behind `-DCH_REMOTE_BATTLE=ON`, which is off by default.

| Doc | What |
|---|---|
| [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) | Campaign memory: units, bases, OKEs, cards, end-of-turn routine, battles |
| [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md) | VS mode, the shared battle engine, memory card and save format, and remote-player battles (`src/remote_battle.c`) |
| [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) | The plan for online campaign versus, live VS and card sharing; the hot-seat test (`src/hotseat.c`); turn files (`src/turn_files.c`) |

## Reproducing the remote-player battle tests
1. Build with `-DCH_REMOTE_BATTLE=ON` and run headless from `build/` with the remote designs (extract `DATA/DEFRP.CRD` from the disc with `tools/research/iso.py` + `ext.py`):
   `CH_REMOTE_BATTLE=1 CH_REMOTE_DESIGNS=DEFRP.CRD ./CarnageHeart_Recompiled --headless --no-launcher --disc "../disc/Carnage Heart (USA).cue" --debug-port 4370`
   (On Linux with no X11/Wayland, also configure with `-DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_OFFSCREEN=ON`.)
2. `python3 tools/research/to_turn6.py` (its boot timing can drift and land in another scenario: check funds are 500000 at turn 1. By hand: Start to skip the intro, X on Start Game, X, X on Europa): boots, starts Europa (Practice), ends turns to turn 6 and saves savestate slot 10. A CPU unit is then next to the player's start base at (8,8).
3. Put a player unit on the map with `ch.py` (`write_bulk` at the unit table `0x80134B6C`, unit 0):
   - CPU attacks: `01 02 0800 0800 C800 C800 00 C8 0800 0900 0A00 1027 E803` (defend the base at (8,8), OKEs 8/9/10).
   - Player attacks: `01 03 0A00 0800 C900 C800 00 C9 0800 0900 0A00 1027 E803` (at (10,8), moving onto the CPU unit at (9,8)).
4. `python3 tools/research/battle_test.py`: ends the turn, saves screenshots to `build/research/`, prints units and OKEs. The game log shows `[remote-battle]` lines for the set-up and the result.

Savestates survive a rebuild only while the hook list and `SNAPSHOT_TAG` stay the same.

## Reproducing the hot-seat test
1. Same build. Run with `CH_HOTSEAT=1` and the output in a log: `CH_HOTSEAT=1 ./CarnageHeart_Recompiled --headless --no-launcher --disc "../disc/Carnage Heart (USA).cue" --debug-port 4370 > research/game.log 2>&1 &`
2. Start Europa by hand (Start, X on Start Game, X, X on Europa; check funds 500000 at turn 1). Save a savestate there if you like, but restart the game before loading it again: the hot-seat state is not in savestates.
3. `python3 tools/research/hotseat.py demo`: A ends its turn; B forms a unit and orders Capture on A's base; End Turn until B takes the base. The log shows `[hotseat]` lines for set-up, each swap and the end. `hotseat.py` also has `state()` (bases, OKEs, units) and `form_and_order()` for other set-ups (Defend = 1 gives a battle).

## Turn files: simulator test
`sh tools/research/turnsim/run.sh` (Linux/macOS, needs a C compiler and Python 3; no disc or build needed). It runs `src/turn_files.c`, `src/hotseat.c` and `src/remote_battle.c` on a fake RAM as two players sharing a folder, then checks the logs: a straight game, and one where B quits and catches up. It ends with "all checks passed".

## Reproducing the turn-file test (two copies, one PC)
1. Same build. Start two copies on one shared folder, each with its own debug port and log (the commands are at the top of `tools/research/turns_demo.py`): `CH_TURN_DIR=research/turns CH_TURN_ROLE=A ... --debug-port 4370 > research/A.log` and the same with `CH_TURN_ROLE=B`, port 4371, `research/B.log`. Empty `research/turns` first for a new game.
2. In both copies start Europa by hand (Start, X on Start Game, X, X on Europa; funds 500000) and stay on the Command menu with Tactics selected.
3. `python3 tools/research/turns_demo.py`: the hot-seat demo game across the two copies (B captures A's base). The logs show `[turns]` lines: sent, waiting, loaded, and what the opponent did.
4. By hand instead: end the turn on A, then on B (B's first End Turn only receives), and from then on take turns. A copy waiting for the other's file is paused; it carries on by itself when the file arrives. To test resuming, close one copy while it waits, start it again with the same switches, start Europa, and press End Turn.

## Tools (`tools/research/`)
Python helpers used for the mapping. Set `CH_REPO` to the repo (default: two folders up) and `CH_OUT` for screenshots (default `build/research`).
- `ch.py`: drive the game over the debug server (press buttons, screenshot, read/write RAM, dump RAM). Hold buttons 6-10 frames. `click(x, y)` puts the menu pointer at a screen position and presses X, which is much faster than steering it.
- `hotseat.py`: hot-seat test steps (see above). `turns_demo.py`: the two-copy turn-file test. `turnsim/`: the turn-file simulator.
- `ch.py` talks to port 4370; set `CH_PORT` (or `ch.PORT` in a script) for a second copy.
- `turns.py N`: end N turns and print the unit table. `to_turn6.py`, `battle_test.py`: the remote-battle test steps above.
- `xref.py LO HI`: code that touches an address range. `fx.py calls|dis ADDR`: a function's calls or disassembly. `cg.py ADDR...`: calls and callers. `ptr.py ADDR...`: pointers to an address.
- `iso.py BIN`, `ext.py BIN LBA SIZE OUT`: list and extract disc files. `un7z.py`: unpack the disc archive without 7-Zip. `diff.py`, `mdis.py`: RAM diff and MIPS disassembly helpers.
- Note: `fx.py` prints `addiu` offsets raw: `lui 0x800C` + `addiu 0xB238` means `0x800BB238`.

Debug server notes: savestate slots are 0-11; savestates don't load after a rebuild that changes code; `write_bulk` writes RAM; `wtrace_range` + `wtrace_dump` show who writes an address; `cdrom_sector_history` shows recent disc reads.
