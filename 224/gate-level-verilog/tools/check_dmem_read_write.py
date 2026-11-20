#!/usr/bin/env python3
"""Grade tb_dmem_read_write output: written words must read back exactly, an
unwritten cell must read zero, and the DAB must be high-Z in idle frames."""
import re
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
results = []
reads = idles = 0
for line in text.splitlines():
    m = re.match(r"RW (\S+) expect ([0-9a-fx]+) got ([0-9a-fxz]+)", line)
    if m:
        reads += 1
        results.append((m.group(1), m.group(2) == m.group(3), f"expect {m.group(2)} got {m.group(3)}"))
    m = re.match(r"RW (\S+) bus ([01xz]+)", line)
    if m:
        idles += 1
        results.append((f"{m.group(1)} high-Z", set(m.group(2)) == {"z"}, f"bus {m.group(2)}"))
results.append(("coverage: 3 reads + 3 idle checks", reads == 3 and idles == 3, f"reads={reads} idles={idles}"))
failed = 0
for name, ok, detail in results:
    print(f"{'PASS' if ok else 'FAIL'}  {name:<32}  {detail}")
    failed += 0 if ok else 1
print(f"{len(results)-failed}/{len(results)} rules pass")
sys.exit(1 if failed else 0)
