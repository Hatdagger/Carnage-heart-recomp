# Carnage Heart ReComp: Campaign RAM Map (USA, SCUS-94604)

Mapped 2026-11-22 on a Linux headless build of the recomp, Europa (Practice) scenario, using the debug server (RAM dumps, write traces, savestates) and a static scan of the executable. Verified = seen in live RAM and in the code that writes it.

See also [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md) and [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md).

## Globals (gp = 0x800DF358)
| Address | gp+ | What | Status |
|---|---|---|---|
| 0x800DF37C | 36 | Turn counter (shown on screen), clamped 1..9999 | Verified |
| 0x800DF380 | 40 | Second turn counter, clamped 1..999 | Verified |
| 0x800DF38C | 52 | Player funds | Verified (500000 at start) |
| 0x800DF390 | 56 | CPU funds (drops when the CPU forms units; end-turn income added here, capped 9,999,999) | Very likely |
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
| +01 | u8 | Task: 0 none/done, 2 defend a base, 3 attack/capture a base (others not seen yet) |
| +02 / +04 | s16 | x / y |
| +06 | u16 | Target cell |
| +08 | u16 | Home/defend cell |
| +0A | u8 | Movement state |
| +0B | u8 | Next cell on the path (low byte) |
| +0C / +0E / +10 | s16 | The 3 OKE slots (OKE table index, -1 empty) |
| +12 | u16 | Ammo (10000 when formed) |
| +14 | u16 | Fuel (1000 when formed) |

## Base table (16 x 92 bytes)
| Offset | What |
|---|---|
| +00 | Owner: 0 none, 1 player, 2 CPU |
| +01 | Unknown (3 on the player's start base) |
| +02 / +04 | x / y |
| +08 | Ammo stock |
| +0C | Fuel stock (end-turn income added, capped 99,900) |
| +10..+5B | Production/factory lines (written by the CPU factory code 0x80055B00); not decoded yet |

## OKE table (12 bytes each)
| Offset | What |
|---|---|
| +00 | Owner (0 empty, 1, 2). Also decides player vs CPU card table and the battle's icon/fuel display |
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
5. Base income loop, then `0x80033E28`, `0x8004BBB8`, `0x800566F4`, per-unit resupply `0x80075A80`, `0x8007654C`.
6. Win/lose check: no player bases = lose; no CPU bases = win.

## Battles
- Started inside the move function `0x8003553C`: after a unit steps, if an enemy **unit** is on the same hex, it calls **`0x80035CD4(player-side unit, CPU-side unit)`**, which runs the 3D battle (`0x8001CF48`).
- Then **`0x80035988(unit)`** runs for both units: each OKE with HP of 0 or less is removed (`0x80035B4C` also adds kills and losses to the card records), and a unit with no OKEs left is deleted. If the moving unit lost, it goes back to its previous hex.
- Then base capture: if the unit is on a base hex, the base's owner byte changes (`0x800357B0`). OKEs stored at a base with no unit defending it are simply dismantled by `0x800358CC`; **no battle happens**.
- **The battle's entire result is the HP (and the 2 consumables and kills) of the 6 OKEs** in the OKE table. Everything after that (removing OKEs, deleting units, retreat, base capture) is done by the game's own code.

Observed (turn 6 -> 7, Europa): with no player unit on the start base, the CPU unit walked on, the 6 stored player OKEs were dismantled, the base changed owner 1 -> 2 and the game ended. With a player unit placed on the base, a real 3-on-3 battle ran; CPU tanks won (player OKEs HP 0; CPU OKEs 722 / 106 / 98 HP, kills 0 / 1 / 2).

## Not mapped yet
- Player order screens (Unit Command, Deploy, Form Unit): expected to write the same unit fields (+01 task, +06 target).
- Factory/production lines in the base record and the OKE stock per base.
- Negotiation/research state.
- (Answered in VS_MODE_AND_SAVES.md: how the battle engine picks player or CPU designs for each side.)
