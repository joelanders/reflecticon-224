#!/usr/bin/env python3
"""Check that netlists/*.net are what KiCad exports from the schematics.

Stage 2 generates its Verilog from these netlists, so a schematic edit reaches
the simulation only through them. This exports each board with kicad-cli and
compares the result with the committed file.

  python3 check_netlists.py            # compare; exit 1 on any difference
  python3 check_netlists.py --write    # after editing a schematic: update netlists/

kicad-cli comes from KiCad 9 (set KICAD_CLI if it is not at the macOS default).
"""
import difflib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
KICAD_CLI = os.environ.get("KICAD_CLI", "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")
BOARDS = {
    "tc": "tc/Timing_And_Control.kicad_sch",
    "aru": "aru/ARU Project.kicad_sch",
    "dmem": "dmem/00-dmem-board.kicad_sch",
    "dmem-io": "dmem-io/Dmem-and-IO.kicad_sch",
    "fpc": "fpc/00-fpc-board.kicad_sch",
}


def export(schematic: Path) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "board.net"
        subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
                        "-o", str(out), str(schematic)], check=True, capture_output=True)
        text = out.read_text(encoding="utf-8")
    # paths relative to this folder, and no export date: an unchanged schematic
    # always gives the same file
    text = text.replace(str(HERE) + "/", "")
    return re.sub(r'\n    \(date "[^"]*"\)', "", text, count=1)


def main() -> int:
    write = "--write" in sys.argv
    bad = 0
    for board, schematic in BOARDS.items():
        committed = HERE / "netlists" / f"{board}.net"
        fresh = export(HERE / schematic)
        if write:
            committed.write_text(fresh, encoding="utf-8")
            print(f"{board}: written")
            continue
        old = committed.read_text(encoding="utf-8")
        if old == fresh:
            print(f"{board}: netlist matches the schematic")
            continue
        bad += 1
        diff = list(difflib.unified_diff(old.splitlines(), fresh.splitlines(), "committed", "exported", lineterm="", n=1))
        print(f"{board}: DIFFERS from the schematic ({len(diff)} diff lines); run with --write after checking")
        print("\n".join(diff[:20]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
