#!/usr/bin/env python3
"""ARU alone under a Fig. 3.4 drive model, against the firmware's E8x multiply table.

The testbench serializes the coefficient bits onto M0/M1 as the manual's
Table 2 reads. The real machine delivers the coefficient differently (as
enable-gated taps of the shifting register), and with the as-drawn adder
carries the two differ by one LSB depending on CSIGN. So the product values
are reported, not graded; only the saturation clamps (robust to that
difference) and coverage are hard rules. The graded version of this test is
check_aru_multiply.py, which replays the control signals the T&C produces.
Expected values: the firmware's E8x multiply test.
"""
import re
import sys
from pathlib import Path

POS_OPERANDS = {0x5555, 0x6666}

def main() -> int:
    text = Path(sys.argv[1]).read_text()
    results = []
    n = 0
    for line in text.splitlines():
        m = re.match(r"ARES (\d+) f=(\w+) c=(\w+) cs=(\d) rr=(\w+) want=(\w+)", line)
        if not m:
            continue
        n += 1
        i, f = int(m.group(1)), int(m.group(2), 16)
        c6 = int(m.group(3), 16)
        rr, want = int(m.group(5), 16), int(m.group(6), 16)
        diff = ((rr - want) + 0x8000) % 0x10000 - 0x8000
        if want in (0x7FFF, 0x8000):
            results.append((f"case {i} clamp value (ROM-anchored)", rr == want,
                            f"rr={rr:04x} ROM={want:04x}"))
        else:
            # Drive superseded 2026-06-12: report the diff, do not gate on it.
            results.append((f"REPORT case {i} value (drive superseded, see header)", True,
                            f"rr={rr:04x} ROM={want:04x} diff={diff:+d}"))
    results.append(("coverage: 20 ROM cases", n == 20, f"{n} seen"))
    width = max(len(nm) for nm, _, _ in results)
    failed = sum(0 if ok else 1 for _, ok, _ in results)
    for nm, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {nm:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
