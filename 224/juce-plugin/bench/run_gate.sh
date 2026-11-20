#!/bin/sh
# One M0 configuration, real-time and paced (as a DAW's audio callback):
#   sh bench/run_gate.sh SET_DIR_NAME TIMELINE BLOCK RATE SECONDS [extra options]
# Appends one labelled result to build/m0_results.txt.
FW="${LEXICON_FIRMWARE:?set LEXICON_FIRMWARE to your firmware folder}"
set_dir="$1"; timeline="$2"; block="$3"; rate="$4"; seconds="$5"; shift 5
out=$(build/speed_gate "$FW/$set_dir" "tests/timelines/$timeline.events" --block "$block" --rate "$rate" \
      --seconds "$seconds" --realtime --paced --csv "build/m0_${timeline}_${block}_${rate}.csv" "$@" 2>&1)
printf '%s\n' "== $set_dir $timeline block $block rate $rate seconds $seconds $*" "$out" | tee -a build/m0_results.txt
