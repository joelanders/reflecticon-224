#!/usr/bin/env python3
"""The FPC's self-test (debug) multiplexer.
"""

import re
import sys


def fail(msg):
    print(f"FAIL  {msg}")
    raise SystemExit(1)


def main(path):
    text = open(path).read()
    ext = re.search(r"DMUX ext_ck_ignored cnt=([0-9a-fx]+) cp=([01x]) ext_cp_edges=(\d+)", text)
    if not ext:
        fail("missing ext_ck_ignored line")

    summary = re.search(
        r"DMUX summary ext_cp_edges=(\d+) o2_cp_edges=(\d+) count_changes=(\d+) "
        r"read_low=(\d+) write_low=(\d+) reset_low=(\d+) "
        r"u42_enabled=(\d+) out_nonzero=(\d+) final_cnt=([0-9a-fx]+)",
        text,
    )
    if not summary:
        fail("missing summary line")

    ext_cp = int(summary.group(1))
    o2_cp = int(summary.group(2))
    count_changes = int(summary.group(3))
    read_low = int(summary.group(4))
    write_low = int(summary.group(5))
    reset_low = int(summary.group(6))
    u42_enabled = int(summary.group(7))
    out_nonzero = int(summary.group(8))
    final_cnt = summary.group(9)

    checks = [
        ("FPC_CK ignored while FPC_DBUG=1", ext_cp == 0, f"ext_cp_edges={ext_cp}"),
        ("O2 clocks internal CP through U4", o2_cp >= 150, f"o2_cp_edges={o2_cp}"),
        ("U7/U8 debug counter advances", count_changes >= 120, f"count_changes={count_changes}"),
        ("U5 debug read OE asserts", read_low > 0, f"read_low={read_low}"),
        ("U5 debug write load asserts", write_low > 0, f"write_low={write_low}"),
        ("U5 debug reset asserts", reset_low > 0, f"reset_low={reset_low}"),
        ("U42 output mux is enabled by U3", u42_enabled > 0, f"u42_enabled={u42_enabled}"),
        ("U42 drives self-test OUT states", out_nonzero > 0, f"out_nonzero={out_nonzero}"),
        ("final counter is known", "x" not in final_cnt.lower(), f"final_cnt={final_cnt}"),
    ]

    ok = True
    width = max(len(name) for name, _, _ in checks)
    for name, passed, detail in checks:
        print(f"{'PASS' if passed else 'FAIL'}  {name:<{width}}  {detail}")
        ok &= passed

    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: check_fpc_debug_mux.py <log>", file=sys.stderr)
        raise SystemExit(2)
    main(sys.argv[1])
