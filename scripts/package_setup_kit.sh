#!/usr/bin/env bash
# Carnage Heart self-compiling kit (what players download).
#
# The repo holds no recompiled game code, so the release is a SETUP KIT: the
# zip-root CarnageHeart_Recompiled(.exe) is a launcher + setup wizard built
# with no game C. On first launch it finds the disc beside the exe (or asks),
# downloads the portable compiler pack, generates the game C from the disc and
# builds the game into build-release/. Every later launch goes straight to the
# built game.
#
# Usage: scripts/package_setup_kit.sh <setup-host-build-dir> <artifact-tag> [recompiler-build-dir]
# Writes: dist/carnageheart-setup-<VERSION>-<artifact-tag>.zip
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:?usage: $0 <build-dir> <artifact-tag> [recompiler-build-dir]}"
ARTIFACT="${2:?usage: $0 <build-dir> <artifact-tag> [recompiler-build-dir]}"
RECOMPILER_BUILD="${3:-build-recompiler}"

PACKAGER="${ROOT}/psxrecomp/tools/package_setup_host.sh"
[[ -f "${PACKAGER}" ]] || { echo "error: missing ${PACKAGER}" >&2; exit 1; }

# Vendor the source archives the player's build would otherwise download
# (SDL3, zlib, libwebp; libchdr is committed) so setup works behind firewalls.
bash "${ROOT}/psxrecomp/tools/ci/vendor_deps.sh" SDL3 psx_zlib psx_libwebp

PROJECT_ARGS=()
for f in CMakeLists.txt game.toml game_options.toml VERSION symbols.toml \
         psx_symbols.h catalog_identity.json disc_probe.json \
         codegen_setup.c codegen_setup.h README.md; do
  [[ -e "${ROOT}/${f}" ]] && PROJECT_ARGS+=(--project-file "${f}")
done
for d in seeds src assets mods; do
  [[ -d "${ROOT}/${d}" ]] && PROJECT_ARGS+=(--project-dir "${d}")
done

cd "${ROOT}"
bash "${PACKAGER}" \
  --root "${ROOT}" \
  --build-dir "${BUILD_DIR}" \
  --artifact "${ARTIFACT}" \
  --zip-prefix carnageheart-setup \
  --exe-name CarnageHeart_Recompiled \
  --display-name "CarnageHeart Recompiled" \
  --recompiler-build "${RECOMPILER_BUILD}" \
  --version-env RELEASE_VERSION \
  --disc-hint "your Carnage Heart (USA) disc image (.cue + .bin), placed beside the exe" \
  --ship-without-overlay-cache-because "Carnage Heart has no code overlays; the whole game is compiled on first launch" \
  "${PROJECT_ARGS[@]}"
