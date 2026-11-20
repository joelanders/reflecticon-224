#!/usr/bin/env python3
"""Convert a KiCad netlist (kicadsexpr) of one board to structural Verilog.

- One flat module per board, one wire per net.
- Each part becomes an instance of its primitive in prims/prims.v; a part with
  no primitive becomes a black box (bb_<part>).
- Parts marked do-not-populate in the schematic are left out.
- --ports lists the nets that become module ports; --sim applies the few
  changes that exist only for simulation (sim/*.json).
"""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from netlist_lint import parse_sexpr, tokenize


@dataclass
class Component:
    ref: str
    value: str
    part: str


@dataclass
class Node:
    ref: str
    pin: str
    pinfunction: str
    pintype: str


@dataclass
class Net:
    code: str
    name: str
    nodes: list[Node] = field(default_factory=list)


def get_field(item: list[Any], key: str) -> str | None:
    for e in item[1:]:
        if isinstance(e, list) and e and e[0] == key and len(e) >= 2 and isinstance(e[1], str):
            return e[1]
    return None


def parse_netlist(tree: Any) -> tuple[dict[str, Component], list[Net]]:
    if not (isinstance(tree, list) and tree and tree[0] == "export"):
        raise ValueError("not a KiCad export netlist")

    comps: dict[str, Component] = {}
    nets: list[Net] = []
    unpopulated: set[str] = set()

    for top in tree[1:]:
        if not (isinstance(top, list) and top):
            continue
        if top[0] == "components":
            for c in top[1:]:
                if not (isinstance(c, list) and c and c[0] == "comp"):
                    continue
                ref = get_field(c, "ref") or ""
                value = get_field(c, "value") or ""
                part = ""
                dnp = False
                for e in c[1:]:
                    if isinstance(e, list) and e and e[0] == "property" and get_field(e, "name") == "dnp":
                        dnp = True
                    if not (isinstance(e, list) and e and e[0] == "libsource"):
                        continue
                    p = get_field(e, "part")
                    if p:
                        part = p
                if ref and dnp:
                    unpopulated.add(ref)
                elif ref:
                    comps[ref] = Component(ref=ref, value=value, part=part)
        elif top[0] == "nets":
            for n in top[1:]:
                if not (isinstance(n, list) and len(n) >= 2 and n[0] == "net"):
                    continue
                code = get_field(n, "code") or ""
                name = get_field(n, "name") or ""
                net = Net(code=code, name=name)
                for maybe_node in n[1:]:
                    if not (isinstance(maybe_node, list) and maybe_node and maybe_node[0] == "node"):
                        continue
                    net.nodes.append(
                        Node(
                            ref=get_field(maybe_node, "ref") or "",
                            pin=get_field(maybe_node, "pin") or "",
                            pinfunction=get_field(maybe_node, "pinfunction") or "",
                            pintype=get_field(maybe_node, "pintype") or "",
                        )
                    )
                nets.append(net)

    # Do-not-populate parts (build options the board does not carry): drop
    # the part and every pin it has on a net.
    for net in nets:
        net.nodes = [n for n in net.nodes if n.ref not in unpopulated]
    nets[:] = [n for n in nets if n.nodes]
    return comps, nets


def apply_sim(comps: dict[str, Component], nets: list[Net], sim_path: Path) -> dict[str, str]:
    """Apply simulation-only changes to the netlist (see sim/*.json):

    remove_nodes  pins taken off a net, so a testbench can drive the net
    tie_nets      nets held at a constant (returned; emitted as assigns)
    """
    import json

    sim = json.loads(sim_path.read_text(encoding="utf-8"))
    by_name = {n.name: n for n in nets}
    for item in sim.get("remove_nodes", []):
        net = by_name.get(item["net"])
        if net is None:
            raise ValueError(f"sim remove_nodes: no net {item['net']!r}")
        before = len(net.nodes)
        net.nodes = [n for n in net.nodes if not (n.ref == item["ref"] and n.pin == item["pin"])]
        if len(net.nodes) == before:
            raise ValueError(f"sim remove_nodes: {item['ref']}.{item['pin']} not on {item['net']!r}")
    nets[:] = [n for n in nets if n.nodes]
    tie_nets = dict(sim.get("tie_nets", {}))
    for name in tie_nets:
        if name not in by_name:
            raise ValueError(f"sim tie_nets: no net {name!r}")
    return tie_nets


def sanitize_ident(name: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]", "_", name)
    s = re.sub(r"_+", "_", s).strip("_")
    if not s:
        s = "net"
    if s[0].isdigit():
        s = f"n_{s}"
    return s


def uniq_map(items: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    used: dict[str, int] = {}
    for name in items:
        base = sanitize_ident(name)
        n = used.get(base, 0)
        used[base] = n + 1
        out[name] = base if n == 0 else f"{base}_{n}"
    return out


def sanitize_port(pin: str) -> str:
    if re.fullmatch(r"[0-9]+", pin):
        return f"p_{pin}"
    p = sanitize_ident(pin)
    return f"p_{p}"


def resolve_port_name(part: str, pin: str, pinfunction: str) -> str:
    """Map a KiCad node to the HDL port name used by the primitive model."""
    if part == "AMB304" and pin == "11" and pinfunction == "G":
        # The Timing & Control AMB304 symbol incorrectly exports G on pin 11.
        return "p_9"
    return sanitize_port(pin)


PART_TO_MODULE = {
    "SN74F374N": "ttl_sn74f374n",
    "74LS374": "ttl_sn74f374n",
    "SN74S163N": "ttl_sn74s163n",
    "74LS163": "ttl_sn74s163n",
    "74LS02": "ttl_74ls02",
    "74LS14": "ttl_74ls14",
    "74LS20": "ttl_74ls20",
    "74LS175": "ttl_74ls175",
    "74LS194": "ttl_74ls194a",
    "74LS194A": "ttl_74ls194a",
    "74LS109": "ttl_74ls109",
    "SN74S74N": "ttl_74ls74",
    "74LS74": "ttl_74ls74",
    "SN74S112AN": "ttl_sn74s112an",
    "74LS112": "ttl_sn74s112an",
    "74LS27": "ttl_74ls27",
    "SN74S00N": "ttl_74ls00",
    "74S00": "ttl_74ls00",
    "74LS00": "ttl_74ls00",
    "74LS03": "ttl_74ls03",
    "74LS04": "ttl_74ls04",
    "74S04": "ttl_74ls04",
    "74LS08": "ttl_74ls08",
    "74S08": "ttl_74ls08",
    "74LS10": "ttl_74ls10",
    "74LS123": "ttl_74ls123",
    "74LS133": "ttl_74ls133",
    "74S10": "ttl_74ls10",
    "SN74S10N": "ttl_74ls10",
    "74LS139": "ttl_74ls139",
    "74LS155": "ttl_74ls155",
    "74LS157": "ttl_74ls157",
    "74F157": "ttl_74ls157",
    "74LS174": "ttl_74ls174",
    "74LS195": "ttl_74ls195",
    "74LS244": "ttl_74ls244",
    "74LS377": "ttl_74ls377",
    "74LS670": "ttl_74ls670",
    "74S287": "ttl_74s287",
    "82S129": "ttl_74s287",
    "74LS86": "ttl_74ls86",
    "AMB304": "ttl_am8304n",
    "AM8304N": "ttl_am8304n",
    "MCM68B10": "ttl_mcm68b10",
    "74LS283": "ttl_74ls283",
    "74LS138": "ttl_74ls138",
    "74LS393": "ttl_74ls393",
    # DMEM split-symbol LS244 variants; netlist nodes still use physical pins.
    "74LS244_Split": "ttl_74ls244",
    "MK4164N": "ttl_mk4164n",
    # Series resistors must conduct (DMEM strings 33R in the DRAM address and
    # strobe lines); model as ideal bidirectional links.
    "R": "ideal_resistor",
    "R_US": "ideal_resistor",
    "R_Small_US": "ideal_resistor",
    "R_Pack08_Split": "ideal_resistor_pack8",
}

PART_ALIASES = {
    "mc4044": "MC4044",
}

PART_VALUE_OVERRIDES = {
    # The FPC board uses an embedded symbol whose lib part says 74LS257, but the
    # actual device value and pin naming match a 74S287/82S129-style PROM.
    ("74LS257", "S287"): "74S287",
    ("74LS257", "74S287"): "74S287",
    ("74LS257", "82S129"): "82S129",
}

# Keyed by (module, ref) so a ref like U6 on one board cannot collide with the
# same ref on another board's netlist.
INSTANCE_PARAMETER_OVERRIDES = {
    # U6 is the FPC board's cycle-timing PROM. Keep its image external so the
    # generated netlist can stay structural while the recovered contents evolve.
    ("ttl_74s287", "U6"): ['.INIT_FILE("rom/u6_74s287_init.hex")', ".DEFAULT_WORD(4'hx)"],
    # T&C WCS RAM byte lanes (U43=b0/MI0-7, U29=b1/MI8-15, U15=b2/MI16-23,
    # U2=b3/MI24-31). Lane images are written by tools/split_wcs_hex.py.
    ("ttl_mcm68b10", "U43"): ['.INIT_FILE("out/wcs_lane_b0.hex")'],
    ("ttl_mcm68b10", "U29"): ['.INIT_FILE("out/wcs_lane_b1.hex")'],
    ("ttl_mcm68b10", "U15"): ['.INIT_FILE("out/wcs_lane_b2.hex")'],
    ("ttl_mcm68b10", "U2"): ['.INIT_FILE("out/wcs_lane_b3.hex")'],
    # T&C U36 is the XFER_CK gate and is labeled SN74S10N/74S10 in the
    # netlist. Keep its timing in the S-series range without changing the
    # plain LS10 at T&C U49.
    ("ttl_74ls10", "U36"): [".TPLH(3.0)", ".TPHL(3.0)"],
}

# Board-scoped instance timing.  Keep this separate from the historical
# (module, ref) table above: reference designators repeat across boards.
BOARD_INSTANCE_PARAMETER_OVERRIDES = {
    # ARU, drawing 060-01318 rev 4 (10-18-84), "FOR 224X VERSION ONLY" table,
    # confirmed by the ARU parts list (no 74LS283/LS374/LS377 on the board):
    # U13, U19-U25, U38, U39 = 74F283; U10, U11, U43, U44 = 74F374;
    # U12 = 74S175; U7, U9 = 74S86; U33-U37 = 74F157; U54 = 74S04.  Drawn
    # without the table: U5, U6, U42 = 74S86, U8 = 74LS86, U45-U49 = 74LS163.
    # The schematic symbols carry the base-224 LS values, so the 224X grades are
    # set here.  Delays are TI datasheet TYPICAL (Vcc 5 V, 25 C); where a
    # part has two typical figures the slower one is used.
    **{("aru_board_netlist", u): [".S_LH(6.6)", ".S_HL(6.6)", ".C_LH(5.3)", ".C_HL(5.0)"]
       for u in ("U13", "U19", "U20", "U21", "U22", "U23", "U24", "U25", "U38", "U39")},
    ("aru_board_netlist", "U10"): [".TPD(6)"],       # 74F374 in the LS377 socket: CLK->Q 6.1
    ("aru_board_netlist", "U11"): [".TPD(6)"],
    ("aru_board_netlist", "U43"): [".TPD(6.1)", ".TSU(2.0)"],  # 74F374 result register
    ("aru_board_netlist", "U44"): [".TPD(6.1)", ".TSU(2.0)"],
    ("aru_board_netlist", "U12"): [".CLK_TPD(11.5)"],           # 74S175: CLK->Q 8 / 11.5
    **{("aru_board_netlist", u): [".TPLH(6.6)", ".TPHL(4.6)"]   # 74F157: select->Y
       for u in ("U33", "U34", "U35", "U36", "U37")},
    **{("aru_board_netlist", u): [".TPLH(7.0)", ".TPHL(6.5)"]   # 74S86
       for u in ("U5", "U6", "U7", "U9", "U42")},
    ("aru_board_netlist", "U8"): [".TPLH(20.0)", ".TPHL(13.0)"],  # 74LS86, other input high
    **{("aru_board_netlist", u): [".TPD(18)"]                   # 74LS163A: CLK->Q 13 / 18
       for u in ("U45", "U46", "U47", "U48", "U49")},
    # Keep the default first-pass U6 image for compatibility, but make the
    # repository-generated FPC board selectable by a wrapper.  The separate
    # primary-constrained 0..99 candidate is evidence-qualified, not silently
    # promoted to a physical PROM dump.
    ("fpc_board_netlist", "U6"): [
        ".INIT_FILE(FPC_U6_INIT_FILE)", ".DEFAULT_WORD(4'hx)"
    ],
    # T&C U25 is BOM- and netlist-identified SN74S74N, not the LS74 used at
    # U24/U53/U54.  TI's S74 CLK-to-Q maximum is 9 ns; the shared LS74
    # primitive's 40 ns default incorrectly moves ARUCKE relative to MS2.
    ("tc_board_netlist", "U25"): [".TPD(9.0)"],
    # The T&C address-select devices are BOM/netlist-labeled 74F157 rather
    # than the generic LS157 represented by the shared primitive name.  The
    # conservative 9 ns F157 propagation bound is required by the natural
    # U21 write-window setup witness and was formerly applied only by the
    # external overlay companion.
    ("tc_board_netlist", "U42"): [".TPLH(9.0)", ".TPHL(9.0)"],
    ("tc_board_netlist", "U28"): [".TPLH(9.0)", ".TPHL(9.0)"],
}

TOP_PARAMETER_DECLARATIONS = {
    "fpc_board_netlist": [
        'parameter FPC_U6_INIT_FILE = "rom/u6_74s287_init.hex"'
    ],
}

# Per-board module parameter defaults, keyed by (top, module). Applied to
# every instance of that module unless an instance override exists.
MODULE_PARAMETER_DEFAULTS = {
    # 224X DMEM board is strapped for 4164s (64Kx16): DRAM pin 9 is AD7.
    ("dmem_board_netlist", "ttl_mk4164n"): [".ADDR_BITS(8)"],
    # The Dmem-and-IO timing cone uses S-series parts (schematic 060-02512
    # sheet 2: U46=S74, U45=S08, U44=S00, U43=S10, U58=S04). Datasheet
    # TYPICAL propagation delays, calibrated against service-manual Fig 3.3
    # (RAS/ <44ns into slot 2 = <12ns strobe skew + buffer + FF + gate).
    # T&C AS-state register and seed gates: LS-series datasheet typicals.
    ("tc_board_netlist", "ttl_74ls175"): [".CLK_TPD(13.0)", ".CLR_TPD(13.0)"],
    ("tc_board_netlist", "ttl_74ls27"): [".TPLH(8.0)", ".TPHL(8.0)"],
    ("dmem_io_board_netlist", "ttl_74ls74"): [".TPD(9.0)"],
    ("dmem_io_board_netlist", "ttl_74ls244"): [".TPLH(12.0)", ".TPHL(12.0)"],
    ("dmem_io_board_netlist", "ttl_74ls08"): [".TPLH(4.75)", ".TPHL(4.75)"],
    ("dmem_io_board_netlist", "ttl_74ls00"): [".TPLH(3.0)", ".TPHL(3.0)"],
    ("dmem_io_board_netlist", "ttl_74ls10"): [".TPLH(3.0)", ".TPHL(3.0)"],
    ("dmem_io_board_netlist", "ttl_74ls04"): [".TPLH(3.0)", ".TPHL(3.0)"],
}

# Per-instance module substitution, keyed by (top, ref), for parts whose
# schematic symbol is a stand-in for the physical device.
INSTANCE_MODULE_OVERRIDES = {
    # U59 is the DLG308 delay module, drawn with a stand-in LS244 symbol; the tap
    # net names (/30ns /60ns /120ns /150ns) give the function.
    ("dmem_io_board_netlist", "U59"): "dlg308_delay_module",
}

# Unconnected input pins on real LS-TTL parts float HIGH, so single-node
# "unconnected-*" nets on input pins are tied to 1 by default. Split symbols
# can also drop pins the real board ties LOW; list those here per
# (top, ref, pin). A value of None leaves the pin floating.
UNCONNECTED_PIN_TIES = {
    # U61 (LS244) output enables: the split symbol lost the connection; DIP
    # pins 1/19 are grounded on the board (buffers permanently enabled).
    ("dmem_io_board_netlist", "U61", "1"): "1'b0",
    ("dmem_io_board_netlist", "U61", "19"): "1'b0",
    # T&C WCS address muxes: schematic grounds the LS157 enables (pin 15);
    # a floating-high enable forces the outputs LOW and pins the WCS address.
    ("tc_board_netlist", "U42", "15"): "1'b0",
    ("tc_board_netlist", "U28", "15"): "1'b0",
}


def canonicalize_part(part: str) -> str:
    part = part.strip()
    if not part:
        return part

    alias = PART_ALIASES.get(part.lower())
    if alias:
        return alias

    # KiCad library variants often suffix split or duplicated symbols as *_1, *_2, ...
    m = re.fullmatch(r"(.+)_\d+", part)
    if m:
        return m.group(1)

    return part


def resolve_component_part(comp: Component) -> str:
    canon_part = canonicalize_part(comp.part)
    value = comp.value.strip().upper()
    return PART_VALUE_OVERRIDES.get((canon_part, value), canon_part)


def write_part_report(
    out_path: Path,
    source_netlist: Path,
    comps: dict[str, Component],
) -> None:
    grouped: dict[tuple[str, str | None], dict[str, Any]] = {}

    for ref in sorted(comps.keys()):
        comp = comps[ref]
        canon = resolve_component_part(comp)
        module = PART_TO_MODULE.get(canon)
        key = (canon, module)
        bucket = grouped.setdefault(
            key,
            {
                "count": 0,
                "refs": [],
                "original_parts": set(),
                "values": set(),
            },
        )
        bucket["count"] += 1
        bucket["refs"].append(ref)
        bucket["original_parts"].add(comp.part or "<missing>")
        bucket["values"].add(comp.value or "<missing>")

    mapped_components = sum(info["count"] for (_, module), info in grouped.items() if module)
    blackbox_components = sum(info["count"] for (_, module), info in grouped.items() if not module)

    lines = [
        "# HDL Part Coverage",
        "",
        f"- Source: `{source_netlist}`",
        f"- Components: {len(comps)}",
        f"- Mapped components: {mapped_components}",
        f"- Black-box components: {blackbox_components}",
        "",
        "## Mapped Parts",
    ]

    mapped_rows = sorted((item for item in grouped.items() if item[0][1]), key=lambda item: item[0][0])
    if not mapped_rows:
        lines.append("- none")
    else:
        for (canon, module), info in mapped_rows:
            orig = ", ".join(sorted(info["original_parts"]))
            vals = ", ".join(sorted(info["values"]))
            refs = ", ".join(info["refs"])
            lines.append(
                f"- `{canon}` -> `{module}`: count={info['count']}, original_parts=`{orig}`, values=`{vals}`, refs=`{refs}`"
            )

    lines.extend(["", "## Black-Box Parts"])
    blackbox_rows = sorted((item for item in grouped.items() if not item[0][1]), key=lambda item: item[0][0])
    if not blackbox_rows:
        lines.append("- none")
    else:
        for (canon, _module), info in blackbox_rows:
            orig = ", ".join(sorted(info["original_parts"]))
            vals = ", ".join(sorted(info["values"]))
            refs = ", ".join(info["refs"])
            lines.append(
                f"- `{canon}`: count={info['count']}, original_parts=`{orig}`, values=`{vals}`, refs=`{refs}`"
            )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("netlist", type=Path)
    ap.add_argument("--out", type=Path, default=Path("hdl/generated_netlist.v"))
    ap.add_argument("--top", default="timing_and_control_netlist")
    ap.add_argument("--part-report", type=Path)
    ap.add_argument("--sim", type=Path, help="simulation-only changes (sim/<board>.json)")
    ap.add_argument(
        "--ports",
        type=Path,
        help="JSON {net name: port ident}; listed nets become inout ports of "
        "the generated module so boards can be wired through a backplane top",
    )
    args = ap.parse_args()

    text = args.netlist.read_text(encoding="utf-8")
    comps, nets = parse_netlist(parse_sexpr(tokenize(text)))
    tie_nets: dict[str, str] = {}
    if args.sim is not None:
        tie_nets = apply_sim(comps, nets, args.sim)

    port_map: dict[str, str] = {}
    if args.ports is not None:
        import json

        port_map = json.loads(args.ports.read_text(encoding="utf-8"))
        known = {n.name for n in nets}
        missing = sorted(set(port_map) - known)
        if missing:
            raise ValueError(f"--ports: nets not in netlist: {missing}")

    net_names = [n.name for n in nets]
    net_id = uniq_map(net_names)

    # Build logical port->net map per component.
    by_ref: dict[str, dict[str, str]] = defaultdict(dict)
    for net in nets:
        for nd in net.nodes:
            if nd.ref and nd.pin:
                comp = comps.get(nd.ref)
                canon_part = resolve_component_part(comp) if comp else ""
                port_name = resolve_port_name(canon_part, nd.pin, nd.pinfunction)
                prev = by_ref[nd.ref].get(port_name)
                if prev is not None and prev != net.name:
                    raise ValueError(
                        f"conflicting logical port mapping for {nd.ref}.{port_name}: {prev!r} vs {net.name!r}"
                    )
                by_ref[nd.ref][port_name] = net.name

    # Unknown parts => generated stubs with observed pins.
    unknown_parts: dict[str, set[str]] = defaultdict(set)
    for ref, c in comps.items():
        canon_part = resolve_component_part(c)
        if canon_part not in PART_TO_MODULE:
            for port_name in by_ref.get(ref, {}):
                unknown_parts[canon_part].add(port_name)

    lines: list[str] = []
    lines.append("`timescale 1ns/1ps")
    lines.append("// Auto-generated from KiCad netlist")
    lines.append(f"// Source: {args.netlist}")
    lines.append("")

    for part, pins in sorted(unknown_parts.items()):
        m = f"bb_{sanitize_ident(part)}"
        ports = sorted(pins, key=lambda x: (len(x), x))
        if not ports:
            continue
        lines.append(f"module {m}({', '.join(ports)});")
        lines.append(f"  inout {', '.join(ports)};")
        lines.append("endmodule")
        lines.append("")

    taken = {ident for name, ident in net_id.items() if name not in port_map}
    for net_name, port in port_map.items():
        if port in taken:
            raise ValueError(
                f"--ports: port ident {port!r} for net {net_name!r} collides "
                f"with another net's identifier; rename the port"
            )
        net_id[net_name] = port

    top_parameters = TOP_PARAMETER_DECLARATIONS.get(args.top, [])
    parameter_block = f" #({', '.join(top_parameters)})" if top_parameters else ""
    if port_map:
        ports = ", ".join(port_map.values())
        lines.append(f"module {args.top}{parameter_block}({ports});")
        lines.append(f"  inout {ports};")
    else:
        lines.append(f"module {args.top}{parameter_block}();")
    lines.append("  // Net declarations")
    for n in net_names:
        if n in port_map:
            continue
        lines.append(f"  wire {net_id[n]}; // {n}")
    lines.append("")
    lines.append("  // Common rail assumptions")
    for n in net_names:
        up = n.upper()
        ident = net_id[n]
        if up in {"+5V", "VCC", "VDD"}:
            lines.append(f"  assign {ident} = 1'b1;")
        elif up in {"GND", "GNDPWR", "VSS", "GNDREF", "EARTH"}:
            lines.append(f"  assign {ident} = 1'b0;")
        if n in tie_nets:
            lines.append(f"  assign {ident} = {tie_nets[n]}; // sim tie_nets")
    lines.append("")

    lines.append("  // Floating TTL input pins read high (overridable per pin)")
    for net in nets:
        if len(net.nodes) != 1 or not net.name.startswith("unconnected"):
            continue
        node = net.nodes[0]
        comp = comps.get(node.ref)
        if comp is None or resolve_component_part(comp) not in PART_TO_MODULE:
            continue
        if not node.pintype.startswith("input"):
            continue
        tie = UNCONNECTED_PIN_TIES.get((args.top, node.ref, node.pin), "1'b1")
        if tie is None:
            continue
        lines.append(f"  assign {net_id[net.name]} = {tie}; // {node.ref}.{node.pin}")
    lines.append("")

    for ref in sorted(comps.keys()):
        c = comps[ref]
        pinmap = by_ref.get(ref, {})
        if not pinmap:
            continue
        canon_part = resolve_component_part(c)
        mod = INSTANCE_MODULE_OVERRIDES.get(
            (args.top, ref), PART_TO_MODULE.get(canon_part, f"bb_{sanitize_ident(canon_part)}")
        )
        params = BOARD_INSTANCE_PARAMETER_OVERRIDES.get(
            (args.top, ref),
            INSTANCE_PARAMETER_OVERRIDES.get(
                (mod, ref), MODULE_PARAMETER_DEFAULTS.get((args.top, mod), [])
            ),
        )
        param_block = f" #({', '.join(params)})" if params else ""
        conns = []
        for port_name in sorted(pinmap.keys(), key=lambda x: (len(x), x)):
            conns.append(f".{port_name}({net_id[pinmap[port_name]]})")
        part_note = c.part if c.part == canon_part else f"{c.part} -> {canon_part}"
        lines.append(f"  {mod}{param_block} {sanitize_ident(ref)} ({', '.join(conns)}); // {part_note} {c.value}")

    lines.append("endmodule")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if args.part_report is not None:
        write_part_report(args.part_report, args.netlist, comps)
        print(f"Wrote {args.part_report}")
    print(f"Wrote {args.out}")
    print(f"components={len(comps)} nets={len(nets)} unknown_parts={len(unknown_parts)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
