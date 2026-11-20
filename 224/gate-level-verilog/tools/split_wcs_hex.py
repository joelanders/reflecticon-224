#!/usr/bin/env python3
"""Split a 512-byte WCS dump (hex-dump text or raw binary) into the four
per-chip lane images loaded by the structural T&C board's MCM68B10 prims.
Lane order follows the MI bus wiring: b0=U43 (MI0-7), b1=U29, b2=U15, b3=U2.

  split_wcs_hex.py DUMP [OUTDIR] [--physical]

By default each lane holds the dump's bytes as they are, step by step (the
older benches' convention). With --physical the lanes hold what the WCS RAMs
hold when the CPU wrote the dump: every byte complemented (the SBC's bus
drivers invert) and row a taken from step 127 - a.
"""
import sys
from pathlib import Path

def load(path: Path) -> bytes:
    raw = path.read_bytes()
    if len(raw) == 512:
        return raw
    out = bytearray()
    for line in raw.decode("ascii", "replace").splitlines():
        if ":" not in line:
            continue
        for tok in line.split(":", 1)[1].split():
            try:
                out.append(int(tok, 16))
            except ValueError:
                break
    if len(out) != 512:
        raise SystemExit(f"expected 512 WCS bytes, got {len(out)}")
    return bytes(out)

def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--physical"]
    physical = "--physical" in sys.argv[1:]
    src = Path(args[0])
    outdir = Path(args[1]) if len(args) > 1 else Path("out")
    data = load(src)
    outdir.mkdir(parents=True, exist_ok=True)
    for lane in range(4):
        if physical:
            lines = [f"{data[(row ^ 127) * 4 + lane] ^ 0xFF:02x}" for row in range(128)]
        else:
            lines = [f"{data[step * 4 + lane]:02x}" for step in range(128)]
        (outdir / f"wcs_lane_b{lane}.hex").write_text("\n".join(lines) + "\n")
    print(f"wrote 4 lane images to {outdir}/ from {src}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
