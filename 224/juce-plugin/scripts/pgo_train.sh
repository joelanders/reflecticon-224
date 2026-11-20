#!/bin/sh
# pgo_train.sh — a clang PGO profile for the machine, from the speed gate on
# your own ROMs. Writes pgo/plugin.profdata, and its text form pgo/plugin.proftext,
# which is committed: the release workflows index it with the runner's own
# llvm-profdata (the text form reads under any LLVM version). Locally,
# configure a release build with
#
#   cmake -S . -B build-pgo -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
#         -DLEXPLUG_PGO_PROFILE="$PWD/pgo/plugin.profdata" ...
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
# macOS (the release job's own clang and llvm-profdata) and Linux (apt's clang
# and llvm) both work; the gate is portable.
set -eu
cd "$(dirname "$0")/.."
FW="${1:?usage: scripts/pgo_train.sh FIRMWARE_ROOT [SECONDS]}"
SECONDS_PER_SET="${2:-30}"
mkdir -p pgo

# Internal-linkage functions are keyed "<training file name>:<mangled name>",
# and the 8080 core is internal linkage (I8080_API is `static` in
# ../emulator/host.hpp). The plugin compiles those same headers from
# plugin/PluginProcessor.cpp, so the gate is compiled through a file with that
# name: otherwise i8080_tick's counters (the whole 8080 interpreter) would
# never apply to a plugin build. See the PGO notes in CMakeLists.txt.
mkdir -p pgo/train
printf '#include "../../bench/speed_gate.cpp"\n' > pgo/train/PluginProcessor.cpp

# The gate needs no frameworks off macOS.
FRAMEWORKS=""
case "$(uname -s)" in
    Darwin) FRAMEWORKS="-framework AudioToolbox -framework CoreFoundation" ;;
esac
# shellcheck disable=SC2086
clang++ -std=c++20 -O3 -g -DNDEBUG -ffp-contract=off -fprofile-instr-generate \
    -Wno-deprecated-declarations -o pgo/speed_gate_instrumented pgo/train/PluginProcessor.cpp $FRAMEWORKS
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
PROFDATA="$(xcrun -f llvm-profdata 2>/dev/null || command -v llvm-profdata || echo llvm-profdata)"
"$PROFDATA" merge -output=pgo/plugin.profdata pgo/*.profraw
"$PROFDATA" merge -text -output=pgo/plugin.proftext pgo/*.profraw
echo "wrote pgo/plugin.profdata and pgo/plugin.proftext from $n set(s)"

# The committed text profile for this platform: the macOS release job merges
# pgo/plugin.proftext, the Linux one pgo/plugin-linux.proftext. The profile
# keys on hashes of each function's control flow, so each toolchain wants the
# profile its own compiler made; the counts themselves are hardware-neutral.
case "$(uname -s)" in
    Darwin) COMMITTED=pgo/plugin.proftext ;;
    *) COMMITTED=pgo/plugin-linux.proftext ;;
esac
if [ "$COMMITTED" != pgo/plugin.proftext ]; then cp pgo/plugin.proftext "$COMMITTED"; fi
echo "commit $COMMITTED: $(grep -c 'Func Hash' "$COMMITTED") functions"
echo "  $(grep -c '^PluginProcessor.cpp:' "$COMMITTED" || true) of them internal linkage under the plugin's translation unit name"
