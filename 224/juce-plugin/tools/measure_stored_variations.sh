#!/usr/bin/env bash
# Measure every variation other than 1 of every program of one firmware set
# (tools/measure_stored --variation V), one process per (program, variation),
# JOBS at a time; OUT_DIR gets <key>.v<V>.txt. Resumable (a file ending in
# "# done" is kept). Then give these files to tools/gen_stored_tables.py
# together with the variation-1 sweeps: a slider whose tables differ in a
# variation gets a per-variation entry.
#
#   tools/measure_stored_variations.sh ROM_SET_DIR CATALOG_JSON OUT_DIR [JOBS] [-- measure_stored options]
set -euo pipefail
PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PLUGIN_DIR"
if [[ $# -lt 3 ]]; then
	sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
fi
SET_DIR="$1"; CATALOG="$2"; OUT="$3"; shift 3
JOBS=8
if [[ $# -gt 0 && "$1" != "--" ]]; then JOBS="$1"; shift; fi
if [[ $# -gt 0 && "$1" == "--" ]]; then shift; fi
mkdir -p "$OUT"
pairs="$(python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for p in d["programs"]:
    if isinstance(p.get("identity"), int) and d.get("remote") != "larc":
        key = "x%x" % p["identity"]
    else:
        key = "%d.%d" % (p["bank"], p["program"])
    for v in p["variations"]:
        if v != 1:
            print("%s %d" % (key, v))
' "$CATALOG")"
missing=0
while read -r key v; do
	[[ -z "$key" ]] && continue
	file="$OUT/$key.v$v.txt"
	if grep -q '^# done' "$file" 2>/dev/null; then continue; fi
	while [[ $(jobs -r | wc -l) -ge $JOBS ]]; do sleep 5; done
	./build-i/measure_stored "$SET_DIR" "$CATALOG" --program "$key" --variation "$v" "$@" > "$file" 2>&1 &
done <<< "$pairs"
wait
while read -r key v; do
	[[ -z "$key" ]] && continue
	if ! grep -q '^# done' "$OUT/$key.v$v.txt" 2>/dev/null; then echo "not done: $key v$v" >&2; missing=1; fi
done <<< "$pairs"
exit $missing
