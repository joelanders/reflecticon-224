#!/bin/sh
# ROM discovery and recognition tests.
#   sh tests/roms/run_tests.sh FIRMWARE_DIR SCRATCH_DIR [--boot]
# FIRMWARE_DIR: ".../Lexicon 224 and 224X Firmware" (one folder per set).
# SCRATCH_DIR: outside the repo; fixtures (copies of the user's chips) go there.
# Needs build-c/ built (see tests/roms/CMakeLists.txt) and node.
set -e
cd "$(dirname "$0")/../.."
FW="$1"; S="$2"; BOOT="$3"
sh tests/roms/make_fixtures.sh "$FW" "$S/fixtures" >/dev/null
F="$S/fixtures"
set -- "all=$FW"
for d in "$FW"/*/; do n=$(basename "$d" | tr ' ' '_'); set -- "$@" "set_$n=$d"; done
set -- "$@" "zip=$F/xl821_nested.zip" "mixed=$F/mixed" "dups=$F/dups" "wrong=$F/wrong" "v22=$F/v22" "nothing=$F/nothing"

node tests/roms/web_reference.mjs "$@" > "$S/web.txt"
build-c/test_rom_library_artefacts/Release/test_rom_library "$S/home" "$FW/224 v4.3" "$F/xl821_nested.zip" "$F/dups" "$F/nothing" "$@" > "$S/juce_full.txt" || { cat "$S/juce_full.txt"; exit 1; }
grep -v '^scan \|^  message\|^drop folder\|^PASS' "$S/juce_full.txt" > "$S/juce.txt"
grep '^scan \|^  message' "$S/juce_full.txt"
# The core harness cannot read zips: compare it on the non-zip groups only.
build-c/test_roms_core $BOOT "$@" > "$S/core_full.txt" || { cat "$S/core_full.txt"; exit 1; }
grep -v '^#\|^PASS\|^boot' "$S/core_full.txt" > "$S/core.txt"
grep '^boot' "$S/core_full.txt" || true
grep -v '^zip\|^mixed' "$S/web.txt" > "$S/web_nozip.txt"
grep -v '^zip\|^mixed' "$S/core.txt" > "$S/core_nozip.txt"
diff "$S/web.txt" "$S/juce.txt" && echo "JUCE layer == web findSets on $(wc -l < "$S/web.txt") lines"
diff "$S/web_nozip.txt" "$S/core_nozip.txt" && echo "core == web findSets on $(wc -l < "$S/web_nozip.txt") lines (non-zip groups)"
echo; cat "$S/web.txt"
