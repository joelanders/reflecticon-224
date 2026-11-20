#!/bin/sh
# Measure the resampler and the rate bridge.
#   sh tests/resampler/run.sh        (from juce-plugin)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build-g"
mkdir -p "$out"
clang++ -std=c++20 -O2 -ffp-contract=off -Wall -Wextra -o "$out/resampler_quality" "$here/resampler_quality.cpp"
"$out/resampler_quality"
