#!/bin/sh
# Check the measured stored-byte sidecars against the catalogs with the
# plugin's C++ catalog model.
#   sh tests/stored_tables/run.sh        (from juce-plugin)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build-i"
mkdir -p "$out"
clang++ -std=c++20 -O2 -ffp-contract=off -Wall -Wextra -o "$out/stored_tables_test" \
    "$here/stored_tables_test.cpp" "$root/source/catalog/json.cpp" "$root/source/catalog/catalog.cpp" \
    "$root/source/catalog/help.cpp" "$root/source/catalog/share.cpp"
"$out/stored_tables_test" "$root"/../web-demo/page/catalogs/*.json "$root"/catalogs-extra/*.json \
    "$root"/catalogs-extra/stored/*.stored.json
