#!/bin/sh
# Build and run the parameter/automation (M5) and state (M6) tests.
#   sh tests/params/run.sh        (from juce-plugin)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build-e"
web="$root/../web-demo/page"
mkdir -p "$out"
clang++ -std=c++20 -O2 -ffp-contract=off -Wall -Wextra -o "$out/params_test" \
    "$here/params_test.cpp" "$root/source/state/plugin_state.cpp" \
    "$root/source/catalog/json.cpp" "$root/source/catalog/catalog.cpp" "$root/source/catalog/share.cpp"
"$out/params_test" "$web"
