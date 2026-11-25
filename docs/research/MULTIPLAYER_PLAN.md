# Carnage Heart ReComp: Multiplayer Plan

Researched 2026-11-21, updated 2026-11-24 (session 6: replay of the opponent's turn; Android Online screen and notifications; launcher shows the scenario; launcher Online screen, catching up when the map opens), 2026-11-23 after the first real two-copy test, and 2026-11-22 after the campaign RAM mapping, the VS/saves mapping, the hot-seat test and the first turn files (see [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) and [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md)).

**Priority (decided 2026-11-21): campaign versus (Option C) comes first.** VS mode (Options A and B) is less important and comes later.

## What the original game has
- **No live two-player play.** The box's "VS. Mode" puts already-designed OKEs from campaign saves on memory cards into a battle against each other. Players only watch.
- A card = a hardware profile (body/parts) + a software profile (chip program).
- The campaign is one player against the CPU only.

## Option C: Campaign versus, async turns (top priority)

**Decided 2026-11-21:**
- The turn file holds **committed orders, not button presses**: unit tasks and targets, units formed, what was built or produced in the factory, deployments.
- **The player who starts a battle watches it play out on their own device and reports the result.** The result goes in their turn file.

### What the mapping showed (2026-11-22)
- **Orders are tiny.** A unit's order is just its task byte (+01: 2 defend, 3 attack/capture) and a target hex (+06). Everything else (pathing, moving one hex a turn) the game works out itself.
- **Players can re-order units already on the move.** So a turn's orders include changes to existing units, not only new ones.
- **Battles happen during End Turn**, not while giving orders. When you end your turn, your units move, and any unit that walks onto an enemy **unit** fights there. (A unit walking onto an undefended base just captures it and dismantles the OKEs stored there; no battle.)
- **A battle result is 6 small records**: HP (plus 2 consumables and kills) of the 6 OKEs. The game's own code then removes destroyed OKEs, deletes wiped-out units, sends a losing attacker back a hex, and changes base ownership.
- **The CPU's brain is one function, `0x8001C268`**, called once per End Turn between the player's units and the CPU's units. The CPU itself only gives orders to units it forms that turn, but the replacement must handle more (see step 2).
- **Sides are fixed by slot**: units 0-31 are the player's, 32-63 the CPU's. Units, bases and OKEs each carry an owner byte (1 player, 2 CPU).

### How it would work
1. **My turn**: I give orders (new units and changes to units already moving) and end the turn. On my device only my half of End Turn runs (my units move and fight; I watch my battles). The mod skips the CPU part. The turn file = my orders + the results of my battles + the end-of-turn state.
2. **Their device**: the mod replaces `0x8001C268` with "apply the remote player's orders": units formed, **new task/target for any existing unit that was re-ordered**, production. Because this runs before the CPU units move, a re-ordered unit turns toward its new target in that same enemy phase, just as it did on the sender's device. The game's own enemy phase then moves those units on screen.
3. **Battles in the replay are not fought again**: the mod replaces the battle call `0x80035CD4` with "write the reported HP/kills into the 6 OKE records". The game's own code does the rest (removals, retreat, base capture), so both devices end in the same state.
4. **Side swap**: each player always plays "as the player" (slots 0-31, owner 1). The turn file is stored from the sender's point of view; the receiver swaps owner bytes 1<->2 and unit halves 0-31 <-> 32-63 when loading it.
5. Then they take their turn, and so on.

**Capturing orders**: the simplest reliable way is to compare each of my units' task/target at End Turn with what they were at the start of my turn. Any difference is an order. This catches re-orders however they were given, without hooking every order screen.

### What the order and factory mapping showed (2026-11-22, step 3)
Details in CAMPAIGN_RAM_MAP.md ("Order screens" and "Production line").
- **Every order is 1-3 unit fields**: task (+01: 0 standby, 1 move, 2 defend, 3 capture, 4 patrol), target cell (+06) and, for Defend, home cell (+08). Deploy and Unit Command use the same five handlers, so re-ordering a moving unit is no different from a first order.
- **Re-ordering does reset the path state** (+0A/+0B), through `0x80034C38(unit)`. The replay should write the fields and call `0x80034C38`, the same as the game does (or copy +0A/+0B).
- **Forming a unit** spends no funds; it moves OKEs from a base into a free unit slot and takes ammo/fuel from that base's stock. So a formed unit = a new unit record + those OKE records (+01 = 1) + the base's new stock.
- **Factory orders** are the line records in the base (product, amount), plus Add Line (base +58) and Upgrade (line level/capacity), which cost funds immediately. Production itself (spending funds, finishing OKEs and ammo) happens in End Turn (`0x800566F4`).
- **Consequence for the turn file**: diffing at End Turn gives the orders; for formed units and the factory it is simpler and safer to send the resulting records (the unit, its OKEs, the base record and funds) than to replay the screens. On the receiver's device the remote side's production must not be run by the CPU factory code (`0x80055B00`); either run the player's production routine for that side or apply the sender's post-production base records and new OKEs.

### Custom programs on the CPU side: answered (2026-11-22)
**Campaign battles can be fought as VS-mode battles** (the project owner's idea). Tested at Europa turn 6 → 7 with an experiment build: a CPU unit made of three MACHINE02 player designs fought the player's unit with their own chip programs. Afterwards the game's own code removed the dead OKEs, deleted the beaten unit and kept the base. Details and the exact recipe are in VS_MODE_AND_SAVES.md.

What was found:
- VS mode and the campaign share **one battle engine** (`0x8001CF48`) in one executable (no overlays), so there is no mode switch or RAM save/restore to do.
- The engine treats side 2 as CPU because of the **battle mode flag** (`0x800DF3D0`: 1 campaign, 2 VS) and the **OKE owner byte**. Setting side 2's OKEs to owner 1 with a player-card index, and the mode to 2 for the whole battle, makes it a VS-style battle.
- Map damage carries over: the engine takes each OKE's starting HP from the OKE table.
- The result still lands in the 6 OKE records, so the "report the result" plan is unchanged.

**Built (2026-11-22): `src/remote_battle.c`** (`-DCH_REMOTE_BATTLE=ON`). It borrows spare card slots for the remote designs during the battle, runs it in VS mode, then puts the remote OKEs back on side 2 and restores the cards. Tested both ways round (CPU side attacking, player attacking), including remote survivors. Next: drive it from the turn file instead of test switches.

### Hot-seat: built and tested (2026-11-22, step 4)
**`src/hotseat.c`** (same `-DCH_REMOTE_BATTLE=ON` build; run with `CH_HOTSEAT=1`). Two people share one copy and take turns; each always plays "as the player", and the sides are swapped in RAM at the end of every turn, so the game's own screens, movement, battles and win/lose code serve whoever's turn it is.

How End Turn runs with hot-seat on:
1. The current side's units move and fight (the game's own loop). Battles against the other side's units are fought with that side's own designs (`remote_battle.c` borrows them from the waiting side's card table).
2. The CPU brain `0x8001C268` is skipped, and the waiting side's units are hidden from the CPU-unit loop (owner byte set to 0 for the loop) so they do not move on the other side's turn.
3. The rest of End Turn runs as usual. It is written for "player" and "CPU" and would also touch the waiting side with CPU rules: base repair (`0x8007654C`) caps HP at the CPU card's value, and research/negotiation (`0x8004BBB8`) spends the CPU funds. So the waiting side's units, bases, OKEs and funds are **snapshotted when the brain would run and put back just before the swap**. Each side gets base fuel, repair, resupply and production once a round, on its own turn.
4. **Swap** at `0x8008A2B8` (called only from End Turn, after the bases are counted, before the win/lose check): units i <-> i+32, owner bytes 1 <-> 2 in units, bases and OKEs, funds (gp+52 <-> gp+56), the player card table (the waiting side's designs are kept in the module) and the selected base. The turn number goes up once a round. If either side has no bases left there is no swap, so the win or lose screens are shown to the side that just played.

Set-up (first End Turn): side B gets a copy of side A's card table, the CPU's OKEs become side-B OKEs built from the stock designs MACHINE01-03 (cards 25-27, fresh HP and ammo), side B's funds are set to side A's, and the CPU bases' production lines are cleared.

Tested at Europa (Practice), all orders given through the game's own screens:
- Swap: B gets its bases, its 8 OKEs at full HP (520), 500000 funds and the camera on its base. B formed a unit (Form Unit, Auto) and ordered Capture on A's base; it moved one hex per B turn and stood still on A's turns.
- Battle: A formed a unit and ordered Defend. On A's turn 4 A's defender went out to meet B's attacker; the battle was fought with B's designs on side 2. B won 3 kills to 0 (one B OKE lost, two left at 179 and 276 HP). The game removed A's unit and B's dead OKE; B's unit marched on.
- End: in a second game B walked onto A's undefended base, A's stored OKEs were dismantled, all bases went to B and the victory screen played for B.

Found on the way:
- **Skipping a function**: calls are continuation-passing. A hook that does not run the original must still "return" by setting `cpu->pc = cpu->gpr[31]`; without it the game exits (PC = 0).
- The normal End Turn finishes right after the base count (the code after it is the map-won path: `0x80036C70` sets up the next map).
- **Known gaps** (fine for a local test; the turn files will replace this): research/negotiation still runs for the CPU every End Turn (only the waiting side's funds are protected); kills and losses of the waiting side's OKEs are added to the CPU card records; the hot-seat state lives in the module, so savestates and save files do not carry it (restart the game before loading a savestate from before set-up).

What this means for step 5: the turn file can be written at the swap point. Everything the swap moves (units, bases, OKEs, funds, designs) is exactly what the other device needs, and the snapshot/restore shows which parts of End Turn belong to the side that just played.

### Turn files: built, tested in the real game (2026-11-22/23, step 5)
**`src/turn_files.c`** (same `-DCH_REMOTE_BATTLE=ON` build; run with `CH_TURN_DIR=<shared folder>` and `CH_TURN_ROLE=A` or `B`). Two copies of the game play one campaign through a shared folder. It reuses all of hot-seat; only the swap point is different.

**One change from the plan above: the turn file carries the state, and the receiver loads it instead of replaying the turn.** Replaying orders through the CPU brain (steps 2-3 of "How it would work") only gives the same result if both copies run End Turn identically, and End Turn does things for both sides with CPU rules (repair cap, research spending, production) that the hot-seat test had to undo by hand. Taking the sender's end state cannot drift, and a battle is still fought only once, on the device of the player who started it, who watches it. The orders and battle results are in the file too, so a visual replay ("watch what the enemy did") can be added later on top without risking the result.

How End Turn runs on each copy:
1. My half, exactly as in hot-seat: my units move and fight (with the opponent's real designs), the CPU brain is skipped, the other side's records are snapshotted and put back, my production, repair and resupply run.
2. At the swap point (`0x8008A2B8`): **send** the state after my half, already turned round to the opponent's point of view (units i <-> i+32, owner bytes 1 <-> 2, funds swapped), plus my design cards, the orders I gave (found by comparing my units with how they were at the start of my turn) and my battle results.
3. **Wait** for the opponent's next file (the game keeps running meanwhile; see "Waiting without freezing" below), then **load** it: its tables go straight into RAM, its cards become the designs I fight next turn, and the turn counters are set. End Turn then goes on to the win/lose check as usual.

Details:
- **Files** are `turn-000001.cht`, `turn-000002.cht`... A writes the odd ones, B the even ones. About 37 KB each, mostly the 28 design cards. Written to a `.part` file and renamed, so a half-written file is never read. Layout at the top of `src/turn_files.c`.
- **Turn numbers**: A's turn n and B's turn n are one round; both copies show the same number.
- **Chain check**: each file records which state it was played from (a hash of the previous file's tables). If it is not the state the receiver sent, the log says DESYNC and the file is loaded anyway, which puts both copies back in step.
- **Starting and resuming**: the first End Turn after the game starts only receives (my units are held still and nothing is sent), unless I am A and the folder is empty, which starts a new game. So B begins by pressing End Turn once, and either player can close the game, start Europa again (loading their own save brings their designs back) and press End Turn to catch up.
- **Game over**: when a side has no bases left, the last file is sent with a game-over flag; the receiver loads it and the game shows its win or lose screen.
- **What the receiver sees**: ~~the enemy's units jump to their new places~~ since step 7 (below) the enemy's moves and battles are replayed on screen; the log also lists what the enemy did ("their unit 32: formed, capture a base", "battle at (x,y): their unit 33 attacked your unit 2. Theirs: 420HP... Yours: ...").

Tested with `tools/research/turnsim/run.sh`: the real `turn_files.c`, `hotseat.c` and `remote_battle.c` running in two processes on a fake RAM, with a stand-in End Turn that calls the hooks in the game's order. Checked over 8 rounds: each copy sees the other side exactly as its owner left it (units, OKE HP, funds, bases), turn numbers match, the CPU brain never runs, units only move on their owner's turn, a battle's result reaches the other copy, a player who quits and restarts catches up, and the game-over file ends both games.

**Tested in the real game (2026-11-23)**, two copies on one Linux PC (`tools/research/turns_demo.py`), no desyncs:
- **Capture game**: B formed a unit and ordered Capture on A's base; it marched one hex per B turn. On turn 6 it took A's undefended base: A got the defeat screen and B the victory screen, from the game-over turn file (screenshot `docs/screenshots/turn-files-two-copies-game-over.png`).
- **Battle game**: A also formed a unit on Defend. On A's turn 4 A's defender met B's attacker; the battle was fought on A's copy with B's real designs (A won 3 kills to 0). B's copy got the result in turn file 7 ("battle at (9,6): their unit 32 attacked your unit 0. Theirs: 44HP 141HP 157HP Yours: 0HP 0HP 0HP"), B's unit was gone, and after 9 rounds both copies showed the same bases, units and OKE HP (mirrored).
- **Restart**: B was closed in the middle of the game and started again; it booted Europa, pressed End Turn once, loaded the newest turn file and the game went on.
- The first run found one real problem: the waiting copy was frozen (no window updates, debug server silent). Fixed below.

### Waiting without freezing: built (2026-11-23, step 6a)
The waiting copy no longer pauses inside End Turn. At the swap point the hook sends my turn file and looks for the opponent's. If it is not there yet, the hook keeps the game running: it "calls" the game's own VSync (`0x800AC734`, PsyQ `VSync(0)`: wait one frame) with itself as the return address, checks for the file twice a second, and loops. The registers End Turn had when it called the hook are kept and put back when the file has been loaded, so End Turn carries on exactly as before. Meanwhile the window responds, the game shows "Waiting for player X's turn (turn file N)..." on the status line at the bottom, and the debug server answers.

What this does not do yet: the player still has to keep the game open until the opponent has played. The next step (with storage) is to let the player close the game after sending, and have the launcher tell them when it is their turn; starting the game then catches up automatically (the restart test above shows that part already works).

Known gaps:
- ~~Research/negotiation (still unmapped) is not in the file; each copy keeps its own. The CPU's research still spends the waiting side's funds on every End Turn~~ (fixed 2026-11-25, step 6f below: the CPU's negotiation is off and turn files carry both players' negotiation).
- Hot-seat's gaps still apply (kills of the other side's OKEs go to the CPU card records).
- Each copy uses its own save for its own designs. (The scenario check was added on 2026-11-24, step 6d.)
- The "waiting" status line was not seen on screen in the test (headless copies draw no window); the log lines were.

### Turn-file storage: a small relay, built (2026-11-23, step 6b)
**Decided 2026-11-23 by the project owner: a small relay** (not GitHub sign-in). Share Data will use the same relay later.

- **`relay/worker.js`**: a Cloudflare Worker (one file, free plan, storage in Workers KV). No accounts: creating a game gives a code like `K7QX-M2PD` and player A's secret key; joining gives player B's key (only once). Only A's key can store odd turn files and only B's the even ones; files must come in order and can't be changed once stored. `GET /v1/games/<code>` says whose turn it is (for the launcher's "your turn" check and notifications). Setup steps for the owner are in `relay/README.md`.
- **`src/turn_relay.c`**: the game keeps its folder of turn files, and a background thread keeps it in step with the relay through the system's curl (as the launcher's updater does): it uploads my files as soon as they are written (retrying until the relay has them) and fetches the file the game is waiting for every 3 seconds. Switches: `CH_TURN_RELAY`, `CH_TURN_GAME`, `CH_TURN_KEY` (plus `CH_TURN_ROLE`); the folder defaults to `online-turns/<code>`.
- **Tested**: `node relay/test.mjs` (the relay's rules); the simulator plays an online game with separate folders through the relay run locally (`relay/dev_server.mjs`); and **two real copies with separate folders played the capture game through the local relay** to the victory/defeat screens, no desyncs. Not yet tested on a real Cloudflare deployment (needs the owner's account).
- ~~Gap: a player who starts on a new device (empty folder) is not caught up from the relay yet~~ (fixed 2026-11-24, below).

### Launcher Online screen, closing after sending, catching up on start (2026-11-24, step 6c)
**Launcher: ONLINE PLAY → ONLINE CAMPAIGN** (`launcher/ch_online.cpp` for the relay side, `launcher/ch_online_menu.inc` for the pages; `launcher/ch_menu.inc` only got a few hooks). Screenshots: `docs/screenshots/launcher-online-game.png`, `launcher-online-join.png`, `launcher-main-your-turn.png`.
- **Your games**: a list with each game's code and whose turn it is (YOUR TURN, WAITING, SENDING, OVER, OFFLINE), then NEW GAME and JOIN A GAME.
- **NEW GAME** asks the relay for a game; you are player A. The game page shows the code in large letters to give to the other player.
- **JOIN A GAME**: the code is entered without typing if you like: left/right pick a letter, up/down change it (controller or arrow keys). A keyboard can also just type it or paste it (Ctrl+V); a mouse clicks the top or bottom half of a letter. Codes never contain 0, O, 1 or I, so they can't be misread.
- **The game page**: PLAY (starts the game with the four relay switches set for this game), CHECK AGAIN, FORGET THIS GAME (press twice; it warns that the key goes with it).
- **Keys** are kept in `online-turns/games.txt` beside the game (one line per game: code, A/B, key, relay address). Turn files stay in `online-turns/<code>/`, where the game already put them.
- **"It's your turn"**: when the launcher opens it asks the relay about each game (only when the player has online games), and every minute while the Online screen is open. The main page then shows YOUR TURN next to ONLINE PLAY and on the status panel.
- While checking, the launcher also **sends any turn file the game could not send** (the game was closed too soon or the connection was down) and **fetches the turn files the folder is missing**.
- **Relay address**: built in with `-DCH_TURN_RELAY_URL=<address>`, or the first line of `online-turns/relay.txt` beside the game, or the `CH_TURN_RELAY` switch. Without one, the page says so and NEW GAME / JOIN are greyed out. Each game remembers the relay it was made on.
- The Online Campaign entry only works in builds with `-DCH_REMOTE_BATTLE=ON` (otherwise it stays "SOON").

**Catching up when the map opens** (`src/turn_files.c`, hook on `0x800333F8`). `0x8003188C` is the routine that opens the campaign map: it sets up the tables for a new map (or keeps the ones a save loaded), and its last step calls `0x800333F8`, which nothing else calls. At that moment the game now: brings the folder up to date with the relay (waits up to 10 seconds), runs hot-seat's set-up straight away, and loads the newest turn file:
- **the opponent's file is newest**: it is my turn; the map shows the real state before I give any orders ("Caught up with player B's turn. It's your turn.");
- **my own file is newest**: I already played; my file is loaded turned back to my point of view, and the next End Turn only waits for theirs ("It's player B's turn. Close the game, or press End Turn to wait.");
- **no files**: player A starts a new game; player B's first End Turn waits for A's turn 1.
So nobody needs the "press End Turn once to catch up" step any more, and orders can't be lost to it.

**Closing after sending**: after End Turn the bottom line says "Sending your turn to the relay..." and then "Your turn is sent. You can close the game, or wait for player B." If the game is closed before that, the launcher sends the file the next time it opens.

**New computer / empty folder**: the game's relay thread first asks the relay for its newest turn file and downloads every one the folder lacks. Player A only starts a new game once the relay has said it has no turn files, so an empty folder never overwrites a game in progress. The game now also creates the turn folder itself (it failed with curl error 23 when the folder did not exist).

**Tested**
- Simulator (`tools/research/turnsim/run.sh`, now with a map-start step): straight game; B restarts at the start of its turn (caught up when the map opens); B closes right after sending (its own file comes back, the next End Turn waits); online through the local relay, then B on a new computer (turn files 1-7 fetched from the relay). All checks pass.
- **Real game, two copies, separate folders, local relay** (`tools/research/online_demo.py`): both map starts; B closed at the start of its turn and started again: the map caught up with turn file 3 without an End Turn and **all units, bases, OKEs and funds were byte-for-byte what B had before closing**; the same from an empty folder (all three files from the relay); B closed right after sending turn 4: its own state came back exactly and the next End Turn waited for A's turn 5; then played on to B's victory and A's defeat. No desyncs.
- Launcher pages: drawn and driven with key presses through the real menu code against the local relay (`tools/research/launcher_shots/run.sh`, which draws the pages in software since this sandbox has no screen): create, PLAY (sets the switches and launches), join by typing and with the arrows, wrong code, a third player, game over, forget.
- Not tested: the launcher in a real window (no screen here). (The real Cloudflare relay was tested on 2026-11-24, session 4.)

Known gaps:
- ~~The map start does not check which scenario was started~~ (done, step 6d below).
- If player A's very first End Turn happens while the relay can't be reached, that End Turn waits for the relay and then ends without sending; A presses End Turn again to play turn 1 (production ran once for nothing).
- ~~Desktop only for now~~ (Android: step 6e below).

### Scenario check (2026-11-24, step 6d)
**Found**: the scenario is kept at `0x800DF384` (gp+44): 0 Europa (Practice), 1 Europa, 2 Ganymede, 3 Callisto; the map number within the scenario is next to it at `0x800DF388` (gp+48, from 0). Found by starting each of the four scenarios from a fresh boot and comparing memory; confirmed by the code: the next-map routine `0x80036C70` calls the map set-up `0x80037240(scenario, map)` with exactly these two, and the save/new-game code (`0x80039340`, `0x8007DD5C`) writes both.

**Built** (`src/turn_files.c`): turn files are now **version 2**, with the scenario and map number in the header (bytes 48 and 49; the header grew from 48 to 52 bytes). When the map opens, before anything is loaded:
- **A game in progress on another scenario or map** (for example the turn files are Europa (Practice) but the player started Europa): nothing is loaded and nothing will be sent. The bottom line says "This online game is on Europa (Practice). Restart and choose Europa (Practice)." and online play is off until the game is restarted; the map plays as a normal campaign against the CPU, so the player's turn files are never touched.
- **A new online game on a scenario online play doesn't support yet**: the same, with "Online games are Europa (Practice) only for now. Restart and choose it." Which scenarios are allowed is one setting (`CH_ONLINE_SCENARIOS`, a bit per scenario; now only bit 0, Europa (Practice)).
- Every turn file read is also checked against the scenario being played, as a safety net.
- Older version-1 turn files are refused with a message to start a new online game (only test games used them).

Why "switch off" rather than "refuse to start": the map is already open when the check runs, and a normal campaign is a safe state that can't damage the online game. The player only needs to restart and pick the right scenario.

**Launcher shows the scenario** (2026-11-24, session 4): the game page says "SCENARIO: EUROPA (PRACTICE)" (or e.g. "GANYMEDE MAP 2"), read from the newest turn file's bytes 48/49; the games list says "START ..." for the selected game. Before any turn file it shows Europa (Practice). Screenshot `docs/screenshots/launcher-online-scenario.png`; tested with a new step in `launcher_shots/run.sh`.

**Tested**: simulator (`turnsim/run.sh`, two new cases: a copy started on Europa against a Europa (Practice) game loads and sends nothing; a new game started on Ganymede writes no turn file); the real two-copy online test (`online_demo.py`) still plays through to victory/defeat with no desyncs, and its logs show "map start: Europa (Practice), as in the turn files"; and in the real game, a copy started on Europa against that game's turn files said "Europa started, but this online game is on Europa (Practice)", loaded nothing and wrote no file, and its End Turn ran as a normal campaign. (The bottom line itself is not in headless screenshots; the log shows it was set.)

### Replay: watch what the opponent did (2026-11-24, session 6, step 7)
Branch `enemy-replay`, **PR #11** (on main after PR #9). When the opponent's turn arrives, their units now walk across the map and **their battles are fought again on screen**, then the turn file's end state is loaded exactly as before. Screenshots: `docs/screenshots/replay-enemy-move.png` (the red enemy unit stepping on the hex map), `docs/screenshots/replay-enemy-battle.png` (the map, then the replayed 3D battle).

How it works (`src/turn_files.c`, `src/remote_battle.c`, `src/hotseat.c`, new `src/turn_replay.c`):
- **Turn files are version 3.** Two parts are added at the end of the body: the **start tables** (units, bases, OKEs and funds when the sender pressed End Turn, after their orders and before any unit moved; same layout as the tables, receiver's point of view) and a **random seed for each battle**. About 43 KB a file (was 37 KB). Version 2 files are still read (no replay). Header unchanged, so the launcher and Android app need no change.
- **The game's own unit step drives the replay.** End Turn (`0x8001B8EC`) starts with `0x800341FC` (rebuilds a per-hex table at `*0x800E00C8` from the units), then for each unit with a nonzero owner byte calls `0x80034C38(unit)`, `0x8001BCB0(2)`, `0x8001C000(unit)`. The replay writes the start tables into RAM and calls the same routines for the opponent's units 32-63, with the hook as the return address (the same trick as the waiting loop's VSync). It runs at the swap point (the turn arrives while waiting) and at the map start (the game was opened after the opponent played). Without `0x800341FC` first, a unit walking onto an enemy unit found no one there at map start.
- **A half-second pause after each enemy unit's step** (`CH_TURN_REPLAY_PAUSE` frames, default 30), so the player sees each move. `CH_TURN_REPLAY=0` turns the replay off.
- **Battles are fought again, the same way.** The game's `rand()` goes through one stub, `0x8008E1E4` (BIOS A0:2F, 140 callers). While online, `turn_replay.c` keeps its own seed (same formula as the BIOS), and every battle on the sender's device starts from a fresh seed that goes in the turn file. In the replay, RAM is turned round to the sender's point of view for the battle (`ch_hotseat_swap_view`: units i <-> i+32, owner bytes, funds, card tables), their unit is side 1 again with the same unit numbers, and the same seed is set. **The battle then plays out exactly as it did for them.** The recorded HP and kills are written over the result anyway, and RAM is turned back before the game's result code runs. So the replay can never change the game: if it ever went differently, the log says so and the recorded result stands.
- Also found: base fuel each End Turn is random (`rand()` in the base loop of `0x8001B8EC`), and base repair (+20% HP) also reaches OKEs **in a unit standing on its own base**, not only stored ones.

**Tested**
- Simulator (`turnsim/run.sh`): now plays the hooks' game-routine calls (VSync, `0x800341FC`, the three unit routines) and its stand-in battle uses `rand()`. Every opponent turn is replayed; the replayed battle gives the recorded result; units end where the turn file says. All earlier checks still pass.
- **Real game, two copies, local relay** (new `tools/research/replay_demo.py`): A defends, B attacks; on A's turn 4 they fight on A's copy. **B's replay of that battle went exactly as recorded** (same HP and kills), both when the turn arrived while B was waiting and when B was closed and opened the game after A had played (`REPLAY_RESTART=1`: the battle is replayed when the map opens). In 12 replays per run, every unit ended where the turn file says; no desyncs.
- `online_demo.py` (restarts, empty folder, close after sending, game over) still passes.
- Not tested: Android (no devkit access in this session; same code, `CH_REMOTE_BATTLE` is on there), a real window (headless here), the real Cloudflare relay with version-3 files (the relay only checks "CHTF" and size; 43 KB is under its 64 KB limit).

Known gaps
- **Both players need the new build**: an older build refuses version-3 turn files ("update the game"). Games already in progress carry on (the new build reads version 2; replays start with the first version-3 file).
- Only units, positions and battles are replayed. Production, research and base changes simply appear when the end state is loaded.
- The replay can't be skipped yet (a button to skip would be a small addition).

### Negotiation and research (2026-11-25, session 7, step 6f)
Branch `negotiation-research`, **PR #15**. The map is in CAMPAIGN_RAM_MAP.md ("Negotiation and research"); a plain-words guide to the four Negotiation choices for the tutorials is in NEGOTIATION_GUIDE.md.

**What was wrong before**: turn files carried funds but not negotiation, so each copy kept its own blueprints, investments and goodwill (lost on a new device), each copy's companies developed blueprints at their own random pace, and the CPU's negotiation spent the waiting side's funds every End Turn (hot-seat put the funds back, but the CPU still "owned" blueprints on each copy).

**Built (`src/negotiation_sync.c`, hooks in `CMakeLists.txt`):**
- **The CPU's negotiation is off** in hot-seat and online games: `0x80049828` (buys blueprints), `0x80049F44` (orders optimizations), `0x8004A214` (invests in R&D) and `0x80049CA4` (buys a blueprint as it is finished) are skipped. Single-player campaigns are unchanged.
- **Each player has a ledger**: blueprints they bought, their R&D payments and bonuses, their optimizations, their goodwill and purchases at each company, their Information requests. The rest is the **world**: which blueprints exist, are being developed (turns left, chance) or are on sale, and which R&D slot holds which. On each copy RAM holds the world, the player's own ledger, and the opponent's ownership and R&D payments shown as Drakken's (so Information's "Drakken purchased our..." means the opponent did).
- **The world moves on once a round**, on player A's half (side A in hot-seat). On B's half the game's End Turn step runs as usual (B's optimizations count down, B's goodwill fades) and the world is put back afterwards, so development is as fast as in the original game, not twice as fast. When a blueprint B paid for is finished on A's half, B's bonus (10% or 20% off, flag 0x0800, company flag) is worked out the way the game does it for the player.
- **Turn files are version 4**: a 4008-byte section at the end ("NEG1", the world, the receiver's ledger, the sender's ledger). About 47 KB a file (was 43 KB; the relay's limit is 64 KB). Version 2-3 files are still read (negotiation then stays as it is on that copy). **Both players need the new build** (an older build refuses version 4).
- **Hot-seat** swaps the ledgers with the sides.
- Funds spent in Negotiation travel with the funds, as before.

**Tested**
- Simulator (`turnsim/run.sh`): a stand-in research step, A buys a blueprint, B pays for one in R&D; A owns it and B sees it as the opponent's; B's payment earns B (not A) 20% off when it is finished on A's half; the CPU's negotiation never runs; all earlier cases (restarts, own file, empty folder, relay) still pass with the new section.
- **Real game, two copies, local relay** (new `tools/research/negotiation_demo.py`), through the game's own screens: A bought CP-478FX (Kawashima Corp) on turn 1 and B's copy showed it as the opponent's; B paid 5000 towards Golle 1800C1 (Union Claude) and A's copy showed it as the opponent's payment, with the same 2 turns saved on both copies; over 2 rounds every blueprint in development moved on exactly 2 turns; the waiting side's funds were never touched; both copies had identical worlds throughout; B closed and restarted and its blueprints, payments and goodwill came back from its turn file.

Known gaps
- The **market stance** (`0x800DF394`, prices +-15% for winning or losing) is worked out on each copy from its own battle counters; not synced. Small price differences between the two players are possible.
- A development delay rolled on A's half only shows "delayed" (flag 0x8000) to A, even if B also paid for it.
- The "reserved for the investor" window (+03 = 0x40) only keeps the CPU from buying; between two players it does nothing (as before, both can buy any blueprint on sale).
- Not tested: Android (same code), a real window.

### Saves and memory cards: mapped (2026-11-22)
A campaign save is one 5-block file whose body is a straight copy of the campaign tables in RAM (cards, units, bases, OKEs, globals). No checksum found. A single design is one 1152-byte record at a known offset. See VS_MODE_AND_SAVES.md.

### Still to map
- ~~Negotiation/research state~~ (done 2026-11-25).

### Other parts
- **Notifications**: no push server at first. The Android app checks for new turn files in the background (about every 15 minutes) and posts a local notification. Desktop checks when the launcher opens.
- **Storage** of turn files: the small relay (above).

### Android: Online screen, relay without curl, "your turn" notifications (2026-11-24, step 6e)
Branch `online-android`, **PR #9** (on top of v0.1.3 main, which already has PR #2 and the Android port).
- **Start screen → ONLINE CAMPAIGN** (`android/.../OnlineActivity.java`): the Android version of the launcher's Online screen. Your games with whose turn it is (YOUR TURN, WAITING, SENDING, OVER, OFFLINE), NEW GAME, JOIN A GAME (type the code; lower case, spaces and a missing dash are fine), and a page per game: the code in large letters, which player you are, the scenario to start, PLAY / PLAY (SOFTWARE RENDERER), CHECK AGAIN, SHARE CODE (sends the code through any chat app), FORGET (press twice). The Back button goes back a page, then to the start screen.
- **Same files as the desktop**: games in `files/online-turns/games.txt`, turn files in `files/online-turns/<code>/`. `GameActivity` puts the four `CH_TURN_*` switches in the environment (`Os.setenv`) before the game starts; a normal PLAY clears them.
- **No curl on Android**: `src/turn_relay.c` now sends its two requests (GET a file, PUT a file) through a small helper; on desktop it is curl as before, on Android it calls `Relay.java` (HttpURLConnection, the system's TLS) through JNI (`src/android/ch_android_relay.c`). The answers are given as curl's codes (0 / 22 / 7), so the relay thread works the same on both.
- **"It's your turn" notifications** (`TurnCheckJob.java`): Android's JobScheduler runs a check about every 15 minutes when there is a connection (Android may stretch this to save battery; it carries on after a restart). For each game it does what the desktop launcher's check does (sends a turn file the game could not, fetches new ones) and posts "Carnage Heart: it's your turn — Player B has played (game K7QX-M2PD, turn 3)" once per turn, or "game ... is over". Tapping it opens that game's page. No notification while the game is open (it fetches the turn itself). Android 13+ asks for permission to notify when a game is made or joined.
- **Found**: the app had no `INTERNET` permission, so online play could never have worked on Android without this. Added, plus `POST_NOTIFICATIONS` and `RECEIVE_BOOT_COMPLETED`.
- `CH_REMOTE_BATTLE` is on in the Android build (`cmake/ch_android.cmake`).

**Tested**
- `tools/research/android_relay_test/run.sh`: `Relay.java` run on a PC with a stand-in `Context`, against the local relay and **the real Cloudflare relay**: create, join (lower case, wrong code, third player refused), the game's `nativePut`/`nativeGet` (wrong key refused, retried upload fine, missing file = 22 and no file left, no connection = 7), sending a turn the game could not, a new device catching up, game over on both sides, games.txt, forget. All 19 checks pass on both.
- The turn simulator (`turnsim/run.sh`, online cases included) still passes with the changed `turn_relay.c`.
- The APK builds (sandbox devkit) and contains the online code.

**Tested on a device (2026-11-24, owner)**: the test APK (signed with the owner's key) on the Lenovo Legion Tab Y700 Gen 2 — the owner reports multiplayer worked. PR #9 merged into main.

Known gaps
- The notification icon is the app icon (Android shows it as a plain shape in the status bar).
- If the game is still running in the background and PLAY is pressed for a different online game, Android brings back the running game with its old switches. Closing the game first avoids this.

## Option A: Live VS over the internet (later)
PSXRecomp netplay has a `guest_memcard` setting: player 2's own memory card becomes the match's slot-2 card. Both players open VS mode and watch the same battle.
- Work: turn on `PSX_NETPLAY`, set `guest_memcard = 1`. VS mode reads whole saves from card slots 1 and 2 (confirmed), so the guest's card in slot 2 should appear as the right-hand stack.

## Option B: Card sharing, async (later)
1. ~~Map where each card sits in the save~~ (done: save offset 0x200 + 1152 × slot).
2. Launcher "Share My Data": export a card, upload.
3. "Browse Shared Data": download a card into a spare slot of a save on the slot-2 memory card for VS mode.

## Suggested order
1. ~~Campaign RAM map~~ (done 2026-11-22).
2. ~~Map VS mode and saves; build and test remote-player battles~~ (done 2026-11-22, `src/remote_battle.c`).
3. ~~Map the player order screens (including re-orders) and factory data~~ (done 2026-11-22).
4. ~~Local hot-seat test on one machine: end turn, swap sides, play the other side~~ (done 2026-11-22, `src/hotseat.c`).
5. ~~Turn files: capture orders and battle results on one copy, apply them on a second copy~~ (done 2026-11-22, `src/turn_files.c`; tested in the real game 2026-11-23).
6. ~~Waiting without freezing the game~~ and ~~storage: the turn relay~~ (done 2026-11-23). ~~Launcher Online screen, closing after sending, catching up on start (also from the relay on a new device), desktop "your turn" check~~ (done 2026-11-24). ~~Scenario check~~ (done 2026-11-24). ~~Real relay test~~ (done 2026-11-24, session 4). ~~Android: Online screen and notifications~~ (built 2026-11-24, session 5, PR #9). ~~Device test~~ (passed 2026-11-24, owner).
7. ~~Optional: visual replay of the enemy's turn~~ (done 2026-11-24, session 6, branch `enemy-replay`; battles are fought again from the same seed).
7b. ~~Negotiation and research: map, stop the CPU's, carry both players' in the turn files~~ (done 2026-11-25, session 7, branch `negotiation-research`).
8. Later: Options A and B (the card and save layout they need is now mapped).

## Session status (chat B, 2026-11-25, session 7)
**Done**
- PR #11 (replay) had already been merged by the hub.
- **Negotiation and research mapped** (CAMPAIGN_RAM_MAP.md): the six companies, the blueprint state table, R&D slots, company records, the four choices with their costs and RAM effects, prices, the End Turn routine and what the CPU does there. Checked live (purchase, R&D payment, optimization, Information, End Turns).
- **Plain-words guide** for chat A's tutorials: NEGOTIATION_GUIDE.md (also the project doc "claude/Negotiation Guide").
- **Online**: the CPU's negotiation is off in versus games; turn files version 4 carry the world and both players' ledgers (step 6f above). Simulator and a real two-copy test pass; `replay_demo.py` and `online_demo.py` re-run and pass (no desyncs). Pushed; **PR #15** opened for the hub.

**Next**
- Hub: merge **PR #15** (branch `negotiation-research`). Release note: both players must update again (version-4 turn files).
- Owner: Android test APK with the replay and negotiation (needs `carnageheart.keystore` attached in a chat B session, and the devkit release).
- Chat A: tutorials can use NEGOTIATION_GUIDE.md.
- Small: skip button for the replay; monochrome notification icon; maybe sync the market stance.
- Later: Options A (live VS) and B (card sharing).

**Blocked**
- Test APK: no keystore attached this session.

## Session status (chat B, 2026-11-24, session 6)
**Done**
- **Replay of the opponent's turn** (step 7 above), branch `enemy-replay` from main: their units walk on the map with a short pause after each step, and their battles are fought again on screen from the same seed, giving exactly the recorded result; the turn file's end state is then loaded as before. Turn files are version 3 (start tables + battle seeds); version 2 still read.
- Tests: simulator (replay cases added), new `tools/research/replay_demo.py` (two real copies: battle replayed while waiting and at map start, both exact), `online_demo.py` still passes.
- Sandbox notes: libwebp and SDL3 are not vendored and codeload.github.com is blocked; `git fetch` the libwebp pin from github.com and pass `PSX_LIBWEBP_SOURCE_DIR`; `apt-get install libgl-dev libegl-dev libgles-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxfixes-dev libxtst-dev` for SDL3.

**Next**
- Hub: merge **PR #11** (branch `enemy-replay`). Release note: both players must update (version-3 turn files).
- Owner: device test on Android (the APK could not be built here: no access to the devkit release).
- Map Negotiation and research (owner's request through chat A, project doc "Note for Chat B - Negotiation"): not started; `0x8004BBB8` is the End Turn routine (calls `0x8004B9F4`, loops over 6 records at `0x80128040` and the table at `0x800BD0AC`).
- Small: skip button for the replay; monochrome notification icon.
- Later: Options A (live VS) and B (card sharing).

**Blocked**
- Nothing (pushing works again; the branch is on GitHub as PR #11).

## Session status (chat B, 2026-11-24, session 5)
**Done**
- Hub merged PR #2 and the Android port into main (v0.1.3). New branch `online-android` on top of it, **PR #9**: the online campaign in the Android app (step 6e above): Online screen, relay requests through Java (no curl on Android), `CH_TURN_*` switches from the app, "it's your turn" notifications every ~15 minutes, `INTERNET` permission (was missing).
- `Relay.java` tested on a PC against the local and the real relay (`tools/research/android_relay_test/run.sh`, all pass); turn simulator still passes; APK built.

- Test APK rebuilt with the owner's key (`carnageheart.keystore`, SHA-256 19:6D:C3:EC...C9:98); installs over the old test app.
- **Owner tested on the tablet: multiplayer worked.** Hub merged PR #9 into main.

**Next** (session 6 branches from main)
- Optional: visual replay of the enemy's turn (step 7).
- Later: Options A (live VS) and B (card sharing; Share Data can reuse the relay).
- Small: a monochrome notification icon.

**Blocked**
- Nothing.

## Session status (chat B, 2026-11-24, session 4)
**Done**
- Launcher shows each online game's scenario (commit 1e51af8 on `research/vs-battle-and-saves`): "SCENARIO: ..." on the game page, "START ..." in the games list, from byte 48/49 of the newest turn file. Tested with `launcher_shots/run.sh` (new step with a Ganymede map 2 turn file).
- Owner set up the relay on Cloudflare: **`https://ch-turns.surreal64.workers.dev`** (KV namespace `ch-turns`, binding `TURNS`). It is now the launcher's built-in default (`CH_TURN_RELAY_URL` in `cmake/ch_launcher_menu.cmake`, commit 76941cf).
- **Real relay test passed** (`CH_RELAY_URL=https://ch-turns.surreal64.workers.dev python3 tools/research/online_demo.py`): two real copies, separate folders, game B8J7-RH2W, all five checks (map starts, restart catch-up, empty folder from the relay, close after sending, game over), no desyncs; the relay ended with 10 turn files. Screenshots `docs/screenshots/online-real-relay-victory.png` / `-defeat.png`.
- Found on the way: Cloudflare refuses Python's default User-Agent (error 1010, HTTP 403); curl's is accepted, so the game and launcher are fine. `online_demo.py` now sends its own User-Agent and waits for a closed copy's debug port to be free before starting the next.
- A first try failed with "relay error" (HTTP 500): the KV binding had been made as `KV` instead of `TURNS`. Worth a line in `relay/README.md` (done).

**Next**
- Android notifications after chat C has an APK.
- Release builds need `-DCH_REMOTE_BATTLE=ON` for the Online Campaign entry to work.

**Blocked**
- Nothing for chat B now; Android notifications wait for chat C's APK.

## Session status (chat B, 2026-11-24, session 3)
**Done**
- Landed session 2's commit (launcher Online screen, catch-up at map start) on `research/vs-battle-and-saves`; pushing works again.
- Found the scenario (`0x800DF384`) and map number (`0x800DF388`) in RAM.
- Turn files version 2 carry them; the map start refuses a game on the wrong scenario, and new online games are Europa (Practice) only (`CH_ONLINE_SCENARIOS`). Tested in the simulator and the real game (step 6d above).
- Sandbox build notes updated (the game code has to be generated from the disc first).

**Next**
- Owner: set up the relay on Cloudflare (`relay/README.md`) and send the address; then a real online test (`CH_RELAY_URL=<address> python3 tools/research/online_demo.py`) and building the address into releases (`-DCH_TURN_RELAY_URL=`).
- The launcher could show the scenario on each game's page (it is in byte 48 of the turn files), so players know which one to start.
- Android notifications after chat C has an APK.
- Release builds need `-DCH_REMOTE_BATTLE=ON` for the Online Campaign entry to work.

**Blocked**
- A real (non-local) relay test needs the owner's Cloudflare relay address.

## Session status (chat B, 2026-11-24, session 2)
**Done**
- Launcher ONLINE CAMPAIGN screen (on this branch): your games with whose turn it is, NEW GAME, JOIN A GAME by code (controller, keyboard or mouse), PLAY with the relay switches, CHECK AGAIN, FORGET. The main page shows YOUR TURN.
- The launcher checks the relay when it opens, sends turn files the game could not, and fetches missing ones.
- The game catches up by itself when the map opens (no End Turn needed), and from the relay on an empty folder. The bottom line says when a sent turn is on the relay, so the game can be closed.
- Fixed: the game did not create its turn folder (relay catch-up failed with curl error 23); the launcher's background thread could hang the program on exit.
- Tests: simulator (two new cases), real two-copy online test with restarts (`online_demo.py`), launcher pages driven through the real menu code (`launcher_shots/run.sh`).

**Next**
- Owner: set up the relay on Cloudflare (`relay/README.md`) and send the address; then a real online test (task 1) and building the address into releases (`-DCH_TURN_RELAY_URL=`).
- Chat A: look over the small hooks in `launcher/ch_menu.inc` and `cmake/ch_launcher_menu.cmake` when merging; the pages themselves are in the new `launcher/ch_online_menu.inc`.
- Release builds need `-DCH_REMOTE_BATTLE=ON` for the Online Campaign entry to work.
- Check the scenario at map start (put the scenario number in the turn file).
- Android notifications after chat C has an APK.

**Blocked**
- A real (non-local) relay test needs the owner's Cloudflare relay address.
- Pushing from this sandbox (see the session notes).

## Session status (chat B, 2026-11-23, previous)
**Done**
- Real two-copy turn-file test: capture game, battle game and restart all worked; no desyncs.
- Fixed the frozen waiting copy: it keeps running and shows a "waiting" line until the opponent's file arrives.
- Storage decided (small relay) and built: `relay/worker.js` (Cloudflare Worker) and `src/turn_relay.c` (game side). Two real copies played a full online game through the relay run locally.
- Test helpers: `boot_europa.py`, `un7z.py` (compressed 7z headers), relay tests and local relay server, online scenario in the simulator.

**Next**
- Owner: set up the relay on Cloudflare (`relay/README.md`, about 10 minutes) and send the relay address.
- Launcher Online screen (on this branch; chat A owns `launcher/`): create or join a game by code, keep the key, start the game with the switches, and on opening check `GET /v1/games/<code>` to show "It's your turn".
- Let the player close the game after sending; on start, catch up automatically (also from the relay on an empty folder) instead of needing one End Turn.
- Android notifications after chat C has an APK.

**Blocked**
- A real (non-local) relay test needs the owner's Cloudflare relay address.
