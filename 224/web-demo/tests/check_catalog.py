#!/usr/bin/env python3
"""Check a precomputed web catalog against the firmware, independently.

For every program in page/catalogs/HASH.json, the probe (../emulator/probe.cpp) (a different code
path from the page's operator, with slow, safe key timing) loads it and
reads its name, its page-1 slider names and preset values, and how many
variations it has. Every difference is reported.

A 224X (front panel) catalog is checked the same way through the panel:
CALL + the program's buttons, the six select buttons on page 1 (digits and
unit LED), and PROGRAM n for each variation (the firmware's variation byte).

An original-224 catalog (remote "panel224"): CALL + PROGRAM n, the program
byte 3F65, the six select buttons held (digits and unit LED) and SHIFT +
DEPTH (diffusion); then each parameter's value table at four pot positions,
each reached from a fresh boot by sweeping the pot through its whole range
(so the soft pickup takes it) and coming down onto the position (the scan
records a fall as read).

  python3 tests/check_catalog.py ROM_SET_DIRECTORY page/catalogs/HASH.json
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIGIT = [0x3D, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x2D, 0x31, 0x35, 0x39]
SELECT = [0x23, 0x27, 0x2B, 0x2F, 0x33, 0x37]


def probe(rom: Path, script: str) -> list[str]:
    out = subprocess.run([str(ROOT.parent / "emulator/build/probe"), str(rom), script], check=True,
                         capture_output=True, text=True).stdout
    return [line[1:25] for line in out.splitlines() if line.startswith("[")]


SEGMENTS = {0x3f: "0", 0x06: "1", 0x5b: "2", 0x4f: "3", 0x66: "4", 0x6d: "5", 0x7d: "6", 0x07: "7",
            0x7f: "8", 0x6f: "9", 0x00: " ", 0x40: "-"}
UNITS = ["SEC", "MSEC", "HZ", "KHZ"]


def panel_text(line: str) -> str:
    """A probe `panel` line as the LARC would spell it: "2.6 SEC"."""
    lit = [~int(x, 16) & 0xff for x in line.split()[1:]]
    text = "".join(SEGMENTS.get(lit[d] & 0x7f, "?") + ("." if lit[d] & 0x80 else "") for d in (2, 1, 0)).strip()
    unit = next((u for bit, u in enumerate(UNITS) if lit[5] >> bit & 1), None)
    return f"{text} {unit}" if unit else text


def check_panel(rom: Path, catalog: dict) -> int:
    problems = 0
    for p in catalog["programs"]:
        load = f"boot 16; button 1 04 200; wait 300; button 0 {p['identity']:x} 200; wait 2000"
        page1 = "; ".join(f"button 2 {1 << k:x} 200; wait 400; panel" for k in range(6))
        variations = "; ".join(f"button 0 {1 << (v - 1):x} 200; wait 2500; peek 3c58 1" for v in range(2, 9))
        out = subprocess.run([str(ROOT.parent / "emulator/build/probe"), str(rom), f"{load}; peek 3c9a 1; {page1}; {variations}"],
                             check=True, capture_output=True, text=True).stdout.splitlines()
        identity = int(out[0].split()[-1], 16)
        values = [panel_text(line) for line in out[1:7]]
        found = [1]
        for v, line in zip(range(2, 9), out[7:]):
            mask = int(line.split()[-1], 16)
            if mask == 1 << (v - 1):
                found.append(v)
        issues = []
        if identity != p["identity"]:
            issues.append(f"loaded identity {identity:02x}")
        if values != p["presets"]["1"][0]:
            issues.append(f"page 1 {values} != {p['presets']['1'][0]}")
        if found != p["variations"]:
            issues.append(f"variations {found} != {p['variations']}")
        status = "ok" if not issues else "DIFFERS: " + "; ".join(issues)
        print(f"{p['buttons']:<9} {p['name']:<13} {status}", flush=True)
        problems += bool(issues)
    return problems


def check_panel224(rom: Path, catalog: dict) -> int:
    problems = 0
    select = lambda slot, shift: (f"hold 1 08; wait 100; " if shift else "") + \
        f"hold 2 {1 << slot:x}; wait 400; panel; hold 2 00; wait 100" + ("; hold 1 00; wait 100" if shift else "")
    for p in catalog["programs"]:
        load = f"boot 16; button 1 04 200; wait 300; button 0 {p['identity']:x} 200; wait 2000"
        reads = "; ".join(select(k, False) for k in range(6)) + "; " + select(4, True)
        out = subprocess.run([str(ROOT.parent / "emulator/build/probe"), str(rom), f"{load}; peek 3f65 1; {reads}"],
                             check=True, capture_output=True, text=True).stdout.splitlines()
        identity = int(out[0].split()[-1], 16) & 0x3F
        values = [panel_text(line) for line in out[1:8]]
        expected = p["presets"]["1"][0] + [p["presets"]["1"][1][4]]
        issues = []
        if identity != p["identity"]:
            issues.append(f"loaded program {identity:02x}")
        if values != expected:
            issues.append(f"presets {values} != {expected}")
        # Value tables: four pot positions per parameter.
        for page in p["pages"]:
            for slot, slider in enumerate(page["sliders"]):
                if slider["name"] == "INACTIVE":
                    continue
                shift = page["page"] == 2
                for raw in (12, 100, 180, 250):
                    want = next(text for start, text in reversed(slider["table"]) if start <= raw)
                    moves = f"pot {slot} 0; wait 300; pot {slot} 255; wait 300; pot {slot} {raw}; wait 300"
                    if shift:
                        moves = f"hold 1 08; wait 100; {moves}; hold 1 00; wait 100"
                    got = subprocess.run([str(ROOT.parent / "emulator/build/probe"), str(rom), f"{load}; {moves}; {select(slot, shift)}"],
                                         check=True, capture_output=True, text=True).stdout.splitlines()
                    got = panel_text(got[-1])
                    if got != want:
                        issues.append(f"{slider['name']} at pot {raw}: {got} != {want}")
        status = "ok" if not issues else "DIFFERS: " + "; ".join(issues)
        print(f"{p['buttons']:<7} {p['name']:<21} {status}", flush=True)
        problems += bool(issues)
    return problems


def main():
    rom, catalog_path = Path(sys.argv[1]), Path(sys.argv[2])
    # the probe is the row machine's command-line host; the 8080 core compiles in
    subprocess.run(["make", "-s", "-C", str(ROOT.parent / "emulator"), "build/probe"], check=True)
    catalog = json.loads(catalog_path.read_text())
    if catalog.get("remote") == "panel224":
        problems = check_panel224(rom, catalog)
        print(f"{len(catalog['programs'])} programs checked, {problems} with differences")
        sys.exit(1 if problems else 0)
    if catalog.get("remote") == "panel":
        problems = check_panel(rom, catalog)
        print(f"{len(catalog['programs'])} programs checked, {problems} with differences")
        sys.exit(1 if problems else 0)
    problems = 0
    for p in catalog["programs"]:
        load = (f"boot 16; key 0x22; wait 150; key {DIGIT[p['bank']]}; wait 400; "
                f"key 0x21; wait 150; key {DIGIT[p['program']]}; wait 2500; show")
        # Each slider key twice, keeping the second reading: a key pressed
        # while the firmware is still busy after the load can be dropped.
        sliders = "; ".join(f"key {k}; wait 350; key {k}; wait 350; show" for k in SELECT)
        # A harmless key first: a VAR pressed while the firmware is busy is lost,
        # and the digit alone would then select a PROGRAM.
        variations = "; ".join(f"key {SELECT[0]}; wait 400; key 0x26; wait 400; key {DIGIT[v]}; wait 2500; show"
                               for v in range(2, 10))
        lines = probe(rom, f"{load}; {sliders}; {variations}")
        name = lines[0][:12].strip()
        page1 = [(line[:12].strip(), re.sub(r"\s+", " ", line[12:].strip())) for line in lines[1:7]]
        found = [1] + [v for v, line in zip(range(2, 10), lines[7:])
                       if re.search(rf"B{p['bank']} P{p['program']} V{v}", line)]
        expected_page1 = [(s["name"], v) for s, v in zip(p["pages"][0]["sliders"], p["presets"]["1"][0])]
        issues = []
        if name != p["name"]:
            issues.append(f"name {name!r} != {p['name']!r}")
        if page1 != expected_page1:
            issues.append(f"page 1 {page1} != {expected_page1}")
        if found != p["variations"]:
            # Chained variation loads can drop keys (Chorus&Echo loads slowly):
            # re-check each disputed variation alone, from a fresh boot.
            for v in sorted(set(found) ^ set(p["variations"])):
                alone = probe(rom, f"{load}; key {SELECT[0]}; wait 500; key 0x26; wait 500; "
                                   f"key {DIGIT[v]}; wait 4000; show")
                present = bool(re.search(rf"B{p['bank']} P{p['program']} V{v}", alone[-1]))
                found = sorted((set(found) | {v}) if present else (set(found) - {v}))
            if found != p["variations"]:
                issues.append(f"variations {found} != {p['variations']}")
        status = "ok" if not issues else "DIFFERS: " + "; ".join(issues)
        print(f"B{p['bank']} P{p['program']} {p['name']:<13} {status}", flush=True)
        problems += bool(issues)
    print(f"{len(catalog['programs'])} programs checked, {problems} with differences")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
