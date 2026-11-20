#!/usr/bin/env python3
"""Check full-board T&C U11 against the 224X v8.2.1 HP 5004A table."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

from hp5004a_signature import compress_bits, format_signature


ROOT = Path(__file__).resolve().parents[1]
HDL = ROOT
DEFAULT_WCS = ROOT.parent / "captures/224x_diag_pgm3.hex"
DEFAULT_OUT = ROOT / "out/hp5004a_tc_u11"
EXPECTED_WCS_SHA256 = "19fb2e3c3f518ac8930d8c3173661af712778cbaa69e7941cd1b66a63271260c"
SIGNALS = (
    "rail", "ground",
    "u5p1", "u5p3", "u5p4", "u5p5", "u5p6", "u5p7", "u5p8",
    "u5p9", "u5p10", "u5p11", "u5p12", "u5p13", "u5p14", "u5p16",
    "u10p1", "u10p4", "u10p5", "u10p6", "u10p7", "u10p8",
    "u10p11", "u10p13", "u10p16",
    "u11p1", "u11p4", "u11p5", "u11p6",
    "u11p7", "u11p8", "u11p11", "u11p13", "u11p16",
)
EXPECTED = {
    "rail": "FP54",
    "ground": "0000",
    "u5p1": "FP54",
    "u5p3": "F5AA",
    "u5p4": "A0A8",
    "u5p5": "53PH",
    "u5p6": "028H",
    "u5p7": "0000",
    "u5p8": "0000",
    "u5p9": "0000",
    "u5p10": "0000",
    "u5p11": "8146",
    "u5p12": "29U6",
    "u5p13": "H054",
    "u5p14": "P2H5",
    "u5p16": "FP54",
    "u10p1": "FP54",
    "u10p4": "1UFU",
    "u10p5": "H054",
    "u10p6": "8146",
    "u10p7": "8146",
    "u10p8": "0000",
    "u10p11": "F1C3",
    "u10p13": "8146",
    "u10p16": "FP54",
    "u11p1": "FP54",
    "u11p4": "01UH",
    "u11p5": "P2H5",
    "u11p6": "2PU6",
    "u11p7": "29U6",
    "u11p8": "0000",
    "u11p11": "FPAA",
    "u11p13": "29U6",
    "u11p16": "FP54",
}
# U5.12 and U11.6 are the same C4/ net on schematic 060-02475.  The service
# table prints 29U6 at U5.12 but 2PU6 at U11.6, which cannot both be true in one
# run.  Keep the printed value above and require this one exact contradiction;
# any additional mismatch remains a regression failure.
KNOWN_MANUAL_CONTRADICTIONS = {"u11p6": ("29U6", "2PU6")}
SAMPLE_RE = re.compile(
    r"^HP_SAMPLE phase=pre aperture=(\d+) index=(\d+) .*? (?P<payload>rail=.*)$"
)
EXTRA_RE = re.compile(
    r"^HP_EXTRA phase=pre aperture=(\d+) index=(\d+) (?P<payload>u\d+p\d+=.*)$"
)
STOP_RE = re.compile(r"^HP_GATE edge=stop aperture=(\d+) .* samples=(\d+) ")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wcs", type=Path, default=DEFAULT_WCS)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument(
        "--tc-source",
        type=Path,
        default=HDL / "generated/tc_board.v",
        help="T&C Verilog source to compile (used by isolated topology A/B gates)",
    )
    parser.add_argument("--iverilog", default="iverilog")
    parser.add_argument("--vvp", default="vvp")
    return parser.parse_args()


def run(command: list[str], *, cwd: Path) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout}"
        )
    return result.stdout


def load_wcs(path: Path) -> bytes:
    values: dict[int, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ":" not in line:
            continue
        address_text, payload = line.split(":", maxsplit=1)
        address = int(address_text.strip(), 16) - 0x4000
        for index, token in enumerate(payload.split()):
            values[address + index] = int(token, 16)
    if set(values) != set(range(512)):
        raise ValueError(f"{path}: expected exactly 512 WCS bytes, got {len(values)}")
    return bytes(values[index] for index in range(512))


def prepare_runtime(out_dir: Path, wcs: bytes) -> None:
    # CPU-visible wcssave bytes are active-low relative to the MCM68B10 output
    # pin domain.  Physical SRAM address A contains logical row 127-A.
    physical = bytes((~value) & 0xFF for value in wcs)
    lane_dir = out_dir / "out"
    lane_dir.mkdir(parents=True, exist_ok=True)
    for lane in range(4):
        values = [f"{physical[(127 - address) * 4 + lane]:02x}" for address in range(128)]
        (lane_dir / f"wcs_lane_b{lane}.hex").write_text(
            "\n".join(values) + "\n", encoding="ascii"
        )
    rom_dir = out_dir / "rom"
    rom_dir.mkdir(exist_ok=True)
    shutil.copyfile(HDL / "rom/u6_74s287_init.hex", rom_dir / "u6_74s287_init.hex")


def parse_samples(text: str) -> tuple[dict[int, list[dict[str, str]]], dict[int, int]]:
    apertures: dict[int, list[dict[str, str]]] = {}
    stop_counts: dict[int, int] = {}
    for line in text.splitlines():
        sample = SAMPLE_RE.match(line.strip())
        if sample:
            aperture = int(sample.group(1))
            row = {"index": sample.group(2)}
            for item in sample.group("payload").split():
                key, value = item.split("=", maxsplit=1)
                row[key] = value
            apertures.setdefault(aperture, []).append(row)
            continue
        extra = EXTRA_RE.match(line.strip())
        if extra:
            aperture = int(extra.group(1))
            rows = apertures.get(aperture)
            if not rows or rows[-1]["index"] != extra.group(2):
                raise ValueError(
                    f"orphan/out-of-order HP_EXTRA aperture={aperture} "
                    f"index={extra.group(2)}"
                )
            for item in extra.group("payload").split():
                key, value = item.split("=", maxsplit=1)
                rows[-1][key] = value
            continue
        stop = STOP_RE.match(line.strip())
        if stop:
            stop_counts[int(stop.group(1))] = int(stop.group(2))
    return apertures, stop_counts


def signature(rows: list[dict[str, str]], signal: str) -> tuple[int, str]:
    values = [row[signal] for row in rows]
    unknown = [(index, value) for index, value in enumerate(values) if value not in {"0", "1"}]
    if unknown:
        raise AssertionError(f"{signal}: X/Z samples at {unknown}")
    raw = compress_bits(int(value) for value in values)
    return raw, format_signature(raw)


def main() -> int:
    args = parse_args()
    wcs_path = args.wcs.resolve()
    out_dir = args.out_dir.resolve()
    tc_source = args.tc_source.resolve()
    if not tc_source.is_file():
        raise ValueError(f"missing T&C source: {tc_source}")
    out_dir.mkdir(parents=True, exist_ok=True)
    wcs_text_sha = hashlib.sha256(wcs_path.read_bytes()).hexdigest()
    if wcs_path == DEFAULT_WCS.resolve() and wcs_text_sha != EXPECTED_WCS_SHA256:
        raise AssertionError(
            f"firmware-captured Program 3 WCS changed: {wcs_text_sha}"
        )
    wcs = load_wcs(wcs_path)
    prepare_runtime(out_dir, wcs)

    vvp_path = out_dir / "tb_hp5004a_tc_u11.vvp"
    transcript_path = out_dir / "transcript.txt"
    run(
        [
            args.iverilog,
            "-g2012",
            "-s", "tb_hp5004a_tc_u11",
            "-o", str(vvp_path),
            str(ROOT / "hp5004a/tb_hp5004a_tc_u11.v"),
            str(tc_source),
            str(HDL / "prims/prims.v"),
        ],
        cwd=out_dir,
    )
    transcript = run([args.vvp, str(vvp_path)], cwd=out_dir)
    transcript_path.write_text(transcript, encoding="utf-8")
    apertures, stop_counts = parse_samples(transcript)
    if sorted(apertures) != [0, 1, 2] or sorted(stop_counts) != [0, 1, 2]:
        raise AssertionError(
            f"expected three complete RESETD apertures, got samples={sorted(apertures)} "
            f"stops={stop_counts}; transcript={transcript_path}"
        )

    observed_by_aperture: list[dict[str, tuple[int, str]]] = []
    for aperture in range(3):
        rows = apertures[aperture]
        if stop_counts[aperture] != len(rows):
            raise AssertionError(
                f"aperture {aperture}: stop count {stop_counts[aperture]} != parsed {len(rows)}"
            )
        if len(rows) != 30:
            raise AssertionError(f"aperture {aperture}: expected 30 clocks, got {len(rows)}")
        if [int(row["index"]) for row in rows] != list(range(30)):
            raise AssertionError(f"aperture {aperture}: sample indexes are not 0..29")
        observed = {signal: signature(rows, signal) for signal in SIGNALS}
        if any(row["u5p12"] != row["u11p6"] for row in rows):
            raise AssertionError("U5.12 and U11.6 diverged despite sharing schematic net C4/")
        if any(row["u11p7"] != row["u11p13"] for row in rows):
            raise AssertionError("U11.7 and U11.13 diverged despite Q2 feedback connection")
        observed_by_aperture.append(observed)

    first = observed_by_aperture[0]
    if any(observed != first for observed in observed_by_aperture[1:]):
        raise AssertionError(f"signatures are not repeatable across apertures: {observed_by_aperture}")
    mismatches = {
        signal: (first[signal][1], expected)
        for signal, expected in EXPECTED.items()
        if first[signal][1] != expected
    }
    if mismatches != KNOWN_MANUAL_CONTRADICTIONS:
        raise AssertionError(
            f"service-table differences changed: expected only "
            f"{KNOWN_MANUAL_CONTRADICTIONS}, got {mismatches}"
        )

    print("HP 5004A / full T&C coefficient path: PASS WITH ONE MANUAL CONTRADICTION")
    print(f"  WCS: firmware Diagnostic Program 3, display E0A, sha256={wcs_text_sha[:12]}")
    print("  gate: consecutive falling RESETD edges; 30 rising DAB_RSTB/ clocks; 3 repeat apertures")
    for signal in SIGNALS:
        raw, display = first[signal]
        marker = "CONTRADICTION" if signal in KNOWN_MANUAL_CONTRADICTIONS else "match"
        print(
            f"  {signal:7s}: raw=0x{raw:04x} display={display} "
            f"manual={EXPECTED[signal]} {marker}"
        )
    print(
        "  diagnosis: U5.12 and U11.6 are the same C4/ net; manual prints "
        "29U6 and 2PU6 respectively, while HDL consistently gives 29U6 at both"
    )
    print(f"  transcript: {transcript_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
