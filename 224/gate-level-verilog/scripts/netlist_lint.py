#!/usr/bin/env python3
"""Lint KiCad Eeschema S-expression netlists for schematic cleanup issues.

Outputs:
  1) Human report (Markdown)
  2) Machine report (JSON)

Checks:
  - Multi-driven nets (multiple output/open_collector pins on one net)
  - Conflicting complementary outputs from same chip on one net (Q + ~Q)
  - Nets with only a single connection
  - Nets with no drivers
  - Similar-name net pairs (possible naming inconsistencies)
  - Alias-like net groups sharing many endpoints
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


TOKEN_RE = re.compile(r'\(|\)|"([^"\\]*(?:\\.[^"\\]*)*)"|[^\s()]+')


def tokenize(text: str) -> list[str]:
    out: list[str] = []
    for m in TOKEN_RE.finditer(text):
        tok = m.group(0)
        if tok.startswith('"'):
            out.append(tok[1:-1])
        else:
            out.append(tok)
    return out


def parse_sexpr(tokens: list[str]) -> Any:
    i = 0

    def parse_one() -> Any:
        nonlocal i
        if i >= len(tokens):
            raise ValueError("unexpected EOF")
        tok = tokens[i]
        i += 1
        if tok == "(":
            arr = []
            while True:
                if i >= len(tokens):
                    raise ValueError("missing ')'")
                if tokens[i] == ")":
                    i += 1
                    return arr
                arr.append(parse_one())
        if tok == ")":
            raise ValueError("unexpected ')'")
        return tok

    root = parse_one()
    if i != len(tokens):
        raise ValueError("trailing tokens")
    return root


@dataclass
class Node:
    ref: str
    pin: str
    pinfunction: str = ""
    pintype: str = ""


@dataclass
class Net:
    code: str
    name: str
    cls: str
    nodes: list[Node] = field(default_factory=list)


@dataclass
class Component:
    ref: str
    value: str


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

    for top in tree[1:]:
        if not (isinstance(top, list) and top):
            continue
        if top[0] == "components":
            for c in top[1:]:
                if not (isinstance(c, list) and c and c[0] == "comp"):
                    continue
                ref = get_field(c, "ref") or ""
                value = get_field(c, "value") or ""
                if ref:
                    comps[ref] = Component(ref=ref, value=value)
        elif top[0] == "nets":
            for n in top[1:]:
                if not (isinstance(n, list) and len(n) >= 2 and n[0] == "net"):
                    continue
                code = ""
                name = ""
                nclass = ""
                for e in n[1:]:
                    if not (isinstance(e, list) and e):
                        continue
                    if e[0] == "code" and len(e) >= 2 and isinstance(e[1], str):
                        code = e[1]
                    elif e[0] == "name" and len(e) >= 2 and isinstance(e[1], str):
                        name = e[1]
                    elif e[0] == "class" and len(e) >= 2 and isinstance(e[1], str):
                        nclass = e[1]
                net = Net(code=code, name=name, cls=nclass)
                for maybe_node in n[1:]:
                    if not (isinstance(maybe_node, list) and maybe_node and maybe_node[0] == "node"):
                        continue
                    ref = get_field(maybe_node, "ref") or ""
                    pin = get_field(maybe_node, "pin") or ""
                    pinfn = get_field(maybe_node, "pinfunction") or ""
                    pintype = get_field(maybe_node, "pintype") or ""
                    net.nodes.append(Node(ref=ref, pin=pin, pinfunction=pinfn, pintype=pintype))
                nets.append(net)

    return comps, nets


def norm_name(name: str) -> str:
    s = name.upper()
    s = s.replace("/", "")
    s = s.replace("_", "")
    s = s.replace("-", "")
    s = s.replace("~{", "")
    s = s.replace("}", "")
    return s


def jaccard(a: set[str], b: set[str]) -> float:
    if not a and not b:
        return 1.0
    return len(a & b) / len(a | b)


def build_report(comps: dict[str, Component], nets: list[Net]) -> dict[str, Any]:
    report: dict[str, Any] = {
        "summary": {},
        "multi_driven_nets": [],
        "complement_conflicts": [],
        "single_node_nets": [],
        "no_driver_nets": [],
        "name_inconsistencies": [],
        "possible_alias_groups": [],
    }

    output_types = {"output", "open_collector", "bidirectional", "tri_state"}
    ignore_no_driver_prefixes = ("unconnected-(",)
    ignore_no_driver_exact = {"+5V", "GND", "VCC", "VDD", "VSS"}

    # multi-driver, complement conflicts, no-driver, single-node
    for net in nets:
        drivers = [n for n in net.nodes if n.pintype in output_types]
        if len(drivers) > 1:
            report["multi_driven_nets"].append(
                {
                    "net": net.name,
                    "code": net.code,
                    "drivers": [
                        {
                            "ref": n.ref,
                            "pin": n.pin,
                            "pinfunction": n.pinfunction,
                            "pintype": n.pintype,
                            "value": comps.get(n.ref, Component(n.ref, "")).value,
                        }
                        for n in drivers
                    ],
                }
            )

        # same-chip Q and ~Q shorted or similar complement outputs
        by_ref: dict[str, list[Node]] = {}
        for n in drivers:
            by_ref.setdefault(n.ref, []).append(n)
        for ref, nodes in by_ref.items():
            pinf = [n.pinfunction.upper() for n in nodes if n.pinfunction]
            has_q = any((x == "Q" or x.endswith("Q")) and "~" not in x for x in pinf)
            has_qn = any(("~" in x and "Q" in x) or x.endswith("Q_N") for x in pinf)
            if has_q and has_qn:
                report["complement_conflicts"].append(
                    {
                        "net": net.name,
                        "code": net.code,
                        "ref": ref,
                        "nodes": [
                            {"pin": n.pin, "pinfunction": n.pinfunction, "pintype": n.pintype}
                            for n in nodes
                        ],
                    }
                )

        if len(net.nodes) == 1:
            n = net.nodes[0]
            report["single_node_nets"].append(
                {
                    "net": net.name,
                    "code": net.code,
                    "node": {
                        "ref": n.ref,
                        "pin": n.pin,
                        "pinfunction": n.pinfunction,
                        "pintype": n.pintype,
                    },
                }
            )

        if (
            len(drivers) == 0
            and net.name not in ignore_no_driver_exact
            and not any(net.name.startswith(p) for p in ignore_no_driver_prefixes)
        ):
            report["no_driver_nets"].append({"net": net.name, "code": net.code, "nodes": len(net.nodes)})

    # Naming inconsistencies: exact normalized collisions (e.g. WRDA vs WR_DA)
    names = [n.name for n in nets if n.name]
    by_norm: dict[str, set[str]] = {}
    for nm in names:
        by_norm.setdefault(norm_name(nm), set()).add(nm)

    for nkey, variants in by_norm.items():
        if len(nkey) < 3 or len(variants) < 2:
            continue
        vv = sorted(variants)
        for i, a in enumerate(vv):
            for b in vv[i + 1 :]:
                report["name_inconsistencies"].append({"kind": "normalized_equal", "a": a, "b": b})

    # Alias groups: different nets with highly overlapping endpoint sets
    endpoint_sets: dict[str, set[str]] = {}
    for net in nets:
        eps = {f"{n.ref}:{n.pin}" for n in net.nodes}
        if len(eps) >= 2:
            endpoint_sets[net.name] = eps

    alias_candidates = []
    names2 = sorted(endpoint_sets.keys())
    for i, a in enumerate(names2):
        for b in names2[i + 1 :]:
            ja = endpoint_sets[a]
            jb = endpoint_sets[b]
            score = jaccard(ja, jb)
            if score >= 0.6:
                alias_candidates.append(
                    {"a": a, "b": b, "jaccard": round(score, 3), "shared": sorted(ja & jb)[:12]}
                )
    alias_candidates.sort(key=lambda x: x["jaccard"], reverse=True)
    report["possible_alias_groups"] = alias_candidates[:200]

    report["summary"] = {
        "components": len(comps),
        "nets": len(nets),
        "multi_driven_nets": len(report["multi_driven_nets"]),
        "complement_conflicts": len(report["complement_conflicts"]),
        "single_node_nets": len(report["single_node_nets"]),
        "no_driver_nets": len(report["no_driver_nets"]),
        "name_inconsistencies": len(report["name_inconsistencies"]),
        "possible_alias_groups": len(report["possible_alias_groups"]),
    }
    return report


def to_markdown(report: dict[str, Any], source: Path) -> str:
    s = report["summary"]
    lines: list[str] = []
    lines.append(f"# Netlist Lint Report\n")
    lines.append(f"- Source: `{source}`")
    lines.append(f"- Components: {s['components']}")
    lines.append(f"- Nets: {s['nets']}")
    lines.append(f"- Multi-driven nets: {s['multi_driven_nets']}")
    lines.append(f"- Complement conflicts: {s['complement_conflicts']}")
    lines.append(f"- Single-node nets: {s['single_node_nets']}")
    lines.append(f"- Nets with no driver: {s['no_driver_nets']}")
    lines.append(f"- Naming inconsistencies: {s['name_inconsistencies']}")
    lines.append("")

    def section(title: str, rows: list[Any], limit: int = 40) -> None:
        lines.append(f"## {title}")
        if not rows:
            lines.append("- none")
            lines.append("")
            return
        for r in rows[:limit]:
            lines.append(f"- `{json.dumps(r, ensure_ascii=True)}`")
        if len(rows) > limit:
            lines.append(f"- ... {len(rows) - limit} more")
        lines.append("")

    section("Multi-Driven Nets", report["multi_driven_nets"])
    section("Complement Conflicts", report["complement_conflicts"])
    section("Likely Naming Inconsistencies", report["name_inconsistencies"], limit=80)
    section("Potential Alias Groups (Endpoint Overlap)", report["possible_alias_groups"], limit=80)
    section("Single-Node Nets", report["single_node_nets"], limit=80)
    section("Nets With No Driver", report["no_driver_nets"], limit=80)

    lines.append("## Notes")
    lines.append("- Multi-driver nets are not always wrong (wired-OR, Q/~Q buses, etc.), but should be reviewed.")
    lines.append("- Single-node nets usually indicate dangling pins, no-connect artifacts, or naming mistakes.")
    lines.append("- No-driver nets may be valid external inputs, but often reveal missing connectivity.")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="Lint KiCad Eeschema S-expression netlist")
    ap.add_argument("netlist", type=Path, help="Path to *.net file")
    ap.add_argument(
        "--out-prefix",
        type=Path,
        default=Path("reports/fpc_netlist_lint"),
        help="Output prefix for .json/.md",
    )
    args = ap.parse_args()

    text = args.netlist.read_text(encoding="utf-8")
    tokens = tokenize(text)
    tree = parse_sexpr(tokens)
    comps, nets = parse_netlist(tree)
    report = build_report(comps, nets)

    out_json = args.out_prefix.with_suffix(".json")
    out_md = args.out_prefix.with_suffix(".md")
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    out_md.write_text(to_markdown(report, args.netlist), encoding="utf-8")

    print(f"Wrote: {out_json}")
    print(f"Wrote: {out_md}")
    print("Summary:")
    for k, v in report["summary"].items():
        print(f"  {k}: {v}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
