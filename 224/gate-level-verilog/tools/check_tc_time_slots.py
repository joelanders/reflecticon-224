#!/usr/bin/env python3
"""The T&C master timing chain against the service manual's Table 1.

Hard rules (unambiguous in Table 1 / Fig. 3.4):
  - every observed signal is periodic with one microinstruction (293 ns
    = 9 slots of 32.55 ns)
  - MS0,1,2,4,6,7,8 are one slot wide, one-hot, and walk in slot order
    (MS3/MS5 are on outputs the netlist does not export; their slots must be low)
  - ARUCK: three one-slot pulses per frame, 3 slots apart (slots 0/3/6)
  - DAB RSTB: low exactly 2 slots per frame, spanning MS0 and MS1
    (from U20: high in slots 2-8, low in slots 0-1)
  - FPC CK: one one-slot pulse per frame
  - AS0: one 3-slot-wide pulse per frame (one ARU state)

Reported, not graded (Table 1's column alignment is drawing-level):
  - AS1/ low width vs AS0 width (Table 1 note: AS1 is shorter than AS0/AS2)
  - relative phases of ARUCK/AS0 vs MS0
"""

import re
import sys
from pathlib import Path

SLOT = 32.55
FRAME = 9 * SLOT
HALF = SLOT / 2


def main() -> int:
    text = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else sys.stdin.read()
    m = re.search(r"WINDOW start ([\d.]+)", text)
    m2 = re.search(r"WINDOW end ([\d.]+)", text)
    if not m or not m2:
        print("FAIL: no WINDOW markers")
        return 1
    w0, w1 = float(m.group(1)), float(m2.group(1))

    edges = {}
    for line in text.splitlines():
        em = re.match(r"EDGE (\w+) (rise|fall|x) ([\d.]+)", line)
        if em and w0 <= float(em.group(3)) < w1:
            edges.setdefault(em.group(1), []).append((em.group(2), float(em.group(3))))

    results = []

    def check(name, ok, detail):
        results.append((name, ok, detail))

    def rises(sig):
        return [t for d, t in edges.get(sig, []) if d == "rise"]

    def falls(sig):
        return [t for d, t in edges.get(sig, []) if d == "fall"]

    def near(x, target, tol=2.0):
        return abs(x - target) <= tol

    # x-cleanliness
    xs = [s for s, ev in edges.items() if any(d == "x" for d, _ in ev)]
    check("no x transitions in window", not xs, f"x on {xs}" if xs else "clean")

    # periodicity of every signal
    for sig in sorted(edges):
        rs = rises(sig)
        if len(rs) < 3:
            check(f"{sig} periodic 293ns", False, f"only {len(rs)} rises")
            continue
        deltas = [rs[i + 1] - rs[i] for i in range(len(rs) - 1)]
        bad = [d for d in deltas if not near(d % FRAME, 0, 2.0) and not near(d % FRAME, FRAME, 2.0)]
        # signals with multiple pulses per frame have sub-frame spacing;
        # grade instead that the pattern repeats frame-to-frame:
        per_frame = round(len(rs) / ((w1 - w0) / FRAME))
        rs_sorted = rs
        frame_ok = all(
            near((rs_sorted[i + per_frame] - rs_sorted[i]) if i + per_frame < len(rs_sorted) else FRAME * 1.0
                 if False else (rs_sorted[i + per_frame] - rs_sorted[i]), FRAME, 2.0)
            for i in range(len(rs_sorted) - per_frame)
        )
        check(f"{sig} periodic 293ns", frame_ok, f"{per_frame} pulses/frame")

    # MS one-hot walk
    ms_names = ["MS0", "MS1", "MS2", "MS4", "MS6", "MS7", "MS8"]
    ms0r = rises("MS0")
    if len(ms0r) < 3:
        check("MS walk", False, "no MS0 rises")
    else:
        t0 = ms0r[1]  # anchor mid-window
        phases = {}
        widths = {}
        ok_slots = True
        detail = []
        for i, sig in enumerate(ms_names):
            slot = [0, 1, 2, 4, 6, 7, 8][i]
            rs = [t for t in rises(sig) if t0 - SLOT <= t < t0 + FRAME - SLOT / 2]
            fs = [t for t in falls(sig) if rs and t > rs[0]]
            if not rs or not fs:
                ok_slots = False
                detail.append(f"{sig}:missing")
                continue
            phase = ((rs[0] - t0) / SLOT) % 9.0
            if phase > 8.5:
                phase -= 9.0
            width = (fs[0] - rs[0]) / SLOT
            phases[sig] = phase
            widths[sig] = width
            if not near(phase, slot, 0.15) or not near(width, 1.0, 0.15):
                ok_slots = False
            detail.append(f"{sig}@{phase:+.2f}w{width:.2f}")
        check("MS slots 0/1/2/4/6/7/8, 1 slot wide", ok_slots, " ".join(detail))

    # ARUCK: 3 pulses per frame, 1 slot wide, 3 slots apart
    ar = rises("ARUCK")
    if len(ms0r) >= 3 and len(ar) >= 6:
        t0 = ms0r[1]
        window = [t for t in ar if t0 - SLOT <= t < t0 + FRAME - SLOT]
        gaps = [window[i + 1] - window[i] for i in range(len(window) - 1)]
        widths_ok = True
        for r in window:
            f = min((t for d, t in edges["ARUCK"] if d == "fall" and t > r), default=None)
            if f is None or not near(f - r, SLOT, 3.0):
                widths_ok = False
        check("ARUCK 3 pulses/frame, 1 slot wide, 3 slots apart",
              len(window) == 3 and all(near(g, 3 * SLOT, 3.0) for g in gaps) and widths_ok,
              f"n={len(window)} gaps={[f'{g:.1f}' for g in gaps]} phase={((window[0]-t0)/SLOT if window else 99):+.2f}slots vs MS0")

    # DAB RSTB low spans MS0+MS1 (slots 0-1)
    rstb_f = falls("RSTB")
    rstb_r = rises("RSTB")
    if rstb_f and rstb_r and len(ms0r) >= 3:
        t0 = ms0r[1]
        f = max((t for t in rstb_f if t <= t0 + HALF), default=None)
        r = min((t for t in rstb_r if f is not None and t > f), default=None)
        if f is not None and r is not None:
            low = (r - f) / SLOT
            fall_phase = (f - t0) / SLOT
            check("RSTB low 2 slots spanning MS0/MS1",
                  near(low, 2.0, 0.35) and -1.0 <= fall_phase <= 0.25,
                  f"low {low:.2f} slots, falls {fall_phase:+.2f} slots vs MS0")
        else:
            check("RSTB low 2 slots spanning MS0/MS1", False, "edges not found")

    # FPC CK: one pulse per frame, 1 slot wide
    fr = rises("FPCCK")
    if len(ms0r) >= 3:
        t0 = ms0r[1]
        window = [t for t in fr if t0 <= t < t0 + FRAME]
        ok = len(window) == 1
        w = None
        if ok:
            f = min((t for d, t in edges["FPCCK"] if d == "fall" and t > window[0]), default=None)
            w = None if f is None else (f - window[0]) / SLOT
            ok = w is not None and near(w, 1.0, 0.2)
        check("FPCCK 1 pulse/frame, 1 slot", ok, f"n={len(window)} w={w if w is None else f'{w:.2f}'}")

    # AS0 3 slots wide; AS1/ low shorter than AS0 (Table 1 note)
    as0r, as0f = rises("AS0"), falls("AS0")
    as1f, as1r = falls("AS1N"), rises("AS1N")
    if as0r and as0f and len(ms0r) >= 3:
        t0 = ms0r[1]
        r = min((t for t in as0r if t >= t0), default=None)
        f = min((t for t in as0f if r is not None and t > r), default=None)
        if r is not None and f is not None:
            as0w = (f - r) / SLOT
            check("AS0 high 3 slots", near(as0w, 3.0, 0.35), f"{as0w:.2f} slots, rises {((r-t0)/SLOT):+.2f} vs MS0")
            fl = min((t for t in as1f if t >= t0 - FRAME), default=None)
            rl = min((t for t in as1r if fl is not None and t > fl), default=None)
            if fl is not None and rl is not None:
                as1w = (rl - fl) / SLOT
                # Table 1 explicitly says AS1 is shorter than AS0/AS2.  The
                # BOM-correct U25 S74 timing makes that visible: about two
                # slots versus AS0's three.  Reject equality as well as gross
                # distortion; the former was an artifact of modeling U25 with
                # the shared 40 ns LS74 delay.
                check("AS0/AS1 width report (Table 1 'AS1 shorter' note)",
                      as1w < as0w and near(as1w, 2.0, 0.35),
                      f"AS1/ low {as1w:.2f} slots vs AS0 high {as0w:.2f}")

    width = max(len(n) for n, _, _ in results)
    failed = sum(0 if ok else 1 for _, ok, _ in results)
    for name, ok, detail in results:
        print(f"{'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    print(f"{len(results) - failed}/{len(results)} rules pass")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
