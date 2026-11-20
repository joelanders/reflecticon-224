#!/usr/bin/env python3
"""Build the page's WASM module: page/lexicon224x.{js,wasm}.

    python3 build.py                 # needs emscripten (em++) on PATH
    python3 page/serve.py            # then open http://127.0.0.1:8000

The module is the whole machine: the row machine and its 8080 host
(../emulator), the analog boards, and the reading lenses, behind the
C API in wasm/web.cpp. One ES module serves both the page and Node
(page/precompute.mjs, tests/soak.mjs).
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main() -> int:
    subprocess.run(["em++", "-std=c++20", "-O3", "-sALLOW_MEMORY_GROWTH=1", "-sENVIRONMENT=web,worker,node",
                    "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sEXPORT_NAME=createLexicon",
                    "-sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,HEAPU8,HEAP16,HEAPU32,HEAPF32",
                    "-sEXPORTED_FUNCTIONS=_malloc,_free", "-o", str(HERE / "page/lexicon224x.js"),
                    str(HERE / "wasm/web.cpp"), str(HERE / "wasm/web_lenses.cpp")], check=True)
    print("built page/lexicon224x.js and .wasm; serve with: python3 page/serve.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
