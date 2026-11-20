#!/usr/bin/env python3
"""The WCS images of the firmware's E8x multiply test, one per call.

Written as 512-byte address-prefixed hex dumps, for the T&C + ARU test. The
four rows are the ones the firmware's loader stores (high step to low):
  step 127: FF FF FF FF
  step 126: DF DF <b2> <b3>   (the multiply; b2/b3 per call)
  step 125: F7 FF FE FE
  step 124: BF EF FE FF
Steps 0..123 are FF FF FF FF (no operation).

Calls (b2, b3): 1:(7E,A9) 2:(FE,A9) 3:(7E,55) 4:(FE,55) 5:(FE,01).
"""
import sys

CALLS = {1: (0x7E, 0xA9), 2: (0xFE, 0xA9), 3: (0x7E, 0x55),
         4: (0xFE, 0x55), 5: (0xFE, 0x01)}

def gen(call: int, path: str) -> None:
    b2, b3 = CALLS[call]
    mem = bytearray([0xFF] * 512)
    mem[124 * 4:125 * 4] = bytes([0xBF, 0xEF, 0xFE, 0xFF])
    mem[125 * 4:126 * 4] = bytes([0xF7, 0xFF, 0xFE, 0xFE])
    mem[126 * 4:127 * 4] = bytes([0xDF, 0xDF, b2, b3])
    # step 127 already FF FF FF FF
    with open(path, "w") as fh:
        for off in range(0, 512, 16):
            row = " ".join(f"{b:02x}" for b in mem[off:off + 16])
            fh.write(f"{0x4000 + off:04x}: {row}\n")

if __name__ == "__main__":
    for call in CALLS:
        gen(call, f"out/diag_call{call}.hex")
    print("wrote out/diag_call{1..5}.hex")
