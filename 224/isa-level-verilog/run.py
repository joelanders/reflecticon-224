#!/usr/bin/env python3
"""Build the ISA-level machine (SystemVerilog and C++) and compare it with the board-level machine.

  python3 run.py check    # four captured programs, 4,194,304 rows each

The expected traces are the board-level machine's own events for the same
programs (`python3 run.py run-programs` in ../board-level-verilog, which
writes build/NAME-0.events there).
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
# A Verilator install to use when verilator is not on PATH (bin/, share/verilator).
TOOLCHAINS = [os.environ.get("VERILATOR_HOME", "")]

PROGRAMS = {
    "xl_max_delay_combo": "8bc5b61830df4f84dc399b3b6d14c7e67503855a6c29ef7ba0d4a4f78067b0f6",
    "m_band_delay": "23dac1963ca50c3805565f98ec34067a04ce3801bdc92fef7c2e9bcde0938db1",
    "chorus_echo": "30b863abfbb0a9b01411d5e5e0072f8bd49fc446057f623065d9b169365b6daf",
    "call_prog4": "03bd8b323e6729f752c21868c8190e11a27e4d74dff6248662f384605d9ca438",
}


def verilator():
    env = dict(os.environ)
    found = shutil.which("verilator")
    if found:
        return Path(found), env
    for prefix in map(Path, filter(None, map(str, TOOLCHAINS))):
        if (prefix / "bin/verilator").is_file():
            env["VERILATOR_ROOT"] = str((prefix / "share/verilator").resolve())
            return prefix / "bin/verilator", env
    sys.exit("Verilator 5 is required on PATH (or set VERILATOR_HOME)")


def build():
    compiler, env = verilator()
    directory = BUILD / "static"
    subprocess.run([str(compiler), "--binary", "--timing", "-O3", "-Wall",
                    "-Wno-DECLFILENAME", "-Wno-UNUSEDSIGNAL", "-Wno-PROCASSINIT",
                    "--top-module", "static_run", "--Mdir", str(directory),
                    "-CFLAGS", "-std=c++20 -O2", "-j", "4",
                    str(ROOT / "lexicon224x.sv"), str(ROOT / "dsp.sv"),
                    str(ROOT / "tests/static_run.sv")],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    return directory / "Vstatic_run"


def build_cpp():
    binary = BUILD / "static_run_cpp"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-o", str(binary),
                    str(ROOT.parent / "isa-level-cpp/static_run.cpp")], check=True)
    return binary


def check(images: Path, references: Path, rows: int):
    binaries = {"SystemVerilog": build(), "C++": build_cpp()}
    failures = 0
    for name, digest in PROGRAMS.items():
        image = images / f"{name}.bin"
        if hashlib.sha256(image.read_bytes()).hexdigest() != digest:
            sys.exit(f"{image}: not the documented captured program")
        expected = (references / f"{name}-0.events").read_text().splitlines()
        for model, binary in binaries.items():
            events = BUILD / f"{name}.{'sv' if model == 'SystemVerilog' else 'cpp'}.events"
            command = ([str(binary), f"+image={image}", f"+events={events}", f"+rows={rows}"]
                       if model == "SystemVerilog" else [str(binary), str(image), str(events), str(rows)])
            started = time.monotonic()
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            seconds = time.monotonic() - started
            actual = events.read_text().splitlines()
            mismatch = next((n for n, (a, e) in enumerate(zip(actual, expected)) if a != e), None)
            if mismatch is None and len(actual) == len(expected):
                realtime = rows * 292.96875e-9 / seconds
                print(f"{name} [{model}]: {len(actual)} events identical over {rows} rows "
                      f"({seconds:.2f} s, {realtime:.1f}x realtime)")
            else:
                failures += 1
                where = mismatch if mismatch is not None else min(len(actual), len(expected))
                print(f"{name} [{model}]: FIRST DIFFERENCE at event {where} "
                      f"({len(actual)} vs {len(expected)} events)")
                if mismatch is not None:
                    print(f"  rows model: {actual[mismatch]}\n  reference:  {expected[mismatch]}")
    if failures:
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["build", "check"])
    parser.add_argument("--images", type=Path, default=ROOT.parent / "captures")
    parser.add_argument("--references", type=Path, default=ROOT.parent / "board-level-verilog/build")
    parser.add_argument("--rows", type=int, default=4194304)
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    if args.command == "build":
        print(build())
    else:
        check(args.images, args.references, args.rows)


if __name__ == "__main__":
    main()
