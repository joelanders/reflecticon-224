#!/bin/sh
# Build the operator tests into build-b/ (run from juce-plugin).
set -e
mkdir -p build-b
flags="-std=c++20 -O2 -ffp-contract=off -Wall -Wextra -I ../analog"
clang++ $flags tests/soak/soak.cpp -o build-b/soak
clang++ $flags tests/operator_equiv/record_timeline.cpp -o build-b/record_timeline
clang++ $flags tests/operator_equiv/chunks.cpp -o build-b/chunks
clang++ $flags tests/operator_equiv/replay_display.cpp -o build-b/replay_display
clang++ $flags tests/operator_equiv/extras.cpp -o build-b/extras
clang++ $flags tests/operator_equiv/task_test.cpp -o build-b/task_test
