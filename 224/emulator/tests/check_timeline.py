#!/usr/bin/env python3
"""Check emulator/timeline.cpp's output against tests/timeline.expected.

The timelines are the pictures the notes and README use (a CPU write to the
WCS, one DSP row). Keeping them in a checked file means the pictures cannot
drift from the code: a change in the host's timing shows up here.

    python3 tests/check_timeline.py            # compare
    python3 tests/check_timeline.py --update   # rewrite the expected file
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = ROOT / "tests/timeline.expected"


def timelines() -> str:
    out = ROOT / "build"
    out.mkdir(exist_ok=True)
    subprocess.run(["make", "-s", "-C", str(ROOT), "build/timeline"], check=True)
    binary = ROOT / "build" / "timeline"
    parts = []
    for mode in ["wcs-write", "row"]:
        result = subprocess.run([str(binary), mode], check=True, capture_output=True, text=True)
        parts.append(f"$ timeline {mode}\n{result.stdout}")
    return "\n".join(parts)


def main():
    text = timelines()
    if "--update" in sys.argv:
        EXPECTED.write_text(text)
        print(f"wrote {EXPECTED}")
        return
    expected = EXPECTED.read_text()
    if text == expected:
        print("timelines: identical to tests/timeline.expected")
        return
    ours, theirs = text.splitlines(), expected.splitlines()
    for n, (a, b) in enumerate(zip(ours, theirs)):
        if a != b:
            sys.exit(f"timelines differ at line {n + 1}:\n  now:      {a}\n  expected: {b}")
    sys.exit(f"timelines differ in length: {len(ours)} lines now, {len(theirs)} expected")


if __name__ == "__main__":
    main()
