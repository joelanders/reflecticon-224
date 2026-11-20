#!/usr/bin/env python3
"""Run the standard CP/M 8080 exercisers on the core, clock state by clock state.

    python3 tests/exercisers.py           # TST8080, 8080PRE, CPUTEST (seconds)
    python3 tests/exercisers.py --full    # and 8080EXM (about 95 s)

The test programs are not in this repository. They are fetched once from
github.com/superzazu/8080 at a pinned commit into refs/cpu_tests/ and checked
by SHA-256. Each must run to completion with exactly its published number of
clock states and print no ERROR.
"""
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFS = ROOT / "refs" / "cpu_tests"
BUILD = ROOT / "build"
SOURCE = "https://raw.githubusercontent.com/superzazu/8080/274ffd700b81baabea99b0963bc1260b67132185/cpu_tests/"

# program: (SHA-256, clock states without wait states)
PROGRAMS = {
    "TST8080": ("9561c6fb6c99efe3de00eb77e4044fd102151058b39ac2d7bce10483838a08e7", 4924),
    "8080PRE": ("18eb3c79cba42c0718f160be6a1853cb64cdce7aa47d65780189a57bdd98c4e0", 7817),
    "CPUTEST": ("e61a9a75348c774486c2207080ea4effbf6c2367fdace31b0731081a4144030b", 255653383),
    "8080EXM": ("6e3286e11bb1a8f47b8ee1280b4a067be813193363e3223c99b0d21912f44aeb", 23803381171),
}


def fetch(name: str, sha: str) -> Path:
    path = REFS / f"{name}.COM"
    if not path.exists():
        REFS.mkdir(parents=True, exist_ok=True)
        print(f"fetching {name}.COM")
        with urllib.request.urlopen(SOURCE + f"{name}.COM") as response:
            path.write_bytes(response.read())
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha:
        sys.exit(f"{path}: SHA-256 {digest[:12]} is not the pinned file's {sha[:12]}")
    return path


def main() -> int:
    names = list(PROGRAMS) if "--full" in sys.argv else [n for n in PROGRAMS if n != "8080EXM"]
    BUILD.mkdir(exist_ok=True)
    binary = BUILD / "exerciser"
    subprocess.run(["cc", "-std=c11", "-O2", f"-I{ROOT}", "-o", str(binary), str(ROOT / "tests/exerciser.c")], check=True)
    failed = 0
    for name in names:
        sha, states = PROGRAMS[name]
        run = subprocess.run([str(binary), str(fetch(name, sha))], capture_output=True, text=True)
        match = re.search(r": (\d+) states", run.stdout)
        got = int(match.group(1)) if match else None
        ok = run.returncode == 0 and got == states and "ERROR" not in run.stdout.upper()
        failed += not ok
        print(f"{name}: {got if got is not None else '?'} states (published {states}) {'ok' if ok else 'FAILED'}")
        if not ok:
            print(run.stdout[-2000:])
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
