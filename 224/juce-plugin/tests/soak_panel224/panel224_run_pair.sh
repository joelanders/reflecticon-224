#!/bin/sh
# one 224 soak seed, C++ (panel224_soak) and JS side by side,
# then their input traces compared. As ../soak/run_pair.sh.
#   sh tests/soak_panel224/panel224_run_pair.sh SET_DIR_NAME TAG SEED [ACTIONS] [JS]
# JS: stream-b (../soak/soak_record.mjs, rows-sv panel224.js; v4.3/v4.4 only)
#     or m4b (panel224_soak_record.mjs; any 224 set; the default).
# SKIP_JS=1: run only the C++ soak, comparing with the JS trace already there.
# Needs build-m4b/panel224_soak (see panel224_build.sh). Logs and traces go to build-m4b/.
FW="${LEXICON_FIRMWARE:?set LEXICON_FIRMWARE to your firmware folder}"
CATALOGS="../web-demo/page/catalogs:catalogs-extra"
set_dir="$1"; tag="$2"; seed="$3"; actions="${4:-300}"; js="${5:-m4b}"
base="build-m4b/soak_${tag}_s${seed}_a${actions}"
build-m4b/panel224_soak "$FW/$set_dir" "$CATALOGS" --actions "$actions" --seed "$seed" \
    --events "$base.cpp.events" > "$base.cpp.log" 2>&1 &
cpp=$!
if [ -z "$SKIP_JS" ]; then
    if [ "$js" = "stream-b" ]; then
        node tests/soak/soak_record.mjs "$FW/$set_dir" --actions "$actions" --seed "$seed" \
            --events "$base.js.events" > "$base.js.log" 2>&1
    else
        node tests/soak_panel224/panel224_soak_record.mjs "$FW/$set_dir" "$CATALOGS" --actions "$actions" \
            --seed "$seed" --events "$base.js.events" > "$base.js.log" 2>&1
    fi
    js_status=$?
else
    js_status="(earlier run)"
fi
wait $cpp
cpp_status=$?
echo "== $set_dir seed $seed actions $actions (JS: $js)"
echo "C++ (exit $cpp_status): $(tail -1 "$base.cpp.log")"
grep -h "^FAIL\|^HEALTH\|^RAM" "$base.cpp.log"
echo "JS  (exit $js_status): $(tail -1 "$base.js.log")"
grep -h "^FAIL" "$base.js.log"
if cmp -s "$base.cpp.events" "$base.js.events"; then
    echo "traces: identical ($(wc -l < "$base.cpp.events") lines)"
else
    echo "traces: DIFFER"; diff "$base.cpp.events" "$base.js.events" | head -5
fi
