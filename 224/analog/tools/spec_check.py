#!/usr/bin/env python3
"""End-to-end frequency response against the 224X specification.

  spec_check.py AIN.cir AIN_OUT AOUT.cir AOUT_OUT [ain:name=value ...] [aout:name=value ...]

End to end = AIN chain x DAC sample-and-hold (a zero-order hold at the
34,133 Hz pass rate: sinc droop) x AOUT chain, normalized at 1 kHz. The
digital path in between is unity (a straight-through program such as MAX
DELAY). The service manual's specification: 20 Hz-15 kHz within +-1.5 dB,
20 Hz-12 kHz within +-0.5 dB.
"""
import sys
from pathlib import Path
import numpy as np
from spice_response import response

FS = 48000 * 32 / 45


def main():
    ain, ain_out, aout, aout_out = sys.argv[1:5]
    ov_in = dict(a[4:].split("=", 1) for a in sys.argv[5:] if a.startswith("ain:"))
    ov_out = dict(a[5:].split("=", 1) for a in sys.argv[5:] if a.startswith("aout:"))
    f, a = response(Path(ain), ain_out, ov_in, lo=20, hi=20000, points=4000)
    _, b = response(Path(aout), aout_out, ov_out, lo=20, hi=20000, points=4000)
    zoh = np.abs(np.sinc(f / FS))
    total = a * b * zoh
    db = 20 * np.log10(total / np.interp(1000, f, total))
    band12 = (f >= 20) & (f <= 12000)
    band15 = (f >= 20) & (f <= 15000)
    dev12 = np.max(np.abs(db[band12]))
    dev15 = np.max(np.abs(db[band15]))
    print(f"{Path(ain).name} {ov_in} + {Path(aout).name} {ov_out}")
    print("  " + "  ".join(f"{x/1000:g}k:{np.interp(x, f, db):6.2f}" for x in (20, 100, 1000, 5000, 10000, 12000, 14000, 15000)))
    print(f"  max deviation 20 Hz-12 kHz {dev12:.2f} dB (spec 0.5): {'PASS' if dev12 <= 0.5 else 'FAIL'};"
          f"  20 Hz-15 kHz {dev15:.2f} dB (spec 1.5): {'PASS' if dev15 <= 1.5 else 'FAIL'}")


if __name__ == "__main__":
    main()
