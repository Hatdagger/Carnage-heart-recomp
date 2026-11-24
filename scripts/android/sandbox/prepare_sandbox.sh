#!/usr/bin/env bash
# Prepare a no-NDK APK build (Claude build sandbox). Run from the repo root
# of a clone of the android branch, with gh authenticated for this repo.
#   scripts/android/sandbox/prepare_sandbox.sh WORK_DIR
# Downloads the android-devkit draft release and the v0.1.2 Linux kit,
# fills psxrecomp/ and recomp-ui/ from the kit, makes the clang resource dir.
# Then: generate game C from the disc and run build_apk.py (see README.md).
set -euo pipefail
WORK="$(mkdir -p "${1:?WORK_DIR}" && cd "$1" && pwd)"
REPO=Hatdagger/carnage-heart-recomp
asset() { # tag name -> file
  local id
  id=$(gh api "repos/$REPO/releases" --jq ".[] | select(.tag_name==\"$1\") | .assets[] | select(.name==\"$2\") | .id")
  [ -n "$id" ] || { echo "asset $2 not found in $1" >&2; exit 1; }
  gh api -H "Accept: application/octet-stream" "repos/$REPO/releases/assets/$id" > "$WORK/$2"
}
[ -f "$WORK/android-devkit.tar.xz" ] || asset android-devkit android-devkit.tar.xz
[ -f "$WORK/kit.zip" ] || { asset v0.1.2 carnageheart-setup-0.1.2-linux-x64.zip; mv "$WORK/carnageheart-setup-0.1.2-linux-x64.zip" "$WORK/kit.zip"; }
[ -d "$WORK/android-devkit" ] || tar -C "$WORK" -xJf "$WORK/android-devkit.tar.xz"
[ -d "$WORK/kit" ] || unzip -q "$WORK/kit.zip" -d "$WORK/kit"
for d in psxrecomp recomp-ui; do
  if [ -z "$(ls -A "$d" 2>/dev/null)" ]; then rm -rf "$d"; cp -a "$WORK/kit/$d" "$d"; fi
done
RES="$WORK/ndkres"
mkdir -p "$RES/lib/linux"
HOSTINC="$(clang -print-resource-dir)/include"
cp -a "$HOSTINC" "$RES/"
cp -a "$WORK/android-devkit/clang-rt/"* "$RES/lib/linux/"
echo "export ANDROID_DEVKIT=$WORK/android-devkit NDK_RESOURCE_DIR=$RES"
