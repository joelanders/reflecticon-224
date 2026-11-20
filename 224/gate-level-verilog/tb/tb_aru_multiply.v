`timescale 1ns/1ps
// ARU replay of the E8x control signatures captured from the firmware-driven
// T&C+ARU stitch. Unlike tb_aru_fig34_drive, this does not serialize coefficient
// bits from Fig 3.4/Table 2. It drives the five observed 21-clock control
// signatures from E8CTL: idle rows 124..0, three work taps at rows 1/2, then
// the row-2 XFER/ZERO capture.
module tb_aru_multiply;

  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;
  reg wstb_n = 1'b1, arucke = 1'b0, s0 = 1'b0, s1 = 1'b0;
  reg m0 = 1'b1, m1 = 1'b1, csign_n = 1'b1, zero_n = 1'b1;
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
  reg [48:0] cases [0:31];
  integer ncases;

  task automatic write_operand(input [15:0] op);
    begin
      // Firmware write path convention: CPU-side op appears complemented on
      // the ARU DAB pins in the live replay.
      dab_drv = ~op;
      dab_oe = 1'b1; wa0_n = 1'b1; wa1_n = 1'b1; #30;
      wstb_n = 1'b0; #60; wstb_n = 1'b1; #20; dab_oe = 1'b0; #30;
      ra0_n = 1'b1; ra1_n = 1'b1; #30;
    end
  endtask

  task automatic ctl_edge(
    input [1:0] s,
    input [1:0] m,
    input csign,
    input zero,
    input pulse_xfer
  );
    begin
      {s1, s0} = s;
      {m1, m0} = m;
      csign_n = csign;
      zero_n = zero;
      if (pulse_xfer) xfer_ck = 1'b0;
      #20;
      arucke = 1'b1; #45;
      if (pulse_xfer) xfer_ck = 1'b1;
      #4;
      arucke = 1'b0; #49;
    end
  endtask

  task automatic xfer_zero_edge(
    input capture_csign,
    input post_xfer_csign
  );
    begin
      {s1, s0} = 2'b01;
      {m1, m0} = 2'b11;
      csign_n = capture_csign;
      zero_n = 1'b0;
      xfer_ck = 1'b0;
      #20;
      arucke = 1'b1; #45;
      xfer_ck = 1'b1;
      csign_n = post_xfer_csign;
      #4;
      arucke = 1'b0; #49;
    end
  endtask

  task automatic idle_row;
    begin
      ctl_edge(2'b11, 2'b11, 1'b1, 1'b1, 1'b0);
      ctl_edge(2'b01, 2'b11, 1'b1, 1'b0, 1'b1);
      ctl_edge(2'b01, 2'b11, 1'b1, 1'b1, 1'b0);
    end
  endtask

  task automatic replay_signature(input [5:0] c6, input cs);
    reg [1:0] work_m;
    reg work_csign;
    integer k;
    begin
      if (c6 == 6'h15) begin
        work_m = 2'b10;
      end else if (c6 == 6'h2a) begin
        work_m = 2'b01;
      end else begin
        work_m = 2'b00;
      end
      // CSIGN/ is active low: a subtracting call (cs = 1) drives it low.
      // (Until 2026-09-27 this was inverted, and the checker compared the raw
      // RR with the table; the two errors cancelled except when the sum is a
      // multiple of 8, which is the old op=3fff "+1 boundary".)
      work_csign = (cs == 1'b1);

      // Rows 124,125,126,127,0: no PP taps, ZERO/XFER on the middle state.
      for (k = 0; k < 5; k = k + 1)
        idle_row;

      // Row 1: idle AS0, then two active taps.
      ctl_edge(2'b11, 2'b11, 1'b1, 1'b1, 1'b0);
      ctl_edge(2'b01, work_m, work_csign, 1'b1, 1'b0);
      ctl_edge(2'b01, work_m, work_csign, 1'b1, 1'b0);

      // Row 2: final active tap, then XFER/ZERO capture, then settle.
      ctl_edge(2'b11, work_m, work_csign, 1'b1, 1'b0);
      // T&C changes CSIGN/ back to idle before the logged ARUCK edge, but
      // XFER_CK's aperture still sees the work-row CSIGN/ level. This is
      // visible only on 7E/subtract rows; FE/add rows have the same level.
      xfer_zero_edge(work_csign, 1'b1);
      ctl_edge(2'b01, 2'b11, 1'b1, 1'b1, 1'b0);
    end
  endtask

  integer i;
  reg [15:0] f_op, want;
  reg [5:0] c6;
  reg cs;
  initial begin
    $readmemh("out/aru_cases.memh", cases);
    ncases = 0;
    while (ncases < 32 && cases[ncases][7:0] !== 8'hxx) ncases = ncases + 1;
    #100;
    for (i = 0; i < ncases; i = i + 1) begin
      f_op = cases[i][39:24]; c6 = cases[i][45:40]; cs = cases[i][48];
      want = cases[i][23:8];
      write_operand(f_op);
      replay_signature(c6, cs);
      rd_freg_n = 1'b0; #60;
      $display("ECTL %0d f=%h c=%h cs=%b rr=%h want=%h sat=%b",
               i, f_op, c6, cs, dab, want, sat);
      rd_freg_n = 1'b1; #40;
    end
    $finish;
  end
endmodule
