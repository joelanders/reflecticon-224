`timescale 1ns/1fs
// HP 5004A witness for the 224X v8.2.1 ARU (and DMEM) signature tables.
//
// The composition follows tb_clean_digital_dsp.v: generated T&C, ARU,
// DMEM-I/O, DMEM and FPC boards on one shared DAB, running the
// firmware-captured Diagnostic Program 3 WCS (display E0A).
//
// Program 3 writes only register 0 (every row has WA=0) but multiplies
// registers 1..3, and many rows load register 0 from the XREG input. On the
// v8.1 firmware (row machine + 8080, experiments/hp5004a-signature-tables/
// tools/diag_state.cpp) the setup single-steps the DSP while loading XREG
// input 5555, 6666, 3878 and 9F80, leaving R3=5555, R2=6666, R1=3878 and
// XREG input 9F80, then runs continuously with no further WCS or DSP port
// writes. This bench installs that end state directly: registers 1..3 are
// set before the first row, and XREG input is loaded through the DMEM
// board's own CPU write strobes.
//
// MEMW_RR selects the RDRREG/ enable seen by the ARU: 1 = the 2026-08-28
// architectural law (additive MEMW term, as tb_clean_digital_dsp.v), 0 = the
// crop drawing alone.
//
// The bench prints every ARUCK (U10 pin 11) rising edge with the value of
// every ARU chip pin just before the edge, and the gate events the printed
// setups use; the checker forms the analyzer windows.
module tb_hp5004a_aru;
  parameter integer MEMW_RR=1;
  parameter integer CYCLES=20000;
  parameter integer SAMPLE_FROM=8000;
  parameter U6_IMAGE="rom/u6_74s287_init.hex";
  parameter [15:0] R1_INIT=16'h3878;
  parameter [15:0] R2_INIT=16'h6666;
  parameter [15:0] R3_INIT=16'h5555;
  parameter [7:0] XREG_LOW_DATA=8'h80;   // SBC DAT0/..DAT7/ level (the CPU wrote 7F)
  parameter [7:0] XREG_HIGH_DATA=8'h9f;  // (the CPU wrote 60)
  parameter [11:0] ADC_CODE=12'h000;
  parameter [1:0] GAIN_CODE=2'b00;
  parameter integer DMEM_MODE=0;         // 1: sample the DMEM board once per pass (DMEM tables)
  parameter real DMEM_PRE=10;            // ... this many ns before the RESET/ edge
  parameter integer FPC_MODE=0;          // 1: sample the FPC board at FPCCLK (U4 pin 5) (FPC table)
  parameter real FPC_PRE=10;             // ... this many ns before the FPCCLK edge

  reg mc=0, run=0;
  wire mc_w=mc, run_w=run, hi=1'b1, lo=1'b0;
  tc_board_netlist dut(.MC(mc_w),.HALT(run_w),.SAT(lo),.MWTC(hi),.MRDC(hi),
    .ADR0(hi),.ADR1(hi),.DPORT3_n(hi),.DPORT4_n(hi),.DPORT5_n(hi));
  // The qualified U37 grade, as tb_clean_digital_dsp.v.
  parameter real INVERTER_RISE=3;
  parameter real INVERTER_FALL=3;
  defparam dut.U37.TPLH=INVERTER_RISE;
  defparam dut.U37.TPHL=INVERTER_FALL;

  integer tick=-1;
  wire [15:0] hostDab=16'hzzzz;
  // The AIN module's converter, as the FPC signature procedure leaves it:
  // "Lift pin 11 of SAR IC (U26) on AIN module and jumper to +5V" (224X
  // service manual 5.7, FPC table). Pin 11 of the Am2504 is D, the serial
  // data input from the LM211 comparator (AIN drawing 060-01321 sheet 1:
  // CNVCLK conn. 63 -> pin 13 CP, STCONV conn. 69 -> pin 14 S, E pin 1 to
  // ground, R96 2K pull-up on pin 11). With D held high every successive-
  // approximation trial keeps its bit, so the register walks a fixed
  // pattern independent of any analog input. Datasheet (Am2502/3/4): S low
  // at a clock rise resets to Q11=0, Q10..Q0=1, CC=1; each later rise sets
  // the current bit from D and clears the next lower bit; after Q0 the
  // register holds until the next start. Delays: tpd typ 29 ns (rise) /
  // 27 ns (fall), 35 ns for Q11. The AD bus carries Q10..Q0 and, for the
  // MSB, pin 23 = Q11/ (drawn "MSB/"), which is the sign bit the FPC
  // shifter sign-extends (AD11 fans out to U27 A-D and U28 A).
  parameter integer SAR_JUMPER=0;      // 1: drive AD0-11 from the jumpered Am2504
  parameter integer SAR_MSB_INVERT=1;  // AD11 = Q11/ (pin 23); 0: AD11 = Q11 (pin 21)
  reg [11:0] sar_q=12'h7ff;
  reg sar_cc=1;
  integer sar_bit=11;
  always @(posedge fpc.CNV_CK) begin
    if (fpc.STC===1'b0) begin
      sar_q[11] <= #35 1'b0;
      sar_q[10:0] <= #29 11'h7ff;
      sar_cc <= #29 1'b1;
      sar_bit <= 11;
    end else if (sar_cc) begin
      if (sar_bit==11) sar_q[11] <= #35 1'b1;   // D = +5V
      else sar_q[sar_bit] <= #29 1'b1;
      if (sar_bit>0) sar_q[sar_bit-1] <= #27 1'b0;
      else sar_cc <= #29 1'b0;
      sar_bit <= sar_bit-1;
    end
  end
  wire [11:0] sar_ad={SAR_MSB_INVERT ? ~sar_q[11] : sar_q[11], sar_q[10:0]};
  wire [11:0] adc=SAR_JUMPER ? sar_ad : ADC_CODE;
  wire [1:0] gain=GAIN_CODE;
  wire debug=1'b0, o2=1'b0;
  wire [3:0] fpcOut;
  reg [7:0] hostData=8'hzz;
  reg wrLow=1,wrHigh=1;
  wire [6:0] control={dut.RD_XREG_n,dut.WR_XREG_n,hi,hi,hi,wrHigh,wrLow};
  wire [15:0] dab=hostDab;
  wire [7:0] data=hostData;
  fpc_board_netlist #(.FPC_U6_INIT_FILE(U6_IMAGE)) fpc(
    .DAB0(dab[0]),.DAB1(dab[1]),.DAB2(dab[2]),.DAB3(dab[3]),.DAB4(dab[4]),.DAB5(dab[5]),.DAB6(dab[6]),.DAB7(dab[7]),
    .DAB8(dab[8]),.DAB9(dab[9]),.DAB10(dab[10]),.DAB11(dab[11]),.DAB12(dab[12]),.DAB13(dab[13]),.DAB14(dab[14]),.DAB15(dab[15]),
    .AD0(adc[0]),.AD1(adc[1]),.AD2(adc[2]),.AD3(adc[3]),.AD4(adc[4]),.AD5(adc[5]),
    .AD6(adc[6]),.AD7(adc[7]),.AD8(adc[8]),.AD9(adc[9]),.AD10(adc[10]),.AD11(adc[11]),
    .IGA0(gain[0]),.IGA1(gain[1]),
    .SDAA(dut.SDAA),.SDAB(dut.SDAB),.SDAC(dut.SDAC),.SDAD(dut.SDAD),
    .OUTA(fpcOut[0]),.OUTB(fpcOut[1]),.OUTC(fpcOut[2]),.OUTD(fpcOut[3]),
    .FPC_CK(dut.FPC_CK),.FPC_DBUG(debug),.O2(o2),.RESET_n(dut.RESET_n),
    .RD_AD_n(dut.RD_AD_n),.WR_DA_n(dut.WR_DA_n));

  wire boardClock=~dut.ARUCK;
  wire rrRead=(MEMW_RR!=0 && dut.MEMW_n===1'b0 && dut.DAB_RSTB===1'b1)?1'b0:dut.RDRREG_n;
  reg clear=0;
  wire cpcClear=clear;
  wire ioRas,ioSelect,ioCas0,ioCas1;
  dmem_io_board_netlist io(
    .DAB_RSTB(dut.DAB_RSTB),.MEMAC(dut.MEMAC),.MC(mc_w),.MS1(dut.MS1),.A14(dut.OFST14),.RESET_n(dut.RESET_n),
    .ADR0_n(hi),.ADR1_n(hi),.ADR2_n(hi),.ADR3_n(hi),
    .ADR4_n(hi),.ADR5_n(hi),.ADR6_n(hi),.ADR7_n(hi),.IOWC_n(hi),.IORC_n(hi),
    .RAS_n(ioRas),.ROW_SEL(ioSelect),.CAS0_n(ioCas0),.CAS1_n(ioCas1));
  aru_board_netlist aru(
    .DAB0(dab[0]),.DAB1(dab[1]),.DAB2(dab[2]),.DAB3(dab[3]),.DAB4(dab[4]),.DAB5(dab[5]),.DAB6(dab[6]),.DAB7(dab[7]),
    .DAB8(dab[8]),.DAB9(dab[9]),.DAB10(dab[10]),.DAB11(dab[11]),.DAB12(dab[12]),.DAB13(dab[13]),.DAB14(dab[14]),.DAB15(dab[15]),
    .DAB_WSTB_n(dut.DAB_WSTB_n),.S0(dut.S0),.S1(dut.S1),.M0(dut.M0_n),.M1(dut.M1_n),
    .CSIGN_n(dut.CSIGN_n),.ZERO_n(dut.ZERO_n),.XFER_CK(dut.XFER_CK),
    .RDRREG_n(rrRead),.ARUCKE(boardClock),.WA0_n(dut.WA0_n),.WA1_n(dut.WA1_n),
    .RA0_n(dut.RA0_n),.RA1_n(dut.RA1_n));
  dmem_board_netlist board(
    .OFST0_n(dut.OFST0),.OFST1_n(dut.OFST1),.OFST2_n(dut.OFST2),.OFST3_n(dut.OFST3),
    .OFST4_n(dut.OFST4),.OFST5_n(dut.OFST5),.OFST6_n(dut.OFST6),.OFST7_n(dut.OFST7),
    .OFST8_n(dut.OFST8),.OFST9_n(dut.OFST9),.OFST10_n(dut.OFST10),.OFST11_n(dut.OFST11),
    .OFST12_n(dut.OFST12),.OFST13_n(dut.OFST13),.OFST14_n(dut.OFST14),.OFST15_n(dut.OFST15),
    .DAB0(dab[0]),.DAB1(dab[1]),.DAB2(dab[2]),.DAB3(dab[3]),.DAB4(dab[4]),.DAB5(dab[5]),.DAB6(dab[6]),.DAB7(dab[7]),
    .DAB8(dab[8]),.DAB9(dab[9]),.DAB10(dab[10]),.DAB11(dab[11]),.DAB12(dab[12]),.DAB13(dab[13]),.DAB14(dab[14]),.DAB15(dab[15]),
    .DATA0(data[0]),.DATA1(data[1]),.DATA2(data[2]),.DATA3(data[3]),
    .DATA4(data[4]),.DATA5(data[5]),.DATA6(data[6]),.DATA7(data[7]),
    .MEMW_n(dut.MEMW_n),.RESET_n(dut.RESET_n),.CPC_CLR(cpcClear),
    .RAS_n(ioRas),.CAS0_n(ioCas0),.CAS1_n(ioCas1),.ROW_SEL(ioSelect),
    .WRL_XREG_n(control[0]),.WRH_XREG_n(control[1]),.RDL_XREG_n(control[2]),.RDH_XREG_n(control[3]),
    .DPORT2(control[4]),.WR_XREG_n(control[5]),.RD_XREG_n(control[6]),.DPORT0(hi),.DPORT1(hi));

  // Registers 1..3 as the firmware's setup leaves them (bit 15 = U29 D1).
  // The LS670 word address is {WA0, WA1} (reads {RA0, RA1}) and WAk = NOT
  // WAk/, so architectural register k = (k1 k0) is physical word (~k0 ~k1):
  // R0 -> 3, R1 -> 1, R2 -> 2, R3 -> 0.
  integer b;
  initial begin
    #1;
    for (b=0; b<4; b=b+1) begin
      aru.U29.mem[1][b]=R1_INIT[15-b]; aru.U30.mem[1][b]=R1_INIT[11-b];
      aru.U31.mem[1][b]=R1_INIT[7-b];  aru.U32.mem[1][b]=R1_INIT[3-b];
      aru.U29.mem[2][b]=R2_INIT[15-b]; aru.U30.mem[2][b]=R2_INIT[11-b];
      aru.U31.mem[2][b]=R2_INIT[7-b];  aru.U32.mem[2][b]=R2_INIT[3-b];
      aru.U29.mem[0][b]=R3_INIT[15-b]; aru.U30.mem[0][b]=R3_INIT[11-b];
      aru.U31.mem[0][b]=R3_INIT[7-b];  aru.U32.mem[0][b]=R3_INIT[3-b];
    end
  end

  // CPU side: load the XREG input (low byte, then high byte), clear CPC,
  // then RUN. The DSP stays halted until then.
  always @(posedge mc) begin
    tick=tick+1;
    hostData=(tick>=2 && tick<6) ? XREG_LOW_DATA : (tick>=8 && tick<12) ? XREG_HIGH_DATA : 8'hzz;
    wrLow=!(tick>=3 && tick<5);
    wrHigh=!(tick>=9 && tick<11);
    clear=tick>=14 && tick<18;
  end
  always @(negedge mc) run=(tick+1>=40);


  // print_aru_pins: every ARU chip pin, generated from the netlist.
  `include "hp5004a_aru_probes.vh"
  // print_tc_pins: every T&C chip pin (used when PROBE_TC=1).
  `include "hp5004a_tc_probes.vh"

  // Architectural registers (physical word (~k0 ~k1)).
  wire [15:0] r0={aru.U29.mem[3][0],aru.U29.mem[3][1],aru.U29.mem[3][2],aru.U29.mem[3][3],aru.U30.mem[3][0],aru.U30.mem[3][1],aru.U30.mem[3][2],aru.U30.mem[3][3],aru.U31.mem[3][0],aru.U31.mem[3][1],aru.U31.mem[3][2],aru.U31.mem[3][3],aru.U32.mem[3][0],aru.U32.mem[3][1],aru.U32.mem[3][2],aru.U32.mem[3][3]};
  wire [15:0] r1={aru.U29.mem[1][0],aru.U29.mem[1][1],aru.U29.mem[1][2],aru.U29.mem[1][3],aru.U30.mem[1][0],aru.U30.mem[1][1],aru.U30.mem[1][2],aru.U30.mem[1][3],aru.U31.mem[1][0],aru.U31.mem[1][1],aru.U31.mem[1][2],aru.U31.mem[1][3],aru.U32.mem[1][0],aru.U32.mem[1][1],aru.U32.mem[1][2],aru.U32.mem[1][3]};
  wire [15:0] r2={aru.U29.mem[2][0],aru.U29.mem[2][1],aru.U29.mem[2][2],aru.U29.mem[2][3],aru.U30.mem[2][0],aru.U30.mem[2][1],aru.U30.mem[2][2],aru.U30.mem[2][3],aru.U31.mem[2][0],aru.U31.mem[2][1],aru.U31.mem[2][2],aru.U31.mem[2][3],aru.U32.mem[2][0],aru.U32.mem[2][1],aru.U32.mem[2][2],aru.U32.mem[2][3]};
  wire [15:0] r3={aru.U29.mem[0][0],aru.U29.mem[0][1],aru.U29.mem[0][2],aru.U29.mem[0][3],aru.U30.mem[0][0],aru.U30.mem[0][1],aru.U30.mem[0][2],aru.U30.mem[0][3],aru.U31.mem[0][0],aru.U31.mem[0][1],aru.U31.mem[0][2],aru.U31.mem[0][3],aru.U32.mem[0][0],aru.U32.mem[0][1],aru.U32.mem[0][2],aru.U32.mem[0][3]};
  wire [15:0] xin={board.U41.q,board.U39.q};

  // Sampling phase relative to the ARUCK (U10.11) rising edge. Phase 0 is at
  // the edge, before any register it clocks has changed (every primitive has
  // a propagation delay). With PHASES > 1 the bench also samples D ns before
  // each edge (phase = D), because the analyzer needs its data set up before
  // the clock: a signal that changes just before the edge reads its old value.
  parameter integer PHASES=1;
  parameter integer PROBE_TC=0;
  task print_sample(input integer phase);
    begin
      $write("HP_CLK t=%0t tick=%0d phase=%0d r0=%h r1=%h r2=%h r3=%h xin=%h wcsa=%0d", $time, tick, phase, r0, r1, r2, r3, xin,
        {dut.WCSA6,dut.WCSA5,dut.WCSA4,dut.WCSA3,dut.WCSA2,dut.WCSA1,dut.WCSA0});
      print_aru_pins();
      if (PROBE_TC) print_tc_pins();
      $display("");
    end
  endtask
  // Samples before the edge: ARUCK is periodic while the DSP runs (three
  // edges per 293 ns row), so the previous edge delayed (transport) by one
  // period minus D marks the instant D ns before the next edge. The checker
  // attributes such a sample to the next edge (phase = D ns before it).
  localparam real ARUCK_PERIOD = 3*6250.0/192.0;  // 3 MC periods = 97.66 ns
  reg pre3=0, pre6=0, pre10=0, pre15=0, pre20=0, pre25=0, pre30=0;
  always @(aru.U10.p_11) begin
    pre3  <= #(ARUCK_PERIOD-3)  aru.U10.p_11;
    pre6  <= #(ARUCK_PERIOD-6)  aru.U10.p_11;
    pre10 <= #(ARUCK_PERIOD-10) aru.U10.p_11;
    pre15 <= #(ARUCK_PERIOD-15) aru.U10.p_11;
    pre20 <= #(ARUCK_PERIOD-20) aru.U10.p_11;
    pre25 <= #(ARUCK_PERIOD-25) aru.U10.p_11;
    pre30 <= #(ARUCK_PERIOD-30) aru.U10.p_11;
  end
  wire sampling = !DMEM_MODE && !FPC_MODE && tick>=SAMPLE_FROM && tick<CYCLES;
  always @(posedge aru.U10.p_11) if (sampling) print_sample(0);
  always @(posedge pre3)  if (sampling && PHASES>1) print_sample(3);
  always @(posedge pre6)  if (sampling && PHASES>1) print_sample(6);
  always @(posedge pre10) if (sampling && PHASES>1) print_sample(10);
  always @(posedge pre15) if (sampling && PHASES>1) print_sample(15);
  always @(posedge pre20) if (sampling && PHASES>1) print_sample(20);
  always @(posedge pre25) if (sampling && PHASES>1) print_sample(25);
  always @(posedge pre30) if (sampling && PHASES>1) print_sample(30);

  // DMEM tables: CLOCK = RESET/ at U58A pin 1 (rising), START = STOP = U65
  // pin 8 (MSB of CPC with the printed U65 jumper). One sample per pass,
  // DMEM_PRE ns before the edge (RESET/ is periodic: one pass = 270 MC).
  localparam real PASS_PERIOD = 270*6250.0/192.0;
  reg reset_pre=0;
  always @(board.U58.p_1) reset_pre <= #(PASS_PERIOD-DMEM_PRE) board.U58.p_1;
  always @(posedge reset_pre) begin
    if (DMEM_MODE && tick>=SAMPLE_FROM && tick<CYCLES) begin
      $write("HP_CLK t=%0t tick=%0d phase=%0d r0=%h r1=%h r2=%h r3=%h xin=%h wcsa=%0d", $time, tick, 0, r0, r1, r2, r3, xin,
        {dut.WCSA6,dut.WCSA5,dut.WCSA4,dut.WCSA3,dut.WCSA2,dut.WCSA1,dut.WCSA0});
      print_dmem_pins();
      $display("");
    end
  end
  always @(board.U65.p_8) begin
    if (DMEM_MODE && tick>=SAMPLE_FROM && tick<CYCLES) $display("HP_EDGE t=%0t signal=CPCMSB value=%b", $time, board.U65.p_8);
  end
  // print_dmem_pins: every DMEM chip pin, generated from the netlist.
  `include "hp5004a_dmem_probes.vh"

  // FPC table: CLOCK = FPCCLK at FPC U4 pin 5 (rising), START = STOP = RESET at
  // U4 pin 2 (falling). FPCCLK is periodic (one per 293 ns row).
  localparam real ROW_PERIOD = 9*6250.0/192.0;
  reg fpc_pre=0;
  always @(fpc.U4.p_5) fpc_pre <= #(ROW_PERIOD-FPC_PRE) fpc.U4.p_5;
  always @(posedge fpc_pre) begin
    if (FPC_MODE && tick>=SAMPLE_FROM && tick<CYCLES) begin
      $write("HP_CLK t=%0t tick=%0d phase=%0d r0=%h r1=%h r2=%h r3=%h xin=%h wcsa=%0d", $time, tick, 0, r0, r1, r2, r3, xin,
        {dut.WCSA6,dut.WCSA5,dut.WCSA4,dut.WCSA3,dut.WCSA2,dut.WCSA1,dut.WCSA0});
      print_fpc_pins();
      $display("");
    end
  end
  always @(fpc.U4.p_2) begin
    if (FPC_MODE && tick>=SAMPLE_FROM && tick<CYCLES) $display("HP_EDGE t=%0t signal=FPCRESET value=%b", $time, fpc.U4.p_2);
  end
  // print_fpc_pins: every FPC chip pin, generated from the netlist.
  `include "hp5004a_fpc_probes.vh"

  // Optional fine trace of the bus owners around a tick range.
  parameter integer TRACE_FROM=0;
  parameter integer TRACE_TO=0;
  wire [15:0] rrq={aru.U43.q[2],aru.U43.q[3],aru.U43.q[0],aru.U43.q[1],aru.U43.q[6],aru.U43.q[7],aru.U43.q[4],aru.U43.q[5],aru.U44.q[2],aru.U44.q[3],aru.U44.q[0],aru.U44.q[1],aru.U44.q[6],aru.U44.q[7],aru.U44.q[4],aru.U44.q[5]};
  always @(dab or rrRead or dut.RD_XREG_n or dut.RD_AD_n or dut.XFER_CK or dut.DAB_WSTB_n or rrq or dut.MEMW_n) begin
    if (tick>=TRACE_FROM && tick<TRACE_TO)
      $display("HP_TRACE t=%0t tick=%0d dab=%h rr=%h rdrreg_n=%b rdxreg_n=%b rdad_n=%b memw_n=%b xferck=%b wstb_n=%b wcsa=%0d",
        $time, tick, dab, rrq, rrRead, dut.RD_XREG_n, dut.RD_AD_n, dut.MEMW_n, dut.XFER_CK, dut.DAB_WSTB_n,
        {dut.WCSA6,dut.WCSA5,dut.WCSA4,dut.WCSA3,dut.WCSA2,dut.WCSA1,dut.WCSA0});
  end
  always @(aru.U42.p_1 or aru.U42.p_2 or aru.U42.p_3 or dut.ARUCK) begin
    if (tick>=TRACE_FROM && tick<TRACE_TO)
      $display("HP_CLKTRACE t=%0t aruck=%b u42p1=%b u42p2=%b u42p3=%b", $time, dut.ARUCK, aru.U42.p_1, aru.U42.p_2, aru.U42.p_3);
  end
  always @(aru.U2.p_11) if (tick>=TRACE_FROM && tick<TRACE_TO) $display("HP_CSIGN t=%0t csign_n=%b", $time, aru.U2.p_11);
  always @(posedge aru.U10.p_11) if (tick>=TRACE_FROM && tick<TRACE_TO) $display("HP_EDGE_CP t=%0t", $time);
  // The register-file write: the DAB word at the rising edge of DAB_WSTB/.
  always @(posedge dut.DAB_WSTB_n) begin
    if (tick>=SAMPLE_FROM && tick<CYCLES) $display("HP_WSTB t=%0t dab=%h wa_n=%b%b memw_n=%b rdrreg_n=%b", $time, dab,
      dut.WA1_n, dut.WA0_n, dut.MEMW_n, rrRead);
  end
  always @(dut.RESET_n) begin
    if (tick>=SAMPLE_FROM && tick<CYCLES) $display("HP_EDGE t=%0t signal=RESET_n value=%b", $time, dut.RESET_n);
  end
  always @(aru.U44.p_11) begin
    if (tick>=SAMPLE_FROM && tick<CYCLES) $display("HP_EDGE t=%0t signal=XFERCK value=%b", $time, aru.U44.p_11);
  end

  initial begin
    repeat (2*CYCLES+2) #(3125.0/192.0) mc=~mc;
    #100 $finish;
  end
endmodule
