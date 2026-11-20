#!/bin/sh
# Build and run the catalog model tests against the web demo's own code.
#   sh tests/catalog/run.sh        (from juce-plugin)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$root/build-d"
web="$root/../web-demo/page"
mkdir -p "$out"
node "$here/js_reference.mjs" "$out/js_reference.json"
c++ -std=c++20 -O1 -Wall -Wextra -o "$out/catalog_test" \
    "$here/catalog_test.cpp" "$root/source/catalog/json.cpp" "$root/source/catalog/catalog.cpp" \
    "$root/source/catalog/help.cpp" "$root/source/catalog/share.cpp"
"$out/catalog_test" "$web" "$out/js_reference.json"
python3 "$page/check_param_help.py"
