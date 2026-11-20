#!/usr/bin/env python3
"""Exact modal form of a linear ngspice netlist (R, C, L, VCVS, one AC source).

Modified nodal analysis gives (G + sC) x = b u, y = c.x. Its finite
generalized eigenvalues are the poles; residues come from the left and right
eigenvectors, and the direct term from H at a large s minus the pole sum.
The result reproduces H(s) exactly (checked against the MNA solve and,
with --spice, against ngspice's own AC analysis).

  mna.py circuit.cir --out node [--in Vin] [--json out.json] [--spice]

Supported: R, C, L, E (VCVS: E name out+ out- in+ in- gain), V (the input,
AC 1), X instances of subcircuits made of the same, .param, .subckt/.ends,
comments (* and ;), engineering suffixes (f p n u m k meg g).
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
import scipy.linalg

SUFFIX = {"f": 1e-15, "p": 1e-12, "n": 1e-9, "u": 1e-6, "m": 1e-3, "k": 1e3, "meg": 1e6, "g": 1e9}


def ternary(text: str) -> str:
    """ngspice's `a ? b : c` as Python (right-associative, one level at a time)."""
    m = re.fullmatch(r"\s*([^?]+?)\s*\?\s*([^:]+?)\s*:\s*(.+)", text)
    if not m:
        return text
    return f"(({ternary(m.group(2))}) if ({m.group(1)}) else ({ternary(m.group(3))}))"


def suffixes(text: str) -> str:
    """Engineering suffixes inside an expression: 11.3k -> 11.3e3."""
    return re.sub(r"(?<![\w.])([0-9.]+(?:e[-+]?\d+)?)(meg|[fpnumkg])(?![a-z])",
                  lambda m: f"({m.group(1)}*{SUFFIX[m.group(2).lower()]})", text, flags=re.I)


def value(text: str, params: dict) -> float:
    text = text.strip().strip("{}'\"")
    if text.lower() in params:
        return params[text.lower()]
    try:
        return float(eval(suffixes(ternary(text.lower())), {"__builtins__": {}, "max": max, "min": min, "abs": abs}, params))
    except Exception:
        pass
    m = re.fullmatch(r"([-+]?[0-9.]+(?:e[-+]?\d+)?)(meg|[fpnumkg])?[a-z]*", text.lower())
    if not m:
        raise ValueError(f"cannot read value {text!r}")
    return float(m.group(1)) * SUFFIX.get(m.group(2) or "", 1.0)


def parse(path: Path, overrides: dict | None = None):
    """Flatten the netlist: returns (elements, params). Elements are tuples
    (kind, name, nodes, value)."""
    lines = []
    text = re.sub(r"(?ims)^\s*\.control\b.*?^\s*\.endc\b", "", path.read_text())
    for raw in text.splitlines():
        line = raw.split(";")[0].strip()
        if not line or line.startswith("*"):
            continue
        if line.startswith("+") and lines:
            lines[-1] += " " + line[1:]
        else:
            lines.append(line)
    params: dict[str, float] = {k.lower(): v for k, v in (overrides or {}).items()}
    subckts: dict[str, tuple[list[str], list[list[str]]]] = {}
    top: list[list[str]] = []
    current = None
    def split(line):
        # brace groups ({X ? 11.3k : 23.7k}) are single words
        return re.findall(r"\{[^}]*\}|[^\s]+", line)

    for line in lines:
        words = split(line)
        head = words[0].lower()
        if head == ".param":
            for assignment in re.findall(r"(\w+)\s*=\s*(\{[^}]*\}|[^\s]+)", line[6:]):
                if assignment[0].lower() not in (overrides or {}):
                    params[assignment[0].lower()] = value(assignment[1], params)
        elif head == ".subckt":
            current = words[1].lower()
            subckts[current] = (words[2:], [])
        elif head == ".ends":
            current = None
        elif head.startswith("."):
            continue
        elif current is not None:
            subckts[current][1].append(words)
        else:
            top.append(words)

    elements = []

    def expand(words_list, prefix, mapping):
        for words in words_list:
            name = words[0]
            kind = name[0].upper()
            def node(n):
                n = n.lower()
                if n in ("0", "gnd"):
                    return "0"
                return mapping.get(n, prefix + n)
            if kind in "RCL":
                elements.append((kind, prefix + name, [node(words[1]), node(words[2])], value(words[3], params)))
            elif kind == "E":
                elements.append(("E", prefix + name, [node(w) for w in words[1:5]], value(words[5], params)))
            elif kind == "V":
                elements.append(("V", prefix + name, [node(words[1]), node(words[2])], 0.0))
            elif kind == "X":
                sub = words[-1].lower()
                ports, body = subckts[sub]
                inner = {p.lower(): node(a) for p, a in zip(ports, words[1:-1])}
                expand(body, prefix + name + ".", inner)
            else:
                raise ValueError(f"unsupported element {name}")

    expand(top, "", {})
    return elements, params


def build(elements, out_node: str, source: str):
    nodes = sorted({n for e in elements for n in e[2] if n != "0"})
    index = {n: i for i, n in enumerate(nodes)}
    branches = [e for e in elements if e[0] in "VEL"]
    size = len(nodes) + len(branches)
    G = np.zeros((size, size))
    C = np.zeros((size, size))
    b = np.zeros(size)

    def stamp(M, a, bnode, v):
        ia, ib = index.get(a), index.get(bnode)
        if ia is not None:
            M[ia, ia] += v
        if ib is not None:
            M[ib, ib] += v
        if ia is not None and ib is not None:
            M[ia, ib] -= v
            M[ib, ia] -= v

    for kind, name, ns, v in elements:
        if kind == "R":
            stamp(G, ns[0], ns[1], 1.0 / v)
        elif kind == "C":
            stamp(C, ns[0], ns[1], v)
    for k, (kind, name, ns, v) in enumerate(branches):
        row = len(nodes) + k
        p, n = index.get(ns[0]), index.get(ns[1])
        if p is not None:
            G[p, row] += 1
            G[row, p] += 1
        if n is not None:
            G[n, row] -= 1
            G[row, n] -= 1
        if kind == "V":
            if name.lower() == source.lower():
                b[row] = 1.0
        elif kind == "E":
            cp, cn = index.get(ns[2]), index.get(ns[3])
            if cp is not None:
                G[row, cp] -= v
            if cn is not None:
                G[row, cn] += v
        elif kind == "L":
            C[row, row] -= v
    c = np.zeros(size)
    c[index[out_node.lower()]] = 1.0
    return G, C, b, c


def modal(G, C, b, c):
    """Poles, residues and direct term of c (G + sC)^-1 b."""
    w, vl, vr = scipy.linalg.eig(-G, C, left=True, right=True)
    finite = np.isfinite(w) & (np.abs(w) < 1e18)
    poles, residues = [], []
    for k in np.nonzero(finite)[0]:
        denom = vl[:, k].conj() @ C @ vr[:, k]
        r = (c @ vr[:, k]) * (vl[:, k].conj() @ b) / denom
        if abs(r) > 1e-30:
            poles.append(w[k])
            residues.append(r)
    poles, residues = np.array(poles), np.array(residues)
    def h(s):
        return c @ np.linalg.solve(G + s * C, b)
    s_big = 1j * 2 * np.pi * 1e9
    d = (h(s_big) - np.sum(residues / (s_big - poles))).real
    return poles, residues, d, h


def audio_fit(poles, h, limit=1e6):
    """Keep the poles between 1e-3 and `limit` rad/s (the ideal op-amps' 10 pF
    compensation caps add near-coincident poles around 1e7 rad/s, whose
    eigenvector residues are ill-conditioned) and solve the residues and
    direct term by least squares against the exact MNA response, 20 Hz to
    200 kHz. Conjugate symmetry is enforced so the output stays real."""
    # A pole at (numerically) zero is a DC pole-zero pair from a node with no
    # DC path (a coupling capacitor into an otherwise floating node): it has
    # no effect above DC, and kept alone it would integrate any offset.
    keep = poles[(np.abs(poles) < limit) & (np.abs(poles) > 1e-3)]
    f = np.logspace(np.log10(20), np.log10(2e5), 2000)
    s = 2j * np.pi * f
    target = np.array([h(x) for x in s])
    # real parameterization: real poles -> one real residue; complex pairs -> Re, Im of one residue
    cols, layout = [], []
    used = np.zeros(len(keep), bool)
    for i, p in enumerate(keep):
        if used[i]:
            continue
        if abs(p.imag) < 1e-6 * abs(p):
            cols.append(1 / (s - p.real)); layout.append(("real", p.real)); used[i] = True
        else:
            j = next(k for k in range(len(keep)) if not used[k] and k != i and abs(keep[k] - p.conjugate()) < 1e-6 * abs(p))
            used[i] = used[j] = True
            pp = complex(p.real, abs(p.imag))
            cols.append(1 / (s - pp) + 1 / (s - pp.conjugate()))
            cols.append(1j / (s - pp) - 1j / (s - pp.conjugate()))
            layout.append(("pair", pp))
    cols.append(np.ones_like(s))
    A = np.array(cols).T
    weight = 1 / np.maximum(np.abs(target), 1e-6 * np.max(np.abs(target)))   # relative error
    x, *_ = np.linalg.lstsq(np.vstack([(A * weight[:, None]).real, (A * weight[:, None]).imag]),
                            np.concatenate([(target * weight).real, (target * weight).imag]), rcond=None)
    out_p, out_r, k = [], [], 0
    for kind, p in layout:
        if kind == "real":
            out_p.append(complex(p)); out_r.append(complex(x[k])); k += 1
        else:
            r = complex(x[k], x[k + 1]); k += 2
            out_p += [p, p.conjugate()]; out_r += [r, r.conjugate()]
    return np.array(out_p), np.array(out_r), float(x[k])


def ngspice_ac(path: Path, out_node: str, freqs, overrides=None):
    text = path.read_text()
    for name, v in (overrides or {}).items():
        text = re.sub(rf"(?im)^(\.param\b[^\n]*\b{re.escape(name)}\s*=\s*)[^\s]+", rf"\g<1>{v}", text)
    text = re.sub(r"(?ims)^\s*\.control.*?\.endc", "", text)
    text = re.sub(r"(?im)^\s*\.(ac|print|control|endc|end)\b.*$", "", text)
    lo, hi = freqs[0], freqs[-1]
    text += (f"\n.ac dec 200 {lo} {hi}\n.control\nrun\nwrdata /tmp/mna_ac.txt vm({out_node}) vp({out_node})\n"
             ".endc\n.end\n")
    tmp = Path("/tmp/mna_check.cir")
    tmp.write_text(text)
    subprocess.run(["ngspice", "-b", str(tmp)], check=True, capture_output=True)
    data = np.loadtxt("/tmp/mna_ac.txt")
    return data[:, 0], data[:, 1], data[:, 3]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("circuit", type=Path)
    ap.add_argument("--out", required=True)
    ap.add_argument("--in", dest="source", default="Vin")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--spice", action="store_true")
    ap.add_argument("--audio-fit", action="store_true", help="keep poles < 1e6 rad/s, least-squares residues")
    ap.add_argument("--set", action="append", default=[], help="name=value parameter override")
    ap.add_argument("--report", default="1000,10000,12000,15000,19200,22153,35850")
    args = ap.parse_args()
    overrides = {k: value(v, {}) for k, v in (a.split("=", 1) for a in args.set)}
    elements, _ = parse(args.circuit, overrides)
    G, C, b, c = build(elements, args.out, args.source)
    poles, residues, d, h = modal(G, C, b, c)
    if args.audio_fit:
        poles, residues, d = audio_fit(poles, h)
    freqs = [float(f) for f in args.report.split(",")]
    def hm(f):
        s = 2j * np.pi * f
        return np.sum(residues / (s - poles)) + d
    worst = max(abs(hm(f) - h(2j * np.pi * f)) / max(abs(h(2j * np.pi * f)), 1e-12)
                for f in np.logspace(1, 5, 200))
    print(f"{args.circuit.name}: {len(poles)} poles, direct {d:.3g}, modal vs MNA max rel error {worst:.2e}")
    for f in freqs:
        print(f"  {f/1000:8.3f} kHz  {20*np.log10(abs(hm(f)) + 1e-300):8.2f} dB")
    if args.spice:
        f, mag, ph = ngspice_ac(args.circuit, args.out, [10, 100000], dict(a.split("=", 1) for a in args.set))
        err = max(abs(20*np.log10(abs(hm(x)) + 1e-300) - 20*np.log10(m + 1e-300)) for x, m in zip(f, mag) if m > 1e-4)
        print(f"  ngspice agreement (where |H| > -80 dB): max {err:.4f} dB over {len(f)} points")
    if args.json:
        args.json.write_text(json.dumps({"poles": [[p.real, p.imag] for p in poles],
                                         "residues": [[r.real, r.imag] for r in residues], "direct": d}, indent=1))


if __name__ == "__main__":
    main()
