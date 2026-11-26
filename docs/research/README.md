# Research notes (branch `research/vs-battle-and-saves`)

Findings from mapping the game for multiplayer, and the first multiplayer code. Nothing here changes the normal build: remote-player battles, hot-seat and turn files are behind `-DCH_REMOTE_BATTLE=ON`, which is off by default.

| Doc | What |
|---|---|
| [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) | Campaign memory: units, bases, OKEs, cards, end-of-turn routine, battles, negotiation and research |
| [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md) | VS mode, the shared battle engine, memory card and save format, and remote-player battles (`src/remote_battle.c`) |
| [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) | The plan for online campaign versus, live VS and card sharing; the hot-seat test (`src/hotseat.c`); turn files (`src/turn_files.c`) |
| [NEGOTIATION_GUIDE.md](NEGOTIATION_GUIDE.md) | Negotiation in plain words (the four choices, prices, End Turn), for the tutorials |

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
`sh tools/research/turnsim/run.sh` (Linux/macOS, needs a C compiler and Python 3; no disc or build needed). It runs `src/turn_files.c`, `src/hotseat.c` and `src/remote_battle.c` on a fake RAM as two players sharing a folder, then checks the logs: a straight game, and one where B quits and catches up. It also plays out the frame-by-frame wait (the hook "calling" VSync until the file arrives) and checks that End Turn gets its registers back afterwards. It ends with "all checks passed". It also checks negotiation (`src/negotiation_sync.c`): the CPU's negotiation never runs, each player keeps their own blueprints, and an R&D bonus goes to the player who paid.

## Negotiation online: two real copies
`python3 tools/research/negotiation_demo.py` (build with `-DCH_REMOTE_BATTLE=ON`, disc in `disc/`, Node.js for the local relay): A buys a blueprint and B pays towards one through the Negotiation screens; checks each copy sees the other's as Drakken's, the worlds match, research moves on once a round, no CPU spending, and B's restart. Output in `build/research/negotiation/`.

## Reproducing the turn-file test (two copies, one PC)
Tested in the real game on 2026-11-23 (Linux, headless): a capture game (B takes A's base: defeat screen on A, victory on B, see `docs/screenshots/turn-files-two-copies-game-over.png`), a battle game (A's defender beats B's attacker on A's device; B gets the result and both copies end in the same state) and a restart (B closed and started again mid-game, caught up with one End Turn). No desyncs.
1. Same build. Start two copies on one shared folder, each with its own memory cards, debug port and log, a few seconds apart (the commands are at the top of `tools/research/turns_demo.py`). In a Release build add `PSX_DEBUG_SERVER=1`, or the debug server is off. Use a new, empty `research/turns` folder for a new game.
2. In both copies start Europa (`python3 tools/research/boot_europa.py 4370` and `4371`, then Circle once; or by hand: Start, X on Start Game, X, X on Europa; funds 500000) and stay on the Command menu with Tactics selected.
3. `python3 tools/research/turns_demo.py`: the hot-seat demo game across the two copies (B captures A's base). The logs show `[turns]` lines: sent, waiting, loaded, and what the opponent did.
4. By hand instead: end the turn on A, then on B (B's first End Turn only receives), and from then on take turns. A copy waiting for the other's file keeps running and shows "Waiting for player X's turn" at the bottom of the window; it carries on by itself when the file arrives. To test resuming, close one copy, start it again with the same switches, start Europa, and press End Turn.

## Online test through a relay (two copies, restarts)
Tested 2026-11-24: `python3 tools/research/online_demo.py` (same build; needs Node and curl). It starts the relay locally (or uses `CH_RELAY_URL`), makes a game, starts both copies headless with separate folders, and checks: both map starts; B closed at the start of its turn and started again (the map catches up without an End Turn, and every table is byte-for-byte what it was); the same with an empty folder (a new computer); B closed right after sending (its own turn comes back and the next End Turn waits); then plays to the end. Logs and screenshots in `build/research/online/`.

## Launcher pages without a screen
`sh tools/research/launcher_shots/run.sh` builds `launcher/ch_menu.inc` (with the online pages) against a stand-in launcher, draws the pages in software to PNG files and drives the ONLINE CAMPAIGN pages with key presses against a local relay: create, PLAY, join by typing a code, the main page's YOUR TURN. Needs only a C++ compiler, Node and curl (no disc, no game build).

Building in a fresh sandbox: the game code is not in the repo, so generate it from the disc first (`python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc "disc/Carnage Heart (USA).cue"`, about a minute, then the build takes about 5 minutes), and fetch the nested submodules (`git submodule update --init --recursive` inside `psxrecomp`). Boot timing: in `boot_europa.py` the third X picks Europa (Practice) straight from the scenario list; to start another scenario, press X twice, wait about 240 frames for the list, then move down. Savestates taken on the scenario list did not load back in testing.

Building on Linux without internet access to codeload.github.com: clone SDL3 (`release-3.4.10`), zlib (`v1.3.1`) and libwebp (the commit in `psxrecomp/third_party/deps.manifest`) and point the environment variables `PSX_SDL3_SOURCE_DIR`, `PSX_ZLIB_SOURCE_DIR` and `PSX_LIBWEBP_SOURCE_DIR` at them (they are read from the environment, not as `-D` options). If the first configure stops on a missing `SDL_revision.h`, run the same configure again. To build the launcher (`-DPSX_RECOMP_UI=ON`) on a machine with no GL headers, `-DOPENGL_INCLUDE_DIR=/usr/include -DOPENGL_gl_LIBRARY=<a libGL.so link> -DOPENGL_glx_LIBRARY=<libGLX.so.0>` is enough (SDL brings its own GL declarations). If the link fails on `gl*` functions and only `libGL.so.1` is installed, add `-DCMAKE_CXX_STANDARD_LIBRARIES="-L<dir with a libGL.so link> -lGL"`.

## Tools (`tools/research/`)
Python helpers used for the mapping. Set `CH_REPO` to the repo (default: two folders up) and `CH_OUT` for screenshots (default `build/research`).
- `ch.py`: drive the game over the debug server (press buttons, screenshot, read/write RAM, dump RAM). Hold buttons 6-10 frames. `click(x, y)` puts the menu pointer at a screen position and presses X, which is much faster than steering it.
- `hotseat.py`: hot-seat test steps (see above). `turns_demo.py`: the two-copy turn-file test. `online_demo.py`: the online test through a relay with restarts. `launcher_shots/`: launcher pages drawn without a screen. `boot_europa.py PORT`: boot a copy to Europa turn 1. `turnsim/`: the turn-file simulator.
- `ch.py` talks to port 4370; set `CH_PORT` (or `ch.PORT` in a script) for a second copy.
- `turns.py N`: end N turns and print the unit table. `to_turn6.py`, `battle_test.py`: the remote-battle test steps above.
- `xref.py LO HI`: code that touches an address range. `fx.py calls|dis ADDR`: a function's calls or disassembly. `cg.py ADDR...`: calls and callers. `ptr.py ADDR...`: pointers to an address.
- `iso.py BIN`, `ext.py BIN LBA SIZE OUT`: list and extract disc files. `un7z.py ARCHIVE OUTDIR`: unpack the disc archive without 7-Zip (also archives with a compressed header). `diff.py`, `mdis.py`: RAM diff and MIPS disassembly helpers.
- Note: `fx.py` prints `addiu` offsets raw: `lui 0x800C` + `addiu 0xB238` means `0x800BB238`.

Debug server notes: savestate slots are 0-11; savestates don't load after a rebuild that changes code; `write_bulk` writes RAM; `wtrace_range` + `wtrace_dump` show who writes an address; `cdrom_sector_history` shows recent disc reads.
