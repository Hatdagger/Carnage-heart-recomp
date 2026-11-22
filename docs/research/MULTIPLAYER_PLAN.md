# Carnage Heart ReComp: Multiplayer Plan

Researched 2026-11-21, updated 2026-11-22 after the campaign RAM mapping and the VS/saves mapping (see [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) and [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md)).

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

**Capturing orders**: the simplest reliable way is to compare each of my units' task/target at End Turn with what they were at the start of my turn. Any difference is an order. This catches re-orders however they were given, without hooking every order screen. To confirm when mapping the Unit Command screen: whether re-ordering also resets the path state (+0A/+0B), which the replay would need to copy too.

### Custom programs on the CPU side: answered (2026-11-22)
**Campaign battles can be fought as VS-mode battles** (the project owner's idea). Tested at Europa turn 6 → 7 with an experiment build: a CPU unit made of three MACHINE02 player designs fought the player's unit with their own chip programs. Afterwards the game's own code removed the dead OKEs, deleted the beaten unit and kept the base. Details and the exact recipe are in VS_MODE_AND_SAVES.md.

What was found:
- VS mode and the campaign share **one battle engine** (`0x8001CF48`) in one executable (no overlays), so there is no mode switch or RAM save/restore to do.
- The engine treats side 2 as CPU because of the **battle mode flag** (`0x800DF3D0`: 1 campaign, 2 VS) and the **OKE owner byte**. Setting side 2's OKEs to owner 1 with a player-card index, and the mode to 2 for the whole battle, makes it a VS-style battle.
- Map damage carries over: the engine takes each OKE's starting HP from the OKE table.
- The result still lands in the 6 OKE records, so the "report the result" plan is unchanged.

Still to do for the real feature:
- Put survivors on side 2 back to owner 2 after the battle.
- Make room for the remote player's 3 designs in the local player card table (28 slots, shared with the local player's own designs). Simplest is to swap them in for the battle only.
- Test a battle where the player attacks (the test had the CPU unit attacking).

### Saves and memory cards: mapped (2026-11-22)
A campaign save is one 5-block file whose body is a straight copy of the campaign tables in RAM (cards, units, bases, OKEs, globals). No checksum found. A single design is one 1152-byte record at a known offset. See VS_MODE_AND_SAVES.md.

### Still to map
- The player's order screens, including re-ordering a unit on the move (expected to write the same unit fields).
- Factory/production data in the base record, and OKE stock per base.
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
2. ~~Map VS mode and saves; test the "campaign battle through VS mode" idea~~ (done 2026-11-22: it works). Remaining: owner byte back to 2 for survivors, card-table room for the remote designs, a player-attacks test.
3. Map the player order screens (including re-orders) and factory data.
4. Local hot-seat test on one machine: end turn, swap sides, play the other side.
5. Turn files: capture orders and battle results on one copy, replay them on a second copy.
6. Storage, then notifications (desktop, then Android).
7. Later: Options A and B (the card and save layout they need is now mapped).
