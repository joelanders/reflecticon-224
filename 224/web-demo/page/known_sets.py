#!/usr/bin/env python3
"""Write page/firmware_sets.json: the firmware sets the page recognizes.

For each set: its name, its model (0 = 224X/224XL, 1 = the original 224)
and, for each chip, its SHA-256 and the address it goes to. Only hashes,
never contents: the page identifies a visitor's own chip files by content,
whatever they are named.

  python3 page/known_sets.py "$LEXICON_FIRMWARE"
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Sets that boot on the row machine (v2.2 writes a port its SBC does not have).
UNSUPPORTED = {"224 v2_2": "writes port 0xEF, which the 224's SBC does not have"}


def chip_base(name: str) -> int | None:
    """The address a chip goes to, by name (as larc.js chipBase)."""
    if m := re.search(r"SBC\s*(\d)", name, re.I):
        return (int(m[1]) - 1) * 0x800
    if m := re.search(r"NVS\s*(\d)", name, re.I):
        return 0x8000 + (int(m[1]) - 1) * 0x1000
    if m := re.search(r"ROM\s*([1-4])(?!\d)", name, re.I):
        return (int(m[1]) - 1) * 0x800
    return None


def main() -> None:
    root = Path(sys.argv[1])
    sets = []
    for folder in sorted(p for p in root.iterdir() if p.is_dir() and not p.is_symlink()):
        chips = {}
        for file in sorted(folder.iterdir()):
            if not file.is_file() or not file.suffix.lower() == ".bin":
                continue
            base = chip_base(file.name)
            if base is None or base in chips:
                continue
            chips[base] = hashlib.sha256(file.read_bytes()).hexdigest()
        if not chips:
            continue
        model = 1 if any(re.search(r"ROM\s*[1-4]", f.name, re.I) for f in folder.iterdir()) else 0
        name = folder.name.replace("_", ".")
        entry = {"name": name, "model": model,
                 "chips": [{"base": base, "sha256": sha} for base, sha in sorted(chips.items())]}
        if folder.name in UNSUPPORTED:
            entry["unsupported"] = UNSUPPORTED[folder.name]
        sets.append(entry)
    (HERE / "firmware_sets.json").write_text(json.dumps(sets, indent=1) + "\n")
    print(f"{len(sets)} sets: " + ", ".join(f"{s['name']} ({len(s['chips'])})" for s in sets))


if __name__ == "__main__":
    main()
