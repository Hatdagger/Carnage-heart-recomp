#!/bin/sh
# Builds the simulator and plays A against B on a temporary folder, twice:
# a straight game, then one where B closes the game after sending turn file 6 and
# starts again from scratch (it must catch up from the folder).
set -e
here=$(cd "$(dirname "$0")" && pwd); src="$here/../../../src"
bin="${TMPDIR:-/tmp}/ch-turnsim"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1 -I"$here/mock" -o "$bin" \
   -pthread "$here/sim.c" "$src/turn_files.c" "$src/turn_relay.c" "$src/hotseat.c" "$src/remote_battle.c"
turns=${1:-8}
play() {  # play <dir> <player> <turns> <log>
    CH_TURN_DIR="$1" CH_TURN_ROLE=$2 exec "$bin" $2 $3 > "$4" 2> "$4.err"
}

echo "== straight game"
d=$(mktemp -d)
play "$d" B $((turns + 1)) "$d/B.log" & pb=$!
play "$d" A $turns "$d/A.log" & wait $!
kill $pb 2>/dev/null || true; wait 2>/dev/null || true
python3 "$here/check.py" "$d/A.log" "$d/B.log"

echo "== B restarts after turn file 6"
d=$(mktemp -d)
play "$d" B 4 "$d/B1.log" & pb=$!  # quits after sending file 6 (and loading 7)
play "$d" A $turns "$d/A.log" & pa=$!
wait $pb
play "$d" B 99 "$d/B2.log" & pb=$!
wait $pa; kill $pb 2>/dev/null || true; wait 2>/dev/null || true
first=$(grep -m1 'loaded turn file' "$d/B2.log.err" | sed 's/.*turn file \([0-9]*\).*/\1/')
echo "B came back and caught up with turn file $first"
python3 "$here/check.py" "$d/A.log" "$d/B2.log" $(( (first - 1) / 2 ))
grep -h 'DESYNC\|ERROR' "$d"/*.err && exit 1
grep -h 'END' "$d"/*.log
echo "logs in $d"

# Online: each player has their own folder and the turn files go through the
# relay (relay/worker.js run locally by relay/dev_server.mjs). Needs Node 18+.
if command -v node >/dev/null 2>&1; then
    echo "== online, through a local relay"
    port=$((20000 + $$ % 20000)); R=http://127.0.0.1:$port
    node "$here/../../../relay/dev_server.mjs" $port > /dev/null 2>&1 & pr=$!
    sleep 1
    field() { python3 -c "import json,sys;print(json.load(sys.stdin)['$1'])"; }
    new=$(curl -s -X POST $R/v1/games); game=$(echo "$new" | field game); keyA=$(echo "$new" | field key)
    keyB=$(curl -s -X POST $R/v1/games/$game/join | field key)
    dA=$(mktemp -d); dB=$(mktemp -d)
    CH_TURN_RELAY=$R CH_TURN_GAME=$game CH_TURN_KEY=$keyB CH_TURN_DIR=$dB CH_TURN_ROLE=B \
        "$bin" B $((turns + 1)) > "$dB/B.log" 2> "$dB/B.log.err" & pb=$!
    CH_TURN_RELAY=$R CH_TURN_GAME=$game CH_TURN_KEY=$keyA CH_TURN_DIR=$dA CH_TURN_ROLE=A \
        "$bin" A $turns > "$dA/A.log" 2> "$dA/A.log.err"
    kill $pb $pr 2>/dev/null || true; wait 2>/dev/null || true
    python3 "$here/check.py" "$dA/A.log" "$dB/B.log"
    grep -h 'DESYNC\|ERROR' "$dA"/*.err "$dB"/*.err && exit 1
    echo "logs in $dA and $dB"
fi
