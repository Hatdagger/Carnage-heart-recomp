#!/bin/sh
# Builds menu_shots and drives the launcher's ONLINE CAMPAIGN pages against a
# local relay: player A makes a game and presses PLAY, player B joins it by
# typing the code, then A opens the launcher again. Pictures of each page go
# to <tmp>/a/shots and <tmp>/b/shots. Needs a C++ compiler, Node and curl.
set -e
here=$(cd "$(dirname "$0")" && pwd); repo="$here/../../.."
imgui="$repo/recomp-ui/src/third_party/imgui"
bin="${TMPDIR:-/tmp}/ch-menu-shots"
c++ -std=c++17 -O1 -w -DCH_ONLINE_CAMPAIGN=1 -DCH_TURN_RELAY_DEFAULT='"http://127.0.0.1:8788"' \
    -I"$imgui" -I"$repo/launcher" -I"$repo/recomp-ui/src/third_party" -o "$bin" -pthread \
    "$here/menu_shots.cpp" "$repo/launcher/ch_online.cpp" \
    "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_widgets.cpp" "$imgui/imgui_tables.cpp"
export no_proxy=127.0.0.1 NO_PROXY=127.0.0.1
node "$repo/relay/dev_server.mjs" 8788 > /dev/null 2>&1 & pr=$!
trap 'kill $pr 2>/dev/null' EXIT
sleep 1
d=$(mktemp -d); mkdir -p "$d/a/shots" "$d/b/shots"
(cd "$d/a" && "$bin" A) && (cd "$d/b" && "$bin" B) && (cd "$d/a" && "$bin" A2)
echo "pictures in $d/a/shots and $d/b/shots"
