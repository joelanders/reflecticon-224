#!/bin/sh
# Parse the extra catalogs with the plugin's C++ catalog model.
#   sh tests/catalogs_extra/run.sh        (from juce-plugin)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build-g"
mkdir -p "$out"
clang++ -std=c++20 -O2 -ffp-contract=off -Wall -Wextra -o "$out/parse_extra" \
    "$here/parse_extra.cpp" "$root/source/catalog/json.cpp" "$root/source/catalog/catalog.cpp" \
    "$root/source/catalog/help.cpp" "$root/source/catalog/share.cpp"
"$out/parse_extra" "$root"/catalogs-extra/*.json
