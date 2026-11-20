#!/usr/bin/env python3
"""Firmware runs on the row machine against the board-level machine.

Runs emulator/factory with the user's v8.1 ROM directory and compares, with an
existing board-level run of the same length and controls:
  S/P/E lines  CPU state every 250,000 cycles, diagnostic PCs, controls
  .wcs.csv     every CPU WCS write: cycle, writer PC, address, value, commit tick
  .events      DAC captures: time, channels, and every DAC/gain bit the
               board-level machine reports as known

  python3 tests/qualify_firmware.py boot        # 18M-cycle v8.1 boot
  python3 tests/qualify_firmware.py max-delay   # PG7/PG8 MAX DELAY with input pulses
  python3 tests/qualify_firmware.py lfo         # default program: live LFO WCS writes on signal
  python3 tests/qualify_firmware.py pots        # panel program change and pot moves
  python3 tests/qualify_firmware.py xl-boot     # 224XL v8.21 boot (LARC connected)
  python3 tests/qualify_firmware.py xl-larc     # 224XL LARC fader moves
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARDS = ROOT.parent / "board-level-verilog"
# Your own firmware: a folder with one subfolder per set ("224X v8_1", "224XL v8_21").
FIRMWARE = Path(os.environ.get("LEXICON_FIRMWARE", ROOT.parent / "firmware"))
ROM = FIRMWARE / "224X v8_1"
XL_ROM = FIRMWARE / "224XL v8_21"
CASES = {
    "boot": dict(cycles=18000000, reference=BOARDS / "build/v81-boot-final", controls=None),
    "max-delay": dict(cycles=5000000, reference=BOARDS / "build/v81-max-delay-final",
                      controls=BOARDS / "examples/v81-max-delay.controls"),
    # The default application program with its LFO rewriting WCS, fed input
    # pulses. Reference: the board-level machine run by hand, e.g.
    #   Vmachine_host factory ROM 19000000 build/qual/lfo-ref tests/controls/v81-default-program-pulses.controls
    "lfo": dict(cycles=19000000, reference=ROOT / "build/qual/lfo-ref",
                controls=ROOT / "tests/controls/v81-default-program-pulses.controls"),
    # A panel program change, then all six pots, with input pulses.
    "pots": dict(cycles=20500000, reference=ROOT / "build/qual/pots-ref",
                 controls=ROOT / "tests/controls/v81-program-and-pots.controls"),
    # 224XL v8.21: boot to CONCERT HALL on the LARC; then the six LARC faders.
    "xl-boot": dict(cycles=30000000, reference=ROOT / "build/qual/xl-ref", controls=None, rom=XL_ROM),
    "xl-larc": dict(cycles=34500000, reference=ROOT / "build/qual/xl-larc-ref",
                    controls=ROOT / "tests/controls/v821-larc-faders.controls", rom=XL_ROM),
}
# The references under build/qual/ come from the board-level machine run by hand:
#   ../board-level-verilog/build/machine-0/Vmachine_host factory ROM CYCLES build/qual/NAME-ref [CONTROLS]


def build():
    subprocess.run(["make", "-s", "-C", str(ROOT), "build/factory"], check=True)
    return ROOT / "build" / "factory"


def first_difference(ours, theirs, same=lambda a, b: a == b):
    for index, (a, b) in enumerate(zip(ours, theirs)):
        if not same(a, b):
            return index
    return None if len(ours) == len(theirs) else min(len(ours), len(theirs))


def event_same(ours: str, theirs: str) -> bool:
    a, b = ours.split(), theirs.split()
    if a[1] != b[1] or a[2] != b[2]:
        return False
    dac, dac_known, gain, gain_known = int(a[5]), int(b[6]), int(a[8]), int(b[9])
    if dac_known != 4095:  # unknown DAC word: the reference's gain is not meaningful
        return (dac ^ int(b[5])) & dac_known == 0
    return (dac ^ int(b[5])) & dac_known == 0 and (gain ^ int(b[8])) & gain_known == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("case", choices=CASES)
    args = parser.parse_args()
    case = CASES[args.case]
    binary = build()
    prefix = ROOT / "build/qual" / args.case
    prefix.parent.mkdir(parents=True, exist_ok=True)
    command = [str(binary), str(case.get("rom", ROM)), str(case["cycles"]), str(prefix)]
    if case["controls"]:
        command.append(str(case["controls"]))
    with open(f"{prefix}.log", "w") as log:
        subprocess.run(command, check=True, stdout=log)
    reference = case["reference"]
    failed = False

    ours = [l for l in Path(f"{prefix}.log").read_text().splitlines() if l[:1] in "SPE" and l[1:2] == " "]
    theirs = [l for l in Path(f"{reference}.log").read_text().splitlines() if l[:1] in "SPE" and l[1:2] == " "]
    n = first_difference(ours, theirs)
    print(f"state/diagnostic/control records: {len(ours)} vs {len(theirs)}", end="")
    if n is None:
        print(" -- identical")
    else:
        failed = True
        print(f" -- FIRST DIFFERENCE at record {n}:\n  rows:   {ours[n] if n < len(ours) else ''}\n"
              f"  boards: {theirs[n] if n < len(theirs) else ''}")

    ours = Path(f"{prefix}.wcs.csv").read_text().splitlines()
    theirs = Path(f"{reference}.wcs.csv").read_text().splitlines()
    n = first_difference(ours, theirs)
    print(f"WCS writes: {len(ours) - 1} vs {len(theirs) - 1}", end="")
    if n is None:
        print(" -- identical (cycle, writer PC, address, value, commit tick)")
    else:
        failed = True
        print(f" -- FIRST DIFFERENCE at line {n}:\n  rows:   {ours[n] if n < len(ours) else ''}\n"
              f"  boards: {theirs[n] if n < len(theirs) else ''}")

    ours = Path(f"{prefix}.events").read_text().splitlines()
    theirs = Path(f"{reference}.events").read_text().splitlines()
    unknown = sum(1 for line in theirs if line.split()[6] != "4095")
    n = first_difference(ours, theirs, event_same)
    print(f"audio events: {len(ours)} vs {len(theirs)} ({unknown} reference events carry unknown DAC bits)", end="")
    if n is None:
        print(" -- agree on every known bit")
    else:
        failed = True
        print(f" -- FIRST DIFFERENCE at event {n}:\n  rows:   {ours[n] if n < len(ours) else ''}\n"
              f"  boards: {theirs[n] if n < len(theirs) else ''}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
