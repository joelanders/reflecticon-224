#!/usr/bin/env python3
"""Netlist lint: sweep the five boards for the classes of drawing error that
matter to simulation.

  C1 control pin unconnected      (an enable, clock or chip select left open)
  C2 control pin on undriven net  (a board input, or a lost wire?)
  C3 cross-sheet label twins      (one signal under two local labels)
  C4 ground/power net not in rail set
  C5 multiple totem-pole drivers on one net

The output is a list of places to check against the drawing. It runs on the
netlists as simulated (sim/*.json applied).
"""
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
from netlist_lint import parse_sexpr, tokenize
from netlist_to_verilog import parse_netlist, apply_sim, resolve_component_part, PART_TO_MODULE

# control pins per canonical part: pin -> meaning
CONTROL_PINS = {
    "74LS157": {"1": "S", "15": "E/"}, "74F157": {"1": "S", "15": "E/"},
    "74LS244": {"1": "1G/", "19": "2G/"}, "74LS244_Split": {"1": "1G/", "19": "2G/"},
    "74LS374": {"1": "OE/", "11": "CLK"}, "SN74F374N": {"1": "OE/", "11": "CLK"},
    "74LS377": {"1": "E/", "11": "CLK"},
    "74LS163": {"1": "CLR/", "2": "CLK", "7": "CEP", "9": "PE/", "10": "CET"},
    "SN74S163N": {"1": "CLR/", "2": "CLK", "7": "CEP", "9": "PE/", "10": "CET"},
    "74LS175": {"1": "CLR/", "9": "CLK"},
    "74LS74": {"1": "1CLR/", "3": "1CLK", "4": "1PRE/", "10": "2PRE/", "11": "2CLK", "13": "2CLR/"},
    "SN74S74N": {"1": "1CLR/", "3": "1CLK", "4": "1PRE/", "10": "2PRE/", "11": "2CLK", "13": "2CLR/"},
    "SN74S112AN": {"1": "1CLK", "4": "1PRE/", "13": "2CLK", "14": "2CLR/", "15": "1CLR/", "10": "2PRE/"},
    "74LS670": {"11": "GR/", "12": "GW/"},
    "74LS155": {"1": "1C", "2": "1G/", "14": "2G/", "15": "2C/"},
    "74LS139": {"1": "1G/", "15": "2G/"},
    "74LS138": {"4": "G2A/", "5": "G2B/", "6": "G1"},
    "74LS194": {"1": "CLR/", "9": "S0", "10": "S1", "11": "CLK"},
    "74LS195": {"1": "CLR/", "9": "PE/", "10": "CLK"},
    "74LS393": {"1": "1CP", "2": "1MR", "12": "2MR", "13": "2CP"},
    "MCM68B10": {"10": "CS0", "11": "CS1", "12": "CS2", "13": "CS3", "14": "CS4", "15": "CS5", "16": "RW"},
}

RAILS = {"+5V", "VCC", "VDD", "GND", "GNDPWR", "VSS", "GNDREF", "EARTH", "+12V", "-5V"}

NETLISTS = "../schematics/netlists"
BOARDS = {   # board: (netlist, ports, simulation-only changes)
    "tc": (f"{NETLISTS}/tc.net", "ports/tc.json", "sim/tc.json"),
    "aru": (f"{NETLISTS}/aru.net", "ports/aru.json", None),
    "dmem": (f"{NETLISTS}/dmem.net", "ports/dmem.json", "sim/dmem.json"),
    "dmem_io": (f"{NETLISTS}/dmem-io.net", "ports/dmem_io.json", None),
    "fpc": (f"{NETLISTS}/fpc.net", "ports/fpc.json", None),
}


def main() -> int:
    findings = []
    for board, (netpath, portpath, simpath) in BOARDS.items():
        comps, nets = parse_netlist(parse_sexpr(tokenize(open(netpath).read())))
        if simpath:
            apply_sim(comps, nets, Path(simpath))
        ports = json.loads(Path(portpath).read_text())

        net_of = {}
        drivers = defaultdict(list)
        for net in nets:
            for n in net.nodes:
                net_of[(n.ref, n.pin)] = net
                t = n.pintype.split("+")[0]
                if t in ("output", "tri_state", "power_out", "open_collector"):
                    drivers[net.name].append((n.ref, n.pin, t))

        # C5: multiple totem-pole drivers
        for net in nets:
            tot = [d for d in drivers[net.name] if d[2] == "output"]
            if len(tot) > 1:
                findings.append((board, "C5", net.name,
                                 "totem-pole drivers: " + ", ".join(f"{r}.{p}" for r, p, _ in tot)))

        # C4: rail-looking net names not in rail set
        for net in nets:
            up = net.name.upper()
            if re.search(r"GND|VCC|VDD|VSS|EARTH|\+5|\+12|-5", up) and up not in RAILS \
               and not net.name.startswith(("Net-(", "unconnected")):
                findings.append((board, "C4", net.name, "rail-like name outside rail set"))

        # C3: same-basename twins
        groups = defaultdict(list)
        for net in nets:
            base = net.name.split("/")[-1].strip()
            if base and not net.name.startswith(("unconnected", "Net-(")):
                groups[base].append(net.name)
        for base, names in groups.items():
            if len(names) > 1:
                findings.append((board, "C3", base, f"label twins: {names}"))

        # C1/C2: control pins
        for ref, comp in sorted(comps.items()):
            part = resolve_component_part(comp)
            pins = CONTROL_PINS.get(part)
            if not pins:
                continue
            for pin, meaning in pins.items():
                net = net_of.get((ref, pin))
                if net is None or net.name.startswith("unconnected"):
                    findings.append((board, "C1", f"{ref}.{pin}",
                                     f"{part} {meaning} unconnected (float-tie rule applies — verify vs schematic)"))
                    continue
                if net.name in RAILS or net.name.upper() in RAILS:
                    continue
                if net.name in ports:
                    continue
                if not drivers[net.name]:
                    findings.append((board, "C2", f"{ref}.{pin}",
                                     f"{part} {meaning} on undriven net {net.name!r} (input? lost wire?)"))

    by_board = defaultdict(list)
    for f in findings:
        by_board[f[0]].append(f)
    for board in BOARDS:
        fs = by_board[board]
        print(f"== {board}: {len(fs)} findings")
        for _, cls, where, what in fs:
            print(f"  [{cls}] {where}: {what}")
    print(f"TOTAL {len(findings)} findings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
