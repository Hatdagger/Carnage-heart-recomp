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
# A version-2 turn file 1 on Ganymede map 2 (header only: scenario byte 48,
# map byte 49), as if A's game had written it but could not send it.
code=$(cat "$d/a/code.txt")
mkdir -p "$d/a/online-turns/$code"
python3 -c 'import struct,sys; h=b"CHTF"+struct.pack("<HBBI",2,0,0,1)+bytes(36)+bytes([2,1,0,0]); open(sys.argv[1],"wb").write(h)' \
    "$d/a/online-turns/$code/turn-000001.cht"
(cd "$d/a" && "$bin" A3)
echo "pictures in $d/a/shots and $d/b/shots"
