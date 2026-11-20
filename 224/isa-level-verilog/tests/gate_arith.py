#!/usr/bin/env python3
"""Every coefficient x sign x operand: the gate-level netlists against the row machine.

One 128-row program covers all 64 coefficients and both signs, one per row.
Every row is OPER: it loads R0 from the X register input, clears the
accumulator (ZERO), multiplies R0 by its coefficient, and saves the previous
row's product in RR (XFER). So RR after row k is R0 x coefficient(k-1),
signed and saturated, and one pass measures 128 products.

The netlists run in the six-board bench (tests/gate_level.py's), driven the
way the machine is: WCS as the RAMs hold it, the operand written into the X
register through its CPU strobes between passes. The row machine (C++) runs
the same program with the same X register input. Each operand is held for
two passes; RR after every row of the second pass must be identical.

  python3 tests/gate_arith.py --operands edges          # ~200 structured operands (minutes)
  python3 tests/gate_arith.py --operands all --jobs 8   # all 65,536 (about 35 minutes on 8 cores)
  python3 tests/gate_arith.py --force "aru.PP3=1'b0"    # negative control: must FAIL
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(HERE))
import gate_level  # noqa: E402
import run as rows_run  # noqa: E402

OUT = rows_run.BUILD / "gate-arith"
# The netlist logs the T&C's WCS address 20 ns after each XFER_CK edge; the row
# machine logs the row it is about to execute. The two differ by a fixed three
# rows (fetch prefetch plus the XFER's place in the row). Measured: at offset 3
# every product agrees; every other offset agrees on 13-18 % (by chance).
OFFSET = 3


PROGRAMS = {
    # every row a fresh product: R0 x coefficient(row), ZERO and XFER on every row
    "products": dict(zero=lambda r: True, shift=lambda r: False, coefficient=lambda r: r & 63),
    # running sums: ZERO every fourth row, so sums build over up to four rows and
    # reach the saturation rails from nonzero starting values
    "chains": dict(zero=lambda r: r % 4 == 0, shift=lambda r: False, coefficient=lambda r: (r * 37 + 11) & 63),
    # keep-shifting: three of every four rows multiply the previous x / 64
    "shifts": dict(zero=lambda r: r % 2 == 0, shift=lambda r: r % 4 != 0, coefficient=lambda r: (r * 29 + 63) & 63),
}
PROGRAM = PROGRAMS["products"]


def word(row: int) -> int:
    """T&C instruction word (the RAMs' polarity) for one row of the program."""
    coefficient, negative = PROGRAM["coefficient"](row), (row >> 6) & 1
    w = coefficient << 26
    w |= int(PROGRAM["zero"](row)) << 25    # ZERO
    w |= 1 << 24                            # XFER on every row
    w |= negative << 23
    w |= 1 << 16                            # OPER (RA = WA = 0)
    w |= 2 << 12                            # bus source: the X register input
    w |= int(PROGRAM["shift"](row)) << 4    # keep-shifting
    if row == 126:
        w |= 1 << 3                         # RESET: the pass is rows 0..127
    return w


def images(directory: Path) -> Path:
    """The lane files the T&C board loads, and the CPU-order image for the row machine."""
    lanes = directory / "out"
    lanes.mkdir(parents=True, exist_ok=True)
    for lane in range(4):
        (lanes / f"wcs_lane_b{lane}.hex").write_text(
            "".join(f"{(word(a) >> (8 * lane)) & 0xFF:02x}\n" for a in range(128)))
    cpu = bytearray(512)
    for a in range(128):
        for lane in range(4):
            cpu[(a ^ 127) * 4 + lane] = ((word(a) >> (8 * lane)) & 0xFF) ^ 0xFF
    path = directory / "program.bin"
    path.write_bytes(bytes(cpu))
    return path


DRIVER = r'''
  integer tick=-1, half=0, opi=-1, passes=0;
  real target;
  reg [15:0] ops [0:OPCOUNT-1];
  initial $readmemh("operands.hex", ops);
  always @(posedge mc) begin tick=tick+1; clear=tick>=8 && tick<12; end
  always @(negedge mc) run=(tick+1>=RUN_TICK);
  initial begin
    for(half=0;half<HALVES;half=half+1) begin
      target=(half+1)*3125.0/192.0;
      #(target-$realtime) mc=~mc;
    end
    $finish;
  end
  wire [6:0] address={dut.WCSA6, dut.WCSA5, dut.WCSA4, dut.WCSA3, dut.WCSA2, dut.WCSA1, dut.WCSA0};
  event write_next;
  always @(write_next) begin
    data_r = ops[opi][7:0]; data_oe = 1; #40 wrl_r = 0; #80 wrl_r = 1; #20 data_oe = 0;
    #20 data_r = ops[opi][15:8]; data_oe = 1; #40 wrh_r = 0; #80 wrh_r = 1; #20 data_oe = 0;
  end
  // A new operand at the start of every other pass; the pass after it is measured.
  always @(posedge dut.DAB_RSTB_slash) if (tick>=0 && run && address==7'd127) begin
    if (opi < 0 || passes == 1) begin
      opi = opi + 1; passes = 0;
      if (opi >= OPCOUNT) $finish;
      -> write_next;
    end else passes = passes + 1;
  end
  always @(negedge dut.XFER_CK) if (tick>=0 && run && opi>=0 && passes==1) begin
    #20 $display("X %0d %h %b", address, ops[opi], rr);
  end
endmodule
'''


def sources(tool, directory: Path, timescale):
    """The bench's Verilog sources; with a timescale (e.g. 100ps) every primitive
    delay is scaled at once, since prims.v states its delays in its own time unit."""
    paths = [tool.FROZEN / s for s in tool.SOURCES]
    if timescale:
        prims = next(p for p in paths if p.name == "prims.v")
        text = prims.read_text().split("\n", 1)
        assert text[0].startswith("`timescale"), prims
        scaled = directory / "prims_scaled.v"
        scaled.write_text(f"`timescale {timescale}/1ps\n" + text[1])
        paths = [scaled if p == prims else p for p in paths]
    return [str(p) for p in paths]


def build(tool, directory: Path, count: int, forces=(), timescale=None) -> Path:
    tb = tool.testbench()
    tb = tb.replace(".WRL_XREG_n(hi),.WRH_XREG_n(hi)", ".WRL_XREG_n(wrl_n),.WRH_XREG_n(wrh_n)")
    tb = tb.replace("  wire [7:0] data;", "  reg [7:0] data_r = 0; reg data_oe = 0, wrl_r = 1, wrh_r = 1;\n"
                    "  wire wrl_n = wrl_r, wrh_n = wrh_r;\n  wire [7:0] data = data_oe ? data_r : 8'hzz;")
    tb = tb[:tb.index("  integer tick=-1")]          # keep the boards and the probes, drop the logging
    rows = count * 2 * 128 + 400
    halves = 2 * (tool.RUN_TICK + 9 * rows + 60)
    for force in forces:                              # negative controls: a stuck net
        net, value = force.split("=")
        tb += f"  initial force {net} = {value};\n"
    tb += DRIVER.replace("OPCOUNT", str(count)).replace("RUN_TICK", str(tool.RUN_TICK)).replace("HALVES", str(halves))
    source = directory / "tb_gate_arith.v"
    source.write_text(tb)
    executable = directory / "gate_arith.vvp"
    tool.run(["iverilog", "-g2012", "-s", "tb_dsp_ports", "-DLEXICON_EXTERNAL_MK4164", "-DLEXICON_MK4164_BOARD_ALIAS",
              "-P", "tb_dsp_ports.MEMW_RR=0", "-o", str(executable)]
             + sources(tool, directory, timescale) + [str(source)])
    return executable


def hdl_chunk(args):
    """One vvp run over a slice of operands: {(op, row): rr} from the netlist."""
    index, operands, directory, forces, program, timescale = args
    global PROGRAM
    PROGRAM = PROGRAMS[program]
    case = Path(directory) / f"chunk{index}"
    tool = gate_level.load_go_tool(1)
    tool.FROZEN = tool.ROOT     # SOURCES are repository-relative: the current generated boards
    tool.OUT = case
    case.mkdir(parents=True, exist_ok=True)
    images(case)
    (case / "operands.hex").write_text("".join(f"{op:04x}\n" for op in operands))
    executable = build(tool, case, len(operands), forces, timescale)
    text = subprocess.run(["vvp", "-n", str(executable)], cwd=case, capture_output=True, text=True, check=True).stdout
    shutil.rmtree(case)          # the compiled bench is ~10 MB per chunk
    result = {}
    for line in text.splitlines():
        if line.startswith("X "):
            _, address, op, rr = line.split()
            result[(int(op, 16), int(address))] = rr
    return result


def rows_side(operands, directory: Path):
    binary = directory / "arith_rows"
    subprocess.run(["clang++", "-std=c++20", "-O2", "-I", str(ROOT.parent / "isa-level-cpp"), "-o", str(binary),
                    str(HERE / "gate_arith_rows.cpp")], check=True)
    image = images(directory)
    text = subprocess.run([str(binary), str(image)], input="".join(f"{op:04x}\n" for op in operands),
                          capture_output=True, text=True, check=True).stdout
    table = {}
    for line in text.splitlines():
        op, row, rr = line.split()
        table.setdefault(int(op, 16), {})[int(row)] = int(rr, 16)
    return table


def edge_operands():
    ops = {0x0000, 0xFFFF, 0x7FFF, 0x8000, 0x8001, 0x7FFE, 0x0001, 0x3FFF, 0xC000, 0xC001, 0x4000, 0xBFFF}
    for k in range(16):
        for v in (1 << k, (1 << k) - 1, -(1 << k), -(1 << k) - 1, -(1 << k) + 1):
            ops.add(v & 0xFFFF)
    for pattern in (0x5555, 0xAAAA, 0x3333, 0xCCCC, 0x6666, 0x9999, 0x0F0F, 0xF0F0, 0x00FF, 0xFF00):
        ops.add(pattern)
    return sorted(ops)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--operands", default="edges", help="edges | all | a comma list of hex values | START:COUNT")
    parser.add_argument("--timescale", help="scale every gate delay: prims.v's time unit, e.g. 100ps "
                        "(0.1x) or 10ns (10x); the default is prims.v's own 1ns")
    parser.add_argument("--program", choices=sorted(PROGRAMS), default="products")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--chunk", type=int, default=64, help="operands per simulator run")
    parser.add_argument("--force", action="append", default=[],
                        help="negative control: hold a netlist net, e.g. aru.PP3=1'b0 (the run must FAIL)")
    args = parser.parse_args()
    if args.operands == "edges":
        operands = edge_operands()
    elif args.operands == "all":
        operands = list(range(65536))
    elif ":" in args.operands:
        start, count = (int(v, 0) for v in args.operands.split(":"))
        operands = list(range(start, start + count))
    else:
        operands = [int(v, 16) for v in args.operands.split(",")]
    global PROGRAM, OUT
    PROGRAM = PROGRAMS[args.program]
    OUT = OUT / (args.program + (f"-{args.timescale}" if args.timescale else ""))
    OUT.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    expected = rows_side(operands, OUT)
    chunks = [(i, operands[i * args.chunk:(i + 1) * args.chunk], str(OUT), tuple(args.force), args.program, args.timescale)
              for i in range((len(operands) + args.chunk - 1) // args.chunk)]
    mismatches, compared, unknown, missing = [], 0, 0, 0
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for done, hdl in enumerate(pool.map(hdl_chunk, chunks), 1):
            for op in {op for op, _ in hdl}:
                for row, want in expected[op].items():
                    got = hdl.get((op, (row + OFFSET) % 128))
                    if got is None:
                        missing += 1
                        continue
                    if "x" in got or "z" in got:
                        unknown += 1
                        continue
                    compared += 1
                    if int(got, 2) != want:
                        mismatches.append((op, row, want, int(got, 2)))
            print(f"  chunk {done}/{len(chunks)}: {compared} products compared, {len(mismatches)} different "
                  f"({time.monotonic() - started:.0f} s)", flush=True)
    covered = {op for op in operands if op in expected}
    print(f"program {args.program}{' at gate delays in units of ' + args.timescale if args.timescale else ''}: {len(covered)} operands x 128 rows: {compared} products compared, "
          f"{len(mismatches)} different, {unknown} unknown, {missing} missing")
    for op, row, want, got in mismatches[:20]:
        print(f"  op={op:04x} coefficient={row & 63} negative={row >> 6 & 1}: row machine {want:04x}, netlist {got:04x}")
    ok = not mismatches and not unknown and not missing and compared == len(covered) * 128
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
