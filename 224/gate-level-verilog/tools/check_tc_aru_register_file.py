#!/usr/bin/env python3
"""T&C + ARU: register-file writes and reads under the firmware's E51-E7F register test program.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


W_RE = re.compile(
    r"^E51RF_W seq=(\d+) b2=([0-9a-fA-F]{2}) a=(\d+) "
    r"value=([0-9a-fA-F]{4}) dab=([0-9a-fA-FxzXZ]{4}) "
    r"wa=([01xzXZ]{2}) ra=([01xzXZ]{2}) f=([0-9a-fA-FxzXZ]{4}) "
    r"rf0=([0-9a-fA-FxzXZ]{4}) rf1=([0-9a-fA-FxzXZ]{4}) "
    r"rf2=([0-9a-fA-FxzXZ]{4}) rf3=([0-9a-fA-FxzXZ]{4}) "
    r"known=(\d+) rdr=([01xzXZ]) memw=([01xzXZ]) "
    r"rd_xreg=([01xzXZ]) wr_xreg=([01xzXZ])$"
)
WDONE_RE = re.compile(
    r"^E51RF_WDONE seq=(\d+) b2=([0-9a-fA-F]{2}) value=([0-9a-fA-F]{4}) "
    r"delta=(\d+) guard=(\d+) rf0=([0-9a-fA-FxzXZ]{4}) "
    r"rf1=([0-9a-fA-FxzXZ]{4}) rf2=([0-9a-fA-FxzXZ]{4}) "
    r"rf3=([0-9a-fA-FxzXZ]{4})$"
)
R_RE = re.compile(
    r"^E51RF_R seq=(\d+) b2=([0-9a-fA-F]{2}) a=(\d+) "
    r"expect=([0-9a-fA-F]{4}) f=([0-9a-fA-FxzXZ]{4}) "
    r"wa=([01xzXZ]{2}) ra=([01xzXZ]{2}) known=(\d+) "
    r"rf0=([0-9a-fA-FxzXZ]{4}) rf1=([0-9a-fA-FxzXZ]{4}) "
    r"rf2=([0-9a-fA-FxzXZ]{4}) rf3=([0-9a-fA-FxzXZ]{4}) "
    r"rdr=([01xzXZ]) memw=([01xzXZ]) rd_xreg=([01xzXZ]) "
    r"wr_xreg=([01xzXZ])$"
)

WRITE_CASES = {
    0: (0xC2, 0x5555),
    1: (0xC6, 0x6666),
    2: (0xCA, 0x7777),
    3: (0xCE, 0xEEEE),
    8: (0xC2, 0xAAAA),
    9: (0xC6, 0x9999),
    10: (0xCA, 0x8888),
    11: (0xCE, 0x1111),
}
READ_CASES = {
    4: (0xCE, 0x5555),
    5: (0xDE, 0x6666),
    6: (0xEE, 0x7777),
    7: (0xFE, 0xEEEE),
    12: (0xCE, 0xAAAA),
    13: (0xDE, 0x9999),
    14: (0xEE, 0x8888),
    15: (0xFE, 0x1111),
}
PHYSICAL_AFTER_GROUP = {
    3: (0xEEEE, 0x6666, 0x7777, 0x5555),
    11: (0x1111, 0x9999, 0x8888, 0xAAAA),
}


def clean_hex(text: str) -> bool:
    lowered = text.lower()
    return "x" not in lowered and "z" not in lowered


def parse_hex(text: str) -> int | str:
    return int(text, 16) if clean_hex(text) else text.lower()


def fmt(value: int | str | None, width: int = 4) -> str:
    if isinstance(value, int):
        return f"{value:0{width}x}"
    return "missing" if value is None else str(value)


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    writes: dict[int, dict[str, int | str]] = {}
    wdones: dict[int, dict[str, int | str]] = {}
    reads: dict[int, dict[str, int | str]] = {}

    for line in text.splitlines():
        if m := W_RE.match(line):
            (
                seq_s, b2_s, a_s, value_s, dab_s, wa_s, ra_s, f_s,
                rf0_s, rf1_s, rf2_s, rf3_s, known_s, rdr_s, memw_s,
                rd_xreg_s, wr_xreg_s,
            ) = m.groups()
            writes[int(seq_s)] = {
                "b2": int(b2_s, 16),
                "a": int(a_s),
                "value": int(value_s, 16),
                "dab": parse_hex(dab_s),
                "wa": wa_s.lower(),
                "ra": ra_s.lower(),
                "f": parse_hex(f_s),
                "rf": tuple(parse_hex(x) for x in (rf0_s, rf1_s, rf2_s, rf3_s)),
                "known": int(known_s),
                "rdr": rdr_s.lower(),
                "memw": memw_s.lower(),
                "rd_xreg": rd_xreg_s.lower(),
                "wr_xreg": wr_xreg_s.lower(),
            }
            continue
        if m := WDONE_RE.match(line):
            seq_s, b2_s, value_s, delta_s, guard_s, rf0_s, rf1_s, rf2_s, rf3_s = m.groups()
            wdones[int(seq_s)] = {
                "b2": int(b2_s, 16),
                "value": int(value_s, 16),
                "delta": int(delta_s),
                "guard": int(guard_s),
                "rf": tuple(parse_hex(x) for x in (rf0_s, rf1_s, rf2_s, rf3_s)),
            }
            continue
        if m := R_RE.match(line):
            (
                seq_s, b2_s, a_s, expect_s, f_s, wa_s, ra_s, known_s,
                rf0_s, rf1_s, rf2_s, rf3_s, rdr_s, memw_s, rd_xreg_s,
                wr_xreg_s,
            ) = m.groups()
            reads[int(seq_s)] = {
                "b2": int(b2_s, 16),
                "a": int(a_s),
                "expect": int(expect_s, 16),
                "f": parse_hex(f_s),
                "wa": wa_s.lower(),
                "ra": ra_s.lower(),
                "known": int(known_s),
                "rf": tuple(parse_hex(x) for x in (rf0_s, rf1_s, rf2_s, rf3_s)),
                "rdr": rdr_s.lower(),
                "memw": memw_s.lower(),
                "rd_xreg": rd_xreg_s.lower(),
                "wr_xreg": wr_xreg_s.lower(),
            }

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    check("coverage: eight table writes", set(writes) >= set(WRITE_CASES), f"{sorted(writes)}")
    check("coverage: eight write completions", set(wdones) >= set(WRITE_CASES), f"{sorted(wdones)}")
    check("coverage: eight table reads", set(reads) >= set(READ_CASES), f"{sorted(reads)}")

    for seq, (b2, value) in WRITE_CASES.items():
        rec = writes.get(seq, {})
        done = wdones.get(seq, {})
        ok = (
            rec.get("b2") == b2
            # Re-keyed 2026-07-05 (schematic-true CLK_NEW, one-row prefetch):
            # the row-126 write command now executes/latches when wcsa==127
            # (was 0 under the old two-bug model). See tb comment + sweep note.
            and rec.get("a") == 127
            and rec.get("value") == value
            and rec.get("dab") == value
            and rec.get("known") == 1
            and done.get("b2") == b2
            and done.get("value") == value
            and done.get("delta") == 1
            and isinstance(done.get("guard"), int)
            and int(done.get("guard")) < 512
        )
        check(
            f"seq {seq} b2={b2:02x} row-0 DAB write",
            ok,
            (
                f"value={fmt(rec.get('value'))} dab={fmt(rec.get('dab'))} "
                f"a={rec.get('a', 'missing')} known={rec.get('known', 'missing')} "
                f"delta={done.get('delta', 'missing')} guard={done.get('guard', 'missing')} "
                f"wa={rec.get('wa', 'missing')} ra={rec.get('ra', 'missing')}"
            ),
        )

    for seq, rf_expect in PHYSICAL_AFTER_GROUP.items():
        done = wdones.get(seq, {})
        rf = done.get("rf")
        check(
            f"seq {seq} active-low physical RF map",
            rf == rf_expect,
            "rf=" + (
                ",".join(fmt(x) for x in rf) if isinstance(rf, tuple) else "missing"
            ) + " want=" + ",".join(f"{x:04x}" for x in rf_expect),
        )

    for seq, (b2, expect) in READ_CASES.items():
        rec = reads.get(seq, {})
        ok = (
            rec.get("b2") == b2
            # Re-keyed 2026-07-05: F-bus readback now samples at wcsa==127
            # (was 0), matching the write-side re-key above.
            and rec.get("a") == 127
            and rec.get("expect") == expect
            and rec.get("f") == expect
            and rec.get("known") == 1
        )
        check(
            f"seq {seq} b2={b2:02x} F-bus readback",
            ok,
            (
                f"expect={expect:04x} f={fmt(rec.get('f'))} "
                f"a={rec.get('a', 'missing')} known={rec.get('known', 'missing')} "
                f"wa={rec.get('wa', 'missing')} ra={rec.get('ra', 'missing')}"
            ),
        )

    check("simulation completed", "E51RF_DONE" in text,
          "E51RF_DONE present" if "E51RF_DONE" in text else "missing")

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        failed += 0 if ok else 1
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
