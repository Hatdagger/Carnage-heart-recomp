#!/usr/bin/env bash
# Build the Android app (APK) from your installed game (PSXRecomp Android kit).
# Copy this file to the root of the game's setup kit.
cd "$(dirname "$0")" && exec python3 psxrecomp-android-kit/scripts/build_android_app.py "$@"
