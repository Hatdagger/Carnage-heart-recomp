# CarnageHeart Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/Hatdagger/Carnage-heart-recomp/total)](https://github.com/Hatdagger/Carnage-heart-recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/Hatdagger/Carnage-heart-recomp/latest/total)](https://github.com/Hatdagger/Carnage-heart-recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/Hatdagger/Carnage-heart-recomp)](https://github.com/Hatdagger/Carnage-heart-recomp/releases/latest)
<!-- /retcomm-readme-metrics -->

Static recompilation of **CarnageHeart** built on
[psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

Native PC port of Carnage Heart (USA, SCUS-94604), the Artdink game where you program your mechs (OKEs) with chip-based AI and send them into battle. Targets: Linux and Windows now; Android and browser planned.

This repository contains **no game data and no recompiled game code**. You generate the code yourself from your own disc.

| | |
|---|---|
| Players | 2 |
| Region | USA |
| Publisher | — |
| Year | — |

## Status

- Linux x64: boots on the bundled OpenBIOS (no Sony BIOS needed), reaches the title screen and the campaign.
- **PlayStation Mouse support restored.** The USA release had its mouse code removed; it is put back from *Carnage Heart EZ* (Japan) in `src/mouse_pad_hle.c`. Mouse movement and clicking work. List menus still act on the highlighted item rather than the item under the cursor (work in progress).

## Play on Windows (setup kit)

1. Download `carnageheart-setup-<version>-windows-x64.zip` from [Releases](https://github.com/Hatdagger/Carnage-heart-recomp/releases) and unzip it anywhere.
2. Put your own Carnage Heart (USA) Redump dump (the `.cue` and its `.bin` tracks) **beside `CarnageHeart_Recompiled.exe`** (or in a `disc` folder next to it).
3. Run `CarnageHeart_Recompiled.exe`. The first launch is the installer: it finds the disc (it only asks if the disc isn't there), downloads the portable compiler pack once, turns your disc into C and builds the game on your PC. This takes a while the first time.
4. Every launch after that opens the game's launcher directly.

No game data or recompiled code is downloaded; everything game-specific comes from your disc.

## Build from your own disc (developers)

You need your own Redump dump of Carnage Heart (USA): the `.cue` and all three `.bin` tracks.

```sh
git clone --recurse-submodules https://github.com/Hatdagger/Carnage-heart-recomp
cd Carnage-heart-recomp
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc "/path/to/Carnage Heart (USA).cue"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_NETPLAY=OFF -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
cmake --build build
./build/CarnageHeart_Recompiled --no-launcher --disc "/path/to/Carnage Heart (USA).cue"
```

Generation takes a few minutes and writes about 250 MB of C into `generated/`, which is not committed here. Set `CH_MOUSE_IMPL=LLE` to build the original controller routine without mouse support.

Scaffolded with the New Project Layout. See
`psxrecomp/docs/GAME_PROJECT_SETUP.md` for the full flow.

<!-- retcomm-readme-launcher -->
## Retro Launcher

You can run this title **standalone** (download the release zip, point it at
your disc, play), or manage installs, updates, and disc/BIOS wiring with
**[Retro Launcher](https://github.com/RetroPortingToolKit/Retro-Launcher)** —
the Retro Compilation Manager hub for self-compiling recomps.

[Downloads](https://github.com/RetroPortingToolKit/Retro-Launcher/releases) ·
[Full README & features](https://github.com/RetroPortingToolKit/Retro-Launcher#readme)

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/hub-and-game-launcher.png" alt="Retro hub with a background build, next to a title’s recomp-ui launcher" width="720">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/queue-and-background-build.png" alt="Background cmake build with titles queued" width="720">
</p>

Retro checks for updates, installs the prebuilt release zips, and automates
BIOS/ROM/save plumbing so you are not stuck repeating each game’s first run by hand.
<!-- /retcomm-readme-launcher -->

## Legal

You must own the original game. Disc images under `disc/` are gitignored and
must never be committed. Retail BIOS dumps are not redistributed and no C
derived from one may be committed; releases run on the bundled MIT OpenBIOS.

`generated/` (the recompiled game C) is **not** committed. Releases are setup kits built by the *Setup kit* workflow (`.github/workflows/setup-kit.yml`) with no disc; each player's first launch generates and compiles the game locally.

Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`) — Retro-themed controller mark from `psxrecomp/assets/`. Windows builds embed it via `APP_ICON`.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Quick start (dev)

```bash
git submodule update --init --recursive
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc disc/<your>.cue
git add generated && git commit -m "Regenerate game C"
# Configures, builds, and stages overlay_toolchain/ beside the runtime so
# streamed overlays can be compiled to native shards (a bare cmake build
# skips that stage and leaves every overlay gap interpreted).
python3 psxrecomp/psxrecomp_cli.py rebuild \
  --config game.toml --project-root . --build-dir build-release --no-pgo
```

Official optimized Windows/Linux releases use the maintained shared builder
with the title disc available. Commit the desired `VERSION`, source and
framework pins, then run:

```bash
scripts/package_release.sh --channel public
# Use --channel playtest for a non-public package; --platform selects Windows/Linux.
```

On Windows PowerShell, use
`psxrecomp/tools/package_title_release.ps1 --channel public`. The generated
`packaging/release.toml` and player instructions define the title payload.
The shared builder owns regeneration, qualified build defaults, fresh private
PGO boot training on supported native toolchains, staging and verification.
Boot training is a build step, not gameplay qualification or a performance
claim. See `psxrecomp/docs/RELEASING_A_TITLE.md`.

The separate *Release builds* CI workflow (tag `vX.Y.Z` or dispatch) builds
committed `generated/` C without a disc on Linux, Windows and macOS. It uses
`scripts/package_prebuilt_release.sh` and attaches compiled-game ZIPs. That
explicit prebuilt path does not train selective PGO and is not the official
optimized-release command. The common builder does not add macOS support.

Studio's current Export action still uses the old prebuilt argument contract;
use the shared command above for optimized releases until Studio adopts the
recipe/channel interface. Its packager detection and `packaging/` warning also
need that adoption; the generated recipe is maintained configuration, not a
forked packager.

## Symbols

Progressive map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately — do not float on `main`/`master`
in release CI.

<!-- retcomm-readme-raid -->
---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
<!-- /retcomm-readme-raid -->
