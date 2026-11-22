# Research notes (branch `research/vs-battle-and-saves`)

Findings from mapping the game for multiplayer, and the first multiplayer code. Nothing here changes the normal build: remote-player battles are behind `-DCH_REMOTE_BATTLE=ON`, which is off by default.

| Doc | What |
|---|---|
| [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) | Campaign memory: units, bases, OKEs, cards, end-of-turn routine, battles |
| [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md) | VS mode, the shared battle engine, memory card and save format, and remote-player battles (`src/remote_battle.c`) |
| [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) | The plan for online campaign versus, live VS and card sharing |

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

## Tools (`tools/research/`)
Python helpers used for the mapping. Set `CH_REPO` to the repo (default: two folders up) and `CH_OUT` for screenshots (default `build/research`).
- `ch.py`: drive the game over the debug server (press buttons, screenshot, read/write RAM, dump RAM). Hold buttons 6-10 frames. `click(x, y)` puts the menu pointer at a screen position and presses X, which is much faster than steering it.
- `turns.py N`: end N turns and print the unit table. `to_turn6.py`, `battle_test.py`: the remote-battle test steps above.
- `xref.py LO HI`: code that touches an address range. `fx.py calls|dis ADDR`: a function's calls or disassembly. `cg.py ADDR...`: calls and callers. `ptr.py ADDR...`: pointers to an address.
- `iso.py BIN`, `ext.py BIN LBA SIZE OUT`: list and extract disc files. `un7z.py`: unpack the disc archive without 7-Zip. `diff.py`, `mdis.py`: RAM diff and MIPS disassembly helpers.
- Note: `fx.py` prints `addiu` offsets raw: `lui 0x800C` + `addiu 0xB238` means `0x800BB238`.

Debug server notes: savestate slots are 0-11; savestates don't load after a rebuild that changes code; `write_bulk` writes RAM; `wtrace_range` + `wtrace_dump` show who writes an address; `cdrom_sector_history` shows recent disc reads.
