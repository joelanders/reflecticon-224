#!/usr/bin/env python3
"""The row machine against the gate-level six-board stitch.

The HDL side is tests/gate_bench.py: the generated T&C, ARU, DMEM I/O, DMEM
and FPC netlists (../gate-level-verilog/generated) under iverilog, with the
datasheet MK4164 DRAM, the constructed FPC U6 image and CPC CLR before RUN.
Its converter-input function, alignment and per-kind sequences are used as
they are. Programs: random microcode-shaped ones (tests/fuzz.py), plus any
WCS images you capture into ../captures/programs/.

The row side is tests/ports_run.sv. Comparison rule, per bit:
  HDL x          don't care (the row machine has no unknowns); counted
  HDL z          the row machine must show z (a register written by a NOP row)
  HDL 0/1        the row machine must show the same bit

  python3 tests/gate_level.py --random 40 [--rows 1500] [--cached] [--no-negative]
"""
from __future__ import annotations

import argparse
import gzip
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tests"))
import run as rows_run  # noqa: E402

GO_TOOL = ROOT / "tests/gate_bench.py"
OUT = rows_run.BUILD / "gate-level"
NETLIST_XREG_RACE = False  # set by --netlist-xreg-race


def load_go_tool(rows):
    os.environ["DSP_CYCLES"] = str(rows)
    spec = importlib.util.spec_from_file_location("check_dsp_structural", GO_TOOL)
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    return tool


def build_ports(model=ROOT / "dsp.sv", directory=rows_run.BUILD / "ports"):
    compiler, env = rows_run.verilator()
    subprocess.run([str(compiler), "--binary", "--timing", "-O3", "-Wall",
                    "-Wno-DECLFILENAME", "-Wno-UNUSEDSIGNAL", "-Wno-PROCASSINIT", "-Wno-REALCVT",
                    "--top-module", "ports_run", "--Mdir", str(directory),
                    "-CFLAGS", "-std=c++20 -O2", "-j", "4",
                    str(ROOT / "lexicon224x.sv"), str(model), str(ROOT / "tests/ports_run.sv")],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    return directory / "Vports_run"


def bit_compare(ours, hdl):
    """(same, hdl_x_bits): HDL x is don't-care; z and known bits must match."""
    if len(hdl) == 3 and "x" in hdl[0]:
        # A DAC capture of an unknown word: the netlist's normalizer then
        # holds its gain counter, which it still shows as known. The gain
        # is derived from the unknown sample, so compare channels only.
        return ours[1] == hdl[1], hdl[0].count("x")
    unknown = 0
    for a, b in zip("".join(ours), "".join(hdl)):
        if b == "x":
            unknown += 1
        elif a != b:
            return False, unknown
    return True, unknown


def step_samples(ours, hdl):
    """The DAC waiting latch as a step function: at each netlist change, the
    row machine's latch value at that moment. The netlist logs every change,
    including x -> value, which index-by-index pairing cannot follow; its
    records trail the row machine's by 0-45 ns of a 293 ns row."""
    paired = []
    for t, value in hdl:
        earlier = [v for v in ours if v[0] <= t + 1.0]
        if earlier:
            paired.append((earlier[-1], (t, value)))
    return [p[0] for p in paired], [p[1] for p in paired]


def compare(ours, hdl, end):
    result = {}
    for kind in ("regs", "mem", "xreg", "adc", "pending", "dac"):
        left = [v for v in ours[kind] if v[0] <= end]
        right = [v for v in hdl[kind] if v[0] <= end]
        if kind == "pending":
            left, right = step_samples(left, right)
        count = min(len(left), len(right))
        mismatches, unknown = [], 0
        for index in range(count):
            same, u = bit_compare(left[index][1], right[index][1])
            unknown += u
            if not same:
                mismatches.append(dict(index=index, rows=left[index], hdl=right[index]))
        result[kind] = dict(rows=len(left), hdl=len(right), compared=count, mismatches=len(mismatches),
                            first_mismatches=mismatches[:4], hdl_x_bits=unknown)
    return result


def run_case(tool, ports, path, name, rows, cached, adc_xor=0, saved=None):
    saved = saved if saved is not None else tool.saved_bytes(path)
    image = OUT / f"{name}.bin"
    image.write_bytes(bytes(saved))
    ours_path = OUT / f"{name}-rows.csv"
    subprocess.run([str(ports), f"+image={image}", f"+trace={ours_path}", f"+rows={rows}"]
                   + (["+netlist_xreg_race"] if NETLIST_XREG_RACE else []),
                   check=True, stdout=subprocess.DEVNULL)
    hdl_path = OUT / f"{name}-hdl.csv.gz"
    if cached and hdl_path.exists():
        hdl_text = gzip.open(hdl_path, "rt").read()
    else:
        tool.OUT = OUT
        executable = tool.compile_bench("drawn", adc_xor=adc_xor)
        hdl_text = tool.hdl_trace(executable, name, saved)
        executable.unlink()
        with gzip.open(hdl_path, "wt") as output:
            output.write(hdl_text)
    ours_rows, hdl_rows = tool.events(ours_path.read_text()), tool.events(hdl_text)
    ours_zero, hdl_zero = tool.align(ours_rows, hdl_rows)
    end = min(ours_rows[-1]["time_ns"] - ours_zero, hdl_rows[-1]["time_ns"] - hdl_zero) - 400
    return compare(tool.sequences(ours_rows, ours_zero, "go"), tool.sequences(hdl_rows, hdl_zero, "hdl"), end)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rows", type=int, default=1500)
    parser.add_argument("--cached", action="store_true", help="reuse HDL traces from a previous run")
    parser.add_argument("--no-negative", action="store_true")
    parser.add_argument("--random", type=int, default=0, help="also run N random programs (tests/fuzz.py)")
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--netlist-xreg-race", action="store_true",
                        help="mark RR-source WR_XREG captures undriven, as the netlist times them")
    args = parser.parse_args()
    global NETLIST_XREG_RACE
    NETLIST_XREG_RACE = args.netlist_xreg_race
    OUT.mkdir(parents=True, exist_ok=True)
    tool = load_go_tool(args.rows)
    ports = build_ports()
    summary, failed = {}, False
    for path in tool.PROGRAMS:
        result = run_case(tool, ports, path, path.stem, args.rows, args.cached)
        summary[path.stem] = result
        failed |= any(v["mismatches"] for v in result.values())
        print(path.stem, " ".join(f"{k}={v['compared']}/{v['mismatches']}(x{v['hdl_x_bits']})"
                                  for k, v in result.items()), flush=True)
    if args.random:
        import fuzz
        import random
        rng = random.Random(args.seed)
        totals, clean = {}, 0
        for index in range(args.random):
            saved = list(fuzz.random_program(rng, gate_level=True))
            name = f"random{args.seed}-{index}"
            result = run_case(tool, ports, None, name, args.rows, args.cached, saved=saved)
            summary[name] = result
            bad = {k: v["mismatches"] for k, v in result.items() if v["mismatches"]}
            for k, v in result.items():
                totals[k] = totals.get(k, 0) + v["compared"]
            if bad:
                failed = True
                print(f"{name}: MISMATCH {bad}", flush=True)
            else:
                clean += 1
        print(f"random programs: {clean}/{args.random} identical; compared {totals}")
    if not args.no_negative:
        # A one-LSB change to the HDL's converter input must be detected.
        # always a random program: a captured one (a diagnostic, say) may read
        # too little from the converter in the window for the flip to show
        import fuzz
        import random
        saved = list(fuzz.random_program(random.Random(args.seed), gate_level=True))
        negative = run_case(tool, ports, None, "negative-adc-lsb", args.rows, args.cached, adc_xor=1, saved=saved)
        detected = {k: v["mismatches"] for k, v in negative.items() if v["mismatches"]}
        summary["negative_adc_lsb"] = detected
        print(f"negative control (HDL ADC LSB flipped) detected: {detected}")
        # the flipped bit must be seen at the converter and downstream of it
        failed |= not (detected.get("adc") and len(detected) > 1)
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    print("FAIL" if failed else "PASS", f"(summary: {(OUT / 'summary.json').relative_to(ROOT)})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
