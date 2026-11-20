#!/usr/bin/env python3
"""AC response of an ngspice netlist at chosen frequencies, with .param overrides.

  spice_response.py circuit.cir OUTNODE [name=value ...] [--freqs 1000,10000,...] [--nulls]

Prints |H| in dB at each frequency (relative to the netlist's own input
source, AC 1) and, with --nulls, the frequencies of deep minima between
15 kHz and 60 kHz.
"""
import re, subprocess, sys, tempfile
from pathlib import Path
import numpy as np


def response(circuit: Path, out: str, overrides: dict, lo=10.0, hi=100e3, points=2000):
    text = circuit.read_text()
    for name, value in overrides.items():
        text = re.sub(rf"(?im)^(\.param\b[^\n]*\b{re.escape(name)}\s*=\s*)[^\s]+", rf"\g<1>{value}", text)
    text = re.sub(r"(?ims)^\s*\.control.*?\.endc", "", text)
    text = re.sub(r"(?im)^\s*\.(ac|print|plot|end|op|tran|noise)\b.*$", "", text)
    text = re.sub(r"(?im)^\s*(print|run|wrdata|quit)\b.*$", "", text)
    with tempfile.TemporaryDirectory() as d:
        data = Path(d) / "ac.txt"
        text += (f"\n.ac lin {points} {lo} {hi}\n.control\nrun\nwrdata {data} vm({out}) vp({out})\n.endc\n.end\n")
        cir = Path(d) / "c.cir"
        cir.write_text(text)
        r = subprocess.run(["ngspice", "-b", str(cir)], capture_output=True, text=True)
        if not data.exists():
            raise RuntimeError(r.stdout[-2000:] + r.stderr[-2000:])
        a = np.loadtxt(data)
    return a[:, 0], a[:, 1]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    circuit, out = Path(args[0]), args[1]
    overrides = dict(a.split("=", 1) for a in args[2:])
    freqs = [1000, 5000, 10000, 12000, 14000, 15000, 16000, 17065, 20000]
    for a in sys.argv:
        if a.startswith("--freqs="):
            freqs = [float(x) for x in a.split("=", 1)[1].split(",")]
    f, m = response(circuit, out, overrides, lo=20, hi=60000, points=12000)
    db = 20 * np.log10(np.maximum(m, 1e-12))
    print(f"{circuit.name} {overrides}")
    print("  " + "  ".join(f"{x/1000:g}k:{np.interp(x, f, db):7.2f}" for x in freqs))
    if "--nulls" in sys.argv:
        k = [i for i in range(1, len(db) - 1) if db[i] < db[i-1] and db[i] < db[i+1] and 15000 < f[i] < 60000
             and db[i] < db.max() - 40]
        print("  nulls (kHz): " + ", ".join(f"{f[i]/1000:.3f}" for i in k))


if __name__ == "__main__":
    main()
