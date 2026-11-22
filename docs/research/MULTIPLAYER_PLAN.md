# Carnage Heart ReComp: Multiplayer Plan

Researched 2026-11-21, updated 2026-11-22 after the campaign RAM mapping, the VS/saves mapping and the hot-seat test (see [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) and [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md)).

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

### Saves and memory cards: mapped (2026-11-22)
A campaign save is one 5-block file whose body is a straight copy of the campaign tables in RAM (cards, units, bases, OKEs, globals). No checksum found. A single design is one 1152-byte record at a known offset. See VS_MODE_AND_SAVES.md.

### Still to map
- Negotiation/research state.

### Other parts
- **Notifications**: no push server at first. The Android app checks for new turn files in the background (about every 15 minutes) and posts a local notification. Desktop checks when the launcher opens.
- **Storage** of turn files: same open question as Share Data in the launcher plan (GitHub sign-in in the launcher, or a small relay).

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
5. Turn files: capture orders and battle results on one copy, replay them on a second copy.
6. Storage, then notifications (desktop, then Android).
7. Later: Options A and B (the card and save layout they need is now mapped).
