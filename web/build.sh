#!/bin/sh
# Build the web page's machine modules, page/machines/<id>.js (wasm embedded), from the machines built on shared/.
# Needs emscripten (em++ on PATH). Holds no ROM bytes: the page asks for the user's files.
#   web/build.sh [id ...]            (default: all)
#   python3 -m http.server -d web/page 8323
set -e
cd "$(dirname "$0")"
ROOT=..
OUT=page/machines
GEN=build/gen
mkdir -p $OUT $GEN build
FLAGS="-std=c++20 -O3 -ffp-contract=off -Wall -I$ROOT/shared/core"
LINK="-sMODULARIZE=1 -sEXPORT_ES6=1 -sSINGLE_FILE=1 -sENVIRONMENT=web,worker,worklet,node -sALLOW_MEMORY_GROWTH=1 \
  -sSTACK_SIZE=1048576 -sINCOMING_MODULE_JS_API=print,printErr \
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPU8,HEAPF32 \
  -sEXPORTED_FUNCTIONS=_malloc,_free"

# the DMX repair masks as a C string (as dmx1580/CMakeLists.txt does)
{ printf '// generated from core/patches.txt\n#pragma once\nstatic const char kPatches[] = R"DMXP('
  cat $ROOT/dmx1580/core/patches.txt; printf ')DMXP";\n'; } > $GEN/dmx1580_patches.h

build() {   # id namespace [extra C sources]
    id=$1 ns=$2; shift 2
    printf '#include "%s"\nconst kit::MachineType& kw_machine_type() { return %s::machine_type(); }\n' \
        "../../$ROOT/$id/plugin/${id}_machine.hpp" "$ns" > build/shim_$id.cpp
    objs=""
    for c in "$@"; do
        o=build/$id-$(basename "$c" .c).o
        emcc -std=c11 -O3 -ffp-contract=off -c "$c" -o "$o"
        objs="$objs $o"
    done
    em++ $FLAGS -I$GEN $LINK -sEXPORT_NAME=create_$id kit_web.cpp build/shim_$id.cpp \
        $ROOT/$id/plugin/${id}_machine.cpp $objs -o $OUT/$id.js
    printf "// written by build.sh\nimport create from './machines/%s.js';\nimport { register } from './worklet.mjs';\nregister('kit-%s', create);\n" $id $id > page/worklet-$id.mjs
    echo "built $OUT/$id.js ($(wc -c < $OUT/$id.js | tr -d ' ') bytes)"
}

want() { [ $# -eq 0 ] && return 0; for w in $ARGS; do [ "$w" = "$1" ] && return 0; done; return 1; }
ARGS="$*"
[ -z "$ARGS" ] || want dn780 && build dn780 dn780plug
[ -z "$ARGS" ] || want rmx16 && build rmx16 rmx16plug $ROOT/rmx16/core/rmx16_core.c
[ -z "$ARGS" ] || want dmx1580 && build dmx1580 dmx1580plug
[ -z "$ARGS" ] || want sg323 && build sg323 sg323plug
