# Carnage Heart ReComp: Campaign RAM Map (USA, SCUS-94604)

Mapped 2026-11-22 on a Linux headless build of the recomp, Europa (Practice) scenario, using the debug server (RAM dumps, write traces, savestates) and a static scan of the executable. Verified = seen in live RAM and in the code that writes it.

See also [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md) and [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md).

## Globals (gp = 0x800DF358)
| Address | gp+ | What | Status |
|---|---|---|---|
| 0x800DF37C | 36 | Turn counter (shown on screen), clamped 1..9999 | Verified |
| 0x800DF380 | 40 | Second turn counter, clamped 1..999 | Verified |
| 0x800DF368 / 0x800DF36C | 16 / 20 | Menu pointer x / y (s32, screen pixels). Writing them moves the pointer | Verified |
| 0x800DF38C | 52 | Player funds | Verified (500000 at start) |
| 0x800DF390 | 56 | CPU funds (drops when the CPU forms units; end-turn income added here, capped 9,999,999) | Very likely |
| 0x800DF408 | 176 | Index of the unit the order screens are working on | Verified (read by the order handlers) |
| 0x800E0014 | — | Pointer to **player card table** (0x8012C97C), 1152 bytes per card (hardware + chip program, name in Shift-JIS) | Verified |
| 0x800E0018 | — | Pointer to **CPU card table** (0x8013477C), 36 bytes per card (stats only) | Verified |
| 0x800E014C | — | Pointer to **OKE table** (0x801356AC), 12 bytes per OKE | Verified |
| 0x800E0154 | — | Pointer to **base table** (0x801350EC), 16 bases x 92 bytes | Verified |
| 0x800E017C | — | Pointer to **unit table** (0x80134B6C), 64 units x 22 bytes | Verified |

The tables are allocated at run time; always go through the pointers.

## Map
Hex cell index = y * 24 + x (used for unit targets).

## Unit table (64 x 22 bytes)
**Units 0-31 belong to the player, 32-63 to the CPU** (the CPU fills from the top down).
| Offset | Size | What |
|---|---|---|
| +00 | u8 | Owner: 0 empty, 1 player, 2 CPU |
| +01 | u8 | Task: 0 standby/none, 1 move, 2 defend a base, 3 capture a base, 4 patrol (see Order screens) |
| +02 / +04 | s16 | x / y |
| +06 | u16 | Target cell |
| +08 | u16 | Home/defend cell (set by Defend Base) |
| +0A | u8 | Movement state (6 = formed, still at the base). Recomputed by `0x80034C38` after every order |
| +0B | u8 | Next cell on the path (low byte) |
| +0C / +0E / +10 | s16 | The 3 OKE slots (OKE table index, -1 empty) |
| +12 | u16 | Ammo (10000 when formed; taken from the base's stock) |
| +14 | u16 | Fuel (1000 when formed; taken from the base's stock) |

## Base table (16 x 92 bytes)
| Offset | What |
|---|---|
| +00 | Owner: 0 none, 1 player, 2 CPU |
| +01 | Unknown (3 on the player's start base) |
| +02 / +04 | x / y |
| +08 | Ammo stock (u32; 1 t on screen = 1000) |
| +0C | Fuel stock (end-turn income added, capped 99,900) |
| +10 + 12×n | Production line n (6 lines, n = 0..5). See below |
| +58 | u8: number of open lines (2 at the start; Add Line +1) |
| +59 | u8: probably the most lines this base can have (6 on the player's start base, 3 on the CPU's) |

### Production line (12 bytes, base +10 + 12×n)
Verified 2026-11-22 on the Factory screens (base 2, Europa).

| Offset | What |
|---|---|
| +00 | u8: 1 = nothing on order, 0 = an amount is on order |
| +01 | u8: product. 0xFF none, 0x63 ammo, otherwise a player card index (0x19 = card 26, MACHINE01) |
| +02 | u16: amount ordered (tons of ammo, or number of OKEs) |
| +04 | u16: amount finished so far |
| +06 | u16: line level (Upgrade +1) |
| +08 | u16: capacity, work points per turn (80 at level 1, 100 at level 2) |
| +0A | u16: work still needed for the OKE being built. Set to the design's "Proc." (182 for MACHINE01) when an OKE starts, minus the capacity each turn; leftover points carry into the next OKE |

How a turn of production went (player production `0x800566F4`, End Turn step 5): the design's cost (1100 for MACHINE01) is taken from funds when an OKE **starts**; ammo costs 20 per ton. 10 t of ammo on a level-1 line finished in one turn (base ammo +10,000). The OKE was done on the third turn (182 - 80 - 80 < 0), appeared as a new OKE record stored at the base (`0x8005B118`), +04 went to 1 and the next one started at once.

## OKE table (12 bytes each)
| Offset | What |
|---|---|
| +00 | Owner (0 empty, 1, 2). Also decides player vs CPU card table and the battle's icon/fuel display |
| +01 | 0 = stored at a base, 1 = in a unit |
| +02 | Base index where it is stored (or was formed) |
| +03 | Card index (into the player or CPU card table, by owner) |
| +04 | Kills in the last battle (added to the card's record afterwards; losses go to card +62) |
| +06 | HP (s16). 0 or less = destroyed. The battle starts each OKE with this value, so damage carries over |
| +08 | u16, from the design (card +54); meaning not confirmed |
| +0A | Main-weapon ammo (shown on the pre-battle screen) |
| +0B | Sub-weapon ammo (missiles etc.) |

A new player OKE gets HP = card +52, +08 = card +54, main ammo and sub ammo from the parts table `0x800C0C84` (36-byte records) for the parts at card +45 and +38 (`0x8005B118`). Up to 200 OKEs.

## End-of-turn routine: 0x8001B8EC
In order:
1. Turn counters +1.
2. **Player units 0-31**: for each active unit, `0x80034C38` (order/path step), `0x8001BCB0`, `0x8001C000` (move; may start a battle).
3. **CPU AI: `0x8001C268`**. Once per turn. Runs CPU production (`0x8001C478` -> `0x80055B00`), then for each CPU base with at least 3 ready OKEs: picks a free unit slot (`0x8001CC5C`), forms a unit (`0x80071D70` -> `0x80071E34`), and sets its task and target (nearest enemy base via `0x8001CBA0`, or a base to defend via `0x8001CDAC`). **The CPU only gives orders to new units**; existing units keep their task.
4. **CPU units 32-63**: same processing as step 2.
5. Base fuel loop (every owned base, either side), then `0x80033E28`, `0x8004BBB8` (looks like research/negotiation; spends CPU funds every turn), `0x800566F4` (player production), per-unit resupply `0x80075A80` (all 64 units), **`0x8007654C` base repair**: every OKE stored at a base (+01 = 0) gets 20% of its max HP back, capped at the max. Max HP is player card +52 for owner 1 and CPU card +16 for owner 2.
6. Bases counted by owner, then `0x8008A2B8` (called only from here), then the win/lose check: no player bases = lose; no CPU bases = map won (next map via `0x80036C70`, which clears and sets up all tables, or the scenario end). Otherwise End Turn returns here; the caller (`0x80038208`) moves the camera to a player base if the selected one was lost.

## Battles
- Started inside the move function `0x8003553C`: after a unit steps, if an enemy **unit** is on the same hex, it calls **`0x80035CD4(player-side unit, CPU-side unit)`**, which runs the 3D battle (`0x8001CF48`).
- Then **`0x80035988(unit)`** runs for both units: each OKE with HP of 0 or less is removed (`0x80035B4C` also adds kills and losses to the card records), and a unit with no OKEs left is deleted. If the moving unit lost, it goes back to its previous hex.
- Then base capture: if the unit is on a base hex, the base's owner byte changes (`0x800357B0`). OKEs stored at a base with no unit defending it are simply dismantled by `0x800358CC`; **no battle happens**.
- **The battle's entire result is the HP (and the 2 consumables and kills) of the 6 OKEs** in the OKE table. Everything after that (removing OKEs, deleting units, retreat, base capture) is done by the game's own code.

Observed (turn 6 -> 7, Europa): with no player unit on the start base, the CPU unit walked on, the 6 stored player OKEs were dismantled, the base changed owner 1 -> 2 and the game ended. With a player unit placed on the base, a real 3-on-3 battle ran; CPU tanks won (player OKEs HP 0; CPU OKEs 722 / 106 / 98 HP, kills 0 / 1 / 2).

## Order screens (mapped 2026-11-22)
Base menu (Tactics): Deploy Units, Form Unit, Factory, Unit Command, Base List.

**Form Unit** (`0x8006F120`). Picking OKEs (by hand or Auto) puts the unit together straight away in the first free player slot: each OKE goes in through `0x80071E34` (the same routine the CPU uses), gets OKE +01 = 1, and is loaded with main/sub ammo and fuel from the base stock (`0x80076044`). Leaving the screen commits the unit: owner 1, x/y = the base, +0A = 6, ammo 10000 and fuel 1000, both taken from the base stock. No funds are spent. The unit has no task yet.

**Deploy Units** and **Unit Command** open the same order menu for one unit: Move, Defend Base, Capture Base, Patrol, Standby. Deploy lists formed units still at a base; Unit Command lists units in the field (including ones on the move). Each order is a small handler that writes the unit at index `0x800DF408`, then calls `0x80034C38(unit)`, which recomputes the path (+06 for patrol, +0A, +0B):

| Order | Handler | Writes |
|---|---|---|
| Move | `0x8003A884` | +01 = 1, +06 = picked cell |
| Defend Base | `0x8003A8E8` | +01 = 2, +06 and +08 = picked base cell |
| Capture Base | `0x8003A97C` | +01 = 3, +06 = picked base cell |
| Patrol | `0x8003AA18` | +01 = 4 (the path code chose target cell 107 itself) |
| Standby | `0x8003AA78` | +01 = 0 |

The map picker is `0x8003AF54(kind, 1)` (kind 0x10 for Move, 1 for Defend, 2 for Capture); it returns the cell or -1 if cancelled.

Verified live: Capture on a unit at its base (task 3, target 107 = (11,4)); Patrol (task 4); and **re-ordering a unit on the move** with Unit Command at turn 2: Capture -> Defend wrote task 2, +06 = +08 = 200 (8,8), and the path bytes were recomputed (+0A 0 -> 3, +0B 176 -> 200). So a re-order is just these fields; a replay can write +01/+06/+08 and call `0x80034C38`, or copy +0A/+0B as well.

**Factory** (base menu): Production (`0x8005785C`: pick a product and an amount for a line, using a number pad), Add Line (`0x800593DC`: funds -24,900 here, base +58 +1), Upgrade (`0x8005A0FC`: funds -7,600 here, line level +1 and capacity 80 -> 100). Funds for production are only spent during End Turn.

## Not mapped yet
- Negotiation/research state.
- (Answered in VS_MODE_AND_SAVES.md: how the battle engine picks player or CPU designs for each side.)
