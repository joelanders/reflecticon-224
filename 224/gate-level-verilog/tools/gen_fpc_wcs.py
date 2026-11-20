#!/usr/bin/env python3
"""WCS image for the T&C + FPC test.

The program keeps all rows idle except:
  - step 20: OUTAB + WR_DA/ via OFST7=1.  b1[3:0]=0101 drives
    SDAA..SDAD such that the FPC captures select code 0xa.
  - step 44: OUTAB + RD_AD/ via decoder-2 select b1[5:4]=11.

This isolates the real T&C-generated FPC_CK, WR_DA/, RD_AD/, RESET/, and SDA
phasing while the testbench supplies the DAB value for the WR_DA row and forces
a prepared FPC read word at U25/U26 for the RD_AD row.
"""

mem = bytearray()
for step in range(128):
    b0, b1, b2, b3 = 0x00, 0x00, 0xFC, 0xFF  # NOP
    if step == 20:
        b0, b1, b2 = 0x80, 0x05, 0x01        # OUTAB + WR_DA/, captured SDA=0xa
    elif step == 44:
        b0, b1, b2 = 0x00, 0x30, 0x01        # OUTAB + RD_AD/
    mem += bytes([b0, b1, b2, b3])

with open("out/fpc_stitch.hex", "w") as fh:
    for off in range(0, 512, 16):
        row = " ".join(f"{b:02x}" for b in mem[off:off + 16])
        fh.write(f"{0x4000 + off:04x}: {row}\n")
print("wrote out/fpc_stitch.hex")
