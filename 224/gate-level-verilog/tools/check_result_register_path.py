#!/usr/bin/env python3
"""Structural check of the XFER -> result register path (T&C + ARU netlists).

It freezes the connections between the accumulator and the result register:
the XFER latch and XFER_CK gating on the T&C, the ARU's final-carry adder
chain, the saturation multiplexers and the result register. It also records the primitive delays
on that path (the LS157 default and the board's 74S157 grade); neither is a
measurement of silicon setup or hold.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def instance(src: str, module: str, ref: str) -> str | None:
    pat = re.compile(
        rf"^\s*{re.escape(module)}(?:\s+#\([^;]*?\))?\s+{re.escape(ref)}\s+"
        rf"\((.*?)\);\s*(?://\s*(.*))?$",
        re.MULTILINE,
    )
    m = pat.search(src)
    if not m:
        return None
    conns = m.group(1)
    comment = m.group(2) or ""
    return f"{conns} // {comment}"


def module_body(src: str, name: str) -> str | None:
    m = re.search(rf"module\s+{re.escape(name)}\b.*?^endmodule\b", src, re.MULTILINE | re.DOTALL)
    return m.group(0) if m else None


def has_pin(inst: str | None, pin: str, net: str) -> bool:
    if inst is None:
        return False
    return re.search(rf"\.{re.escape(pin)}\({re.escape(net)}\)", inst) is not None


def has_all_pins(inst: str | None, pins: dict[str, str]) -> bool:
    return inst is not None and all(has_pin(inst, pin, net) for pin, net in pins.items())


def record(results: list[tuple[str, bool, str]], name: str, ok: bool, detail: str) -> None:
    results.append((name, ok, detail))


def main() -> int:
    tc = read("generated/tc_board.v")
    aru = read("generated/aru_board.v")
    prims = read("prims/prims.v")
    report = read("reports/tc_part_coverage.md")
    script = read("scripts/netlist_to_verilog.py")

    results: list[tuple[str, bool, str]] = []

    tc_u19 = instance(tc, "ttl_74ls377", "U19")
    tc_u25 = instance(tc, "ttl_74ls74", "U25")
    tc_u36 = instance(tc, "ttl_74ls10", "U36")
    aru_u43 = instance(aru, "ttl_sn74f374n", "U43")
    aru_u44 = instance(aru, "ttl_sn74f374n", "U44")

    record(
        results,
        "T&C U19 creates XFER from the ARUCKE-clocked control latch",
        has_all_pins(tc_u19, {"p_1": "AS0_slash", "p_11": "ARUCKE", "p_15": "XFER"}),
        "U19 74LS377: p1=AS0/, p11=ARUCKE, p15=XFER",
    )
    record(
        results,
        "T&C U25 supplies the U36 XFER_CK timing qualifier",
        has_all_pins(tc_u25, {"p_8": "ARUCKE", "p_9": "HIGH_SPEED2_U36_2C", "p_11": "MC"}),
        "U25 74S74-labeled FF: p8=ARUCKE, p9=HIGH_SPEED2_U36_2C, p11=MC",
    )
    record(
        results,
        "T&C U36 gates AS0, XFER, and U25.9 into XFER_CK",
        has_all_pins(
            tc_u36,
            {"p_3": "AS0", "p_4": "XFER", "p_5": "HIGH_SPEED2_U36_2C", "p_6": "XFER_CK"},
        ),
        "U36 NAND gate: p3=AS0, p4=XFER, p5=HIGH_SPEED2_U36_2C, p6=XFER_CK",
    )

    rr_clock_count = len(re.findall(r"\.p_11\(XFER_CK\)", aru))
    record(
        results,
        "ARU U43/U44 are the only XFER_CK result-register clocks",
        has_pin(aru_u43, "p_11", "XFER_CK") and has_pin(aru_u44, "p_11", "XFER_CK")
        and rr_clock_count == 2,
        f"XFER_CK on ttl_sn74f374n U43/U44 p11 only, count={rr_clock_count}",
    )

    carry_checks = [
        ("U19", "Net_U19_C0", "Net_U19_C4"),
        ("U20", "Net_U19_C4", "Net_U20_C4"),
        ("U21", "Net_U20_C4", "Net_U21_C4"),
        ("U22", "Net_U21_C4", "Net_U22_C4"),
        ("U23", "Net_U22_C4", "unconnected_U23_C4_Pad9"),
    ]
    carry_ok = True
    for ref, cin, cout in carry_checks:
        inst = instance(aru, "ttl_74ls283", ref)
        carry_ok = carry_ok and has_all_pins(inst, {"p_7": cin, "p_9": cout})
    record(
        results,
        "ARU U19-U23 form the final-carry adder chain into PP bits",
        carry_ok,
        "U19.C0<-mask, U19.C4->U20.C0, ..., U22.C4->U23.C0; U23.C4 unconnected",
    )

    mux_ok = True
    for ref, base, first_pp in [
        ("U33", "Net_U19", "PP0"),
        ("U34", "Net_U20", "PP4"),
        ("U35", "Net_U21", "PP8"),
        ("U36", "Net_U22", "PP12"),
        ("U37", "Net_U23", "PP16"),
    ]:
        inst = instance(aru, "ttl_74ls157", ref)
        mux_ok = mux_ok and has_all_pins(inst, {"p_1": "SAT", "p_2": f"{base}_S1", "p_4": first_pp})
    record(
        results,
        "ARU U33-U37 mux LS283 sums to the PP bus selected by SAT",
        mux_ok,
        "SAT selects the U19-U23 sum outputs onto PP0..PP19 before RR capture",
    )

    first_visible_path_ok = (
        has_all_pins(
            instance(aru, "ttl_74ls283", "U19"),
            {"p_7": "Net_U19_C0", "p_10": "Net_U19_S4"},
        )
        and has_all_pins(
            instance(aru, "ttl_74ls157", "U33"),
            {"p_1": "SAT", "p_12": "PP3", "p_14": "Net_U19_S4", "p_15": "GNDREF"},
        )
        and has_all_pins(aru_u44, {"p_14": "PP3", "p_15": "DAB0"})
    )
    record(
        results,
        "First visible bit path is U19.S4 -> U33.PP3 -> U44/DAB0",
        first_visible_path_ok,
        "source bit3: U19 p10 S4, U33 p14->p12 when SAT=0, U44 p14 D5 -> p15 DAB0",
    )

    prim_283 = module_body(prims, "ttl_74ls283")
    sum_assigns = ["p_4", "p_1", "p_13", "p_10", "p_9"]
    record(
        results,
        "Primitive ttl_74ls283 has separate sum and carry-out delays; 224X ARU adders are F283",
        prim_283 is not None
        and "parameter real TPD = 9.0;" in prim_283
        and all(re.search(rf"assign\s+#\(S_LH, S_HL\)\s+{pin}\s*=", prim_283) for pin in ["p_4", "p_1", "p_13", "p_10"])
        and re.search(r"assign\s+#\(C_LH, C_HL\)\s+p_9\s*=", prim_283) is not None
        and all(re.search(rf"ttl_74ls283\s+#\(\.S_LH\(6\.6\), \.S_HL\(6\.6\), \.C_LH\(5\.3\), \.C_HL\(5\.0\)\)\s+{ref}\b", aru)
                for ref in ("U13", "U19", "U20", "U21", "U22", "U23", "U24", "U25", "U38", "U39")),
        "sum/carry delays default to TPD=9ns; ARU U13, U19-U25, U38, U39 use 74F283 typicals (6.6 sum, 5.3/5.0 carry)",
    )

    prim_157 = module_body(prims, "ttl_74ls157")
    record(
        results,
        "Primitive ttl_74ls157 uses one shared mux delay",
        prim_157 is not None
        and "parameter real TPLH = 20.0;" in prim_157
        and "parameter real TPHL = 20.0;" in prim_157
        and all(re.search(rf"assign\s+#\(TPLH,\s*TPHL\)\s+{pin}\s*=", prim_157) for pin in ["p_4", "p_7", "p_9", "p_12"]),
        "shared/base primitive defaults to 20ns; active 224X U33-U37 override it below",
    )

    s157_refs = [
        ref
        for ref in ("U33", "U34", "U35", "U36", "U37")
        if re.search(
            rf"ttl_74ls157\s+#\(\.TPLH\(6\.6\), \.TPHL\(4\.6\)\)\s+{ref}\b",
            aru,
        )
        is not None
    ]
    record(
        results,
        "224X ARU U33-U37 carry the 74F157 timing grade (drawing rev 4 table)",
        s157_refs == ["U33", "U34", "U35", "U36", "U37"]
        and all(
            re.search(r'\("aru_board_netlist", u\): \[".TPLH\(6\.6\)", ".TPHL\(4\.6\)"\]', script)
            and f'"{ref}"' in script
            for ref in s157_refs
        ),
        f"board-scoped F157 overrides (6.6/4.6 ns) present for {','.join(s157_refs) or '(none)'}",
    )

    prim_374 = module_body(prims, "ttl_sn74f374n")
    record(
        results,
        "Primitive F374 maps D5 to O5 for the first visible RR bit",
        prim_374 is not None
        and "{p_18, p_17, p_14, p_13, p_8, p_7, p_4, p_3}" in prim_374
        and re.search(r"assign\s+p_15\s*=\s*q_drive\[5\];", prim_374) is not None,
        "U44 p14 is bit 5 of the captured byte; U44 p15/DAB0 drives q[5] when RDRREG/ is low",
    )
    record(
        results,
        "Result register U43/U44 are 74F374 with a 2 ns setup aperture",
        prim_374 is not None
        and "parameter real TSU = 0.0;" in prim_374
        and "assign #(TSU) d_setup = d;" in prim_374
        and all(re.search(rf"ttl_sn74f374n\s+#\(\.TPD\(6\.1\), \.TSU\(2\.0\)\)\s+{ref}\b", aru) for ref in ("U43", "U44")),
        "RR takes D as it was 2 ns before XFER_CK (TI F374 setup) and drives Q 6.1 ns after; no hold check",
    )

    record(
        results,
        "T&C U36 carries an explicit S10 timing override",
        "`SN74S10N` -> `ttl_74ls10`: count=1" in report
        and "refs=`U36`" in report
        and '"SN74S10N": "ttl_74ls10"' in script
        and '("ttl_74ls10", "U36"): [".TPLH(3.0)", ".TPHL(3.0)"]' in script
        and re.search(r"ttl_74ls10\s+#\(\.TPLH\(3\.0\), \.TPHL\(3\.0\)\)\s+U36\b", tc)
        is not None
        and "SN74S10N 74S10" in (tc_u36 or ""),
        "netlist labels U36 as SN74S10N/74S10 and generated U36 uses 3ns S-series timing",
    )

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
        failed += 0 if ok else 1
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
