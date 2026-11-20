#!/bin/sh
# One soak seed, C++ and JS side by side, then their input traces compared.
#   sh tests/soak/run_pair.sh SET_DIR_NAME TAG SEED [ACTIONS]
# SKIP_JS=1: run only the C++ soak, comparing with the JS trace already there.
# Needs build-b/soak (see tests/soak/build.sh). Logs and traces go to build-b/.
FW="${LEXICON_FIRMWARE:?set LEXICON_FIRMWARE to your firmware folder}"
set_dir="$1"; tag="$2"; seed="$3"; actions="${4:-300}"
base="build-b/soak_${tag}_s${seed}"
build-b/soak "$FW/$set_dir" ../web-demo/page/catalogs --actions "$actions" --seed "$seed" \
    --events "$base.cpp.events" > "$base.cpp.log" 2>&1 &
cpp=$!
if [ -z "$SKIP_JS" ]; then
    node tests/soak/soak_record.mjs "$FW/$set_dir" --actions "$actions" --seed "$seed" \
        --events "$base.js.events" > "$base.js.log" 2>&1
    js_status=$?
else
    js_status="(earlier run)"
fi
wait $cpp
cpp_status=$?
echo "== $set_dir seed $seed actions $actions"
echo "C++ (exit $cpp_status): $(tail -1 "$base.cpp.log")"
echo "JS  (exit $js_status): $(tail -1 "$base.js.log")"
if cmp -s "$base.cpp.events" "$base.js.events"; then
    echo "traces: identical ($(wc -l < "$base.cpp.events") lines)"
else
    echo "traces: DIFFER"; diff "$base.cpp.events" "$base.js.events" | head -5
fi
