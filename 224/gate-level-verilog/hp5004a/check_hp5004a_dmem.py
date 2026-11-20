#!/usr/bin/env python3
"""Check the 224X DMEM board against the v8.2.1 HP 5004A signature table.

Same composition and firmware state as tools/check_hp5004a_aru.py (Diagnostic
Program 3, display E0A), with the printed setup:

  Lift U65 pin 13 and jumper to U65 pin 1   (applied to a temporary copy of
                                             the generated DMEM board)
  START = STOP = MSB of CPC, U65 pin 8 (rising)
  CLOCK = RESET/, U58A pin 1 (rising); one sample per pass

With the jumper both halves of U65 count together, so a window is 4096
passes (+5V = 826P). The run simulates about 10,300 passes.

  python3 tools/check_hp5004a_dmem.py                  # unmodified boards (crop U48)
  python3 tools/check_hp5004a_dmem.py --topology direct
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path

import check_hp5004a_aru as aru
from hp5004a_signature import compress_bits, format_signature

ROOT = aru.ROOT
HDL = aru.HDL
TABLE = ROOT / "hp5004a/tables/dmem_v821.tsv"
PASS_TICKS = 270

# Printed entries that do not match, each investigated (see the report):
#   U65.4     manual misprint: with the printed jumper 1QB = 2QB, and U65.10
#             prints 19H6 (the ideal count bit 9) while U65.4 prints 0000.
# U17 is a 33 ohm x 8 resistor pack in series with the DRAM address lines.
# The probes watch only logic parts, so its printed pins are read at the
# multiplexer outputs that drive them: pin k and pin 17-k carry the same line,
# AD0..AD3 from U36 Y0..Y3 (pins 4/7/9/12) and AD4..AD7 from U18 Y0..Y3.
# A series resistor shows the same logic level at both ends.
U17_LINES = {("U17", 13): ("U36", 4), ("U17", 4): ("U36", 4),   # AD0
             ("U17", 9): ("U36", 7), ("U17", 8): ("U36", 7),    # AD1
             ("U17", 11): ("U36", 9), ("U17", 6): ("U36", 9),   # AD2
             ("U17", 14): ("U36", 12), ("U17", 3): ("U36", 12), # AD3
             ("U17", 12): ("U18", 4), ("U17", 5): ("U18", 4),   # AD4
             ("U17", 10): ("U18", 7), ("U17", 7): ("U18", 7),   # AD5
             ("U17", 15): ("U18", 9), ("U17", 2): ("U18", 9),   # AD6
             ("U17", 16): ("U18", 12), ("U17", 1): ("U18", 12)} # AD7
KNOWN = {("U65", 4)}
WINDOW = 4096


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "out/hp5004a_dmem")
    parser.add_argument("--topology", choices=("additive", "direct", "crop"), default="direct")
    parser.add_argument("--passes", type=int, default=10300)
    parser.add_argument("--pre", type=float, default=10.0, help="sample this many ns before the RESET/ edge")
    parser.add_argument("--gate", choices=("start-inclusive", "start-exclusive"), default="start-inclusive")
    parser.add_argument("--reuse", action="store_true", help="reuse the last transcript instead of simulating")
    parser.add_argument("--dump", action="store_true")
    return parser.parse_args()


def jumpered_dmem(out: Path) -> Path:
    text = (HDL / "generated/dmem_board.v").read_text(encoding="utf-8")
    old = ".p_13(Net_U63_A4), .p_14(n_5V)); // 74LS393_2 -> 74LS393 74LS393"
    line = next(l for l in text.splitlines() if " U65 (" in l)
    if old not in line or ".p_1(Net_U50_A4)" not in line:
        raise AssertionError("U65 pins 1/13 are not where the printed setup expects them")
    text = text.replace(line, line.replace(old, old.replace("Net_U63_A4", "Net_U50_A4")))
    copy = out / "dmem_board_u65_jumper.v"
    copy.write_text(text, encoding="utf-8")
    return copy


def main() -> int:
    args = parse_args()
    out = (args.out_dir / "base").resolve()
    out.mkdir(parents=True, exist_ok=True)
    chips = aru.aru_instances(HDL / "generated/dmem_board.v")
    sim_args = argparse.Namespace(
        wcs=aru.DEFAULT_WCS, out_dir=out, topology=args.topology, gate_swaps=False,
        aru_source=HDL / "generated/aru_board.v", cycles=args.passes * PASS_TICKS, phase=0,
        define=["DMEM_MODE=1", "SAMPLE_FROM=0", f"DMEM_PRE={args.pre}"],
        dmem_chips=chips, dmem_source=jumpered_dmem(out))
    cached = out / f"transcript_{args.topology}.txt"
    if args.reuse and cached.exists():
        transcript = cached.read_text(encoding="utf-8")
    else:
        transcript = aru.simulate(sim_args, {})
        cached.write_text(transcript, encoding="utf-8")
    clocks, edges = aru.parse(transcript, chips, 0)
    wins = aru.windows(clocks, edges, ("CPCMSB", "1"), ("CPCMSB", "1"), gate=args.gate)
    if len(wins) < 2:
        raise AssertionError(f"only {len(wins)} complete windows; simulate more passes")
    sigs = [aru.signatures(w, list(clocks[0][1])) for w in wins[:2]]
    counts = [len(w) for w in wins[:2]]
    print(f"DMEM: windows of {counts} clocks (expected {WINDOW}); +5V {sigs[0][('U65', 14)]} (printed 826P); "
          f"repeatable={sigs[0] == sigs[1]}")
    if args.dump:
        for key, value in sorted(sigs[0].items(), key=lambda kv: (int(kv[0][0][1:]), kv[0][1])):
            print(f"  {key[0]}\t{key[1]}\t{value}")
        return 0
    printed = {}
    with TABLE.open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if row["signature"] != "-":
                printed[(row["chip"], int(row["pin"]))] = {row["signature"], row["alternative"]} - {""}
    exact = 0
    differing = []
    for key, accepted in sorted(printed.items(), key=lambda kv: (int(kv[0][0][1:]), kv[0][1])):
        observed = sigs[0].get(U17_LINES.get(key, key))
        if observed in accepted:
            exact += 1
        else:
            differing.append((key, observed, "/".join(sorted(accepted))))
    for (chip, pin), observed, expected in differing:
        print(f"  DIFF {chip}.{pin}: simulated {observed}, printed {expected}")
    print(f"  DMEM: {exact} exact, {len(differing)} different")
    if {key for key, _, _ in differing} == KNOWN and args.gate == "start-inclusive":
        print(f"  DMEM: PASS (the {len(differing)} differences are the named, investigated exceptions)")
        return 0
    print("  REGRESSION: the differences are not the named exceptions")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
