#!/usr/bin/env python3
"""Check the DMEM-IO diagnostic run latch against the T&C WCSA counter."""

from __future__ import annotations

import re
import sys
from pathlib import Path


WAIT_RE = re.compile(
    r"^RUNL_WAIT tag=(\S+) step=(\d+) guard=(\d+) wcsa=(\d+) "
    r"useio=([01xzXZ]) halt=([01xzXZ]) iohalt=([01xzXZ])$"
)
PORT_RE = re.compile(
    r"^RUNL_PORT tag=(\S+) phase=(\S+) port=(\d+) wcsa=(\d+) "
    r"useio=([01xzXZ]) halt=([01xzXZ]) iohalt=([01xzXZ]) "
    r"y0=([01xzXZ]) y1=([01xzXZ]) y2=([01xzXZ]) y3=([01xzXZ]) "
    r"mode=([01xzXZ])([01xzXZ]) net11=([01xzXZ]) net9=([01xzXZ]) "
    r"mr=([01xzXZ]) pehi=([01xzXZ]) pelo=([01xzXZ]) "
    r"cephi=([01xzXZ]) ceplo=([01xzXZ]) cethi=([01xzXZ]) cetlo=([01xzXZ]) "
    r"dhi=([01xzXZ]+) dlo=([01xzXZ]+) "
    r"qhi=([0-9a-fA-FxXzZ]+) qlo=([0-9a-fA-FxXzZ]+)$"
)
SAMPLE_RE = re.compile(
    r"^RUNL_SAMPLE tag=(\S+) i=(\d+) wcsa=(\d+) useio=([01xzXZ]) "
    r"halt=([01xzXZ]) iohalt=([01xzXZ]) mode=([01xzXZ])([01xzXZ]) "
    r"pehi=([01xzXZ]) pelo=([01xzXZ]) cephi=([01xzXZ]) ceplo=([01xzXZ]) "
    r"cethi=([01xzXZ]) cetlo=([01xzXZ]) dhi=([01xzXZ]+) dlo=([01xzXZ]+) "
    r"qhi=([0-9a-fA-FxXzZ]+) qlo=([0-9a-fA-FxXzZ]+)$"
)


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    waits: dict[str, dict[str, int | str]] = {}
    ports: dict[tuple[str, str], dict[str, int | str]] = {}
    samples: dict[str, list[dict[str, int | str]]] = {}

    for line in text.splitlines():
        if m := WAIT_RE.match(line):
            waits[m.group(1)] = {
                "step": int(m.group(2)),
                "guard": int(m.group(3)),
                "wcsa": int(m.group(4)),
                "useio": m.group(5).lower(),
                "halt": m.group(6).lower(),
                "iohalt": m.group(7).lower(),
            }
            continue
        if m := PORT_RE.match(line):
            ports[(m.group(1), m.group(2))] = {
                "port": int(m.group(3)),
                "wcsa": int(m.group(4)),
                "useio": m.group(5).lower(),
                "halt": m.group(6).lower(),
                "iohalt": m.group(7).lower(),
                "y0": m.group(8).lower(),
                "y1": m.group(9).lower(),
                "y2": m.group(10).lower(),
                "y3": m.group(11).lower(),
                "mode_a": m.group(12).lower(),
                "mode_b": m.group(13).lower(),
                "net11": m.group(14).lower(),
                "net9": m.group(15).lower(),
                "mr": m.group(16).lower(),
                "pehi": m.group(17).lower(),
                "pelo": m.group(18).lower(),
                "cephi": m.group(19).lower(),
                "ceplo": m.group(20).lower(),
                "cethi": m.group(21).lower(),
                "cetlo": m.group(22).lower(),
                "dhi": m.group(23).lower(),
                "dlo": m.group(24).lower(),
                "qhi": m.group(25).lower(),
                "qlo": m.group(26).lower(),
            }
            continue
        if m := SAMPLE_RE.match(line):
            samples.setdefault(m.group(1), []).append({
                "i": int(m.group(2)),
                "wcsa": int(m.group(3)),
                "useio": m.group(4).lower(),
                "halt": m.group(5).lower(),
                "iohalt": m.group(6).lower(),
                "mode_a": m.group(7).lower(),
                "mode_b": m.group(8).lower(),
                "pehi": m.group(9).lower(),
                "pelo": m.group(10).lower(),
                "cephi": m.group(11).lower(),
                "ceplo": m.group(12).lower(),
                "cethi": m.group(13).lower(),
                "cetlo": m.group(14).lower(),
                "dhi": m.group(15).lower(),
                "dlo": m.group(16).lower(),
                "qhi": m.group(17).lower(),
                "qlo": m.group(18).lower(),
            })

    results: list[tuple[str, bool, str]] = []

    def check(name: str, ok: bool, detail: str) -> None:
        results.append((name, ok, detail))

    counter_records = list(ports.values()) + [
        sample for sample_batch in samples.values() for sample in sample_batch
    ]
    check("WCSA U1/U14 LOAD/ pins remain inactive through run-control",
          bool(counter_records)
          and all(r["pehi"] == "1" and r["pelo"] == "1" for r in counter_records),
          f"{len(counter_records)} observed counter states")
    check("WCSA U1/U14 CEP pins are tied enabled",
          bool(counter_records)
          and all(r["cephi"] == "1" and r["ceplo"] == "1" for r in counter_records),
          f"{len(counter_records)} observed counter states")
    check("WCSA U1/U14 parallel-load data pins are generated high",
          bool(counter_records)
          and all(r["dhi"] == "1111" and r["dlo"] == "1111" for r in counter_records),
          f"{len(counter_records)} observed counter states")
    check("low WCSA counter CET follows T&C HALT input",
          bool(counter_records)
          and all(r["cetlo"] == r["halt"] for r in counter_records),
          f"{len(counter_records)} observed counter states")

    wait = waits.get("free_to_127")
    check("startup free-runs to WCSA 127 before DMEM-IO owns HALT",
          wait is not None and wait["guard"] < 256 and wait["wcsa"] == 127
          and wait["useio"] == "0" and wait["halt"] == "1",
          str(wait))

    arm_single = ports.get(("arm_single", "idle"))
    check("OUT 00 arms single-step latch without driving T&C HALT yet",
          arm_single is not None
          and arm_single["port"] == 0
          and arm_single["mode_a"] == "1"
          and arm_single["mode_b"] == "0"
          and arm_single["iohalt"] == "0"
          and arm_single["useio"] == "0",
          str(arm_single))

    parked = samples.get("parked_127", [])
    check("DMEM-IO HALT/ parks T&C at row 127",
          len(parked) >= 4
          and all(s["useio"] == "1" and s["halt"] == "0" and s["iohalt"] == "0" for s in parked)
          and {s["wcsa"] for s in parked} == {127},
          str([s["wcsa"] for s in parked]))

    step_miss_during = ports.get(("step_miss", "during"))
    step_miss_idle = ports.get(("step_miss", "idle"))
    after_step_miss = samples.get("after_step_miss", [])
    check("OUT 03 is a level release, not an imperative step",
          step_miss_during is not None and step_miss_idle is not None
          and step_miss_during["port"] == 3
          and step_miss_during["halt"] == "1"
          and step_miss_during["iohalt"] == "1"
          and step_miss_idle["halt"] == "0"
          and step_miss_idle["iohalt"] == "0",
          f"during={step_miss_during} idle={step_miss_idle}")
    check("single-step strobe that misses the counter clock does not advance",
          len(after_step_miss) >= 4
          and all(s["halt"] == "0" and s["iohalt"] == "0" for s in after_step_miss)
          and {s["wcsa"] for s in after_step_miss} == {127},
          str([s["wcsa"] for s in after_step_miss]))

    step1_setup = ports.get(("step1_clocked", "setup"))
    step1_clocked = ports.get(("step1_clocked", "clocked"))
    step1_idle = ports.get(("step1_clocked", "idle"))
    after_step1 = samples.get("after_step1_clocked", [])
    after_step1_rows = [s["wcsa"] for s in after_step1]
    check("clock-overlapped OUT 03 advances once from row 127",
          step1_setup is not None
          and step1_clocked is not None
          and step1_idle is not None
          and step1_setup["wcsa"] == 127
          and step1_clocked["halt"] == "1"
          and step1_idle["halt"] == "0"
          and len(after_step1_rows) >= 4
          and set(after_step1_rows) == {0},
          f"setup={step1_setup} clocked={step1_clocked} rows={after_step1_rows}")

    step2_setup = ports.get(("step2_clocked", "setup"))
    step2_clocked = ports.get(("step2_clocked", "clocked"))
    step2_idle = ports.get(("step2_clocked", "idle"))
    after_step2 = samples.get("after_step2_clocked", [])
    after_step2_rows = [s["wcsa"] for s in after_step2]
    check("next clock-overlapped OUT 03 starts from held row 0, not row 127",
          step2_setup is not None
          and step2_clocked is not None
          and step2_idle is not None
          and step2_setup["wcsa"] == 0
          and step2_clocked["halt"] == "1"
          and step2_idle["halt"] == "0"
          and len(after_step2_rows) >= 4
          and set(after_step2_rows) == {1},
          f"setup={step2_setup} clocked={step2_clocked} rows={after_step2_rows}")

    arm_cont = ports.get(("arm_cont", "idle"))
    release_cont = ports.get(("release_cont", "idle"))
    after_cont = samples.get("after_cont", [])
    cont_rows = [s["wcsa"] for s in after_cont]
    check("OUT 01 arms continuous latch",
          arm_cont is not None
          and arm_cont["port"] == 1
          and arm_cont["mode_a"] == "0"
          and arm_cont["mode_b"] == "1",
          str(arm_cont))
    check("OUT 03 after OUT 01 latches HALT/ high for continuous run",
          release_cont is not None
          and release_cont["port"] == 3
          and release_cont["halt"] == "1"
          and release_cont["iohalt"] == "1",
          str(release_cont))
    check("continuous run advances WCSA",
          len(cont_rows) >= 4 and len(set(cont_rows[:4])) > 1
          and all(s["halt"] == "1" and s["iohalt"] == "1" for s in after_cont[:4]),
          str(cont_rows))

    halt_after = ports.get(("halt_after_cont", "idle"))
    after_halt = samples.get("after_halt", [])
    halted_rows = [s["wcsa"] for s in after_halt]
    check("OUT 02 forces HALT/ low",
          halt_after is not None
          and halt_after["port"] == 2
          and halt_after["halt"] == "0"
          and halt_after["iohalt"] == "0",
          str(halt_after))
    check("OUT 02 holds the current row without presetting 127",
          len(halted_rows) >= 4
          and len(set(halted_rows)) == 1
          and halted_rows[0] != 127
          and all(s["halt"] == "0" and s["iohalt"] == "0" for s in after_halt),
          str(halted_rows))

    check("simulation completed", "RUNL_DONE" in text,
          "RUNL_DONE present" if "RUNL_DONE" in text else "missing")

    width = max(len(name) for name, _, _ in results)
    failed = 0
    for name, ok, detail in results:
        failed += 0 if ok else 1
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
