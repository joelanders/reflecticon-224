#!/usr/bin/env python3
"""Build and run the board-level machine. Generated files stay under build/.

  python3 run.py build-machine [--grade 0|1]   # the whole machine: 8080 + boards (factory, fixture, run)
  python3 run.py build-static  [--grade 0|1]   # the DSP boards alone, from a static WCS image
  python3 run.py example                       # an authored 8080 program -> build/authored-audio.wav
  python3 run.py run-programs --images DIR     # the four captured programs, with audio guardrails
"""
from __future__ import annotations

import argparse
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
# The four captured programs and their SHA-256 (the images themselves are the
# firmware's and are supplied by the caller).
PROGRAMS = {
    "xl_max_delay_combo": "8bc5b61830df4f84dc399b3b6d14c7e67503855a6c29ef7ba0d4a4f78067b0f6",
    "m_band_delay": "23dac1963ca50c3805565f98ec34067a04ce3801bdc92fef7c2e9bcde0938db1",
    "chorus_echo": "30b863abfbb0a9b01411d5e5e0072f8bd49fc446057f623065d9b169365b6daf",
    "call_prog4": "03bd8b323e6729f752c21868c8190e11a27e4d74dff6248662f384605d9ca438",
}
# A Verilator install to use when verilator is not on PATH (bin/, share/verilator).
TOOLCHAINS = [os.environ.get("VERILATOR_HOME", "")]


def run(command, *, cwd=ROOT, log=None, env=None):
    command = list(map(str, command))
    print("+ " + shlex.join(command), flush=True)
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log:
        (BUILD / log).write_text(result.stdout)
    if not log or result.returncode:
        print(result.stdout, end="", flush=True)
    result.check_returncode()
    return result.stdout


def compiler_environment():
    env = dict(os.environ, LC_ALL="C", LC_CTYPE="C", LANG="C")
    compiler = shutil.which("verilator")
    if compiler:
        return compiler, env
    for prefix in map(Path, filter(None, map(str, TOOLCHAINS))):
        if (prefix / "bin/verilator").is_file():
            env["VERILATOR_ROOT"] = str(prefix / "share/verilator")
            return prefix / "bin/verilator", env
    raise RuntimeError("Verilator 5 is required on PATH (or set VERILATOR_HOME)")


def build_machine(compiler, env, grade, *, cpu_connected=True):
    name = "machine" if cpu_connected else "static"
    directory = BUILD / f"{name}-{grade}"
    directory.mkdir(parents=True, exist_ok=True)
    run([compiler, "--cc", "--exe", "--build", "-j", "2", "-O3", "--assert", "--timing", "--no-sched-zero-delay",
         "--top-module", "machine_host", "--prefix", "Vmachine_host", "--Mdir", directory,
         f"-Gmaximum_delays={grade}", f"-Gcpu_connected={int(cpu_connected)}", "-CFLAGS", "-std=c++20 -g",
         "-MAKEFLAGS", "OPT_FAST=-O3 OPT_GLOBAL=-O3 OPT_SLOW=-O3",
         *(ROOT / board for board in ("backplane.sv", "tc.sv", "aru.sv", "dmem.sv", "fpc.sv", "aout.sv", "sbc.sv")),
         ROOT / "host/machine.sv", ROOT / "host/main.cpp"], log=f"{name}-{grade}-build.log", env=env)
    return directory / "Vmachine_host"


def authored_fixtures():
    specification = importlib.util.spec_from_file_location("authored_firmware", ROOT / "tests/firmware_fixtures.py")
    fixtures = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(fixtures)
    yield from fixtures.fixtures()
    # IN 8/9 read FPC's two headroom latches. Releasing each read starts its
    # delayed initialization, so a subsequent read distinguishes stored peak
    # history from the cleared register. The existing machine trace observes
    # these four retained-RAM destinations at instruction/cycle boundaries.
    headroom = [0x3e, 0, 0xd3, 1, 0xd3, 3]  # MVI A,0; continuous; run.
    headroom += [0] * 125  # Let both detector channels store a peak before IN.
    for port, destination in [(8, 0x2000), (9, 0x2001), (8, 0x2002), (9, 0x206b)]:
        headroom += [0xdb, port, 0x32, destination & 255, destination >> 8]  # IN; STA.
    headroom += [0x76]  # HLT; the board clocks continue.
    yield "headroom_readback", headroom


def run_programs(compiler, env, images, grade, rows):
    """Run each captured program on the static machine and check the audio
    guardrails; the events land in build/NAME-GRADE.events."""
    if rows < 4194304:
        raise ValueError("Program guardrails need at least 4194304 rows (1.2288 simulated seconds)")
    for name, wanted in PROGRAMS.items():
        image = images / f"{name}.bin"
        if hashlib.sha256(image.read_bytes()).hexdigest() != wanted:
            raise RuntimeError(f"{image}: input differs from the documented captured program")
    binary = build_machine(compiler, env, grade, cpu_connected=False)
    report = {}
    for name, digest in PROGRAMS.items():
        image = images / f"{name}.bin"
        prefix = BUILD / f"{name}-{grade}"
        run([binary, "static", image, rows, prefix], log=f"{name}-{grade}-run.log", env=env)
        events = Path(str(prefix) + ".events").read_text().splitlines()
        summary = json.loads(Path(str(prefix) + ".summary.json").read_text())
        if summary["source_overlap_events"] or summary["undriven_source_events"]:
            raise RuntimeError(f"{name}: output accepted a contended or undriven DAB source")
        for channel, metrics in enumerate(summary["channels"]):
            if metrics["unknown"] or not metrics["nonzero"] or metrics["peak"] >= 32767:
                raise RuntimeError(f"{name} channel {channel}: unknown, silent or clipped: {metrics}")
        report[name] = dict(sha256=digest, timing_grade=grade, rows=rows, events=len(events), **summary)
        print(f"{name} grade={grade}: {len(events)} events; four active, known channels; no source overlap",
              flush=True)
    (BUILD / f"programs-{grade}.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["build-machine", "build-static", "example", "run-programs"])
    parser.add_argument("--grade", type=int, choices=(0, 1), default=0)
    parser.add_argument("--images", type=Path, default=ROOT.parent / "captures",
                        help="Directory of caller-supplied captured WCS images for run-programs")
    parser.add_argument("--rows", type=int, default=4194304, help="Rows per captured program")
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    compiler, env = compiler_environment()
    if args.command == "run-programs":
        run_programs(compiler, env, args.images.resolve(), args.grade, args.rows)
    elif args.command == "example":
        from render import render
        program = dict(authored_fixtures())["wcs_audio"]
        rom = BUILD / "authored-audio.rom"
        rom.write_bytes(bytes(program))
        binary = build_machine(compiler, env, args.grade)
        prefix = BUILD / "authored-audio"
        run([binary, "run", rom, 200000, prefix], log="authored-audio.log", env=env)
        render(prefix, BUILD / "authored-audio.wav")
    else:
        print(build_machine(compiler, env, args.grade, cpu_connected=args.command == "build-machine"))


if __name__ == "__main__":
    main()
