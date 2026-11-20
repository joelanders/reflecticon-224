"""A small 8080 disassembler, for reading the firmware's handlers.

  python3 emulator/dis8080.py ROM_SET_DIRECTORY START_HEX COUNT

Chips are placed by name as the host places them (SBCn at (n-1)*0x800,
NVSn at 0x8000+(n-1)*0x1000; the 224's ROMn at (n-1)*0x800).
"""
import sys
from pathlib import Path
R=["B","C","D","E","H","L","M","A"]; RP=["B","D","H","SP"]; RQ=["B","D","H","PSW"]
CC=["NZ","Z","NC","C","PO","PE","P","M"]; ALU=["ADD","ADC","SUB","SBB","ANA","XRA","ORA","CMP"]; ALUI=["ADI","ACI","SUI","SBI","ANI","XRI","ORI","CPI"]
MISC={0x07:"RLC",0x0f:"RRC",0x17:"RAL",0x1f:"RAR",0x27:"DAA",0x2f:"CMA",0x37:"STC",0x3f:"CMC",0x76:"HLT",0xc9:"RET",0xe3:"XTHL",0xe9:"PCHL",0xeb:"XCHG",0xf3:"DI",0xf9:"SPHL",0xfb:"EI",0x00:"NOP"}
def one(m,a):
    o=m[a]; b1=m[(a+1)&0xffff]; w=b1|m[(a+2)&0xffff]<<8
    if o in MISC: return 1,MISC[o]
    x,y,z=o>>6,(o>>3)&7,o&7
    if x==1: return 1,f"MOV {R[y]},{R[z]}"
    if x==2: return 1,f"{ALU[y]} {R[z]}"
    if x==0:
        if z==1: return (3,f"LXI {RP[y>>1]},{w:04X}") if not y&1 else (1,f"DAD {RP[y>>1]}")
        if z==2: return [(1,"STAX B"),(1,"LDAX B"),(1,"STAX D"),(1,"LDAX D"),(3,f"SHLD {w:04X}"),(3,f"LHLD {w:04X}"),(3,f"STA {w:04X}"),(3,f"LDA {w:04X}")][y]
        if z==3: return 1,("INX " if not y&1 else "DCX ")+RP[y>>1]
        if z==4: return 1,f"INR {R[y]}"
        if z==5: return 1,f"DCR {R[y]}"
        if z==6: return 2,f"MVI {R[y]},{b1:02X}"
    if x==3:
        if z==0: return 1,f"R{CC[y]}"
        if z==1: return 1,f"POP {RQ[y>>1]}"
        if z==2: return 3,f"J{CC[y]} {w:04X}"
        if o==0xc3: return 3,f"JMP {w:04X}"
        if o==0xd3: return 2,f"OUT {b1:02X}"
        if o==0xdb: return 2,f"IN {b1:02X}"
        if z==4: return 3,f"C{CC[y]} {w:04X}"
        if z==5: return (1,f"PUSH {RQ[y>>1]}") if o!=0xcd else (3,f"CALL {w:04X}")
        if z==6: return 2,f"{ALUI[y]} {b1:02X}"
        if z==7: return 1,f"RST {y}"
    return 1,f"DB {o:02X}"
def load(d):
    m=bytearray(b"\xff"*65536)
    for f in Path(d).iterdir():
        n=f.name
        if "SBC" in n: base=(int(n[n.index("SBC")+3])-1)*0x800
        elif "NVS" in n: base=0x8000+(int(n[n.index("NVS")+3])-1)*0x1000
        elif "ROM" in n and n[n.index("ROM")+3] in "1234": base=(int(n[n.index("ROM")+3])-1)*0x800   # the 224
        else: continue
        b=f.read_bytes(); m[base:base+len(b)]=b
    return m
if __name__=="__main__":
    m=load(sys.argv[1]); a=int(sys.argv[2],16); n=int(sys.argv[3])
    for _ in range(n):
        l,t=one(m,a); print(f"{a:04X}  {m[a:a+l].hex(' '):9} {t}"); a+=l
