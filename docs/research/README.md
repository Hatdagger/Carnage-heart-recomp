# Research notes (branch `research/vs-battle-and-saves`)

Findings from mapping the game for multiplayer. Nothing here changes the normal build: the experiment is behind `-DCH_SIDE2_TEST=ON`, which is off by default.

| Doc | What |
|---|---|
| [CAMPAIGN_RAM_MAP.md](CAMPAIGN_RAM_MAP.md) | Campaign memory: units, bases, OKEs, cards, end-of-turn routine, battles |
| [VS_MODE_AND_SAVES.md](VS_MODE_AND_SAVES.md) | VS mode, the shared battle engine, memory card and save format, and the "campaign battle as VS battle" result |
| [MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md) | The plan for online campaign versus, live VS and card sharing |

## Reproducing the campaign-battle-as-VS test
1. Build with `-DCH_SIDE2_TEST=ON` and the debug server, then run headless from `build/`:
   `CH_SIDE2_PLAYER=1 ./CarnageHeart_Recompiled --headless --no-launcher --disc "../disc/Carnage Heart (USA).cue" --debug-port 4370`
   (On a Linux machine with no X11/Wayland, also configure with `-DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_OFFSCREEN=ON`.)
2. Start Game → Europa (Practice). End Turn until turn 6 (a CPU unit is then next to the player's start base at (8,8)).
3. Over the debug server (`tools/research/ch.py`):
   - Put a player unit on the base: write unit 0 (`0x80134B6C`) = `01 02 0800 0800 C800 C800 00 C8 0800 0900 0A00 1027 E803` (owner 1, defend, (8,8), OKEs 8/9/10).
   - Give OKEs 8, 9, 10 consumables 40 / 336 (OKE +08 / +0A).
   - For the CPU unit's 3 OKEs (unit 62's slots): owner byte = 1, card index = 26 (MACHINE02), HP / consumables = 520 / 40 / 336.
4. End Turn. The battle plays with MACHINE02 designs on the CPU side; the result is written to the OKE table and the game applies it.

## Tools (`tools/research/`)
Python helpers used for the mapping. Set `CH_REPO` to the repo (default: two folders up) and `CH_OUT` for screenshots (default `build/research`).
- `ch.py`: drive the game over the debug server (press buttons, screenshot, read/write RAM, dump RAM). Hold buttons 6-10 frames.
- `turns.py N`: end N turns and print the unit table.
- `xref.py LO HI`: code that touches an address range. `fx.py calls|dis ADDR`: a function's calls or disassembly. `cg.py ADDR...`: calls and callers. `ptr.py ADDR...`: pointers to an address.
- `iso.py BIN`, `ext.py BIN LBA SIZE OUT`: list and extract disc files. `un7z.py`: unpack the disc archive without 7-Zip. `diff.py`, `mdis.py`: RAM diff and MIPS disassembly helpers.
- Note: `fx.py` prints `addiu` offsets raw: `lui 0x800C` + `addiu 0xB238` means `0x800BB238`.

Debug server notes: savestate slots are 0-11; savestates don't load after a rebuild that changes code; `write_bulk` writes RAM; `wtrace_range` + `wtrace_dump` show who writes an address; `cdrom_sector_history` shows recent disc reads.
