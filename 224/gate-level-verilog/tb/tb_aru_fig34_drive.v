`timescale 1ns/1ps
// ARU Fig 3.4 / Table 2 conformance: scripted multiply-accumulate cycles.
// Drives the documented external waveforms (S1S0=11/01/01 per AS state,
// M1M0 = coefficient pairs MSB-first, CSIGN level, ZERO/ low across the
// following AS0, XFER CK low pulse rising at the AS0 boundary), then reads
// the result register back over the DAB via RD_FREG/. Cases come from
// out/aru_cases.memh; results print as ARES lines for the checker.
module tb_aru_fig34_drive;

  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;
  reg wstb_n = 1'b1, arucke = 1'b0, s0 = 1'b0, s1 = 1'b0;
  reg m0 = 1'b0, m1 = 1'b0, csign_n = 1'b1, zero_n = 1'b1;
  reg xfer_ck = 1'b1, rd_freg_n = 1'b1;
  reg wa0_n = 1'b1, wa1_n = 1'b1, ra0_n = 1'b1, ra1_n = 1'b1;

  wire [15:0] dab;
  genvar gi;
  generate for (gi = 0; gi < 16; gi = gi + 1) begin : dd
    assign dab[gi] = dab_oe ? dab_drv[gi] : 1'bz;
  end endgenerate

  wire wstb_n_w = wstb_n, arucke_w = arucke, s0_w = s0, s1_w = s1;
  wire m0_w = m0, m1_w = m1, csign_n_w = csign_n, zero_n_w = zero_n;
  wire xfer_ck_w = xfer_ck, rd_freg_n_w = rd_freg_n;
  wire wa0_w = wa0_n, wa1_w = wa1_n, ra0_w = ra0_n, ra1_w = ra1_n;
  wire sat;

  aru_board_netlist dut(
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .DAB_WSTB_n(wstb_n_w), .S0(s0_w), .S1(s1_w), .M0(m0_w), .M1(m1_w),
    .CSIGN_n(csign_n_w), .ZERO_n(zero_n_w), .XFER_CK(xfer_ck_w),
    .RDRREG_n(rd_freg_n_w), .ARUCKE(arucke_w),
    .WA0_n(wa0_w), .WA1_n(wa1_w), .RA0_n(ra0_w), .RA1_n(ra1_w), .SAT(sat)
  );

  // cases: {csign[48], coeff6[47:40], f[39:24], expect_rr[23:8], idx[7:0]}
  // idx is informational only (wraps at 256); case order in the memh rules.
  reg [48:0] cases [0:1023];
  integer ncases;

  wire [19:0] acc = {dut.AC19, dut.AC18, dut.AC17, dut.AC16, dut.AC15,
                     dut.AC14, dut.AC13, dut.AC12, dut.AC11, dut.AC10,
                     dut.AC9, dut.AC8, dut.AC7, dut.AC6, dut.AC5,
                     dut.AC4, dut.AC3, dut.AC2, dut.AC1, dut.AC0};
  wire [19:0] pp  = {dut.PP19, dut.PP18, dut.PP17, dut.PP16, dut.PP15,
                     dut.PP14, dut.PP13, dut.PP12, dut.PP11, dut.PP10,
                     dut.PP9, dut.PP8, dut.PP7, dut.PP6, dut.PP5,
                     dut.PP4, dut.PP3, dut.PP2, dut.PP1, dut.PP0};

  // adder B-side operand (LS86 bank outputs) and carry-in (U2.6 rail)
  wire [19:0] bop = {dut.Net_U23_B4, dut.Net_U23_B3, dut.Net_U23_B2, dut.Net_U23_B1,
                     dut.Net_U22_B4, dut.Net_U22_B3, dut.Net_U22_B2, dut.Net_U22_B1,
                     dut.Net_U21_B4, dut.Net_U21_B3, dut.Net_U21_B2, dut.Net_U21_B1,
                     dut.Net_U20_B4, dut.Net_U20_B3, dut.Net_U20_B2, dut.Net_U20_B1,
                     dut.Net_U19_B4, dut.Net_U19_B3, dut.Net_U19_B2, dut.Net_U19_B1};
  wire acc_cin = dut.Net_U2_Pad10;

  // internal ARU CK = NOT(ARUCKE): falling edge of arucke clocks the board
  task aruck_edge;
    begin
      arucke = 1'b1; #49;
      $display("VEDGE acc=%05x b=%05x cin=%b", acc, bop, acc_cin);
      arucke = 1'b0; #49;  // board clock rising edge here
      $display("VSTATE acc=%05x pp=%05x m=%b%b s=%b%b zero_n=%b", acc, pp, m1, m0, s1, s0, zero_n);
    end
  endtask

  // One Table 2 state: set S and M, then clock.
  task state(input sl, input ss, input mm1, input mm0);
    begin
      s1 = sl; s0 = ss; m1 = mm1; m0 = mm0; #20;
      aruck_edge;
    end
  endtask

  task prime_load;
    begin
      s1 = 1'b1; s0 = 1'b1; #20;
      aruck_edge;
    end
  endtask

  // One full microinstruction cycle with controls applied per Table 2:
  // ctrl_zero/ctrl_xfer act around the FIRST state (AS0) of this cycle.
  // XFER CK is low across AS0 and rises at the AS0-end boundary (Table 2
  // shows 110000 in the AS0 column). The internal CK (~ARUCKE) falls
  // mid-state, which is when the final PP rank reaches the adder; the
  // rise must come after that fall and before the state-end CK rise, so
  // the result register captures the settled full sum.
  task cycle(input [5:0] c6, input cs, input ctrl_zero, input ctrl_xfer);
    begin
      csign_n = cs;  // A48 is inverted on-board by U2: drive logical csign directly
      if (ctrl_xfer) xfer_ck = 1'b0;           // XFER CK low pulse across AS0
      if (ctrl_zero) zero_n = 1'b0;            // ZERO/ low across AS0 edge
      s1 = 1'b0; s0 = 1'b1; m1 = ~c6[5]; m0 = ~c6[4]; #20; // AS0 = SHIFT (Fig 3.4: shifter already holds F)
      // AS0 edge inlined: CK falls (arucke high), adder settles, XFER CK
      // rises at the boundary, then CK rises (LOAD; acc clears if ZERO/).
      arucke = 1'b1; #45;
      if (ctrl_xfer) xfer_ck = 1'b1;
      #4;
      $display("VEDGE acc=%05x b=%05x cin=%b (AS0%s)", acc, bop, acc_cin, ctrl_xfer ? " xfer" : "");
      arucke = 1'b0; #49;
      $display("VSTATE acc=%05x pp=%05x m=%b%b s=%b%b zero_n=%b", acc, pp, m1, m0, s1, s0, zero_n);
      zero_n = 1'b1;
      state(1'b0, 1'b1, ~c6[3], ~c6[2]);         // AS1 (M invert)
      state(1'b1, 1'b1, ~c6[1], ~c6[0]);         // AS2 = LOAD for the NEXT cycle (Fig 3.4)
    end
  endtask

  integer i;
  reg [15:0] f_op, want;
  reg [5:0] c6;
  reg cs;
  initial begin
    $readmemh("out/aru_cases.memh", cases);
    $dumpfile("out/tb_aru_fig34_drive.vcd");
    $dumpvars(1, dut);
    ncases = 0;
    while (ncases < 1024 && cases[ncases][7:0] !== 8'hxx) ncases = ncases + 1;
    #100;
    for (i = 0; i < ncases; i = i + 1) begin
      f_op = cases[i][39:24]; c6 = cases[i][45:40]; cs = cases[i][48];
      want = cases[i][23:8];
      // write operand into regfile reg 0 (backplane WA0/=WA1/=1 -> /WA=00? polarity self-calibrates)
      dab_drv = f_op; dab_oe = 1'b1; wa0_n = 1'b1; wa1_n = 1'b1; #30;
      wstb_n = 1'b0; #60; wstb_n = 1'b1; #20; dab_oe = 1'b0; #30;
      ra0_n = 1'b1; ra1_n = 1'b1; #30;   // RA selects same reg
      // multiply cycle (controls: zero+xfer at its AS0 = flush of PREVIOUS),
      // then a flush cycle whose AS0 controls latch THIS result.
      prime_load;
      cycle(c6, cs, 1'b1, 1'b1);
      cycle(6'b000000, cs, 1'b1, 1'b1); // NOP flush; CSIGN stays valid into this cycle (Table 2); RR latches prior acc
      // read result register onto DAB
      rd_freg_n = 1'b0; #60;
      $display("ARES %0d f=%h c=%h cs=%b rr=%h want=%h sat=%b", i, f_op, c6, cs, dab, want, sat);
      rd_freg_n = 1'b1; #40;
      // drain: flush pipeline/carry state so cases are independent
      cycle(6'b000000, 1'b0, 1'b1, 1'b0);
      cycle(6'b000000, 1'b0, 1'b1, 1'b0);
    end
    $finish;
  end
endmodule
