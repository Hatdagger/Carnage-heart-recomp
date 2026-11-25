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
         codegen_setup.c codegen_setup.h README.md android_game.json; do
  [[ -e "${ROOT}/${f}" ]] && PROJECT_ARGS+=(--project-file "${f}")
done
for d in seeds src assets mods launcher cmake android scripts psxrecomp-android-kit; do
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

# The framework packager drops every directory named "generated" (to keep
# recompiled game C out), which also strips rabbitizer's generated headers.
# The player's PC rebuilds psxrecomp-game/-bios from these sources on first
# run, and without the headers that build fails ("generated/InstrId_enum.h
# file not found"), so setup stops before the game or OpenBIOS is built.
# Put the headers back; they are library source, not game code.
VERSION_STR="${RELEASE_VERSION:-$(tr -d '[:space:]' < "${ROOT}/VERSION")}"
KIT_ZIP="${ROOT}/dist/carnageheart-setup-${VERSION_STR}-${ARTIFACT}.zip"
[[ -f "${KIT_ZIP}" ]] || KIT_ZIP="$(ls "${ROOT}"/dist/carnageheart-setup-*-"${ARTIFACT}".zip | head -1)"
RABBITIZER_GEN=(
  psxrecomp/recompiler/lib/rabbitizer/include/generated
  psxrecomp/recompiler/lib/rabbitizer/cplusplus/include/generated
)
for d in "${RABBITIZER_GEN[@]}"; do
  [[ -d "${ROOT}/${d}" ]] || { echo "error: missing ${d} (submodule not checked out?)" >&2; exit 1; }
done
(cd "${ROOT}" && zip -r -q "${KIT_ZIP}" "${RABBITIZER_GEN[@]}")
echo "Added rabbitizer generated headers to $(basename "${KIT_ZIP}")"

# Android: "Build Android App" at the kit root builds CarnageHeart.apk from the
# installed game (scripts/android/build_android_app.py, which runs the
# PSXRecomp Android kit; android/, scripts/ and psxrecomp-android-kit/ are
# shipped above).
KIT_LAUNCHER_DIR="$(mktemp -d)"
case "${ARTIFACT}" in
  windows-*) cp "${ROOT}/scripts/android/kit/Build Android App.bat" "${KIT_LAUNCHER_DIR}/" ;;
  *)         cp "${ROOT}/scripts/android/kit/build-android-app.sh" "${KIT_LAUNCHER_DIR}/" ;;
esac
(cd "${KIT_LAUNCHER_DIR}" && zip -q "${KIT_ZIP}" ./*)
rm -rf "${KIT_LAUNCHER_DIR}"
echo "Added the Android app builder to $(basename "${KIT_ZIP}")"
