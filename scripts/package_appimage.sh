#!/usr/bin/env bash
# Carnage Heart AppImage (Steam Deck and other Linux): the Linux setup kit in
# one file. packaging/appimage/AppRun copies the kit into a writable folder on
# first start and runs it from there (an AppImage is read-only; the kit builds
# the game from the player's disc into its own folder).
#
# Usage: scripts/package_appimage.sh <linux kit zip> [appimagetool]
# Writes: dist/CarnageHeart_Recompiled-<VERSION>-x86_64.AppImage
# appimagetool: path to appimagetool (default: appimagetool on PATH). In CI
# it runs with APPIMAGE_EXTRACT_AND_RUN=1 (no FUSE needed).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KIT_ZIP="$(cd "$(dirname "${1:?usage: $0 <linux kit zip> [appimagetool]}")" && pwd)/$(basename "$1")"
TOOL="${2:-appimagetool}"

WORK="${ROOT}/build-appimage"
APPDIR="${WORK}/CarnageHeart.AppDir"
rm -rf "${WORK}"
mkdir -p "${APPDIR}/usr/share/carnageheart/kit"

unzip -q "${KIT_ZIP}" -d "${APPDIR}/usr/share/carnageheart/kit"
KIT="${APPDIR}/usr/share/carnageheart/kit"
# The kit zip may hold one top-level folder; use its contents.
if [[ ! -f "${KIT}/game.toml" ]]; then
  inner="$(find "${KIT}" -mindepth 2 -maxdepth 2 -name game.toml -printf '%h\n' | head -1)"
  [[ -n "${inner}" ]] || { echo "error: no game.toml in ${KIT_ZIP}" >&2; exit 1; }
  tmp="${WORK}/kit-inner"; mv "${inner}" "${tmp}"; rm -rf "${KIT}"; mv "${tmp}" "${KIT}"
fi
[[ -x "${KIT}/CarnageHeart_Recompiled" ]] || chmod +x "${KIT}/CarnageHeart_Recompiled"
[[ -f "${KIT}/VERSION" ]] || { echo "error: kit has no VERSION" >&2; exit 1; }

install -m 755 "${ROOT}/packaging/appimage/AppRun" "${APPDIR}/AppRun"
install -m 644 "${ROOT}/packaging/appimage/carnageheart.desktop" "${APPDIR}/carnageheart.desktop"
install -m 644 "${ROOT}/packaging/appimage/get_toolchain.py" "${APPDIR}/usr/share/carnageheart/get_toolchain.py"
install -m 644 "${ROOT}/assets/psxrecomp.png" "${APPDIR}/carnageheart.png"
ln -sf carnageheart.png "${APPDIR}/.DirIcon"

VER="$(tr -d '[:space:]' < "${KIT}/VERSION")"
mkdir -p "${ROOT}/dist"
OUT="${ROOT}/dist/CarnageHeart_Recompiled-${VER}-x86_64.AppImage"
rm -f "${OUT}"
ARCH=x86_64 "${TOOL}" --no-appstream "${APPDIR}" "${OUT}"
chmod +x "${OUT}"
echo "wrote ${OUT}"
