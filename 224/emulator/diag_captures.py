#!/usr/bin/env python3
"""Capture the WCS images of Diagnostic Programs 3 and 6 from your own firmware.

The gate-level HP 5004A checks (../gate-level-verilog/hp5004a) need the
machine state the service manual's signature tables were measured in: the
firmware running a diagnostic program. Its microcode comes from the
firmware, so it is not in this repository. This script runs your 224X v8.1
chip files on the emulator (diag_state.cpp), presses the panel buttons the
service manual describes (PROGRAM 7, then PROGRAM 3 or PROGRAM 6), and saves
the WCS as the CPU sees it.

    python3 emulator/diag_captures.py "$LEXICON_FIRMWARE/224X v8_1"

writes captures/224x_diag_pgm3.hex and captures/224x_diag_pgm6.hex (the
repository's captures/ folder) and checks each against the SHA-256 the
checkers expect.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
CAPTURES = REPO / "captures"
BUILD = HERE / "build"

# Panel switch bank 0: 0x40 = PROGRAM, then 0x04 = 3 or 0x20 = 6, each pressed
# and released; CPU cycles from power-on (2.048 MHz).
PROGRAMS = {
    "pgm3": dict(cycles=4000000, presses=[(2000005, 0, 0x40), (2120002, 0, 0x00), (2320004, 0, 0x04), (2570002, 0, 0x00)],
                 lowercase=True, sha256="19fb2e3c3f518ac8930d8c3173661af712778cbaa69e7941cd1b66a63271260c"),
    "pgm6": dict(cycles=5000000, presses=[(2000005, 0, 0x40), (2120002, 0, 0x00), (2320004, 0, 0x20), (2570006, 0, 0x00)],
                 lowercase=False, sha256="f9b9fed81f37bfdfaa86a403776ba4aac7d844b33e3eaad8b23c6264c36bf54e"),
}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    firmware = Path(sys.argv[1])
    if not (firmware / "SBC1 2716.BIN").exists():
        sys.exit(f"{firmware}: no 'SBC1 2716.BIN' (a 224X v8.1 chip set is expected)")
    CAPTURES.mkdir(exist_ok=True)
    subprocess.run(["make", "-s", "-C", str(HERE), "build/diag_state"], check=True)
    binary = BUILD / "diag_state"
    failed = False
    for name, spec in PROGRAMS.items():
        raw = BUILD / f"{name}.raw.hex"
        arguments = [str(binary), str(firmware), str(spec["cycles"])]
        for cycle, bank, value in spec["presses"]:
            arguments += [str(cycle), str(bank), f"0x{value:02x}"]
        subprocess.run(arguments, check=True, stdout=subprocess.DEVNULL, env=dict(os.environ, WCS_OUT=str(raw)))
        text = raw.read_text()
        # the checkers hash the file text; the reference captures differ only in hex case
        text = text.lower() if spec["lowercase"] else text
        target = CAPTURES / f"224x_diag_{name}.hex"
        target.write_text(text)
        digest = hashlib.sha256(text.encode()).hexdigest()
        ok = digest == spec["sha256"]
        failed |= not ok
        print(f"{target.relative_to(REPO)}: sha256 {digest[:12]} {'OK' if ok else 'DIFFERENT from the reference capture'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
