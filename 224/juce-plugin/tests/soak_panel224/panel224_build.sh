#!/bin/sh
# build the 224 operator tests into build-m4b/ (run from juce-plugin).
set -e
mkdir -p build-m4b
flags="-std=c++20 -O2 -ffp-contract=off -Wall -Wextra -I ../analog"
clang++ $flags tests/soak_panel224/panel224_soak.cpp -o build-m4b/panel224_soak
clang++ $flags tests/operator_equiv/panel224_record_timeline.cpp -o build-m4b/panel224_record_timeline
clang++ $flags tests/operator_equiv/panel224_chunks.cpp -o build-m4b/panel224_chunks
clang++ $flags tests/operator_equiv/panel224_extras.cpp -o build-m4b/panel224_extras
clang++ $flags tests/soak_panel224/panel224_collapse.cpp -o build-m4b/panel224_collapse
