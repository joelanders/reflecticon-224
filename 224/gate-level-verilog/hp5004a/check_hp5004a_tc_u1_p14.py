#!/usr/bin/env python3
"""Check the HP 5004A model against the 224X T&C U1 signature pilot."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from hp5004a_signature import compress_bits, format_signature, signature_for_bits


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_DIR = ROOT / "temp" / "hp5004a_tc_u1_p14"
SAMPLE_RE = re.compile(r"^HP_SAMPLE phase=(pre|post) index=(\d+) (?P<payload>.+)$")
SIGNAL_NAMES = (
    "rail",
    "ground",
    "p10",
    "p14",
    "p13",
    "p12",
    "u14p16",
    "u14p14",
    "u14p13",
    "u14p12",
    "u14p11",
    "u14p10",
    "u14p9",
    "u14p8",
    "u14p7",
)


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    ap.add_argument("--iverilog", default="iverilog")
    ap.add_argument("--vvp", default="vvp")
    return ap.parse_args()


def run(cmd: list[str], *, cwd: Path) -> str:
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"command failed with exit {proc.returncode}: {' '.join(cmd)}\n{proc.stdout}"
        )
    return proc.stdout


def model_self_check() -> None:
    # Hand-checkable shift vectors establish direction and display order.
    assert compress_bits([]) == 0x0000
    assert compress_bits([0] * 30) == 0x0000
    assert compress_bits([1]) == 0x8000
    assert format_signature(0x8000) == "0001"
    assert compress_bits([1, 1]) == 0xC000
    assert format_signature(0xC000) == "0003"
    assert compress_bits([1] * 30) == 0x2A73
    assert signature_for_bits([1] * 30) == "FP54"


def parse_samples(text: str) -> dict[str, list[dict[str, int]]]:
    phases: dict[str, list[dict[str, int]]] = {"pre": [], "post": []}
    for line in text.splitlines():
        match = SAMPLE_RE.match(line.strip())
        if match is None:
            continue
        phase = match.group(1)
        row = {"index": int(match.group(2))}
        for item in match.group("payload").split():
            key, value = item.split("=", maxsplit=1)
            row[key] = int(value)
        missing = set(SIGNAL_NAMES) - row.keys()
        if missing:
            raise ValueError(f"{phase} sample {row['index']}: missing signals {sorted(missing)}")
        phases[phase].append(row)
    return phases


def signatures(rows: list[dict[str, int]]) -> dict[str, tuple[int, str]]:
    result: dict[str, tuple[int, str]] = {}
    for signal in SIGNAL_NAMES:
        raw = compress_bits(row[signal] for row in rows)
        result[signal] = (raw, format_signature(raw))
    return result


def main() -> int:
    args = parse_args()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    vvp_path = out_dir / "tb_hp5004a_tc_u1_p14.vvp"
    transcript_path = out_dir / "transcript.txt"

    model_self_check()
    run(
        [
            args.iverilog,
            "-g2012",
            "-s",
            "tb_hp5004a_tc_u1_p14",
            "-o",
            str(vvp_path),
            str(ROOT / "prims/prims.v"),
            str(ROOT / "hp5004a/tb_hp5004a_tc_u1_p14.v"),
        ],
        cwd=out_dir,
    )
    transcript = run([args.vvp, str(vvp_path)], cwd=out_dir)
    transcript_path.write_text(transcript, encoding="utf-8")

    phases = parse_samples(transcript)
    for phase, rows in phases.items():
        if len(rows) != 30:
            raise AssertionError(f"{phase}: expected 30 selected-clock samples, got {len(rows)}")
        if [row["index"] for row in rows] != list(range(30)):
            raise AssertionError(f"{phase}: sample indices are not exactly 0..29")

    pre = phases["pre"]
    post = phases["post"]
    pre_signatures = signatures(pre)
    post_signatures = signatures(post)

    # Service manual p. 5-17, T&C v8.2.1, Diagnostic Program 3:
    # +5V=FP54, GND=0000, U1 pin10=40A5, pin14=3U9F,
    # pin13=0000, pin12=0000.  The bit streams state why those values occur.
    expected_pre = {
        "rail": (0x2A73, "FP54"),
        "ground": (0x0000, "0000"),
        "p10": (0xA502, "40A5"),
        "p14": (0x39FC, "3U9F"),
        "p13": (0x0000, "0000"),
        "p12": (0x0000, "0000"),
        # Service manual p. 5-18, continuing the same T&C v8.2.1 setup.
        "u14p16": (0x2A73, "FP54"),
        "u14p14": (0x33A2, "45FF"),
        "u14p13": (0x22C3, "F344"),
        "u14p12": (0x7DA9, "95CP"),
        "u14p11": (0xB6C5, "A36H"),
        "u14p10": (0x2A73, "FP54"),
        "u14p9": (0x2A73, "FP54"),
        "u14p8": (0x0000, "0000"),
        "u14p7": (0x2A73, "FP54"),
    }
    if pre_signatures != expected_pre:
        raise AssertionError(
            f"pre-edge signatures differ from service table:\n"
            f"  expected={expected_pre}\n  observed={pre_signatures}"
        )

    if [row["p14"] for row in pre] != ([0] * 16 + [1] * 14):
        raise AssertionError("U1 pin14 must be 16 zero samples followed by 14 one samples")
    if [row["p10"] for row in pre] != ([0] * 15 + [1] + [0] * 14):
        raise AssertionError("U1 pin10 must pulse only on selected-clock sample 15")
    for bit, signal in enumerate(("u14p14", "u14p13", "u14p12", "u14p11")):
        expected_bits = [(index >> bit) & 1 for index in range(30)]
        if [row[signal] for row in pre] != expected_bits:
            raise AssertionError(f"{signal} does not expose low-PC bit {bit} before clock-to-Q")
    for signal in ("u14p16", "u14p10", "u14p9", "u14p7"):
        if [row[signal] for row in pre] != [row["rail"] for row in pre]:
            raise AssertionError(f"{signal} must be the same tied-high stream as +5V")
    if [row["u14p8"] for row in pre] != [row["ground"] for row in pre]:
        raise AssertionError("u14p8 must be the same tied-low stream as ground")

    # This deliberately wrong phase is a useful sensitivity check: waiting for
    # LS163 clock-to-Q changes pin14 to 15 zeros + 15 ones and produces 7U39.
    if post_signatures["p14"] != (0x9CFE, "7U39"):
        raise AssertionError(
            f"post-edge negative control should be raw=0x9cfe/display=7U39, "
            f"got {post_signatures['p14']}"
        )
    expected_u14_post = {
        "u14p14": (0x19D1, "8C98"),
        "u14p13": (0x1161, "8688"),
        "u14p12": (0xBED4, "2C7H"),
        "u14p11": (0x5B62, "46HA"),
    }
    for signal, expected_value in expected_u14_post.items():
        if post_signatures[signal] != expected_value:
            raise AssertionError(
                f"post-edge negative control {signal}: expected {expected_value}, "
                f"got {post_signatures[signal]}"
            )

    print("HP 5004A / T&C U14-to-U1 program-counter island: PASS")
    print("  qualified clocks: 30 (manual +5V signature FP54)")
    for signal in ("rail", "ground", "p10", "p14", "p13", "p12"):
        raw, display = pre_signatures[signal]
        print(f"  pre-edge {signal:6s}: raw=0x{raw:04x} display={display}")
    print("  U14 continuation (manual p. 5-18):")
    for signal in (
        "u14p16", "u14p14", "u14p13", "u14p12", "u14p11",
        "u14p10", "u14p9", "u14p8", "u14p7",
    ):
        raw, display = pre_signatures[signal]
        print(f"    {signal:7s}: raw=0x{raw:04x} display={display}")
    print(
        "  phase negative control: post-edge U1 pin14 "
        f"raw=0x{post_signatures['p14'][0]:04x} display={post_signatures['p14'][1]}"
    )
    print(f"  transcript: {transcript_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
