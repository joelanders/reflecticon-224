#!/usr/bin/env python3
"""Check the 224X ARU against the v8.2.1 HP 5004A signature tables.

Runs tools/verilog/tb_hp5004a_aru.v (generated T&C/ARU/DMEM/FPC boards,
firmware-captured Diagnostic Program 3, firmware register/XREG state),
records every ARU chip pin at each ARUCK (U10.11) rising edge, forms the two
printed analyzer windows and compares every printed signature:

  "ARU no feedback"  START = RESET/ rising, STOP = XFERCK (U43.11) rising
  "ARU"              START = STOP = RESET/ (edge from the page header)

  python3 tools/check_hp5004a_aru.py --topology direct     # U48.1 = MEMW/ (one-connection copy)
  python3 tools/check_hp5004a_aru.py --topology additive   # crop decode + MEMW term at the ARU pin
  python3 tools/check_hp5004a_aru.py --topology crop       # the crop drawing alone
  python3 tools/check_hp5004a_aru.py --dump                # every simulated pin signature

Expected values come from docs/hp5004a/aru_v821.tsv (two-pass transcription).
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

from hp5004a_signature import compress_bits, format_signature
from check_hp5004a_tc_u11 import DEFAULT_WCS, EXPECTED_WCS_SHA256, load_wcs, prepare_runtime

ROOT = Path(__file__).resolve().parents[1]
HDL = ROOT
DEFAULT_OUT = ROOT / "out/hp5004a_aru"
DEFAULT_TABLE = ROOT / "hp5004a/tables/aru_v821.tsv"
INSTANCE_RE = re.compile(r"^\s*(\w+)\s+(?:#\(.*?\)\s+)?(U\d+)\s*\((.*)\);")
PIN_RE = re.compile(r"\.p_(\d+)\(")
CLK_RE = re.compile(r"^HP_CLK t=(\d+) tick=(\d+) phase=(\d+) r0=(\w+) r1=(\w+) r2=(\w+) r3=(\w+) xin=(\w+) wcsa=(\d+) (.*)$")
EDGE_RE = re.compile(r"^HP_EDGE t=(\d+) signal=(\w+) value=(\w)$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--wcs", type=Path, default=DEFAULT_WCS)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    parser.add_argument("--topology", choices=("additive", "direct", "crop"), default="direct",
                        help="U48 / RDRREG/ enable: 'additive' = crop decode plus a MEMW term (the 2026-08-28 "
                             "functional model, applied at the ARU pin by the bench); 'direct' = MEMW/ replaces "
                             "decoder Y0/ on U48.1 (a one-connection copy of the T&C netlist); 'crop' = as drawn")
    parser.add_argument("--aru-source", type=Path, default=HDL / "generated/aru_board.v")
    parser.add_argument("--prims", type=Path, default=HDL / "prims/prims.v",
                        help="primitive library (A/B timing experiments use an isolated copy)")
    parser.add_argument("--cycles", type=int, default=20000)
    parser.add_argument("--dump", action="store_true", help="print every simulated signature")
    parser.add_argument("--define", action="append", default=[], help="extra -P tb_hp5004a_aru.NAME=VALUE")
    parser.add_argument("--phase", type=int, default=10, help="sampling instant: 0 = at the edge, D = D ns before it (3,6,10,15,20,25,30)")
    parser.add_argument("--gate", choices=("start-inclusive", "start-exclusive"), default="start-exclusive",
                        help="window convention; the ARU tables match start-exclusive, the DMEM table start-inclusive")
    parser.add_argument("--ab", action="store_true", help="(historical) shorthand for --topology direct, now the default")
    parser.add_argument("--search", default="", help="comma-separated signatures: list every simulated net that has one")
    return parser.parse_args()


def aru_instances(source: Path) -> dict[str, list[int]]:
    chips: dict[str, list[int]] = {}
    for line in source.read_text(encoding="utf-8").splitlines():
        match = INSTANCE_RE.match(line)
        if match and match.group(1).startswith("ttl_"):
            chips[match.group(2)] = sorted(int(p) for p in PIN_RE.findall(match.group(3)))
    return chips


def write_probe_include(path: Path, chips: dict[str, list[int]], task: str = "print_aru_pins",
                        instance: str = "aru", prefix: str = "") -> None:
    lines = [f"task {task};", "  begin"]
    for chip, pins in chips.items():
        # One field per chip: its pins in ascending order, as a bit string.
        bits = ",".join(f"{instance}.{chip}.p_{pin}" for pin in pins)
        lines.append(f'    $write(" {prefix}{chip}=%b", {{{bits}}});')
    lines += ["  end", "endtask"]
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def run(command: list[str], cwd: Path) -> str:
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout[-4000:]}")
    return result.stdout


def swap_pins(text: str, chip: str, pairs) -> str:
    line = next(l for l in text.splitlines() if re.search(rf"\s{chip} \(", l))
    pins = dict(re.findall(r"\.p_(\d+)\(([^)]*)\)", line))
    new = line
    for a, b in pairs:
        new = new.replace(f".p_{a}({pins[a]})", f".p_{a}(@@{b}@@)").replace(f".p_{b}({pins[b]})", f".p_{b}({pins[a]})")
        new = new.replace(f"@@{b}@@", pins[b])
    return text.replace(line, new)


def aru_source(args: argparse.Namespace, out: Path) -> Path:
    """The generated ARU board."""
    return args.aru_source.resolve()


def tc_source(args: argparse.Namespace, out: Path) -> Path:
    """The generated T&C board (U48.1 = MEMW/, the ink and the tables, since
    2026-09-25), or for 'crop' an isolated copy with the 2026-07-05 misreading
    (U48.1 on U47B Y0/) for A/B comparison."""
    source = HDL / "generated/tc_board.v"
    if args.topology != "crop":
        return source
    text = source.read_text(encoding="utf-8")
    old = "ttl_74ls00 U48 (.p_1(MEMW_n),"
    if text.count(old) != 1:
        raise AssertionError("U48.1 connection not found exactly once in the generated T&C board")
    copy = out / "tc_board_u48_crop.v"
    copy.write_text(text.replace(old, "ttl_74ls00 U48 (.p_1(unconnected_U47B_O0_Pad12),"), encoding="utf-8")
    return copy


def simulate(args: argparse.Namespace, chips: dict[str, list[int]]) -> str:
    out = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    args.memw_rr = 1 if args.topology == "additive" else 0
    wcs_path = args.wcs.resolve()
    digest = hashlib.sha256(wcs_path.read_bytes()).hexdigest()
    if wcs_path == DEFAULT_WCS.resolve() and digest != EXPECTED_WCS_SHA256:
        raise AssertionError(f"firmware-captured Program 3 WCS changed: {digest}")
    print(f"[wcs-domain] {wcs_path.name}: CPU-visible bytes complemented to physical, row-reversed "
          f"(sha256 {digest[:12]})", file=sys.stderr)
    prepare_runtime(out, load_wcs(wcs_path))
    write_probe_include(out / "hp5004a_aru_probes.vh", chips)
    write_probe_include(out / "hp5004a_tc_probes.vh", aru_instances(HDL / "generated/tc_board.v"),
                        task="print_tc_pins", instance="dut", prefix="TC_")
    write_probe_include(out / "hp5004a_dmem_probes.vh", getattr(args, "dmem_chips", {}),
                        task="print_dmem_pins", instance="board")
    write_probe_include(out / "hp5004a_fpc_probes.vh", getattr(args, "fpc_chips", {}),
                        task="print_fpc_pins", instance="fpc")
    vvp = out / "tb_hp5004a_aru.vvp"
    command = ["iverilog", "-g2012", "-gspecify", "-s", "tb_hp5004a_aru", "-o", str(vvp), "-I", str(out),
               "-P", f"tb_hp5004a_aru.MEMW_RR={args.memw_rr}", "-P", f"tb_hp5004a_aru.CYCLES={args.cycles}",
               "-P", f"tb_hp5004a_aru.PHASES={2 if args.phase else 1}"]
    for item in args.define:
        command += ["-P", f"tb_hp5004a_aru.{item}"]
    command += [str(ROOT / "hp5004a/tb_hp5004a_aru.v"), str(tc_source(args, out)),
                str(aru_source(args, out)), str(HDL / "generated/dmem_io_board.v"),
                str(getattr(args, "dmem_source", HDL / "generated/dmem_board.v")), str(HDL / "generated/fpc_board.v"),
                str(getattr(args, "prims", HDL / "prims/prims.v"))]
    run(command, out)
    transcript = run(["vvp", str(vvp)], out)
    (out / f"transcript_{args.topology}.txt").write_text(transcript, encoding="utf-8")
    return transcript


def parse(transcript: str, chips: dict[str, list[int]], phase: int = 0):
    """Samples at the chosen instant, each attributed to its ARUCK edge.

    A sample taken D ns before an edge (phase D) belongs to the first edge at
    or after it; edges are the phase-0 sample times.
    """
    edge_times = []
    samples = []
    edges = []
    for line in transcript.splitlines():
        line = line.strip()
        match = CLK_RE.match(line)
        if match:
            this_phase = int(match.group(3))
            if this_phase == 0:
                edge_times.append(int(match.group(1)))
            if this_phase != phase:
                continue
            fields = dict(item.split("=", 1) for item in match.group(10).split())
            pins = {}
            for chip, pin_list in chips.items():
                if chip not in fields:
                    continue
                bits = fields[chip]
                if len(bits) != len(pin_list):
                    raise ValueError(f"{chip}: {len(bits)} bits for {len(pin_list)} pins")
                for pin, bit in zip(pin_list, bits):
                    pins[(chip, pin)] = bit
            samples.append((int(match.group(1)), pins))
            continue
        match = EDGE_RE.match(line)
        if match:
            edges.append((int(match.group(1)), match.group(2), match.group(3)))
    clocks = []
    index = 0
    for time, pins in samples:
        while index < len(edge_times) and edge_times[index] < time:
            index += 1
        if index < len(edge_times):
            clocks.append((edge_times[index], pins))
    return clocks, edges


GATE = "start-exclusive"


def windows(clocks, edges, start, stop, gate=None):
    """Analyzer windows, with a synchronous gate: START and STOP are recognized
    on the first clock edge at or after their transitions.

    start-inclusive: data from the edge that sees START, up to but not
    including the edge that sees STOP. start-exclusive: from the edge after
    the one that sees START, through the edge that sees STOP. Both give the
    same clock counts; the printed signatures decide (see the report).
    """
    gate = gate or GATE
    starts = [t for t, s, v in edges if (s, v) == start]
    stops = [t for t, s, v in edges if (s, v) == stop]
    times = [t for t, _ in clocks]
    result = []
    for t0 in starts:
        later = [t for t in stops if t > t0]
        if not later:
            break
        after_start = [t for t in times if t >= t0]
        after_stop = [t for t in times if t >= later[0]]
        if not after_start or not after_stop:
            break
        a, b = after_start[0], after_stop[0]
        if gate == "start-inclusive":
            result.append([pins for t, pins in clocks if a <= t < b])
        else:
            result.append([pins for t, pins in clocks if a < t <= b])
    return result


def signatures(window, keys):
    out = {}
    for key in keys:
        values = [pins[key] for pins in window]
        if any(v not in "01" for v in values):
            out[key] = "X" * 4
        else:
            out[key] = format_signature(compress_bits(int(v) for v in values))
    return out


def load_table(path: Path):
    """{table: {(chip, pin): {accepted signatures}}}; '-' means nothing printed."""
    table = defaultdict(dict)
    with path.open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            accepted = {row["signature"]}
            if row.get("alternative"):
                accepted.add(row["alternative"])
            table[row["table"]][(row["chip"], int(row["pin"]))] = accepted
    return table


# Printed entries that do not match, each investigated
# (hp5004a/README.md):
#   U51.12             manual misprint: net SR16 is printed 35FC at U16.14 and U52.12
KNOWN = {
    "ARU no feedback": {("U51", 12)},
    "ARU": set(),
}

SETS = {
    "ARU no feedback": (("RESET_n", "1"), ("XFERCK", "1"), 62, "29F3"),
    "ARU": (("RESET_n", "1"), ("RESET_n", "1"), 90, "3696"),
}


def main() -> int:
    args = parse_args()
    global GATE
    GATE = args.gate
    if args.ab:
        args.topology = "direct"
    chips = aru_instances(args.aru_source.resolve())
    transcript = simulate(args, chips)
    all_chips = dict(chips)
    if args.search:
        all_chips.update({f"TC_{c}": p for c, p in aru_instances(HDL / "generated/tc_board.v").items()})
    clocks, edges = parse(transcript, all_chips, args.phase)
    rail = ("U16", 16)
    table = load_table(args.table) if args.table.exists() and not args.dump else None
    failures = 0
    for name, (start, stop, count, rail_sig) in SETS.items():
        wins = windows(clocks, edges, start, stop)[1:4]
        if len(wins) < 3:
            raise AssertionError(f"{name}: only {len(wins)} complete windows")
        sigs = [signatures(w, list(clocks[0][1])) for w in wins]
        counts = [len(w) for w in wins]
        repeat = all(s == sigs[0] for s in sigs[1:])
        print(f"{name}: windows of {counts} clocks (expected {count}); +5V {sigs[0][rail]} "
              f"(printed {rail_sig}); repeatable={repeat}")
        if args.search:
            wanted = set(args.search.split(","))
            for key, value in sorted(sigs[0].items()):
                if value in wanted:
                    print(f"  {name}: {key[0]}.{key[1]} = {value}")
            continue
        if args.dump:
            for (chip, pin), value in sorted(sigs[0].items(), key=lambda kv: (int(kv[0][0][1:]), kv[0][1])):
                print(f"  {name}\t{chip}\t{pin}\t{value}")
            continue
        if table is None:
            continue
        printed = table.get(name, {})
        exact = mismatched = missing = 0
        differing = set()
        for (chip, pin), accepted in sorted(printed.items(), key=lambda kv: (int(kv[0][0][1:]), kv[0][1])):
            if accepted == {"-"}:
                continue
            expected = "/".join(sorted(accepted))
            observed = sigs[0].get((chip, pin))
            if observed is None:
                missing += 1
                print(f"  NOT IN NETLIST {chip}.{pin} printed {expected}")
            elif observed in accepted:
                exact += 1
            else:
                mismatched += 1
                differing.add((chip, pin))
                print(f"  DIFF {chip}.{pin}: simulated {observed}, printed {expected}")
        print(f"  {name}: {exact} exact, {mismatched} different, {missing} not in netlist")
        known = KNOWN.get(name)
        if known is None or args.phase != 10 or args.gate != "start-exclusive":
            failures += mismatched + missing
        elif differing != known or missing:
            print(f"  REGRESSION: expected differences {sorted(known)}, got {sorted(differing)}")
            failures += 1
        elif known:
            print(f"  {name}: PASS (the {len(known)} differences are the named, investigated exceptions)")
        else:
            print(f"  {name}: PASS (every printed signature matches)")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
