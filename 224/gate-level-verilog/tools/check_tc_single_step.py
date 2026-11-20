#!/usr/bin/env python3
"""Check the compact E50 T&C halt/single-step row contract."""

from __future__ import annotations

import re
import sys
from pathlib import Path


WAIT_RE = re.compile(r"^E50_WAIT step=(\d+) guard=(\d+) wcsa=(\d+) halt=([01xzXZ])$")
SAMPLE_RE = re.compile(
    r"^E50SAMPLE tag=(\S+) i=(\d+) halt=([01xzXZ]) wcsa=(\d+) "
    r"ofst=([0-9a-fA-FxzXZ]{4}) memac=([01xzXZ]) memw=([01xzXZ]) "
    r"rdr=([01xzXZ]) rd_xreg=([01xzXZ]) reset=([01xzXZ]) "
    r"xfer=([01xzXZ]) csign=([01xzXZ])$"
)
CTRL_RE = re.compile(
    r"^E50CTRL total=(\d+) wstb=([0-9a-fA-F]{32}) "
    r"rd_xreg=([0-9a-fA-F]{32}) rdr=([0-9a-fA-F]{32}) "
    r"memw=([0-9a-fA-F]{32}) wr_xreg=([0-9a-fA-F]{32}) "
    r"rd_ad=([0-9a-fA-F]{32}) reset=([0-9a-fA-F]{32})$"
)

# Re-keyed 2026-07-05: under the schematic-true CLK_NEW (= DAB_RSTB/) the
# WCS[126]/WCS[127] payload rows assert MEMW_n/RDRREG_n at exactly wcsa=127
# and wcsa=0 (one-row prefetch: content(R) executes at address R+1, wrapping
# 127->0), with no residual bleed into wcsa=1. The old two-bug model's
# request mask {127,0,1} carried an extra trailing-edge row (1) that never
# reflected a real assertion; the trailing edge moves one row earlier to
# {127,0}. DAB_WSTB itself is a per-row strobe unrelated to payload content
# (fires on every row the bench visits), so its observed-rows mask is
# unaffected by the clock migration. See the 2026-07-05 sweep note.
E50_REQUEST_MASK = int("80000000000000000000000000000001", 16)
E50_WSTB_OBSERVED_MASK = int("80000000000000000000000000000007", 16)


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    waits = []
    samples: dict[str, list[dict[str, int | str]]] = {}
    ctrl = None
    done = "E50_DONE" in text

    for line in text.splitlines():
        if m := WAIT_RE.match(line):
            waits.append(
                {
                    "step": int(m.group(1)),
                    "guard": int(m.group(2)),
                    "wcsa": int(m.group(3)),
                    "halt": m.group(4).lower(),
                }
            )
            continue
        if m := SAMPLE_RE.match(line):
            tag = m.group(1)
            samples.setdefault(tag, []).append(
                {
                    "i": int(m.group(2)),
                    "halt": m.group(3).lower(),
                    "wcsa": int(m.group(4)),
                    "ofst": m.group(5).lower(),
                    "memac": m.group(6).lower(),
                    "memw": m.group(7).lower(),
                    "rdr": m.group(8).lower(),
                    "rd_xreg": m.group(9).lower(),
                    "reset": m.group(10).lower(),
                    "xfer": m.group(11).lower(),
                    "csign": m.group(12).lower(),
                }
            )
            continue
        if m := CTRL_RE.match(line):
            ctrl = {
                "total": int(m.group(1)),
                "wstb": int(m.group(2), 16),
                "rd_xreg": int(m.group(3), 16),
                "rdr": int(m.group(4), 16),
                "memw": int(m.group(5), 16),
                "wr_xreg": int(m.group(6), 16),
                "rd_ad": int(m.group(7), 16),
                "reset": int(m.group(8), 16),
            }

    results: list[tuple[str, bool, str]] = []

    wait_ok = bool(waits) and waits[0] == {"step": 127, "guard": waits[0]["guard"], "wcsa": 127, "halt": "1"} and waits[0]["guard"] < 128
    results.append(("parks from free-run at WCSA 127", wait_ok, str(waits[:1])))

    hold = samples.get("hold", [])
    hold_ok = len(hold) >= 4 and all(s["halt"] == "0" and s["wcsa"] == 127 for s in hold)
    results.append(("HALT low holds WCSA 127 stable", hold_ok, str([s["wcsa"] for s in hold])))

    release = samples.get("release", [])
    release_seq = [s["wcsa"] for s in release]
    release_ok = release_seq[:3] == [127, 0, 1] and all(s["halt"] == "1" for s in release[:3])
    results.append(("one-frame release samples 127,0,1", release_ok, str(release_seq)))

    rehold = samples.get("rehold", [])
    rehold_ok = len(rehold) >= 4 and all(s["halt"] == "0" and s["wcsa"] == 2 for s in rehold)
    results.append(("HALT low reholds at WCSA 2 after release", rehold_ok, str([s["wcsa"] for s in rehold])))

    if ctrl is None:
        results.append(("E50CTRL summary present", False, "missing"))
    else:
        results.append(("E50CTRL summary present", True, f"total={ctrl['total']}"))
        results.append(("DAB_WSTB observed rows are 127,0,1,2", ctrl["wstb"] == E50_WSTB_OBSERVED_MASK, f"mask={ctrl['wstb']:032x}"))
        results.append(("MEMW/ rows are 127,0", ctrl["memw"] == E50_REQUEST_MASK, f"mask={ctrl['memw']:032x}"))
        results.append(("RDRREG/ rows are 127,0", ctrl["rdr"] == E50_REQUEST_MASK, f"mask={ctrl['rdr']:032x}"))
        results.append(("RD_XREG/ never asserts", ctrl["rd_xreg"] == 0, f"mask={ctrl['rd_xreg']:032x}"))
        results.append(("WR_XREG/ never asserts", ctrl["wr_xreg"] == 0, f"mask={ctrl['wr_xreg']:032x}"))
        results.append(("RD_AD/ never asserts", ctrl["rd_ad"] == 0, f"mask={ctrl['rd_ad']:032x}"))
        results.append(("RESET/ not active in request aperture", ctrl["reset"] == 0, f"mask={ctrl['reset']:032x}"))
        results.append(("request-edge count stable", ctrl["total"] == 7, f"total={ctrl['total']}"))

    results.append(("simulation completed", done, "E50_DONE present" if done else "missing"))

    width = max(len(name) for name, _, _ in results)
    ok_all = True
    for name, ok, detail in results:
        ok_all &= ok
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{sum(1 for _, ok, _ in results if ok)}/{len(results)} rules pass")
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
