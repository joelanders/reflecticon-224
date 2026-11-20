#!/usr/bin/env python3
"""Grade the T&C+ARU stitch against the firmware's E8x multiply tables.

The WCS RAMs are loaded as the CPU leaves them (split_wcs_hex.py --physical:
bytes complemented, rows in execution order), so the program runs as the
firmware's: row 0 NOP, row 1 loads the operand from the bus and multiplies,
row 2 XFER and RESET, row 3 puts RR on the bus for the CPU. The tb plays the
X register: it writes ~op onto the DAB (the SBC's bus drivers invert what the
CPU writes) and reads the ARU result register directly once per frame. The
CPU reads RR through the same inverting bus, so the value it sees is ~RR,
and that is what the firmware's table holds.

Every operand of every call must match the table exactly.

Usage: check_tc_aru_multiply.py <call#> <rr.log>
"""
import re
import sys
from collections import Counter
from pathlib import Path

ROM = {
    1: {0x5555: 0xC7FF, 0xAAAA: 0x37FF, 0x6666: 0xBCCC, 0x9999: 0x4332},
    2: {0x5555: 0x3800, 0xAAAA: 0xC800, 0x6666: 0x4333, 0x9999: 0xBCCD},
    3: {0x5555: 0x8FFF, 0xAAAA: 0x6FFF, 0x6666: 0x8000, 0x9999: 0x7FFF},
    4: {0x5555: 0x7000, 0xAAAA: 0x9000, 0x6666: 0x7FFF, 0x9999: 0x8000},
    5: {0x3333: 0x64CE, 0xCCCC: 0x9B33, 0x3FFF: 0x7DFF, 0xC000: 0x8202},
}
OPS = {1: [0x5555, 0xAAAA, 0x6666, 0x9999],
       5: [0x3333, 0xCCCC, 0x3FFF, 0xC000]}


def main() -> int:
    call = int(sys.argv[1])
    log = Path(sys.argv[2]).read_text()
    ops = OPS[5] if call == 5 else OPS[1]
    table = ROM[call]

    by_op: dict[int, list[int]] = {}
    for line in log.splitlines():
        m = re.match(r"RR f=\d+ a=\d+ op=(\d+) v=([0-9a-f]{4})", line)
        if m and int(m.group(1)) >= 0:
            by_op.setdefault(int(m.group(1)), []).append(int(m.group(2), 16))

    results = []
    for i, op in enumerate(ops):
        values = by_op.get(i, [])
        # the settled result: RR holds the product for the rest of the operand's window
        settled = Counter(values[len(values) // 2:]).most_common(1)[0][0] if values else None
        want = table[op]
        cpu = None if settled is None else (~settled) & 0xFFFF
        results.append((f"call{call} op={op:04x} exact", cpu == want,
                        f"RR={settled if settled is None else f'{settled:04x}'} "
                        f"CPU reads {cpu if cpu is None else f'{cpu:04x}'} want={want:04x}"))
    results.append((f"coverage call{call}", len(by_op) >= 4, f"{len(by_op)} operands"))

    width = max(len(n) for n, _, _ in results)
    failed = sum(0 if ok else 1 for _, ok, _ in results)
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
