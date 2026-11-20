#!/usr/bin/env python3
"""The emulator still produces the firmware runs that qualified against the board-level machine.

    python3 tests/check_qualified.py            # run and compare (a few seconds; needs your ROMs)
    python3 tests/check_qualified.py --record   # after a full qualification: store the new digests

tests/qualify_firmware.py compares the emulator's firmware runs with the
board-level machine's, record by record; it needs board-level reference runs
that take about ten minutes to make. When the emulator passed it, the
SHA-256 of each of its outputs was stored in tests/qualified.json. This check
reruns the same cases on the emulator alone and compares the digests: any
change in the emulator's CPU state, WCS writes or audio events shows up here
in seconds. A mismatch means: rerun the full qualification (make
emulator-qualify-full), and if it passes, --record.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIGESTS = ROOT / "tests" / "qualified.json"
FIRMWARE = Path(os.environ.get("LEXICON_FIRMWARE", ROOT.parent / "firmware"))
CASES = {
    "boot": dict(cycles=18000000, controls=None),
    "max-delay": dict(cycles=5000000, controls=ROOT.parent / "board-level-verilog/examples/v81-max-delay.controls"),
}


def digests(case: str, spec: dict, factory: Path) -> dict[str, str]:
    out = ROOT / "build" / "qualified"
    out.mkdir(parents=True, exist_ok=True)
    prefix = out / case
    command = [str(factory), str(FIRMWARE / "224X v8_1"), str(spec["cycles"]), str(prefix)]
    if spec["controls"]:
        command.append(str(spec["controls"]))
    log = subprocess.run(command, check=True, capture_output=True, text=True).stdout
    # the run-time lines hold wall-clock time; everything else is the machine's
    log = "".join(line + "\n" for line in log.splitlines() if not line.startswith(("T ", "B ")))
    result = {"log": hashlib.sha256(log.encode()).hexdigest()}
    for suffix in ("wcs.csv", "events"):
        result[suffix] = hashlib.sha256(Path(f"{prefix}.{suffix}").read_bytes()).hexdigest()
    return result


def main() -> int:
    record = "--record" in sys.argv
    if not (FIRMWARE / "224X v8_1").is_dir():
        sys.exit(f"no firmware set '224X v8_1' in {FIRMWARE} (set LEXICON_FIRMWARE)")
    subprocess.run(["make", "-s", "-C", str(ROOT), "build/factory"], check=True)
    factory = ROOT / "build" / "factory"
    got = {case: digests(case, spec, factory) for case, spec in CASES.items()}
    if record:
        DIGESTS.write_text(json.dumps(got, indent=1) + "\n")
        print(f"recorded {DIGESTS.relative_to(ROOT)}")
        return 0
    want = json.loads(DIGESTS.read_text())
    failed = 0
    for case, files in got.items():
        for name, digest in files.items():
            same = want.get(case, {}).get(name) == digest
            failed += not same
            print(f"{case} {name}: {'as qualified' if same else 'CHANGED since the last qualification'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
