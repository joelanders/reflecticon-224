#!/bin/sh
# pgo_train.sh — a clang PGO profile for the machine, from the speed gate on
# your own ROMs. Writes pgo/plugin.profdata, and its text form pgo/plugin.proftext,
# which is committed: the release workflow indexes it with the runner's own
# llvm-profdata (the text form reads under any LLVM version). Locally,
# configure a release build with
#
#   cmake -S . -B build-pgo -G Ninja -DLEXPLUG_PGO_PROFILE="$PWD/pgo/plugin.profdata" ...
#
#   scripts/pgo_train.sh FIRMWARE_ROOT [SECONDS]
#
# FIRMWARE_ROOT holds the set folders ("224XL v8_21", "224X v8_1", "224 v4.3");
# each present set is run for SECONDS (default 30) of steady state on its first
# timeline. The gate is compiled standalone, instrumented, with the plugin's
# machine flags (-O3 -std=c++20 -ffp-contract=off): the profile keys on the
# machine's functions, which the plugin's translation units share. The .profdata
# holds function names and counts, no ROM bytes. Of pgo/, only the text
# profile is committed.
# macOS only (Apple clang and the AudioToolbox framework the gate links).
set -eu
cd "$(dirname "$0")/.."
FW="${1:?usage: scripts/pgo_train.sh FIRMWARE_ROOT [SECONDS]}"
SECONDS_PER_SET="${2:-30}"
mkdir -p pgo
clang++ -std=c++20 -O3 -g -DNDEBUG -ffp-contract=off -fprofile-instr-generate \
    -Wno-deprecated-declarations -o pgo/speed_gate_instrumented bench/speed_gate.cpp \
    -framework AudioToolbox -framework CoreFoundation
rm -f pgo/*.profraw
n=0
for pair in "224XL v8_21:xl821_1" "224X v8_1:x81_1" "224 v4.3:v43_1"; do
    set_dir="${pair%%:*}"; timeline="${pair##*:}"
    if [ ! -d "$FW/$set_dir" ]; then echo "skip: no $FW/$set_dir"; continue; fi
    echo "== $set_dir $timeline ($SECONDS_PER_SET s)"
    rm -f "pgo/$timeline.profraw"
    if LLVM_PROFILE_FILE="pgo/$timeline.profraw" pgo/speed_gate_instrumented "$FW/$set_dir" \
        "tests/timelines/$timeline.events" --block 128 --rate 48000 --seconds "$SECONDS_PER_SET" > "pgo/$timeline.log" 2>&1 \
        && [ -s "pgo/$timeline.profraw" ]; then
        tail -1 "pgo/$timeline.log"
        n=$((n + 1))
    else
        echo "failed: see pgo/$timeline.log" >&2
        exit 1
    fi
done
if [ "$n" = 0 ]; then echo "no firmware set found under $FW" >&2; exit 1; fi
PROFDATA="$(xcrun -f llvm-profdata 2>/dev/null || command -v llvm-profdata)"
"$PROFDATA" merge -output=pgo/plugin.profdata pgo/*.profraw
"$PROFDATA" merge -text -output=pgo/plugin.proftext pgo/*.profraw
echo "wrote pgo/plugin.profdata and pgo/plugin.proftext from $n set(s)"
