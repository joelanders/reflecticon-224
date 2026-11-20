#!/usr/bin/env bash
# Measure one firmware set's stored(control value) tables, one process per
# program, JOBS at a time (default 8), then write the sidecar.
#
#   tools/measure_stored_all.sh ROM_SET_DIR CATALOG_JSON OUT_DIR [JOBS] [-- measure_stored options]
#
# OUT_DIR gets <key>.txt per program (tools/measure_stored's lines); a
# program whose file already ends in "# done" is not measured again, so an
# interrupted run can be resumed. When every program is done:
#   python3 tools/gen_stored_tables.py CATALOG_JSON OUT_DIR/*.txt \
#       --out catalogs-extra/stored/<hash>.stored.json
# (run from juce-plugin; builds build-i/measure_stored if missing).
#
# How the sidecars in catalogs-extra/stored/ were made (each file's "method"):
#   224 v4.3/v4.4/v3.2/TEST  -- --down                       (up and down, every reading)
#   224X v8.1                 (defaults: up), then -- --down-only
#   224XL v8.21/v8.1A         -- --down --settle 0.005 --still 0.02
#                             (8.21's SIZE re-measured at --still 0.25; see the file)
# then every other variation with tools/measure_stored_variations.sh
# (-- --down --step 4 ...), and the sliders whose samples differed swept
# again at --step 1 in that variation.
set -euo pipefail
PLUGIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PLUGIN_DIR"
if [[ $# -lt 3 ]]; then
	sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
fi
SET_DIR="$1"; CATALOG="$2"; OUT="$3"; shift 3
JOBS=8
if [[ $# -gt 0 && "$1" != "--" ]]; then JOBS="$1"; shift; fi
if [[ $# -gt 0 && "$1" == "--" ]]; then shift; fi
mkdir -p "$OUT" build-i
if [[ ! -x build-i/measure_stored || tools/measure_stored.cpp -nt build-i/measure_stored ]]; then
	clang++ -std=c++20 -O2 -ffp-contract=off -I ../analog tools/measure_stored.cpp -o build-i/measure_stored
fi
keys="$(python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for p in d["programs"]:
    if isinstance(p.get("identity"), int) and d.get("remote") != "larc":
        print("x%x" % p["identity"])
    else:
        print("%d.%d" % (p["bank"], p["program"]))
' "$CATALOG")"
export SET_DIR CATALOG OUT
for key in $keys; do
	if grep -q '^# done' "$OUT/$key.txt" 2>/dev/null; then continue; fi
	echo "$key"
done | xargs -P "$JOBS" -I{} sh -c './build-i/measure_stored "$SET_DIR" "$CATALOG" --program {} '"$*"' > "$OUT/{}.txt" 2>&1 || true'
missing=0
for key in $keys; do
	if ! grep -q '^# done' "$OUT/$key.txt" 2>/dev/null; then echo "not done: $key" >&2; missing=1; fi
done
exit $missing
