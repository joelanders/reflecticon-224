#!/usr/bin/env python3
"""T&C + FPC: the output sequencing (WR_DA/, SDA, the FPC output strobes).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    events: dict[str, list[dict[str, str | int]]] = {"TW": [], "TCAP": [], "TR": [], "TF": [], "TO": []}

    patterns = {
        "TW": re.compile(
            r"TW a=(\d+) f=(\d+) dab_rstb=([01x]) fpc_ck=([01x]) wr_da_n=([01x]) "
            r"sda=([01xz]{4}) dab=([0-9a-fxz]{4}) cap=([0-9a-fxz]{4}) sel=([0-9a-fxz])"
        ),
        "TCAP": re.compile(
            r"TCAP a=(\d+) f=(\d+) dab_rstb=([01x]) wr_da_n=([01x]) "
            r"sda=([01xz]{4}) dab=([0-9a-fxz]{4}) cap=([0-9a-fxz]{4}) sel=([0-9a-fxz])"
        ),
        "TR": re.compile(
            r"TR a=(\d+) f=(\d+) dab_rstb=([01x]) fpc_ck=([01x]) rd_ad_n=([01x]) "
            r"dab=([0-9a-fxz]{4}) cap=([0-9a-fxz]{4}) sel=([0-9a-fxz])"
        ),
        "TF": re.compile(
            r"TF a=(\d+) f=(\d+) dab_rstb=([01x]) fpc_ck=([01x]) wr_da_n=([01x]) "
            r"rd_ad_n=([01x]) reset_n=([01x]) sda=([01xz]{4}) dab=([0-9a-fxz]{4}) "
            r"cap=([0-9a-fxz]{4}) sel=([0-9a-fxz]) out=([01xz]{4}) u42_en_n=([01x])"
        ),
        "TO": re.compile(
            r"TO a=(\d+) f=(\d+) out=([01xz]{4}) u42_en_n=([01x]) "
            r"cap=([0-9a-fxz]{4}) sel=([0-9a-fxz])"
        ),
    }

    for line in text.splitlines():
        for kind, pat in patterns.items():
            m = pat.match(line)
            if not m:
                continue
            events[kind].append({"raw": line, "groups": m.groups()})

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    clean_tw = [ev for ev in events["TW"] if "x" not in str(ev["raw"])]
    check("WR_DA event seen", len(clean_tw) >= 1,
          f"{len(clean_tw)} events" + (f"; first {clean_tw[0]['raw']}" if clean_tw else ""))

    good_cap = []
    for ev in events["TCAP"]:
        g = ev["groups"]
        if g[3] == "0" and g[4] == "1010" and g[5] == "a55a" and g[6] == "a55a" and g[7] == "a":
            good_cap.append(ev)
    check("FPC captures T&C WR_DA/SDA/DAB on FPC_CK",
          len(good_cap) >= 1,
          f"{len(good_cap)} captures" + (f"; first {good_cap[0]['raw']}" if good_cap else ""))

    good_read = []
    for ev in events["TR"]:
        g = ev["groups"]
        if g[4] == "0" and g[5] == "1357" and g[6] == "a55a" and g[7] == "a":
            good_read.append(ev)
    check("FPC drives prepared word during T&C RD_AD",
          len(good_read) >= 1,
          f"{len(good_read)} reads" + (f"; first {good_read[0]['raw']}" if good_read else ""))

    held = []
    for ev in events["TF"]:
        g = ev["groups"]
        # Groups: a,f,dab_rstb,fpc_ck,wr_da_n,rd_ad_n,reset_n,sda,dab,cap,sel,out,u42_en_n
        if int(g[1]) > 70 and g[4] == "1" and g[5] == "1" and g[8] == "zzzz" and g[9] == "a55a" and g[10] == "a":
            held.append(ev)
    check("FPC releases DAB and holds capture after events",
          len(held) >= 4,
          f"{len(held)} held frames" + (f"; first {held[0]['raw']}" if held else ""))

    out_hits = []
    for ev in events["TO"]:
        g = ev["groups"]
        if g[2] == "1010" and g[3] == "0" and g[4] == "a55a" and g[5] == "a":
            out_hits.append(ev)
    check("FPC strobes captured SDA code on OUTA-D via U42",
          len(out_hits) >= 1,
          f"{len(out_hits)} output strobes" + (f"; first {out_hits[0]['raw']}" if out_hits else ""))

    no_bad_x = all("x" not in str(ev["groups"]) for kind in ("TCAP", "TR", "TO") for ev in events[kind])
    check("no X in capture/read events", no_bad_x, "")

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
        failed += 0 if ok else 1
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
