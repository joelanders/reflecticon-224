#!/usr/bin/env python3
"""Check all printed T&C p5-17..p5-20 WCS/decode HP 5004A signatures."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from check_hp5004a_tc_u11 import parse_samples
from hp5004a_signature import compress_bits, format_signature


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "out/hp5004a_tc_u11"
EXPECTED = {
    "u2p1": "0000", "u2p2": "07P6", "u2p3": "FP4C",
    "u2p4": "03UA", "u2p5": "3U9U", "u2p6": "F5AA",
    "u2p7": "A0A8", "u2p8": "53PH", "u2p9": "028H",
    "u2p11": "0000", "u2p12": "0000", "u2p14": "0000",
    "u2p15": "0000", "u2p16": "FP54", "u2p17": "0000",
    "u2p18": "0000", "u2p19": "3U9F", "u2p20": "A36H",
    "u2p21": "95CP", "u2p22": "F344", "u2p23": "45FF",
    "u2p24": "FP54",
    "u3p1": "07P6", "u3p2": "FP4C", "u3p3": "03UA",
    "u3p4": "3U9U", "u3p5": "F5AA", "u3p6": "A0A8",
    "u3p7": "53PH", "u3p8": "028H", "u3p9": "FP54",
    "u3p10": "0000", "u3p11": "FP54", "u3p20": "FP54",
    "u4p1": "FP54", "u4p3": "07P6", "u4p4": "FP4C",
    "u4p5": "03UA", "u4p6": "3U9U", "u4p7": "0000",
    "u4p8": "0000", "u4p9": "0000", "u4p10": "0000",
    "u4p11": "1UFU", "u4p12": "01UH", "u4p13": "6725",
    "u4p14": "03U3", "u4p16": "FP54",
    "u15p1": "0000", "u15p2": "C5A9", "u15p3": "909P",
    "u15p4": "0000", "u15p5": "0000", "u15p6": "8C99",
    "u15p7": "0H10", "u15p8": "0000", "u15p9": "7302",
    "u15p11": "0000", "u15p12": "0000", "u15p14": "0000",
    "u15p15": "0000", "u15p16": "FP54", "u15p17": "0000",
    "u15p18": "0000", "u15p19": "3U9F", "u15p20": "A36H",
    "u15p21": "95CP", "u15p22": "F344", "u15p23": "45FF",
    "u15p24": "FP54",
    "u16p1": "C5A9", "u16p2": "909P", "u16p3": "0000",
    "u16p4": "0000", "u16p5": "8C99", "u16p6": "0H10",
    "u16p7": "0000", "u16p8": "7302", "u16p9": "FP54",
    "u16p10": "0000", "u16p11": "FP54", "u16p20": "FP54",
    "u17p1": "FP54", "u17p3": "C5A9", "u17p4": "909P",
    "u17p5": "0000", "u17p6": "0000", "u17p7": "0000",
    "u17p8": "0000", "u17p9": "0000", "u17p10": "0000",
    "u17p11": "0000", "u17p12": "0000", "u17p13": "484U",
    "u17p14": "U3AA", "u17p16": "FP54",
    "u18p1": "FP54", "u18p3": "8C99", "u18p4": "0H10",
    "u18p5": "0000", "u18p6": "7302", "u18p7": "0000",
    "u18p8": "0000", "u18p9": "0000", "u18p10": "0000",
    "u18p11": "3981", "u18p12": "0000", "u18p13": "2UU6",
    "u18p14": "PFC2", "u18p16": "FP54",
    "u19p2": "0000", "u19p3": "0000", "u19p4": "PFC2",
    "u19p5": "PFC2", "u19p6": "2UU6", "u19p7": "2UU6",
    "u19p8": "FP54", "u19p9": "672A", "u19p10": "0000",
    "u19p12": "3981", "u19p13": "3981", "u19p14": "03U3",
    "u19p15": "03U3", "u19p16": "6725", "u19p17": "6725",
    "u19p18": "FP54", "u19p19": "FP54", "u19p20": "FP54",
    "u20p8": "0000", "u20p9": "9FF0", "u20p10": "FP54",
    "u20p14": "FP54", "u20p16": "FP54",
    "u28p1": "FP54", "u28p3": "3U9F", "u28p4": "3U9F",
    "u28p6": "0000", "u28p7": "0000", "u28p8": "0000",
    "u28p9": "0000", "u28p10": "0000", "u28p15": "0000",
    "u28p16": "FP54",
    "u29p1": "0000", "u29p2": "4496", "u29p3": "P265",
    "u29p4": "2P14", "u29p5": "F71H", "u29p6": "C10H",
    "u29p7": "A80H", "u29p8": "CA09", "u29p9": "909P",
    "u29p11": "0000", "u29p12": "0000", "u29p14": "0000",
    "u29p15": "0000", "u29p16": "FP54", "u29p17": "0000",
    "u29p18": "0000", "u29p19": "3U9F", "u29p20": "A36H",
    "u29p21": "95CP", "u29p22": "F344", "u29p23": "45FF",
    "u29p24": "FP54",
    "u30p1": "4496", "u30p2": "P265", "u30p3": "2P14",
    "u30p4": "F71H", "u30p5": "C10H", "u30p6": "A80H",
    "u30p7": "CA09", "u30p8": "909P", "u30p9": "FP54",
    "u30p10": "0000", "u30p11": "FP54", "u30p20": "FP54",
    "u31p1": "0000", "u31p2": "7132", "u31p3": "P265",
    "u31p4": "F71H", "u31p5": "638P", "u31p6": "H406",
    "u31p7": "A80H", "u31p8": "909P", "u31p9": "484U",
    "u31p10": "0000", "u31p12": "HH04", "u31p13": "CA09",
    "u31p14": "C10H", "u31p15": "71U8", "u31p16": "970A",
    "u31p17": "2P14", "u31p18": "4496", "u31p19": "A24C",
    "u31p20": "FP54",
    "u32p1": "A24C", "u32p2": "A24C", "u32p3": "A24C",
    "u32p4": "970A", "u32p5": "970A", "u32p6": "970A",
    "u32p7": "0000", "u32p8": "638P", "u32p9": "638P",
    "u32p10": "638P", "u32p11": "7132", "u32p12": "7132",
    "u32p13": "7132", "u32p14": "FP54",
    "u33p3": "484U", "u33p4": "861C", "u33p7": "0000",
    "u33p14": "FP54",
    "u34p1": "861C", "u34p2": "830C", "u34p3": "0000",
    "u34p4": "861C", "u34p5": "HH05", "u34p6": "0001",
    "u34p7": "0000", "u34p9": "U7H5", "u34p13": "3981",
    "u34p14": "FP54",
    "u42p1": "FP54", "u42p3": "45FF", "u42p4": "45FF",
    "u42p6": "F344", "u42p7": "F344", "u42p8": "0000",
    "u42p9": "95CP", "u42p10": "95CP", "u42p12": "A36H",
    "u42p13": "A36H", "u42p15": "0000", "u42p16": "FP54",
    "u43p1": "0000", "u43p2": "0616", "u43p3": "7P28",
    "u43p4": "F237", "u43p5": "CA0C", "u43p6": "0616",
    "u43p7": "7P28", "u43p8": "F184", "u43p9": "1C45",
    "u43p11": "0000", "u43p12": "0000", "u43p14": "0000",
    "u43p15": "0000", "u43p16": "FP54", "u43p17": "0000",
    "u43p18": "0000", "u43p19": "3U9F", "u43p20": "A36H",
    "u43p21": "95CP", "u43p22": "F344", "u43p23": "45FF",
    "u43p24": "FP54",
    "u44p1": "0616", "u44p2": "7P28", "u44p3": "F237",
    "u44p4": "CA0C", "u44p5": "0616", "u44p6": "7P28",
    "u44p7": "F184", "u44p8": "1C45", "u44p9": "FP54",
    "u44p10": "0000", "u44p11": "FP54", "u44p20": "FP54",
    "u45p1": "0000", "u45p2": "3U14", "u45p3": "7P28",
    "u45p4": "CA0C", "u45p5": "HH05", "u45p6": "3U14",
    "u45p7": "7P28", "u45p8": "1C45", "u45p9": "8HA2",
    "u45p10": "0000", "u45p12": "F9CF", "u45p13": "F184",
    "u45p14": "0616", "u45p15": "830C", "u45p16": "611C",
    "u45p17": "F237", "u45p18": "0616", "u45p19": "830C",
    "u45p20": "FP54",
    "u46p1": "FP54", "u46p2": "FP54", "u46p4": "FP54",
    "u46p5": "FP54", "u46p6": "FP54", "u46p7": "FP54",
    "u46p8": "0000", "u46p9": "FP54", "u46p10": "FP54",
    "u46p11": "FP54", "u46p12": "FP54", "u46p14": "FP54",
    "u46p16": "FP54",
    "u47p1": "0000", "u47p2": "U3AA", "u47p3": "484U",
    "u47p5": "484U", "u47p6": "U3AA", "u47p7": "CCP5",
    "u47p8": "0000", "u47p13": "H406", "u47p14": "71U8",
    "u47p16": "FP54",
    "u48p1": "U3AA", "u48p4": "861C", "u48p7": "0000",
    "u48p12": "6725", "u48p14": "FP54",
    "u49p2": "861C", "u49p3": "3U14", "u49p4": "861C",
    "u49p7": "0000", "u49p8": "55C6", "u49p9": "8HA2",
    "u49p10": "FP54", "u49p11": "861C", "u49p13": "F9CF",
    "u49p14": "FP54",
}
# The p5-20 table prints U3AA at U48.1, which is MEMW/'s code. Until 2026-09-25
# the generated board carried a 2026-07-05 patch that moved U48.1 onto U47B Y0/
# (FP55 here) on a misread crop; the 600-DPI ink (both scans of 060-02475
# sheet 2) shows the wire into U48A pin 1 coming down from U47's upper Y2
# output, MEMW/, with the lower Y0 (pin 12) an open bubble. The patch is
# withdrawn and every printed entry matches.
KNOWN_FUNCTIONAL_DIFFERENCES: dict[str, tuple[str, str]] = {}
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--no-refresh", action="store_true")
    return parser.parse_args()


def known_signature(rows: list[dict[str, str]], signal: str) -> tuple[int, str] | None:
    values = [row[signal] for row in rows]
    if any(value not in {"0", "1"} for value in values):
        return None
    raw = compress_bits(int(value) for value in values)
    return raw, format_signature(raw)


def check_package_supplies() -> None:
    generated = (ROOT / "generated/tc_board.v").read_text(
        encoding="utf-8"
    )
    for ref in ("U3", "U16", "U30", "U44"):
        lines = [
            line for line in generated.splitlines()
            if line.startswith(f"  ttl_am8304n {ref} ")
        ]
        if len(lines) != 1:
            raise AssertionError(f"expected exactly one generated AM8304 {ref}")
        if ".p_10(GNDPWR)" not in lines[0] or ".p_20(n_5V)" not in lines[0]:
            raise AssertionError(
                f"{ref}: expected physical package supplies pin10=GND, pin20=+5V"
            )
    required = {
        "U28": (".p_8(GNDPWR)",),
        "U42": (".p_8(GNDPWR)",),
        "U33": (".p_7(GNDPWR)", ".p_14(n_5V)"),
    }
    for ref, fragments in required.items():
        lines = [line for line in generated.splitlines() if f" {ref} (" in line]
        if len(lines) != 1 or any(fragment not in lines[0] for fragment in fragments):
            raise AssertionError(f"{ref}: incomplete HP5004A physical package rails")


def main() -> int:
    args = parse_args()
    check_package_supplies()
    out_dir = args.out_dir.resolve()
    transcript = out_dir / "transcript.txt"
    if not args.no_refresh:
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "hp5004a/check_hp5004a_tc_u11.py"),
                "--out-dir", str(out_dir),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        if result.returncode:
            raise RuntimeError(f"prerequisite full-board capture failed:\n{result.stdout}")
    if not transcript.exists():
        raise RuntimeError(f"missing transcript: {transcript}")

    apertures, stops = parse_samples(transcript.read_text(encoding="utf-8"))
    if sorted(apertures) != [0, 1, 2] or any(stops.get(index) != 30 for index in range(3)):
        raise AssertionError(f"expected three 30-clock apertures, got stops={stops}")

    observed_runs: list[dict[str, tuple[int, str] | None]] = []
    for aperture in range(3):
        rows = apertures[aperture]
        observed_runs.append({signal: known_signature(rows, signal) for signal in EXPECTED})
    if any(run != observed_runs[0] for run in observed_runs[1:]):
        raise AssertionError("T&C WCS/decode observations are not repeatable across apertures")

    observed = observed_runs[0]
    mismatches = {
        signal: (observed[signal][1] if observed[signal] else "Z/Z", expected)
        for signal, expected in EXPECTED.items()
        if observed[signal] is None or observed[signal][1] != expected
    }
    if mismatches != KNOWN_FUNCTIONAL_DIFFERENCES:
        raise AssertionError(
            f"T&C WCS/decode signature differences changed: expected "
            f"{KNOWN_FUNCTIONAL_DIFFERENCES}, got {mismatches}"
        )

    exact = len(EXPECTED) - len(KNOWN_FUNCTIONAL_DIFFERENCES)
    print("HP 5004A / full T&C WCS and decode path: PASS" + (" WITH FUNCTIONAL DIFFERENCES" if KNOWN_FUNCTIONAL_DIFFERENCES else ""))
    print(
        f"  exact service matches: {exact}/{len(EXPECTED)} "
        "(every non-dash U2-U4, U15-U20, U28-U34, and U42-U49 entry)"
    )
    for signal, expected in EXPECTED.items():
        value = observed[signal]
        assert value is not None
        print(
            f"  {signal:7s}: raw=0x{value[0]:04x} display={value[1]} "
            f"manual={expected} "
            f"{'FUNCTIONAL DIFFERENCE' if signal in KNOWN_FUNCTIONAL_DIFFERENCES else 'match'}"
        )
    print(
        "  package boundary: all printed ground/+5V pins are explicit, including "
        "the four WCS AM8304s and U28/U33/U42"
    )
    print("  U48.1 = MEMW/ (U3AA): the ink, the table and the firmware agree (2026-09-25)")
    print(f"  transcript: {transcript}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
