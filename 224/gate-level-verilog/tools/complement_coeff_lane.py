#!/usr/bin/env python3
"""Complement the coefficient field in out/wcs_lane_b3.hex.

WCS images hold the CPU's (logical) bytes; the T&C's coefficient path
consumes the physical field, the complement (the serializers' outputs are
the ~Q pins, U10 and U11 pin 11). This flips b3[7:2] (XOR 0xFC) and leaves b3[1:0]
(MI24/MI25, the RA/DP latch inputs) as they are. split_wcs_hex.py is shared
by checks that use only the decode lanes, so the flip is done here, per check.
"""
import sys
import pathlib

p = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "out/wcs_lane_b3.hex")
vals = p.read_text().split()
p.write_text("\n".join("%02x" % (int(v, 16) ^ 0xFC) for v in vals) + "\n")
