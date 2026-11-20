#!/bin/sh
# speed_gate_ab.sh — the machine's block cost under several builds, one run each.
#
#   scripts/speed_gate_ab.sh ROM_SET TIMELINE.events [SECONDS]
#
# ROM_SET is one firmware folder ("224XL v8_21"); TIMELINE an .events file
# (tests/timelines/xl821_1.events). Each build renders SECONDS (default 30) of
# steady state and prints its steady-state mean; the last line is the
# comparison. Needs no JUCE: the gate is the machine alone.
#
# What it runs, skipping what the machine does not have:
#   1  $CXX -O3                     (g++ by default)
#   2  clang++ -O3
#   3  clang++ -O3 + the committed PGO profile (needs llvm-profdata)
#   4  clang++ -O2                  (only with --with-o2: -O3 bloats the big
#                                    interpreter switch on some targets)
#
# Add CXXFLAGS to try more: CXXFLAGS="-march=native" scripts/speed_gate_ab.sh ...
# The profiles and binaries land in pgo/ab/ (ignored by git).
#
# Every build also prints a hash of its output samples, and the runs are
# compared at the end: the same hash means the builds differ in speed only,
# which is what a flag or profile change has to do (the plugin's byte_exact
# gate makes the same check against the web reference).
set -eu
cd "$(dirname "$0")/.."
ROM_SET="${1:?usage: scripts/speed_gate_ab.sh ROM_SET TIMELINE.events [SECONDS]}"
TIMELINE="${2:?usage: scripts/speed_gate_ab.sh ROM_SET TIMELINE.events [SECONDS]}"
SECONDS_OF_AUDIO="${3:-30}"
WITH_O2=0
case "${4:-}" in --with-o2) WITH_O2=1 ;; esac

if [ ! -d "$ROM_SET" ]; then echo "no ROM folder: $ROM_SET" >&2; exit 2; fi
if [ ! -f "$TIMELINE" ]; then echo "no timeline: $TIMELINE" >&2; exit 2; fi

mkdir -p pgo/ab
FLAGS="-std=c++20 -g -DNDEBUG -ffp-contract=off ${CXXFLAGS:-}"

# The committed profile for this platform (see scripts/pgo_train.sh).
case "$(uname -s)" in
	Darwin) PROFILE_TXT=pgo/plugin.proftext ;;
	*) PROFILE_TXT=pgo/plugin-linux.proftext ;;
esac

# The gate is compiled through a file named PluginProcessor.cpp, as the
# trainer does and as the plugin compiles the machine: internal-linkage
# functions are keyed "<training file name>:<mangled name>", so a PGO build
# from a differently named translation unit would match none of the 8080 core.
mkdir -p pgo/ab/train
printf '#include "../../../bench/speed_gate.cpp"\n' > pgo/ab/train/PluginProcessor.cpp
SRC=pgo/ab/train/PluginProcessor.cpp

# A build of the gate: label, compiler, extra flags. Prints the steady mean.
run_one() {
	label="$1"; compiler="$2"; extra="$3"
	if ! command -v "$compiler" >/dev/null 2>&1; then
		echo "$label: no $compiler, skipped"
		return 0
	fi
	# shellcheck disable=SC2086
	"$compiler" $FLAGS $extra -o "pgo/ab/gate" "$SRC"
	if ! out=$("pgo/ab/gate" "$ROM_SET" "$TIMELINE" --seconds "$SECONDS_OF_AUDIO" 2>&1); then
		echo "$label: the gate failed:" >&2
		printf '%s\n' "$out" >&2
		exit 1
	fi
	line=$(printf '%s\n' "$out" | sed -n 's/^steady *blocks *[0-9]* *mean \([0-9.]*\).*/\1/p')
	hash=$(printf '%s\n' "$out" | sed -n 's/^output hash //p')
	printf '%-28s steady mean %s  hash %s\n' "$label" "${line:-?}" "${hash:-?}"
	printf '%s\t%s\t%s\n' "${line:-?}" "$label" "${hash:-?}" >> pgo/ab/times.txt
}

: > pgo/ab/times.txt
run_one "${CXX:-g++} -O3" "${CXX:-g++}" "-O3"
run_one "clang++ -O3" clang++ "-O3"
if command -v clang++ >/dev/null 2>&1 && command -v llvm-profdata >/dev/null 2>&1 && [ -f "$PROFILE_TXT" ]; then
	llvm-profdata merge -output=pgo/ab/profile.profdata "$PROFILE_TXT"
	run_one "clang++ -O3 PGO" clang++ "-O3 -fprofile-instr-use=$PWD/pgo/ab/profile.profdata"
fi
if [ "$WITH_O2" = 1 ]; then
	run_one "clang++ -O2" clang++ "-O2"
fi

echo "-- steady-state mean, lower is better (block compute / block duration)"
tab="$(printf '\t')"
sort -n pgo/ab/times.txt | while IFS="$tab" read -r value label hash; do
	printf '   %-26s %s\n' "$label" "$value"
done
hashes=$(cut -f3 pgo/ab/times.txt | sort -u | wc -l)
if [ "$hashes" = 1 ]; then
	echo "-- outputs bit-identical across the runs ($(cut -f3 pgo/ab/times.txt | head -1))"
else
	echo "-- WARNING: the outputs differ; these builds are not comparable" >&2
fi
