#!/usr/bin/env python3
"""T&C + DMEM I/O + DMEM: single-stepping the DSP, as the firmware's E50 diagnostic does.

It checks the step behaviour the boards produce (the first row-127/0 write
window). The firmware's repeated single-step presentation of row 127 for the
second OUT 03 is not modeled.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


WAIT_RE = re.compile(
    r"^E50D_WAIT step=(\d+) guard=(\d+) wcsa=(\d+) halt=([01xzXZ]) cpc=([0-9a-fA-FxzXZ]{4})$"
)
PORT_RE = re.compile(
    r"^E50D_PORT port=(\d+) value=([0-9a-fA-F]{2}) "
    r"wcsa=(\d+) halt=([01xzXZ]) iohalt=([01xzXZ]) "
    r".* cpcclr=([01xzXZ]) .* cpc=([0-9a-fA-FxzXZ]{4})$"
)
PORT_SETUP_RE = re.compile(
    r"^E50D_PORT_SETUP port=(\d+) value=([0-9a-fA-F]{2}) "
    r"wcsa=(\d+) halt=([01xzXZ]) iohalt=([01xzXZ]) "
    r".* cpcclr=([01xzXZ]) cpc=([0-9a-fA-FxzXZ]{4})$"
)
MA_RE = re.compile(
    r"^E50D_MA tag=(\S+) row=(\d+) halt=([01xzXZ]) cpc=([0-9a-fA-FxzXZ]{4}) "
    r"sum=([0-9a-fA-FxzXZ]{4}) dram=([0-9a-fA-FxzXZ]{4}) "
    r"ofst=([0-9a-fA-FxzXZ]{4}) dab=([0-9a-fA-FxzXZ]{4}) known=(\d+) "
    r"owner=([01xzXZ]) memw=([01xzXZ]) rdr=([01xzXZ])"
)
SUM_RE = re.compile(
    r"^E50D_SUM tag=(\S+) word=([0-9a-fA-F]{4}) edges=(\d+) memacs=(\d+) "
    r"wstb=([0-9a-fA-F]{32}) memw=([0-9a-fA-F]{32}) "
    r"rdr=([0-9a-fA-F]{32}) owner=([0-9a-fA-F]{32}) cpc=([0-9a-fA-FxzXZ]{4})$"
)

E50_FIRST_WINDOW_MASK = int("80000000000000000000000000000001", 16)


def clean_hex(s: str) -> bool:
    return "x" not in s.lower() and "z" not in s.lower()


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    waits: list[dict[str, int | str]] = []
    ports: list[dict[str, int | str]] = []
    port_setups: list[dict[str, int | str]] = []
    mas: list[dict[str, int | str]] = []
    sums: dict[str, dict[str, int | str]] = {}

    for line in text.splitlines():
        if m := WAIT_RE.match(line):
            waits.append({
                "step": int(m.group(1)),
                "guard": int(m.group(2)),
                "wcsa": int(m.group(3)),
                "halt": m.group(4).lower(),
                "cpc": m.group(5).lower(),
            })
            continue
        if m := PORT_RE.match(line):
            ports.append({
                "port": int(m.group(1)),
                "value": int(m.group(2), 16),
                "wcsa": int(m.group(3)),
                "halt": m.group(4).lower(),
                "iohalt": m.group(5).lower(),
                "cpcclr": m.group(6).lower(),
                "cpc": m.group(7).lower(),
            })
            continue
        if m := PORT_SETUP_RE.match(line):
            port_setups.append({
                "port": int(m.group(1)),
                "value": int(m.group(2), 16),
                "wcsa": int(m.group(3)),
                "halt": m.group(4).lower(),
                "iohalt": m.group(5).lower(),
                "cpcclr": m.group(6).lower(),
                "cpc": m.group(7).lower(),
            })
            continue
        if m := MA_RE.match(line):
            mas.append({
                "tag": m.group(1),
                "row": int(m.group(2)),
                "halt": m.group(3).lower(),
                "cpc": m.group(4).lower(),
                "sum": m.group(5).lower(),
                "dram": m.group(6).lower(),
                "ofst": m.group(7).lower(),
                "dab": m.group(8).lower(),
                "known": int(m.group(9)),
                "owner": m.group(10).lower(),
                "memw": m.group(11).lower(),
                "rdr": m.group(12).lower(),
            })
            continue
        if m := SUM_RE.match(line):
            tag = m.group(1)
            sums[tag] = {
                "word": int(m.group(2), 16),
                "edges": int(m.group(3)),
                "memacs": int(m.group(4)),
                "wstb": int(m.group(5), 16),
                "memw": int(m.group(6), 16),
                "rdr": int(m.group(7), 16),
                "owner": int(m.group(8), 16),
                "cpc": m.group(9).lower(),
            }

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    check("coverage: initial/warm/final row-127 waits", len(waits) >= 3, f"waits={waits}")
    if waits:
        check("startup free-runs to row 127", waits[0]["guard"] < 512 and waits[0]["wcsa"] == 127 and waits[0]["halt"] == "1", str(waits[0]))
    if len(waits) >= 2:
        check("warm step starts from halted row 127", waits[1]["guard"] == 0 and waits[1]["wcsa"] == 127 and waits[1]["halt"] == "0", str(waits[1]))
    if len(waits) >= 3:
        check("E50 named gap: final step lacks repeated row-127 presentation",
              waits[2]["guard"] >= 1024 and waits[2]["wcsa"] != 127 and waits[2]["halt"] == "0",
              str(waits[2]))

    warm_step_setup = next((p for p in port_setups if p["port"] == 3 and p["value"] == 0x55), {})
    final_step_setup = next((p for p in port_setups if p["port"] == 3 and p["value"] == 0xAA), {})
    check("warm OUT 03 setup is at held row 127",
          warm_step_setup.get("wcsa") == 127
          and warm_step_setup.get("halt") == "0"
          and warm_step_setup.get("iohalt") == "0",
          str(warm_step_setup))
    check("final OUT 03 setup is at held row 0, not row 127",
          final_step_setup.get("wcsa") == 0
          and final_step_setup.get("halt") == "0"
          and final_step_setup.get("iohalt") == "0",
          str(final_step_setup))

    clears = [p for p in ports if p["port"] == 5 and p["cpcclr"] == "1"]
    clear_values = {p["value"] for p in clears}
    check("port 5 asserts structural CPCCLR", clear_values >= {0x00, 0x80}, f"clears={clears}")
    check("port 5 clear drives CPC to zero during pulse",
          len(clears) >= 2 and all(p["cpc"] == "0000" for p in clears[:2]),
          f"clears={clears[:2]}")

    warm_ma = [m for m in mas if m["tag"] == "warm"]
    warm_owner_rows = {m["row"] for m in warm_ma if m["owner"] == "1"}
    warm_known = [
        m for m in warm_ma
        if m["owner"] == "1"
        and m["dab"] == "5555"
        and m["known"] == 1
        and m["memw"] == "0"
        and m["rdr"] == "0"
        and m["ofst"] == "dfff"
        and clean_hex(str(m["sum"]) + str(m["dram"]))
    ]
    check("warm structural write has known 5555 owner", len(warm_known) >= 2, f"rows={warm_owner_rows} count={len(warm_known)}")
    check("warm owner rows include row 127 and row 0", warm_owner_rows >= {127, 0}, f"rows={sorted(warm_owner_rows)}")

    warm_sum = sums.get("warm", {})
    check("warm summary is row-127/0 MEMW/RDRREG owner window",
          warm_sum.get("word") == 0x5555
          and warm_sum.get("memw") == E50_FIRST_WINDOW_MASK
          and warm_sum.get("rdr") == E50_FIRST_WINDOW_MASK
          and warm_sum.get("owner") == E50_FIRST_WINDOW_MASK
          and isinstance(warm_sum.get("memacs"), int)
          and int(warm_sum["memacs"]) >= 2,
          str(warm_sum))

    final_sum = sums.get("final", {})
    check("E50 named gap: final window has no value owner under current run-control model",
          final_sum.get("word") == 0xAAAA
          and final_sum.get("memacs") == 0
          and final_sum.get("owner") == 0,
          str(final_sum))
    check("simulation completed", "E50D_DONE" in text,
          "E50D_DONE present" if "E50D_DONE" in text else "missing")

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        failed += 0 if ok else 1
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
