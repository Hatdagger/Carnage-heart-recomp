#!/usr/bin/env bash
# Carnage Heart: build the Android app (CarnageHeart.apk) from your installed game.
cd "$(dirname "$0")" && exec python3 scripts/android/build_android_app.py "$@"
