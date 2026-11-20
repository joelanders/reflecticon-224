#!/usr/bin/env python3
"""T&C control outputs under the firmware's E51-E7F register test program.

Checks which rows, and which register-file write and read selects (WA, RA),
the program presents. It checks control, not data values.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


CTRL_RE = re.compile(
    r"^E51CTRL idx=(\d+) b2=([0-9a-fA-F]{2}) total=(\d+) guard=(\d+) "
    r"wstb=([0-9a-fA-F]{32}) rd_xreg=([0-9a-fA-F]{32}) "
    r"rdr=([0-9a-fA-F]{32}) memw=([0-9a-fA-F]{32}) "
    r"wr_xreg=([0-9a-fA-F]{32}) rd_ad=([0-9a-fA-F]{32})$"
)
EDGE_RE = re.compile(
    r"^E51EDGE idx=(\d+) b2=([0-9a-fA-F]{2}) a=(\d+) "
    r"rd_xreg=([01xzXZ]) rdr=([01xzXZ]) memw=([01xzXZ]) "
    r"wr_xreg=([01xzXZ]) rd_ad=([01xzXZ]) "
    r"wa=([01xzXZ]{2}) ra=([01xzXZ]{2}) "
    r"csign=([01xzXZ]) zero=([01xzXZ]) s=([01xzXZ]{2}) "
    r"m=([01xzXZ]{2}) dab_rstb=([01xzXZ]) memac=([01xzXZ])$"
)

ALL_ROWS_MASK = (1 << 128) - 1
# Re-keyed 2026-07-05: under the schematic-true CLK_NEW (= DAB_RSTB/) the
# payload bytes at WCS rows 124/125/126 execute at rows 125/126/127 (one-row
# prefetch), so the result-request strobes land at {125,126,127} instead of
# the old two-bug model's {126,127,0}. See the 2026-07-05 sweep note (same
# shift as the check_tc_aru_e8x_event_replay.py migration template).
REQUEST_MASK = (1 << 127) | (1 << 126) | (1 << 125)
ZERO_MASK = 0
CASES = [
    (0, 0xC2, "00", "00"),
    (1, 0xC6, "01", "00"),
    (2, 0xCA, "10", "00"),
    (3, 0xCE, "11", "00"),
    (4, 0xDE, "11", "01"),
    (5, 0xEE, "11", "10"),
    (6, 0xFE, "11", "11"),
]


def fmt_mask(value: int | object) -> str:
    return f"{value:032x}" if isinstance(value, int) else str(value)


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    summaries: dict[int, dict[str, int]] = {}
    edges: dict[int, list[dict[str, int | str]]] = {}

    for line in text.splitlines():
        cm = CTRL_RE.match(line)
        if cm:
            (
                idx_s,
                b2_s,
                total_s,
                guard_s,
                wstb_s,
                rd_xreg_s,
                rdr_s,
                memw_s,
                wr_xreg_s,
                rd_ad_s,
            ) = cm.groups()
            summaries[int(idx_s)] = {
                "b2": int(b2_s, 16),
                "total": int(total_s),
                "guard": int(guard_s),
                "wstb": int(wstb_s, 16),
                "rd_xreg": int(rd_xreg_s, 16),
                "rdr": int(rdr_s, 16),
                "memw": int(memw_s, 16),
                "wr_xreg": int(wr_xreg_s, 16),
                "rd_ad": int(rd_ad_s, 16),
            }
            continue
        em = EDGE_RE.match(line)
        if em:
            (
                idx_s,
                b2_s,
                a_s,
                rd_xreg_s,
                rdr_s,
                memw_s,
                wr_xreg_s,
                rd_ad_s,
                wa_s,
                ra_s,
                csign_s,
                zero_s,
                s_s,
                m_s,
                dab_rstb_s,
                memac_s,
            ) = em.groups()
            edges.setdefault(int(idx_s), []).append({
                "b2": int(b2_s, 16),
                "a": int(a_s),
                "rd_xreg": rd_xreg_s.lower(),
                "rdr": rdr_s.lower(),
                "memw": memw_s.lower(),
                "wr_xreg": wr_xreg_s.lower(),
                "rd_ad": rd_ad_s.lower(),
                "wa": wa_s.lower(),
                "ra": ra_s.lower(),
                "csign": csign_s.lower(),
                "zero": zero_s.lower(),
                "s": s_s.lower(),
                "m": m_s.lower(),
                "dab_rstb": dab_rstb_s.lower(),
                "memac": memac_s.lower(),
            })

    results: list[tuple[str, bool, str]] = []
    for idx, b2, expected_wa, expected_ra in CASES:
        summary = summaries.get(idx, {})
        summary_ok = (
            summary.get("b2") == b2
            and summary.get("total") == 128
            and summary.get("guard") == 128
            and summary.get("wstb") == ALL_ROWS_MASK
            and summary.get("rd_xreg") == ZERO_MASK
            and summary.get("rdr") == REQUEST_MASK
            and summary.get("memw") == REQUEST_MASK
            and summary.get("wr_xreg") == ZERO_MASK
            and summary.get("rd_ad") == ZERO_MASK
        )
        results.append((
            f"idx {idx} b2={b2:02x} control-row masks",
            summary_ok,
            (
                f"total={summary.get('total', 'missing')} guard={summary.get('guard', 'missing')} "
                f"wstb={fmt_mask(summary.get('wstb', 'missing'))} "
                f"rd_xreg={fmt_mask(summary.get('rd_xreg', 'missing'))} "
                f"rdr={fmt_mask(summary.get('rdr', 'missing'))} "
                f"memw={fmt_mask(summary.get('memw', 'missing'))} "
                f"wr_xreg={fmt_mask(summary.get('wr_xreg', 'missing'))} "
                f"rd_ad={fmt_mask(summary.get('rd_ad', 'missing'))}"
            ),
        ))

        seq_edges = edges.get(idx, [])
        rows = sorted(int(edge["a"]) for edge in seq_edges)
        shared_rows_ok = all(
            edge.get("b2") == b2
            and edge.get("rd_xreg") == "1"
            and edge.get("rdr") == "0"
            and edge.get("memw") == "0"
            and edge.get("wr_xreg") == "1"
            and edge.get("rd_ad") == "1"
            and edge.get("csign") == "1"
            and edge.get("zero") == "1"
            # S1/S0 at DAB_WSTB/ fall + 12 ns: the operand chain's load window
            # (11) under the 74S74 U25 of the parts list (TPD 9 ns, 2026-08-07).
            and edge.get("s") == "11"
            # The M pair for the E51 control rows follows the serializer outputs
            # on the ~Q3 pins (U10 and U11 pin 11).
            and edge.get("m") == "00"
            and edge.get("dab_rstb") == "1"
            and edge.get("memac") == "1"
            for edge in seq_edges
        )
        # Re-keyed 2026-07-05 (one-row-earlier shift): the case-specific b2
        # byte (WCS row 126) now executes at a==127 instead of the old
        # model's a==0; the two constant/idle rows (WCS rows 124/125) now
        # execute at a in (125,126) instead of the old model's (126,127).
        row0 = next((edge for edge in seq_edges if edge.get("a") == 127), {})
        row0_ok = row0.get("wa") == expected_wa and row0.get("ra") == expected_ra
        pre_row_ok = all(
            edge.get("wa") == "11" and edge.get("ra") == "11"
            for edge in seq_edges
            if edge.get("a") in (125, 126)
        )
        edges_ok = rows == [125, 126, 127] and shared_rows_ok and row0_ok and pre_row_ok
        results.append((
            f"idx {idx} b2={b2:02x} row 125/126/127 decode",
            edges_ok,
            (
                f"rows={rows} row0_wa={row0.get('wa', 'missing')} "
                f"row0_ra={row0.get('ra', 'missing')} "
                f"want_wa={expected_wa} want_ra={expected_ra}"
            ),
        ))

    results.append(("coverage: seven E51/E7F b2 patterns", len(summaries) == 7, f"{len(summaries)} summaries"))
    results.append(("coverage: three active rows per pattern",
                    sum(len(v) for v in edges.values()) == 21,
                    f"{sum(len(v) for v in edges.values())} edges"))

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        failed += 0 if ok else 1
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
