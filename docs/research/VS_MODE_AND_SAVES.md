# Carnage Heart ReComp: VS Mode and Saves Map (USA, SCUS-94604)

Mapped 2026-11-22 on the Linux headless build, using the debug server (RAM dumps, write traces, profiler) and the executable. Builds on [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md). Verified = seen live.

## Headline result (verified)
**A campaign battle can be fought as a VS-mode battle**: player designs on both sides, the result written back into the map's OKE records by the game's own code. Tested at Europa turn 6 → 7: a CPU unit made of three MACHINE02 player designs fought the player's unit, and afterwards the game removed the dead OKEs, deleted the beaten CPU unit, kept the base and carried on. Now built as `src/remote_battle.c` (see "Remote-player battles" below) and tested with the CPU side attacking and with the player attacking.

## Disc layout facts
- **No code overlays.** One executable (SCUS_946.04) holds the campaign, VS mode and the battle engine.
- `\DATA\DEFRP.CRD` (3456 bytes) = the 3 starting designs MACHINE01-03, 1152 bytes each, same format as the player card table.
- Battle 3D models: `\MODEL\ETnn\EGnnA.PAC` ... `F.PAC`, nn = body type 01-12, A-F = colour variant. Name table at `0x800BB238` (index = (type-1)*6 + variant).
- Battlefields for VS: `\BMAP\B_STG01-05.DAT` (name table `0x800BB0CC`).
- `\DATA\EGPAC01-12.DAT` = enemy (CPU) data packs per scenario.

## Top-level game modes
State machine `0x8007DB50`, function table `0x80018AF4` (12 entries). Entry 0 title (`0x8007DC64`), entry 2 VS menu (`0x8007DD90` → `0x8007F540`), entry 3 VS "Loading Data" (`0x8007DE08` → `0x80080384`), later entries run the VS options/bracket and battle.

## VS mode, step by step (verified)
1. Title → VS BATTLE → **"Select a saved game."** Two stacks of 3 = memory card slot 1 and slot 2, 3 save numbers each. VS loads **campaign saves**, not single cards.
2. Picking a save shows its 28 designs → **"Select combatants."** The chosen designs are copied whole (hardware + chip program) into the **player card table** (`0x8012C97C`, the same table the campaign uses), in order from slot 0. You can go back and add designs from another save (another card or slot), up to 28 in the pool.
3. START → **match options**: Teams (One on One / Three on Three), Class (Unlimited / High / Low, by development cost), Contest (1 Match / Tournament), Match Time (50 / 100 / 150 (default) / 300 / 600 seconds).
4. "Select an OKE to enter" then "Select the competitor" → bracket screen (Cancel / Start). Entrant list at `0x80136FDC` (20-byte records, first word = card index). Not part of the save.
5. Start → VS battle set-up `0x800851A0`.

## The shared battle engine
| Function | What |
|---|---|
| `0x8001CF48` | The 3D battle itself. Used by the campaign, VS, the title demo and the Design screen's test battle. **Entered more than once per battle** (it runs in phases). |
| `0x80035CD4` | Campaign battle set-up (player unit vs CPU unit). Sets mode 1, stage 10, fills the combatant list from both units' OKE slots, calls `0x8001CF48`, sets mode back to 0 at the end. |
| `0x800851A0` | VS battle set-up. Sets mode 2, a random stage 0-4, fills the combatant list from the bracket. |
| `0x8002F574` → `0x8002F8F8` | Per-battle preparation and per-OKE loader (stats, weapons, chip program). |
| `0x80030A28` → `0x80031790` | Loads each OKE's 3D model file. |
| `0x80040CC0` → `0x80040F40` | Load a file by name; looks the name up in a cache of 153 known files, else searches the disc (loops forever on a name that doesn't exist). |

### Battle globals
| Address | What |
|---|---|
| `0x800DF3D0` | **Battle mode**: 0 none, 1 campaign, 2 VS (3 = another mode, seen in code) |
| `0x800DF3D4` | Battlefield: 0-4 = `B_STG01-05` (VS), 10 = the map's own terrain (campaign) |
| `0x800DF3B8` | Number of combatants |
| `0x800EF708` | **Combatant list**, 4 bytes each: +0 side (1 or 2), +2 OKE table index (s16) |
| gp+3680 (`0x800DFFB8`) | VS team size (1 or 3) |

### Where "CPU or player design" is decided
In campaign mode (mode 1) the engine treats side 2 as CPU in several places:
- `0x8002F8F8` reads side 2's card from the **CPU card table** (36-byte stat records) instead of the player table.
- `0x80030A28` gives side 2 model variant **F** (CPU colour); otherwise the variant is the design's colour byte (card +44, A-E).
- The **OKE owner byte** (OKE table +00) is used too: with owner 2 the pre-battle screen shows no OKE icon and 0% fuel even when the stats come from a player card. In VS both sides' OKEs have owner 1; only the combatant list says side 1 or 2.

### Remote-player battles (`src/remote_battle.c`, build with `-DCH_REMOTE_BATTLE=ON`)
Three hooks, all passing straight through unless a remote battle is under way:

| Hook | Step |
|---|---|
| `0x80035CD4` campaign battle set-up (a0 = side-1 unit, a1 = side-2 unit) | **Before**: copy the remote designs into spare player card slots (highest slots side 1 does not use in this battle), backing them up. Side 2's OKEs: owner 1, card index = borrowed slot, fresh HP/ammo from the design (same formula as `0x8005B118`). |
| `0x8001CF48` 3D battle | Set the mode to 2 (VS) and **leave it**; `0x80035CD4` writes 0 when the battle ends. |
| `0x80035988` result code (runs for both units after the battle) | **After**, on its first call: side 2's OKEs back to owner 2 with their original card index (HP, kills and ammo from the battle kept); borrowed card slots restored. Then the game's own result code runs. |

**Important framework detail**: the battle waits for frames inside `0x80035CD4`, and the recompiled code then *re-enters the same function* to resume it, with `cpu->pc` set to the resume address. So `0x80035CD4` "returns" to a hook long before the battle is fought, and a hook sees many extra calls. Hooks must act only on fresh calls (`cpu->pc == 0`) and do their "after" work from the next function the game calls once the battle is over. (The first version restored too early and side 2 fought as CPU tanks; another early version stalled because it set the mode back to 1 between calls.)

Run-time switches (test harness until the turn-file layer exists): `CH_REMOTE_BATTLE=1` treats every CPU unit as a remote player's unit; `CH_REMOTE_DESIGNS=<file>` gives the remote designs as 1152-byte records (the disc's `DATA/DEFRP.CRD` = MACHINE01-03), OKE n of the unit using record n; `CH_REMOTE_FRESH=0` keeps the OKEs' current HP/ammo instead of fresh values.

Battlefield: with mode 2 the campaign's stage value 10 still works (the map terrain).

**Tests (Europa, turn 6 → 7, remote designs MACHINE01-03):**
1. *CPU side attacks the player unit on the base.* Side 2 fought as the three remote designs (pre-battle screen shows the borrowed slots CARD-24/25/26 with their own ammo). Player won: two OKEs left (168 and 450 HP, 1 and 2 kills), all remote OKEs destroyed, the remote unit deleted, base kept. The borrowed slots came back byte for byte; the only card change was the game adding a loss to MACHINE03's record (+62), which is normal bookkeeping.
2. *Player attacks* (player unit moving onto the remote unit, player OKEs weakened to 60 HP). Remote side won: its OKEs survived at 468 / 477 / 520 HP with 1 kill each, went back to owner 2 with their original card index, the player unit was deleted and the remote unit went on to take the base.

**Known limit**: once survivors are back on the CPU side, the end-of-turn repair uses the CPU card's maximum (test 2: repaired to 680-732, above the design's 520). In campaign versus this is overwritten by the remote player's own turn file, but a later version could keep remote OKEs tied to their design between battles.

**Still to do**: drive the hook from the turn file (which units are remote, which designs, and the reported result on the receiving device) instead of the test switches.

### Correction to the first campaign mapping
The turn 6 → 7 base loss seen in the first mapping was **not a battle**. The player had no unit on the base, only 6 stored OKEs. When the CPU unit walked on, the base-capture code (`0x800358CC`) dismantled them (HP and owner cleared). A battle only happens when two **units** meet.

## Memory cards and saves (verified)
- Card files: the emulator keeps them in `build/saves/card1.mcd` and `card2.mcd` (128 KB each, standard PS1 card image), not in the `--memcard-dir` given on the command line.
- **3 save numbers per card.** Each save = 1 file of **5 blocks (40,960 bytes)**. Directory name `BISLPS-00151XXXX00` (the US game keeps the Japanese product code SLPS-00151). The last two digits are expected to change per save number (not yet checked).
- File header (first 0x200 bytes): `SC` magic, icon flag 0x13 (3-frame icon), 5 blocks, Shift-JIS title such as "CarnageHeart1/Prac/Map1/Turn1", icon palette and frames.
- **The body is a straight copy of RAM `0x8012C97C`-`0x8013677C`** (40,448 bytes; matched byte for byte). So the save is simply the campaign tables:

| Save offset | RAM | What |
|---|---|---|
| 0x0200 | 0x8012C97C | Player card table, 28 × 1152 bytes (name in Shift-JIS at +00, hardware from about +20, chip program from +80) |
| 0x8000 | 0x8013477C | CPU card table, 28 × 36 bytes |
| 0x83F0 | 0x80134B6C | Unit table, 64 × 22 bytes |
| 0x8970 | 0x801350EC | Base table, 16 × 92 bytes |
| 0x8F30 | 0x801356AC | OKE table, 12 bytes each |
| 0x9FB4 | 0x80136730 | Funds (copy kept in the block) and other campaign globals |

- **Checksum: none found.** Byte sums and XOR over the body did not match anything in the file. Not yet tested whether the game rejects an edited save.
- Save/load menu code: load `0x80078A24`, save `0x8007992C`; card paths `bu00:` / `bu10:`.

**What this means for the plans:**
- **Share Data / Option B:** one design = one 1152-byte record at a known offset; it can be exported and imported straight into a save's card table.
- **Online campaign:** a turn's full state is one 40 KB block that is already the save format, which makes "start from a known save" and resync easy.
- **Option A (live VS):** VS reads whole saves from card slots 1 and 2, so the netplay guest's own card as slot 2 should work as planned.

## Still to map
- Whether the save number changes the file name, and whether an edited save loads.
- Class limits (High/Low) and Tournament flow in VS.
- The meaning of OKE +08 (+0A/+0B are main and sub ammo).
