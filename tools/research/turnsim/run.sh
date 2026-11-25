#!/bin/sh
# Builds the simulator and plays A against B on a temporary folder:
#   - a straight game;
#   - B closes the game after loading turn file 7 and starts again: the map
#     start must catch up with file 7 (B's turn) without an End Turn;
#   - B closes the game while waiting, right after sending file 6, and starts
#     again: the map start must load B's own file 6 and the first End Turn
#     only waits for A's file 7;
#   - online through a local relay, then B on a "new computer" (empty folder)
#     catching up from the relay.
set -e
here=$(cd "$(dirname "$0")" && pwd); src="$here/../../../src"
bin="${TMPDIR:-/tmp}/ch-turnsim"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1 -I"$here/mock" -o "$bin" \
   -pthread "$here/sim.c" "$src/turn_files.c" "$src/turn_relay.c" "$src/hotseat.c" "$src/remote_battle.c" "$src/turn_replay.c"
turns=${1:-8}
play() {  # play <dir> <player> <turns> <log>
    CH_TURN_DIR="$1" CH_TURN_ROLE=$2 exec "$bin" $2 $3 > "$4" 2> "$4.err"
}
# same_state <log1> <tag1> <log2> <tag2>: the last "<tag> mine" lines match.
same_state() {
    x=$(grep "^$2 B mine" "$1" | tail -1 | cut -d' ' -f3-); y=$(grep "^$4 B mine" "$3" | tail -1 | cut -d' ' -f3-)
    if [ -n "$x" ] && [ "$x" = "$y" ]; then echo "ok   B's own state came back after the restart"
    else echo "FAIL B's own state after the restart"; echo "   left: $x"; echo "   back: $y"; exit 1; fi
}

echo "== straight game"
d=$(mktemp -d)
play "$d" B $((turns + 1)) "$d/B.log" & pb=$!
sleep 0.3   # B's map opens before A has played turn 1
play "$d" A $turns "$d/A.log" & wait $!
kill $pb 2>/dev/null || true; wait 2>/dev/null || true
python3 "$here/check.py" "$d/A.log" "$d/B.log"

# The scenario check: the wrong scenario loads and sends nothing, and the
# map is then a normal campaign (the CPU brain runs again: exit code 3).
echo "== the wrong scenario"
n_before=$(ls "$d"/turn-*.cht | wc -l)
st=0; SIM_SCENARIO=1 CH_TURN_DIR="$d" CH_TURN_ROLE=B "$bin" B 2 > "$d/W.log" 2> "$d/W.log.err" || st=$?
grep -q 'STATUS This online game is on Europa (Practice). Restart and choose Europa (Practice).' "$d/W.log" \
    && grep -q 'online play is off' "$d/W.log.err" && [ "$st" = 3 ] \
    && [ "$(ls "$d"/turn-*.cht | wc -l)" = "$n_before" ] && ! grep -q 'loaded' "$d/W.log.err" \
    || { echo "FAIL a copy on Europa joined a Europa (Practice) game"; cat "$d/W.log" "$d/W.log.err"; exit 1; }
echo "ok   a copy started on Europa was told to start Europa (Practice), and loaded and sent nothing"
d2=$(mktemp -d); st=0
SIM_SCENARIO=2 CH_TURN_DIR="$d2" CH_TURN_ROLE=A "$bin" A 2 > "$d2/W.log" 2> "$d2/W.log.err" || st=$?
grep -q 'STATUS Online games are Europa (Practice) only for now' "$d2/W.log" && [ "$st" = 3 ] \
    && ! ls "$d2"/turn-*.cht > /dev/null 2>&1 \
    || { echo "FAIL a new online game started on Ganymede"; cat "$d2/W.log" "$d2/W.log.err"; exit 1; }
echo "ok   a new online game on Ganymede was refused, and no turn file was written"

echo "== B restarts after loading turn file 7"
d=$(mktemp -d)
play "$d" B 4 "$d/B1.log" & pb=$!  # quits after sending file 6 (and loading 7)
sleep 0.3
play "$d" A $turns "$d/A.log" & pa=$!
wait $pb
play "$d" B 99 "$d/B2.log" & pb=$!
wait $pa; kill $pb 2>/dev/null || true; wait 2>/dev/null || true
grep -q 'map start: caught up with turn file 7' "$d/B2.log.err" || { echo "FAIL B2 did not catch up at the map start"; exit 1; }
first=$(grep -m1 'loaded turn file' "$d/B2.log.err" | sed 's/.*turn file \([0-9]*\).*/\1/')
echo "B came back and caught up with turn file $first when the map opened"
grep -q 'map start: Europa (Practice), as in the turn files' "$d/B2.log.err" \
    || { echo "FAIL B's map start did not check the scenario"; exit 1; }
echo "ok   B's map start checked the scenario against the turn files"
python3 "$here/check.py" "$d/A.log" "$d/B2.log" $(( (first - 1) / 2 ))
grep -h 'DESYNC\|ERROR' "$d"/*.err && exit 1
grep -h 'END' "$d"/*.log
echo "logs in $d"

echo "== B closes the game right after sending turn file 6"
d=$(mktemp -d)
SIM_QUIT_WAITING=1 CH_TURN_DIR="$d" CH_TURN_ROLE=B "$bin" B 4 > "$d/B1.log" 2> "$d/B1.log.err" & pb=$!
sleep 0.3
SIM_DELAY_MS=1500 CH_TURN_DIR="$d" CH_TURN_ROLE=A "$bin" A $turns > "$d/A.log" 2> "$d/A.log.err" & pa=$!
wait $pb
grep QUIT "$d/B1.log"
play "$d" B 99 "$d/B2.log" & pb=$!
wait $pa; kill $pb 2>/dev/null || true; wait 2>/dev/null || true
grep -q 'map start: my turn file 6 is the newest' "$d/B2.log.err" || { echo "FAIL B2 did not load its own turn file 6"; exit 1; }
same_state "$d/B1.log" PRE "$d/B2.log" OWN
first=$(grep -m1 'loaded turn file' "$d/B2.log.err" | sed 's/.*turn file \([0-9]*\).*/\1/')
echo "B's first End Turn after the restart waited for and loaded turn file $first"
python3 "$here/check.py" "$d/A.log" "$d/B2.log" $(( (first - 1) / 2 ))
grep -h 'DESYNC\|ERROR' "$d"/*.err && exit 1
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
        "$bin" B 4 > "$dB/B.log" 2> "$dB/B.log.err" & pb=$!
    sleep 0.5
    CH_TURN_RELAY=$R CH_TURN_GAME=$game CH_TURN_KEY=$keyA CH_TURN_DIR=$dA CH_TURN_ROLE=A \
        "$bin" A $turns > "$dA/A.log" 2> "$dA/A.log.err" & pa=$!
    wait $pb
    # B again, on a new computer: an empty folder, everything from the relay.
    dB2=$(mktemp -d)
    CH_TURN_RELAY=$R CH_TURN_GAME=$game CH_TURN_KEY=$keyB CH_TURN_DIR=$dB2 CH_TURN_ROLE=B \
        "$bin" B 99 > "$dB2/B.log" 2> "$dB2/B.log.err" & pb=$!
    wait $pa
    kill $pb $pr 2>/dev/null || true; wait 2>/dev/null || true
    python3 "$here/check.py" "$dA/A.log" "$dB/B.log"
    grep -q 'caught up: the relay has 7' "$dB2/B.log.err" && grep -q 'map start: caught up with turn file 7' "$dB2/B.log.err" \
        || { echo "FAIL B on a new computer did not catch up from the relay"; exit 1; }
    echo "B on a new computer fetched turn files 1-7 from the relay and caught up when the map opened"
    python3 "$here/check.py" "$dA/A.log" "$dB2/B.log" 3
    grep -h 'DESYNC\|ERROR' "$dA"/*.err "$dB"/*.err "$dB2"/*.err && exit 1
    grep -h 'Your turn is sent' "$dA/A.log" | head -1
    echo "logs in $dA, $dB and $dB2"
fi
