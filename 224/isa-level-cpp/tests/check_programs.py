#!/usr/bin/env python3
"""A self-contained check of the C++ machine: three programs, their events by SHA-256.

    python3 tests/check_programs.py

Needs only a C++20 compiler. The programs are random microcode from the
fuzz test (tests/programs.json says which); their expected events are the
ones the SystemVerilog machine and the board-level machine agree with in
../isa-level-verilog's checks, which need Verilator. This is the quick check
for someone who only builds the C++.
"""
from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    binary = build / "static_run"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-o", str(binary), str(ROOT / "static_run.cpp")], check=True)
    programs = json.loads((ROOT / "tests" / "programs.json").read_text())["programs"]
    failed = 0
    for index, program in enumerate(programs):
        image = build / f"program{index}.bin"
        image.write_bytes(bytes.fromhex(program["image"]))
        events = build / f"program{index}.events"
        subprocess.run([str(binary), str(image), str(events), str(program["rows"])], check=True)
        data = events.read_bytes()
        ok = hashlib.sha256(data).hexdigest() == program["sha256"]
        failed += not ok
        print(f"program {index}: {len(data.splitlines())} events, {'as expected' if ok else 'DIFFERENT'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
