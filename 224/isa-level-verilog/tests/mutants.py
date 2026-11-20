#!/usr/bin/env python3
"""Mutation test: does the gate-level comparison see each ordering rule?

Each mutant breaks one statement of the row order in dsp.sv. The cached
gate-level traces (from tests/gate_level.py) must disagree with every
mutant; a mutant that still passes means that rule is untested there.

  python3 tests/gate_level.py --random 40 --netlist-xreg-race   # once: HDL traces
  python3 tests/mutants.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import fuzz  # noqa: E402
import gate_level  # noqa: E402
import random  # noqa: E402

MUTANTS = {
    "operand read before register write": (
        "from_sample(registers[mi.ra])", "from_sample(R[mi.ra])"),
    "XFER after this row's product": (
        "        if (mi.xfer) RR <= to_sample(ACC);\n        acc = mi.zero ? '0 : ACC;\n"
        "        ACC <= multiply_accumulate(acc, x, mi.coefficient, mi.negative);",
        "        acc = mi.zero ? '0 : ACC;\n        acc = multiply_accumulate(acc, x, mi.coefficient, mi.negative);\n"
        "        if (mi.xfer) RR <= to_sample(acc);\n        ACC <= acc;"),
    "ZERO before XFER": (
        "        if (mi.xfer) RR <= to_sample(ACC);\n        acc = mi.zero ? '0 : ACC;",
        "        acc = mi.zero ? '0 : ACC;\n        if (mi.xfer) RR <= to_sample(acc);"),
    "CPC advances one row late": (
        "        position = advance_cpc ? cpc + 16'd1 : cpc;\n        cpc <= position;\n"
        "        advance_cpc <= after_reset;",
        "        position = cpc;\n        cpc <= advance_cpc ? cpc + 16'd1 : cpc;\n"
        "        advance_cpc <= after_reset;"),
    "RD_AD register gets the old converter word": (
        "        if (oper && o.source == FROM_ADC) bus = fpc_next.input_sample;\n", ""),
    "no saturation (plain 20-bit wrap)": None,  # applied to the package below
    "program counter wraps immediately at RESET": (
        "        pc <= after_reset ? 7'd0 : run ? pc + 7'd1 : pc;",
        "        pc <= (oper && o.reset) ? 7'd0 : run ? pc + 7'd1 : pc;"),
    "halted rows advance the program counter": (
        "        pc <= after_reset ? 7'd0 : run ? pc + 7'd1 : pc;",
        "        pc <= after_reset ? 7'd0 : pc + 7'd1;"),
}


def main():
    gate_level.NETLIST_XREG_RACE = True
    tool = gate_level.load_go_tool(1500)
    source = (ROOT / "dsp.sv").read_text()
    escaped = []
    for name, change in MUTANTS.items():
        directory = gate_level.OUT / "mutants" / re.sub(r"[^a-z0-9]+", "-", name.lower())
        directory.mkdir(parents=True, exist_ok=True)
        model = directory / "dsp.sv"
        if change is None:
            model.write_text(source)
        else:
            old, new = change
            assert source.count(old) == 1, f"{name}: pattern not unique"
            model.write_text(source.replace(old, new))
        if change is None:
            package = (ROOT / "lexicon224x.sv").read_text()
            old = "        if (sum[19] != sum[18]) return addend[19] ? ACC_MIN : ACC_MAX;\n"
            assert package.count(old) == 1
            (directory / "lexicon224x.sv").write_text(package.replace(old, ""))
        ports = build(directory, model, change is None)
        mismatches = {}
        cases = [(path, path.stem, None) for path in tool.PROGRAMS]
        rng = random.Random(7)  # the programs of `gate_level.py --random 40` (seed 7)
        cases += [(None, f"random7-{n}", list(fuzz.random_program(rng, gate_level=True))) for n in range(40)]
        for path, case, saved in cases:
            result = gate_level.run_case(tool, ports, path, case, 1500, cached=True, saved=saved)
            for kind, v in result.items():
                mismatches[kind] = mismatches.get(kind, 0) + v["mismatches"]
        detected = {k: v for k, v in mismatches.items() if v}
        print(f"{'caught ' if detected else 'ESCAPED'}  {name}: {detected}", flush=True)
        if not detected:
            escaped.append(name)
    print("all mutants caught" if not escaped else f"escaped: {escaped}")


def build(directory, model, package_mutant):
    if not package_mutant:
        return gate_level.build_ports(model, directory / "build")
    original = gate_level.ROOT
    compiler, env = gate_level.rows_run.verilator()
    import subprocess
    subprocess.run([str(compiler), "--binary", "--timing", "-O3", "-Wall", "-Wno-fatal",
                    "-Wno-DECLFILENAME", "-Wno-UNUSEDSIGNAL", "-Wno-PROCASSINIT", "-Wno-REALCVT",
                    "--top-module", "ports_run", "--Mdir", str(directory / "build"),
                    "-CFLAGS", "-std=c++20 -O2", "-j", "4",
                    str(directory / "lexicon224x.sv"), str(model), str(original / "tests/ports_run.sv")],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    return directory / "build/Vports_run"


if __name__ == "__main__":
    main()
