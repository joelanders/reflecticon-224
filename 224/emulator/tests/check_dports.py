#!/usr/bin/env python3
"""Check the T&C diagnostic ports against the board-level machine while the DSP runs.

The firmware reads DPORT3-5 only while it single-steps the DSP, when every row
repeats, so firmware runs cannot see a port that is a row early or late. This
check can: a small ROM loads a random DSP program (no MEMR and no XREG source,
so the board model's unknown delay memory and XREG never reach the data path),
runs it, and reads DPORT3, DPORT4 and DPORT5 36 times each. The readings go to
RAM, then (after the last read) to WCS rows the program never executes
(101-127). Both hosts log their CPU WCS writes, so the readings can be compared
one by one. Every row drives the data bus and the ROM waits 250 ms before
reading, so the board model's unknown values never reach SAT (see test_rom).

    python3 tests/check_dports.py BOARD_MACHINE [--seeds 12]

BOARD_MACHINE is the board-level host (../board-level-verilog, built with
run.py build-machine: build/machine-1/Vmachine_host). All eight bits of all
three ports are compared, DPORT3 bit 0 (SAT and its one-shot) included.
"""
from __future__ import annotations

import argparse
import csv
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_rom(seed: int) -> bytes:
    """8080 code, then the WCS image it copies into rows 0-100 (RESET at 99, row 100 the flush row).

    Every row drives the data bus (OPER from RR or the ADC, or MEMW), so no
    register is loaded from an undriven bus: the row machine reads such a bus as
    0 by convention, the board model as unknown, and an unknown operand makes its
    SAT unknown, which pessimistically fires the 186 ms SAT one-shot. For the same
    reason the ROM waits 250 ms after starting the DSP (past any one-shot the
    halted copy started) and keeps its readings in RAM until the last one, so no
    CPU WCS write displaces a fetch while the ports are being read.
    """
    rng = random.Random(seed)
    words = []
    for row in range(101):
        op = rng.choice([1, 2])             # OPER, MEMW: both drive the bus
        w = rng.randrange(64) << 26 | rng.randrange(2) << 25 | rng.randrange(2) << 24 | rng.randrange(2) << 23
        w |= rng.randrange(4) << 20 | rng.randrange(4) << 18 | op << 16
        if op == 1:
            w |= rng.choice([1, 3]) << 12 | rng.randrange(16) << 8 | rng.randrange(2) << 7
            w |= rng.randrange(2) << 6 | rng.randrange(2) << 4
        else:
            w |= rng.randrange(1 << 16)
        words.append(w)
    words[99] = (words[99] & ~(3 << 16) & ~0xFFFF) | 1 << 16 | 1 << 12 | 1 << 3   # RESET, bus from RR
    table = bytearray()
    for address in range(0x406C, 0x4200):       # rows 100..0; the bus inverts each byte
        index = address - 0x4000
        table.append(~(words[(index // 4) ^ 127] >> (8 * (index % 4))) & 0xFF)
    code = bytearray([0x31, 0xF0, 0x3F,          # LXI SP,3FF0
                      0xD3, 0x02,                # OUT 02: halt the DSP
                      0x21, 0, 0,                # LXI H,table
                      0x11, 0x6C, 0x40,          # LXI D,406C
                      0x01, len(table) & 0xFF, len(table) >> 8])   # LXI B,len
    copy = len(code)
    code += bytes([0x7E, 0x12, 0x23, 0x13, 0x0B, 0x78, 0xB1, 0xC2, copy, 0])   # copy loop
    code += bytes([0xD3, 0x01, 0xD3, 0x03,       # OUT 01 (continuous), OUT 03 (run)
                   0x01, 0x55, 0x53])            # LXI B,21333: 250 ms at 24 states a turn
    wait = len(code)
    code += bytes([0x0B, 0x78, 0xB1, 0xC2, wait, 0])                          # DCX B; MOV A,B; ORA C; JNZ
    code += bytes([0x21, 0x00, 0x3C, 0x0E, 36])  # LXI H,3C00 (SBC RAM); MVI C,36
    read = len(code)
    for port in (3, 4, 5):
        code += bytes([0xDB, port, 0x77, 0x23])  # IN port; MOV M,A; INX H
    code += bytes([0x0D, 0xC2, read, 0])         # DCR C; JNZ read
    code += bytes([0x21, 0x00, 0x3C, 0x11, 0x00, 0x40, 0x0E, 108])   # LXI H,3C00; LXI D,4000; MVI C,108
    out = len(code)
    code += bytes([0x7E, 0x12, 0x23, 0x13, 0x0D, 0xC2, out, 0, 0x76])  # MOV A,M; STAX D; INX H; INX D; DCR C; JNZ; HLT
    code[6], code[7] = len(code) & 0xFF, len(code) >> 8
    assert len(code) + len(table) <= 2048
    return bytes(code + table).ljust(2048, b'\xff')


def readings(path: Path) -> list[int]:
    rows = [r for r in csv.DictReader(open(path)) if int(r['address']) < 0x406C]
    return [int(r['value']) for r in rows]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('board_machine', type=Path)
    parser.add_argument('--seeds', type=int, default=12)
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix='check_dports_'))
    subprocess.run(['make', '-s', '-C', str(ROOT), 'build/factory'], check=True)
    ours = ROOT / 'build' / 'factory'
    failures = 0
    for seed in range(1, args.seeds + 1):
        roms = work / f'rom{seed}'
        roms.mkdir()
        (roms / 'SBC1 2716.BIN').write_bytes(test_rom(seed))
        (roms / 'SBC2 2716.BIN').write_bytes(b'\xff' * 2048)
        subprocess.run([ours, roms, '600000', work / f'ours{seed}'], check=True, capture_output=True)
        subprocess.run([args.board_machine, 'factory', roms, '600000', work / f'board{seed}'], check=True,
                       capture_output=True)
        a, b = readings(work / f'ours{seed}.wcs.csv'), readings(work / f'board{seed}.wcs.csv')
        line = []
        for index, port in enumerate((3, 4, 5)):
            mask = 0xFF
            same = sum(1 for x, y in zip(a[index::3], b[index::3]) if (x ^ y) & mask == 0)
            line.append(f'DPORT{port} {same}/36')
            failures += same != 36
        sat = sum(1 for x in a[0::3] if not x & 1)      # the bus inverts: stored bit 0 clear = SAT read
        print(f'seed {seed}: ' + ', '.join(line) + f' (SAT seen in {sat}/36 of our readings)')
    print('ok' if failures == 0 else f'FAILED: {failures} port mismatches')
    shutil.rmtree(work)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
