#!/usr/bin/env python3
"""The gate-level six-board bench that tests/gate_level.py compares against.

T&C, ARU, DMEM I/O, DMEM (with XREG) and FPC (../../gate-level-verilog/generated)
run one saved WCS image with RUN high, the CPU interface idle, and identical
converter input, under iverilog. The parts that are not traced:

- DRAM: the datasheet MK4164-15 model (8+8 addressing), all cells 0 at start.
- FPC U6: the constructed "primary functional hold" image (the real PROM's
  contents are unknown).
- CPC CLR: pulsed during MC 8..11, before RUN.
- MEMW data source: the generated T&C board drives RD RREG/ on MEMW rows
  (U48A pin 1 = MEMW/, as drawn). Variant "memw-rr" adds the same enable
  at the ARU's RD RREG/ input as an explicit term; it is only for comparing
  with boards generated before that correction.

Converter input is a function of program pass (advanced at each fetch of
WCS address 0) and the FPC's own CH1 output.

Recorded, per row, as ordered value sequences: working registers and RREG at
each ARUCK; DRAM accesses (bank, write, address, data); XREG output-register
loads; converted input words; WR DA words and channels; DAC outputs with
channels and gain.
"""
from __future__ import annotations

import csv
import io
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]          # the repository root; SOURCES are relative to it
FROZEN = ROOT                   # compile_bench reads FROZEN / source
OUT = HERE.parent / 'build/gate-bench'
# Programs to compare besides random ones: any WCS images you captured into
# ../../captures/programs/ (address-prefixed hex dumps of 0x4000-0x41FF).
PROGRAMS = sorted((ROOT / 'captures/programs').glob('*.hex'))
CYCLES = int(os.environ.get('DSP_CYCLES', '1500'))
RUN_TICK = 40
ALIGN_FETCH = 4
SOURCES = ['gate-level-verilog/prims/prims.v', 'gate-level-verilog/prims/prims_dmem_mk4164_15_datasheet.v',
           'gate-level-verilog/generated/tc_board.v', 'gate-level-verilog/generated/aru_board.v',
           'gate-level-verilog/generated/dmem_io_board.v', 'gate-level-verilog/generated/dmem_board.v',
           'gate-level-verilog/generated/fpc_board.v']
U6 = 'gate-level-verilog/rom/u6_74s287_primary_functional_hold_0_255.hex'


def bus(prefix, width, name):
    return ''.join(f'    .{prefix}{i}({name}[{i}]),\n' for i in range(width))


def chips(first, last, field):
    return '{' + ','.join(f'board.U{n}.{field}' for n in range(last, first - 1, -1)) + '}'


TB = r'''`timescale 1ns/1ps
module tb_dsp_ports;
  parameter integer MEMW_RR=1;  // 1: architectural MEMW RR enable; 0: crop U48 only.
  parameter [11:0] ADC_XOR=12'h000;  // Negative control only.
  reg mc=0, run=0;
  wire mc_w=mc, run_w=run, hi=1'b1, lo=1'b0;
  tc_board_netlist dut(.MC(mc_w),.HALT(run_w),.SAT(lo),.MWTC(hi),.MRDC(hi),
    .ADR0(hi),.ADR1(hi),.DPORT3_n(hi),.DPORT4_n(hi),.DPORT5_n(hi));
  wire [15:0] dab;
  wire [7:0] data;
  reg [11:0] adc_r=0;
  reg [1:0] gain_r=0;
  wire [11:0] adc=adc_r;
  wire [1:0] gain=gain_r;
  wire ch1;
  wire [3:0] fpcOut;
  fpc_board_netlist #(.FPC_U6_INIT_FILE("U6IMAGE")) fpc(
BUS_DAB
BUS_AD
    .IGA0(gain[0]),.IGA1(gain[1]),.CH1(ch1),
    .SDAA(dut.SDAA),.SDAB(dut.SDAB),.SDAC(dut.SDAC),.SDAD(dut.SDAD),
    .OUTA(fpcOut[0]),.OUTB(fpcOut[1]),.OUTC(fpcOut[2]),.OUTD(fpcOut[3]),
    .FPC_CK(dut.FPC_CK),.FPC_DBUG(lo),.O2(lo),.RESET_n(dut.RESET_n),
    .RD_AD_n(dut.RD_AD_n),.WR_DA_n(dut.WR_DA_n));
  wire boardClock=~dut.ARUCK;
  wire rrRead=(MEMW_RR && dut.MEMW_n===1'b0 && dut.DAB_RSTB===1'b1)?1'b0:dut.RDRREG_n;
  reg clear=0;
  wire cpcClear=clear;
  wire ioRas,ioSelect,ioCas0,ioCas1;
  dmem_io_board_netlist io(
    .DAB_RSTB(dut.DAB_RSTB),.MEMAC(dut.MEMAC),.MC(mc_w),.MS1(dut.MS1),.A14(dut.OFST14),.RESET_n(dut.RESET_n),
    .ADR0_n(hi),.ADR1_n(hi),.ADR2_n(hi),.ADR3_n(hi),
    .ADR4_n(hi),.ADR5_n(hi),.ADR6_n(hi),.ADR7_n(hi),.IOWC_n(hi),.IORC_n(hi),
    .RAS_n(ioRas),.ROW_SEL(ioSelect),.CAS0_n(ioCas0),.CAS1_n(ioCas1));
  aru_board_netlist aru(
BUS_DAB
    .DAB_WSTB_n(dut.DAB_WSTB_n),.S0(dut.S0),.S1(dut.S1),.M0(dut.M0_n),.M1(dut.M1_n),
    .CSIGN_n(dut.CSIGN_n),.ZERO_n(dut.ZERO_n),.XFER_CK(dut.XFER_CK),
    .RDRREG_n(rrRead),.ARUCKE(boardClock),.WA0_n(dut.WA0_n),.WA1_n(dut.WA1_n),
    .RA0_n(dut.RA0_n),.RA1_n(dut.RA1_n));
  dmem_board_netlist board(
OFST_PINS
BUS_DAB
BUS_DATA
    .MEMW_n(dut.MEMW_n),.RESET_n(dut.RESET_n),.CPC_CLR(cpcClear),
    .RAS_n(ioRas),.CAS0_n(ioCas0),.CAS1_n(ioCas1),.ROW_SEL(ioSelect),
    .WRL_XREG_n(hi),.WRH_XREG_n(hi),.RDL_XREG_n(hi),.RDH_XREG_n(hi),.DPORT2(hi),
    .WR_XREG_n(dut.WR_XREG_n),.RD_XREG_n(dut.RD_XREG_n),.DPORT0(hi),.DPORT1(hi));

  wire [15:0] r0=REG0, r1=REG1, r2=REG2, r3=REG3;
  wire [15:0] rr={aru.U43.q[2],aru.U43.q[3],aru.U43.q[0],aru.U43.q[1],aru.U43.q[6],aru.U43.q[7],aru.U43.q[4],aru.U43.q[5],aru.U44.q[2],aru.U44.q[3],aru.U44.q[0],aru.U44.q[1],aru.U44.q[6],aru.U44.q[7],aru.U44.q[4],aru.U44.q[5]};
  wire [15:0] outputWord={board.U40.q,board.U38.q};
  wire [15:0] data0=DOUT_BANK0, data1=DOUT_BANK1;
  wire [15:0] fpc_source={fpc.U27.q[0],fpc.U27.q[1],fpc.U27.q[2],fpc.U27.q[3],fpc.U28.q[0],fpc.U28.q[1],fpc.U28.q[2],fpc.U28.q[3],fpc.U38.q[0],fpc.U38.q[1],fpc.U38.q[2],fpc.U38.q[3],fpc.U39.q[0],fpc.U39.q[1],fpc.U39.q[2],fpc.U39.q[3]};
  wire [15:0] fpc_pending={fpc.U37.q[0],fpc.U37.q[1],fpc.U37.q[2],fpc.U37.q[3],fpc.U37.q[4],fpc.U37.q[5],fpc.U37.q[6],fpc.U37.q[7],fpc.U36.q[0],fpc.U36.q[1],fpc.U36.q[2],fpc.U36.q[3],fpc.U36.q[4],fpc.U36.q[5],fpc.U36.q[6],fpc.U36.q[7]};
  wire [3:0] fpc_pending_select=fpc.U40.q;
  wire [11:0] fpc_dac={fpc.DA11,fpc.DA10,fpc.DA9,fpc.DA8,fpc.DA7,fpc.DA6,fpc.DA5,fpc.DA4,fpc.DA3,fpc.DA2,fpc.DA1,fpc.DA0};
  wire [3:0] fpc_output_gain=fpc.U43.q;
  wire [3:0] fpc_output_select=fpc.U41.q;

  integer tick=-1, half=0, pass=-1;
  reg [31:0] x;
  real target;
  task set_adc;
    begin
      x=$unsigned(pass)*32'h9e3779b1+32'h7f4a7c15;
      x=x^(x>>15);
      adc_r=x[11:0]^((ch1===1'b1)?12'h555:12'h000)^ADC_XOR;
      gain_r=pass[1:0];
    end
  endtask
  always @(posedge mc) begin tick=tick+1; clear=tick>=8 && tick<12; end
  always @(negedge mc) run=(tick+1>=RUN_TICK);
  always @(ch1) set_adc;
  initial begin
    set_adc;
    $display("time_ns,kind,a,b,c,d,e");
    for(half=0;half<HALVES;half=half+1) begin
      target=(half+1)*3125.0/192.0;
      #(target-$realtime) mc=~mc;
    end
    $finish;
  end
  wire [6:0] address={dut.WCSA6, dut.WCSA5, dut.WCSA4, dut.WCSA3, dut.WCSA2, dut.WCSA1, dut.WCSA0};
  wire [31:0] pins={PINS};
  always @(posedge dut.DAB_RSTB_slash) if(tick>=0 && run) begin
    $display("%0.3f,fetch,%0d,%08h,,,", $realtime, address, pins);
    if(address==0) begin pass=pass+1; set_adc; end
  end
  // Register snapshots at ARUCKE (T&C U25 pin 8), two gates before ARUCK and
  // before any register reacts to this slot: the result register changes
  // XFER_CK + clock-to-Q later, which lands within ns of ARUCK itself.
  always @(posedge dut.ARUCKE) if(tick>=0)
    $display("%0.3f,regs,%b,%b,%b,%b,%b", $realtime, r0, r1, r2, r3, rr);
  // DRAM writes at the MK4164's own capture edges: CAS falling with W low,
  // or W falling with CAS low. DIN is wired to DAB. Reads when DOUT turns on.
  wire [15:0] cpc={board.U65.b_q,board.U65.a_q,board.U51.b_q,board.U51.a_q};
  wire [15:0] ofst={dut.OFST15,dut.OFST14,dut.OFST13,dut.OFST12,dut.OFST11,dut.OFST10,dut.OFST9,dut.OFST8,
    dut.OFST7,dut.OFST6,dut.OFST5,dut.OFST4,dut.OFST3,dut.OFST2,dut.OFST1,dut.OFST0};
  always @(negedge ioCas0) if(tick>=0 && board.U20.row_valid && board.Untitled_Sheet_WR===1'b0)
    $display("%0.3f,mem,C0,1,%b,%b,%b/%b", $realtime, {board.U20.row_addr, board.U20.mux_addr}, dab, cpc, ofst);
  always @(negedge ioCas1) if(tick>=0 && board.U1.row_valid && board.Untitled_Sheet_WR===1'b0)
    $display("%0.3f,mem,C1,1,%b,%b,%b/%b", $realtime, {board.U1.row_addr, board.U1.mux_addr}, dab, cpc, ofst);
  always @(negedge board.Untitled_Sheet_WR) if(tick>=0) begin
    if(ioCas0===1'b0 && board.U20.row_valid) $display("%0.3f,mem,C0,1,%b,%b,%b/%b", $realtime, {board.U20.row_addr, board.U20.mux_addr}, dab, cpc, ofst);
    if(ioCas1===1'b0 && board.U1.row_valid) $display("%0.3f,mem,C1,1,%b,%b,%b/%b", $realtime, {board.U1.row_addr, board.U1.mux_addr}, dab, cpc, ofst);
  end
  always @(posedge board.U20.dout_active) if(tick>=0) begin #0;
    $display("%0.3f,mem,G0,0,%b,%b,%b/%b", $realtime, board.U20.full_addr, data0, cpc, ofst); end
  always @(posedge board.U1.dout_active) if(tick>=0) begin #0;
    $display("%0.3f,mem,G1,0,%b,%b,%b/%b", $realtime, board.U1.full_addr, data1, cpc, ofst); end
  always @(outputWord) if(tick>=0) $display("%0.3f,xreg,%b,,,,", $realtime, outputWord);
  wire [7:0] fpc_count={fpc.U7.q,fpc.U8.q};
  always @(fpc_source) if(tick>=0) $display("%0.3f,adc,%b,%b,,,", $realtime, fpc_source, fpc_count);
  always @(fpc_count) if(tick>=0) $display("%0.3f,fpccount,%b,,,,", $realtime, fpc_count);
  initial begin #1;  // Starting state of registers otherwise logged only on change.
    $display("%0.3f,xreg,%b,,,,", $realtime, outputWord);
    $display("%0.3f,adc,%b,%b,,,", $realtime, fpc_source, fpc_count);
    $display("%0.3f,pending,%b,%b,,,", $realtime, fpc_pending, fpc_pending_select);
  end
  always @(fpc_pending or fpc_pending_select) if(tick>=0) begin #0;
    $display("%0.3f,pending,%b,%b,,,", $realtime, fpc_pending, fpc_pending_select); end
  always @(fpcOut or fpc_dac or fpc_output_gain or fpc_output_select) if(tick>=0) begin #0;
    $display("%0.3f,dacpins,%b,%b,%b,%b,", $realtime, fpc_dac, fpcOut, fpc_output_gain, fpc_output_select); end
endmodule
'''


def testbench():
    tb = TB
    tb = tb.replace('BUS_DAB', bus('DAB', 16, 'dab').rstrip('\n'))
    tb = tb.replace('BUS_AD', bus('AD', 12, 'adc').rstrip('\n'))
    tb = tb.replace('BUS_DATA', bus('DATA', 8, 'data').rstrip('\n'))
    tb = tb.replace('OFST_PINS', ''.join(f'    .OFST{i}_n(dut.OFST{i}),\n' for i in range(16)).rstrip('\n'))
    for r in range(4):
        tb = tb.replace(f'REG{r}', '{' + ','.join(f'aru.U{c}.mem[{r}][{b}]' for c in (29, 30, 31, 32)
                                                   for b in range(4)) + '}')
    tb = tb.replace('DOUT_BANK0', chips(20, 35, 'dout_data')).replace('DOUT_BANK1', chips(1, 16, 'dout_data'))
    tb = tb.replace('PINS', ', '.join(f'dut.HIGH_SPEED1_MI{i}' for i in range(31, -1, -1)))
    tb = tb.replace('U6IMAGE', str(FROZEN / U6)).replace('RUN_TICK', str(RUN_TICK))
    return tb.replace('HALVES', str(2 * (RUN_TICK + 9 * CYCLES + 60)))


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    require(result.returncode == 0, f'{command[0]} failed:\n{result.stdout[-3000:]}\n{result.stderr[-3000:]}')
    return result.stdout


def saved_bytes(path):
    values = [int(f, 16) for line in path.read_text().splitlines() for f in line.split()[1:]]
    require(len(values) == 512, f'{path.name}: expected 512 bytes')
    return values


def compile_bench(variant, adc_xor=0):
    tb = OUT / 'tb_dsp_ports.v'
    tb.write_text(testbench())
    executable = OUT / f'{variant}.vvp'
    run(['iverilog', '-g2012', '-s', 'tb_dsp_ports', '-DLEXICON_EXTERNAL_MK4164', '-DLEXICON_MK4164_BOARD_ALIAS',
         '-P', f'tb_dsp_ports.MEMW_RR={int(variant == "memw-rr")}',
         '-P', f"tb_dsp_ports.ADC_XOR=12'h{adc_xor:03x}", '-o', str(executable)]
        + [str(FROZEN / s) for s in SOURCES] + [str(tb)])
    return executable


def hdl_trace(executable, name, saved):
    case = OUT / name
    (case / 'out').mkdir(parents=True, exist_ok=True)
    for lane in range(4):
        (case / 'out' / f'wcs_lane_b{lane}.hex').write_text(''.join(
            f'{saved[(address ^ 127) * 4 + lane] ^ 255:02x}\n' for address in range(128)))
    text = run(['vvp', '-n', str(executable)], cwd=case)
    return '\n'.join(line for line in text.splitlines() if ',' in line) + '\n'


def events(text):
    rows = list(csv.DictReader(io.StringIO(text)))
    for row in rows:
        row['time_ns'] = float(row['time_ns'])
    return rows


def align(go, hdl):
    anchor = [r for r in go if r['kind'] == 'fetch'][ALIGN_FETCH]
    for row in hdl:
        if row['kind'] == 'fetch' and row['a'] == anchor['a'] and row['b'] == anchor['b']:
            return anchor['time_ns'], row['time_ns']
    raise AssertionError('no matching HDL fetch')


SETTLE_NS = 20


def dedupe(values, initial=None):
    """Settled value changes: register bits that update within SETTLE_NS of
    each other (for example U40 before U36/U37) form one change."""
    settled = []
    for value in values:
        if settled and value[0] - settled[-1][0] < SETTLE_NS:
            settled[-1] = (settled[-1][0], value[1])
        else:
            settled.append(value)
    values = settled
    result = [initial] if initial else []
    for value in values:
        if not result or result[-1][1] != value[1]:
            result.append(value)
    return result


def sequences(rows, zero, side):
    """Per kind, ordered (relative time, value tuple) in the comparison window."""
    period = 9 * 3125.0 / 96.0
    start = zero - period / 2

    def before(kind, fields):
        """State at the window edge: the last recorded value before it."""
        earlier = [r for r in rows if r['kind'] == kind and r['time_ns'] < start]
        return (-period / 2, tuple(earlier[-1][f] for f in fields)) if earlier else None

    initial = {kind: before(kind, fields) for kind, fields in
               (('xreg', 'a'), ('adc', 'a'), ('pending', 'ab'))}
    rows = [r for r in rows if r['time_ns'] >= start]
    out = {}
    out['regs'] = [(r['time_ns'] - zero, tuple(r[f] for f in 'abcde')) for r in rows if r['kind'] == 'regs']
    mem = [r for r in rows if r['kind'] == 'mem']
    out['mem'] = [(r['time_ns'] - zero, (r['a'][-1], r['b'], r['c'], r['d'])) for r in mem]
    out['xreg'] = dedupe([(r['time_ns'] - zero, (r['a'],)) for r in rows if r['kind'] == 'xreg'], initial['xreg'])
    out['adc'] = dedupe([(r['time_ns'] - zero, (r['a'],)) for r in rows if r['kind'] == 'adc'], initial['adc'])
    out['pending'] = dedupe([(r['time_ns'] - zero, (r['a'], r['b'])) for r in rows if r['kind'] == 'pending'], initial['pending'])
    if side == 'go':
        out['dac'] = [(r['time_ns'] - zero, (r['a'], r['b'], r['c'])) for r in rows
                      if r['kind'] == 'dac' and r['b'] != '0000']
    else:
        # A DAC output is a channel strobe (OUT A..D) turning on: record the
        # converter word and output gain present then.
        out['dac'], previous = [], '0000'
        for r in rows:
            if r['kind'] == 'dacpins':
                if previous == '0000' and r['b'] != '0000':
                    out['dac'].append((r['time_ns'] - zero, (r['a'], r['b'], r['c'])))
                previous = r['b']
    return out


def bit_compare(left, right):
    """Return (equal under the Go-x rule, count of Go-x over HDL-known bits)."""
    covered = 0
    for a, b in zip(''.join(left), ''.join(right)):
        if a in '01' and a != b or a == 'z' and b != 'z':
            return False, covered
        if a == 'x' and b in '01':
            covered += 1
    return True, covered


def compare(go, hdl, end):
    result = {}
    for kind in ('regs', 'mem', 'xreg', 'adc', 'pending', 'dac'):
        left = [v for v in go[kind] if v[0] <= end]
        right = [v for v in hdl[kind] if v[0] <= end]
        count = min(len(left), len(right))
        mismatches, covered, deltas = [], 0, []
        for index in range(count):
            same, c = bit_compare(left[index][1], right[index][1])
            covered += c
            deltas.append(right[index][0] - left[index][0])
            if not same:
                mismatches.append(dict(index=index, go=left[index], hdl=right[index]))
        result[kind] = dict(go=len(left), hdl=len(right), compared=count, mismatches=len(mismatches),
                            first_mismatches=mismatches[:4], go_x_over_hdl_known=covered,
                            hdl_minus_go_ns=[round(min(deltas), 1), round(max(deltas), 1)] if deltas else None)
    return result
