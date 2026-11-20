#!/usr/bin/env python3
"""Check an original-224 catalog against the firmware through emulator/probe.

A port of check_panel224 in ../../../web-demo/tests/check_catalog.py, which cannot be pointed at these
catalogs unchanged: it rebuilds its probe inside its own build folder, it
reads the program byte at 3F65 (v4.x) and it expects a SHIFT page (DIFFUSION).
Here the probe comes from the emulator's Makefile and the program byte and pages come
from the firmware's layout (v3.2: program byte 3F44, one page).

For every program: a fresh boot, CALL + PROGRAM n, the program byte, the six
select buttons held (digits and unit LED) and, if the catalog has a page 2,
SHIFT + DEPTH; then each parameter's value table at four pot positions, each
reached from a fresh boot by sweeping the pot through its range (so the soft
pickup takes it) and coming down onto the position. emulator/probe is a different
code path from the web operator (a native build, fixed slow key timing).

  python3 tests/catalogs_extra/check_catalog_probe.py ROM_SET_DIRECTORY CATALOG.json [--jobs N]
"""
from __future__ import annotations

import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PLUGIN = Path(__file__).resolve().parents[2]
ROWS = PLUGIN.parent / "emulator"
PROBE = ROWS / "build" / "probe"
PROGRAM_BYTE = {"v3.2": 0x3F44}          # catalog "layout" -> program byte; v4.x: 3F65

SEGMENTS = {0x3f: "0", 0x06: "1", 0x5b: "2", 0x4f: "3", 0x66: "4", 0x6d: "5", 0x7d: "6", 0x07: "7",
            0x7f: "8", 0x6f: "9", 0x00: " ", 0x40: "-"}
UNITS = ["SEC", "MSEC", "HZ", "KHZ"]


def panel_text(line: str) -> str:
    lit = [~int(x, 16) & 0xff for x in line.split()[1:]]
    text = "".join(SEGMENTS.get(lit[d] & 0x7f, "?") + ("." if lit[d] & 0x80 else "") for d in (2, 1, 0)).strip()
    for bit, unit in enumerate(UNITS):
        if lit[5] >> bit & 1:
            return f"{text} {unit}"
    return text


def run(rom: Path, script: str) -> list[str]:
    return subprocess.run([str(PROBE), str(rom), script], check=True, capture_output=True, text=True).stdout.splitlines()


def select(slot: int, shift: bool) -> str:
    s = f"hold 2 {1 << slot:x}; wait 400; panel; hold 2 00; wait 100"
    if shift:
        s = f"hold 1 08; wait 100; {s}; hold 1 00; wait 100"
    return s


def check_program(rom: Path, p: dict, program_byte: int) -> list[str]:
    shift_page = len(p["pages"]) > 1
    load = f"boot 16; button 1 04 200; wait 300; button 0 {p['identity']:x} 200; wait 2000"
    reads = "; ".join(select(k, False) for k in range(6))
    expected = list(p["presets"]["1"][0])
    if shift_page:
        reads += "; " + select(4, True)
        expected.append(p["presets"]["1"][1][4])
    out = run(rom, f"{load}; peek {program_byte:x} 1; {reads}")
    identity = int(out[0].split()[-1], 16) & 0x3F
    values = [panel_text(line) for line in out[1:1 + len(expected)]]
    issues = []
    if identity != p["identity"]:
        issues.append(f"loaded program {identity:02x}")
    if values != expected:
        issues.append(f"presets {values} != {expected}")
    for page in p["pages"]:
        for slot, slider in enumerate(page["sliders"]):
            if slider["name"] == "INACTIVE":
                continue
            shift = page["page"] == 2
            for raw in (12, 100, 180, 250):
                want = None
                for start, text in slider["table"]:
                    if start <= raw:
                        want = text
                moves = f"pot {slot} 0; wait 300; pot {slot} 255; wait 300; pot {slot} {raw}; wait 300"
                if shift:
                    moves = f"hold 1 08; wait 100; {moves}; hold 1 00; wait 100"
                got = panel_text(run(rom, f"{load}; {moves}; {select(slot, shift)}")[-1])
                if got != want:
                    issues.append(f"{slider['name']} at pot {raw}: {got} != {want}")
    return issues


def main() -> None:
    args = sys.argv[1:]
    jobs = 6
    if "--jobs" in args:
        i = args.index("--jobs")
        jobs = int(args[i + 1])
        del args[i:i + 2]
    rom, catalog_path = Path(args[0]), Path(args[1])
    subprocess.run(["make", "-s", "-C", str(ROWS), "build/probe"], check=True)
    catalog = json.loads(catalog_path.read_text())
    if catalog.get("remote") != "panel224":
        sys.exit(f"{catalog_path}: not an original-224 (panel224) catalog")
    program_byte = PROGRAM_BYTE.get(catalog.get("layout", "v4"), 0x3F65)
    with ThreadPoolExecutor(jobs) as pool:
        results = list(pool.map(lambda p: check_program(rom, p, program_byte), catalog["programs"]))
    problems = 0
    for p, issues in zip(catalog["programs"], results):
        status = "ok"
        if issues:
            status = "DIFFERS: " + "; ".join(issues)
        print(f"{p['buttons']:<7} {p['name']:<21} {status}")
        problems += bool(issues)
    print(f"{len(catalog['programs'])} programs checked, {problems} with differences")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
