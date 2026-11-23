#!/bin/sh
# Builds the simulator and plays A against B on a temporary folder, twice:
# a straight game, then one where B closes the game after turn file 6 and
# starts again from scratch (it must catch up from the folder).
set -e
here=$(cd "$(dirname "$0")" && pwd); src="$here/../../../src"
bin="${TMPDIR:-/tmp}/ch-turnsim"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1 -I"$here/mock" -o "$bin" \
   "$here/sim.c" "$src/turn_files.c" "$src/hotseat.c" "$src/remote_battle.c"
turns=${1:-8}
play() {  # play <dir> <player> <turns> <log>
    CH_TURN_DIR="$1" CH_TURN_ROLE=$2 exec "$bin" $2 $3 > "$4" 2> "$4.err"
}
until_file() { while [ ! -f "$1" ]; do sleep 0.2; done; }

echo "== straight game"
d=$(mktemp -d)
play "$d" B $((turns + 1)) "$d/B.log" & pb=$!
play "$d" A $turns "$d/A.log" & wait $!
kill $pb 2>/dev/null || true; wait 2>/dev/null || true
python3 "$here/check.py" "$d/A.log" "$d/B.log"

echo "== B restarts after turn file 6"
d=$(mktemp -d)
play "$d" B 99 "$d/B1.log" & pb=$!
play "$d" A $turns "$d/A.log" & pa=$!
until_file "$d/turn-000006.cht"; sleep 0.5; kill $pb
play "$d" B 99 "$d/B2.log" & pb=$!
wait $pa; kill $pb 2>/dev/null || true; wait 2>/dev/null || true
first=$(grep -m1 'loaded turn file' "$d/B2.log.err" | sed 's/.*turn file \([0-9]*\).*/\1/')
echo "B came back and caught up with turn file $first"
python3 "$here/check.py" "$d/A.log" "$d/B2.log" $(( (first - 1) / 2 ))
grep -h 'DESYNC\|ERROR' "$d"/*.err && exit 1
grep -h 'END' "$d"/*.log
echo "logs in $d"
