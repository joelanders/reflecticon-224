#!/usr/bin/env python3
"""Check the structural T&C HALT input gates WCSA advancement."""

from __future__ import annotations

import re
import sys
from pathlib import Path


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    samples: dict[str, list[int]] = {"run_a": [], "hold": [], "run_b": []}
    for line in text.splitlines():
        m = re.match(r"HALT_SAMPLE tag=(\S+) i=\d+ halt=([01x]) wcsa=(\d+)", line)
        if not m:
            continue
        tag, halt, wcsa_s = m.groups()
        if tag in samples and halt in {"0", "1"}:
            samples[tag].append(int(wcsa_s))

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    for tag in ("run_a", "hold", "run_b"):
        check(f"{tag} coverage", len(samples[tag]) >= 6, str(samples[tag]))

    def advances(vals: list[int]) -> bool:
        return len(set(vals)) >= 4 and all(((vals[i + 1] - vals[i]) & 0x7f) == 1 for i in range(len(vals) - 1))

    check("HALT high advances WCSA before hold", advances(samples["run_a"]), str(samples["run_a"]))
    check("HALT low holds WCSA stable", len(set(samples["hold"])) == 1, str(samples["hold"]))
    check("HALT high resumes WCSA after hold", advances(samples["run_b"]), str(samples["run_b"]))
    check("simulation completed", "HALT_DONE" in text, "HALT_DONE present" if "HALT_DONE" in text else "missing")

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
        failed += 0 if ok else 1
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
