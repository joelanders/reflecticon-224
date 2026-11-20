#!/usr/bin/env python3
"""The FPC board's data-bus interface.

The testbench holds the FPC in normal mode and forces one prepared read word
at the U25/U26 inputs, then checks only the bus side: RD_AD/ driving the
bus, WR_DA/ capturing it, and the SDA channel-select capture.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


EXPECTED = {
    "write_bus_driven": {
        "cp": "0",
        "read_oe_n": "1",
        "write_load_n": "1",
        "dab": "a55a",
        "cap": "0000",
        "sda": "0",
    },
    "write_captured": {
        "cp": "0",
        "read_oe_n": "1",
        "write_load_n": "1",
        "dab": "a55a",
        "cap": "a55a",
        "sda": "a",
    },
    "read_bus_driven": {
        "cp": "0",
        "read_oe_n": "0",
        "write_load_n": "1",
        "dab": "1357",
        "cap": "a55a",
        "sda": "a",
    },
    "bus_release": {
        "cp": "0",
        "read_oe_n": "1",
        "write_load_n": "1",
        "dab": "zzzz",
        "cap": "a55a",
        "sda": "a",
    },
}


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    rows: dict[str, dict[str, str]] = {}

    pat = re.compile(
        r"FP label=(\w+) cp=([01xz]) read_oe_n=([01xz]) write_load_n=([01xz]) "
        r"dab=([0-9a-fxz]{4}) cap=([0-9a-fxz]{4}) sda=([0-9a-fxz])"
    )
    for line in text.splitlines():
        m = pat.match(line)
        if not m:
            continue
        rows[m.group(1)] = {
            "cp": m.group(2),
            "read_oe_n": m.group(3),
            "write_load_n": m.group(4),
            "dab": m.group(5),
            "cap": m.group(6),
            "sda": m.group(7),
        }

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    check("coverage: all test rows",
          set(rows) == set(EXPECTED),
          f"got={sorted(rows)}")

    for label, expected in EXPECTED.items():
        row = rows.get(label)
        if row is None:
            check(label, False, "missing")
            continue
        mismatches = [
            f"{k}: got {row[k]} want {v}"
            for k, v in expected.items()
            if row[k] != v
        ]
        check(label, not mismatches, "; ".join(mismatches) if mismatches else str(row))

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
        failed += 0 if ok else 1
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
