#!/usr/bin/env bash
# Tests the Android app's relay code (android/java/.../Relay.java) on a PC:
# create, join, the game's nativeGet/nativePut, the Online screen's check()
# (send unsent turns, fetch missing ones, new device, game over), games.txt.
# A stand-in android.content.Context (fake/) lets it run on a desktop JVM.
#   tools/research/android_relay_test/run.sh                 # local relay (Node 18+)
#   tools/research/android_relay_test/run.sh https://...     # a real relay
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; root="$here/../../.."
work="$(mktemp -d)"
javac -nowarn -d "$work/cls" "$here/fake/android/content/Context.java" \
  "$root/android/java/com/hatdagger/carnageheart/Relay.java" \
  "$here/test/com/hatdagger/carnageheart/RelayTest.java"
url="${1:-}"
if [ -z "$url" ]; then
  node "$root/relay/dev_server.mjs" 8791 > "$work/relay.log" 2>&1 & pid=$!
  trap 'kill $pid' EXIT; sleep 1; url=http://127.0.0.1:8791
fi
java -cp "$work/cls" com.hatdagger.carnageheart.RelayTest "$url" "$work/run"
