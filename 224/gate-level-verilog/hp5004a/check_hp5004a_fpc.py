#!/usr/bin/env python3
"""Check the 224X FPC board against the v8.2.1 HP 5004A signature table.

Diagnostic Program 6 (FPC Signatures, display E0F), captured from the v8.1
firmware (docs/wcs_dumps/224x_diag_pgm6_e0f_2026-09-25.hex). Its setup
single-steps the DSP to leave R1=E666, R2=3333, R3=AAAA and XREG input 0555;
the 98-row pass then writes R3, R2, R1 and the XREG word to DAC channels
A..D and never reads the ADC. Printed setup:

  Lift pin 11 of SAR IC (U26) on AIN module and jumper to +5V
  START = STOP = RESET, U4 pin 2 (falling)
  CLOCK = FPCCLK, U4 pin 5 (rising)          98 clocks per window (+5V 96F6)

The AIN module is not modeled: the modified SAR's output is a constant ADC
code supplied by the bench (--adc), an input chosen, not derived.

  python3 tools/check_hp5004a_fpc.py                     # gain 3, reconstructed U6, ADC FFF
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import sys
from pathlib import Path

import check_hp5004a_aru as aru

ROOT = aru.ROOT
HDL = aru.HDL
TABLE = ROOT / "hp5004a/tables/fpc_v821.tsv"
WCS = ROOT.parent / "captures/224x_diag_pgm6.hex"
WCS_SHA256 = "f9b9fed81f37bfdfaa86a403776ba4aac7d844b33e3eaad8b23c6264c36bf54e"
ROW_TICKS = 9

# Printed entries that do not match under the bench's older constant ADC drive
# (--sar off), each investigated (see the report):
#   ADC path   the 12 AD inputs (U39/U38/U28/U27 parallel inputs) and the shift
#              and buffer nets that carry them. The printed AD signatures vary:
#              the procedure lifts the AIN SAR's pin 11 (the Am2504 D input) to
#              +5V, so the converter walks a fixed pattern. The bench models that
#              (tb SAR_JUMPER, the default here) and all 27 match; a constant
#              --adc cannot.
# With the jumpered SAR every printed entry matches (388/388).
ADC_PATH = {("U26", 2), ("U26", 11), ("U26", 13), ("U26", 15), ("U26", 17),
            ("U27", 3), ("U27", 4), ("U27", 5), ("U27", 6), ("U27", 7),
            ("U27", 12), ("U27", 13), ("U27", 14), ("U27", 15),
            ("U28", 3), ("U28", 4), ("U28", 5), ("U28", 6), ("U28", 15),
            ("U38", 3), ("U38", 4), ("U38", 5), ("U38", 6),
            ("U39", 3), ("U39", 4), ("U39", 5), ("U39", 6)}
KNOWN = {"off": ADC_PATH, "msb-inverted": set()}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "out/hp5004a_fpc")
    parser.add_argument("--topology", choices=("additive", "direct", "crop"), default="direct")
    parser.add_argument("--adc", type=lambda x: int(x, 0), default=0xFFF, help="constant 12-bit ADC code")
    parser.add_argument("--sar", choices=("off", "msb-inverted", "msb-direct"), default="msb-inverted",
                        help="drive AD0-11 from the AIN module's Am2504 with pin 11 (D) jumpered to +5V, as the "
                             "procedure says (the bench's own CNV CK and STC clock it); msb-inverted = AD11 is "
                             "pin 23 Q11/ as drawn, msb-direct = pin 21 Q11")
    parser.add_argument("--gain", type=int, default=3, help="IGA1:IGA0; the table prints 96F6 (constant 1) on both")
    parser.add_argument("--u6", default="u6_74s287_primary_functional_hold_0_255.hex",
                        help="FPC U6 timing PROM image in 04-structural-hdl/rom (the fuses were never read; "
                             "this constraint-derived image matches every printed U6 signature)")
    parser.add_argument("--pre", type=float, default=10.0)
    parser.add_argument("--gate", choices=("start-inclusive", "start-exclusive"), default="start-inclusive")
    parser.add_argument("--passes", type=int, default=40)
    parser.add_argument("--reuse", action="store_true")
    parser.add_argument("--dump", action="store_true")
    parser.add_argument("--quiet", action="store_true", help="print only the totals")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if hashlib.sha256(WCS.read_bytes()).hexdigest() != WCS_SHA256:
        raise AssertionError("firmware-captured Program 6 WCS changed")
    stim = f"sar-{args.sar}" if args.sar != "off" else f"adc{args.adc:03x}"
    out = (args.out_dir / f"{stim}_g{args.gain}_{args.topology}_{Path(args.u6).stem}").resolve()
    out.mkdir(parents=True, exist_ok=True)
    chips = aru.aru_instances(HDL / "generated/fpc_board.v")
    defines = ["FPC_MODE=1", "SAMPLE_FROM=0", f"FPC_PRE={args.pre}",
               "R1_INIT=16'he666", "R2_INIT=16'h3333", "R3_INIT=16'haaaa",
               "XREG_LOW_DATA=8'h55", "XREG_HIGH_DATA=8'h05", f"ADC_CODE=12'h{args.adc:03x}",
               f"GAIN_CODE={args.gain}", f'U6_IMAGE="{HDL / "rom" / args.u6}"']
    if args.sar != "off":
        defines += ["SAR_JUMPER=1", f"SAR_MSB_INVERT={1 if args.sar == 'msb-inverted' else 0}"]
    known = KNOWN.get(args.sar)  # None for msb-direct: a negative control, never a pass
    sim_args = argparse.Namespace(
        wcs=WCS, out_dir=out, topology=args.topology, gate_swaps=False,
        aru_source=HDL / "generated/aru_board.v", cycles=args.passes * 98 * ROW_TICKS + 200, phase=0,
        define=defines, fpc_chips=chips)
    cached = out / f"transcript_{args.topology}.txt"
    if args.reuse and cached.exists():
        transcript = cached.read_text(encoding="utf-8")
    else:
        transcript = aru.simulate(sim_args, {})
    clocks, edges = aru.parse(transcript, chips, 0)
    wins = aru.windows(clocks, edges, ("FPCRESET", "0"), ("FPCRESET", "0"), gate=args.gate)[2:]
    if len(wins) < 3:
        raise AssertionError(f"only {len(wins)} complete windows")
    sigs = [aru.signatures(w, list(clocks[0][1])) for w in wins[:3]]
    counts = [len(w) for w in wins[:3]]
    rail = sigs[0][("U4", 16)]
    print(f"FPC ({stim}, gain {args.gain}): windows of {counts} clocks (expected 98); "
          f"+5V {rail} (printed 96F6); repeatable={all(s == sigs[0] for s in sigs)}")
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
        observed = sigs[0].get(key)
        if observed in accepted:
            exact += 1
        else:
            differing.append((key, observed, "/".join(sorted(accepted))))
    if not args.quiet:
        for (chip, pin), observed, expected in differing:
            print(f"  DIFF {chip}.{pin}: simulated {observed}, printed {expected}")
    print(f"  FPC: {exact} exact, {len(differing)} different")
    if {key for key, _, _ in differing} == known and args.gate == "start-inclusive":
        if differing:
            print(f"  FPC: PASS (the {len(differing)} differences are the named, investigated exceptions)")
        else:
            print("  FPC: PASS (every printed signature matches)")
        return 0
    print("  REGRESSION: the differences are not the named exceptions")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
