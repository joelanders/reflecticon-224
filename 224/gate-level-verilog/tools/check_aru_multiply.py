#!/usr/bin/env python3
"""Grade the ARU-only E8CTL replay against the firmware's E8x multiply table.

The stimulus is the five control signatures the T&C produces for the E8x
calls (not a Fig 3.4 interpretation). The tb writes ~op onto the DAB (the
SBC's bus drivers invert what the CPU writes) and reads the result register
directly; the CPU reads it through the same inverting bus, so the value it
sees is ~RR. Every case must equal the table exactly.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path



def main() -> int:
    text = Path(sys.argv[1]).read_text()
    results: list[tuple[str, bool, str]] = []
    n = 0
    for line in text.splitlines():
        m = re.match(r"ECTL (\d+) f=(\w+) c=(\w+) cs=(\d) rr=(\w+) want=(\w+)", line)
        if not m:
            continue
        n += 1
        idx = int(m.group(1))
        op = int(m.group(2), 16)
        coeff = int(m.group(3), 16)
        cs = int(m.group(4))
        rr = int(m.group(5), 16)
        want = int(m.group(6), 16)
        cpu = (~rr) & 0xFFFF
        results.append((f"case {idx} op={op:04x} exact", cpu == want,
                        f"RR={rr:04x} CPU reads {cpu:04x} want={want:04x}"))
    results.append(("coverage: 20 ROM cases", n == 20, f"{n} seen"))

    width = max(len(name) for name, _, _ in results)
    failed = sum(0 if ok else 1 for _, ok, _ in results)
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
