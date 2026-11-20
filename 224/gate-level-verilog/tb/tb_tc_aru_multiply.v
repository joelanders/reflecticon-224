`timescale 1ns/1ps
// T&C + ARU stitch: the structural T&C executes the E8x diagnostic
// multiply WCS program (out/wcs_lane_b*.hex from tools/gen_diag_wcs.py)
// and drives the structural ARU's clock and controls over the modeled
// backplane: ARUCKE/ (inverted ARUCK, EV-derived polarity), S0/S1,
// M0//M1/, CSIGN/, ZERO/, XFER CK, WA//RA/. The tb plays the Z80/DMEM
// side only: it injects MC and CLK_NEW, writes operands onto the DAB
// with DAB WSTB/, and reads the ARU result register back with RD FREG/
// once per frame, printing RR lines for tools/check_tc_aru_multiply.py.
module tb_tc_aru_multiply;

`include "out/backplane_contract.vh"

  reg mc = 1'b0;
  wire mc_w = mc;
  wire hi = 1'b1;
  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8, fpc_ck, aruck, as0, as1_n;
  wire dab_rstb, memac, xfer_ck;
  wire [6:0] wcsa;
  wire [15:0] ofst;
  wire wr_xreg_n, wr_da_n, rd_xreg_n, rd_ad_n, memw_n, rdrreg_n;
  wire wa0_n, wa1_n, ra0_n, ra1_n, csign_n, zero_n, xfer, wcs_cs;
  wire s0, s1, m0_n, m1_n;
  wire sat;

  tc_board_netlist tc(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck), .SAT(sat), .HALT(hi),
    .MWTC(hi), .MRDC(hi), .ADR0(hi), .ADR1(hi),
    .WCSA0(wcsa[0]), .WCSA1(wcsa[1]), .WCSA2(wcsa[2]), .WCSA3(wcsa[3]), .WCSA4(wcsa[4]), .WCSA5(wcsa[5]), .WCSA6(wcsa[6]), .OFST0(ofst[0]), .OFST1(ofst[1]), .OFST2(ofst[2]), .OFST3(ofst[3]), .OFST4(ofst[4]), .OFST5(ofst[5]), .OFST6(ofst[6]), .OFST7(ofst[7]), .OFST8(ofst[8]), .OFST9(ofst[9]), .OFST10(ofst[10]), .OFST11(ofst[11]), .OFST12(ofst[12]), .OFST13(ofst[13]), .OFST14(ofst[14]), .OFST15(ofst[15]), .WR_XREG_n(wr_xreg_n), .WR_DA_n(wr_da_n), .RD_XREG_n(rd_xreg_n), .RD_AD_n(rd_ad_n), .MEMW_n(memw_n), .RDRREG_n(rdrreg_n), .WA0_n(wa0_n), .WA1_n(wa1_n), .RA0_n(ra0_n), .RA1_n(ra1_n), .CSIGN_n(csign_n), .ZERO_n(zero_n), .XFER(xfer), .WCS_CS(wcs_cs),
    .S0(s0), .S1(s1), .M0_n(m0_n), .M1_n(m1_n)
  );

  always #16.275 mc = ~mc;


  // Backplane ARUCKE/: inverted T&C ARUCK. Polarity from the EV pulse
  // capture: the ARU's internal CK (XOR with the R1 pullup) must rise at
  // the edge that XFER CK's rise trails by ~6ns, so the result register
  // latches the pre-edge sum.
  wire arucke_pin = (R7_ARUCKE_INVERTS_TC_ARUCK != 0) ? ~aruck : aruck;

  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;
  reg wstb_n = 1'b1, rd_freg_n = 1'b1;
  wire [15:0] dab;
  genvar gi;
  generate for (gi = 0; gi < 16; gi = gi + 1) begin : dd
    assign dab[gi] = dab_oe ? dab_drv[gi] : 1'bz;
  end endgenerate
  wire wstb_n_w = wstb_n, rd_freg_n_w = rd_freg_n;

  aru_board_netlist aru(
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .DAB_WSTB_n(wstb_n_w), .S0(s0), .S1(s1), .M0(m0_n), .M1(m1_n),
    .CSIGN_n(csign_n), .ZERO_n(zero_n), .XFER_CK(xfer_ck),
    .RDRREG_n(rd_freg_n_w), .ARUCKE(arucke_pin),
    .WA0_n(wa0_n), .WA1_n(wa1_n), .RA0_n(ra0_n), .RA1_n(ra1_n), .SAT(sat)
  );

  // Optional, bounded board-aperture trace for comparing this known-good
  // diagnostic multiply with the large natural-WCS runner.  The ordinary
  // stitch checker does not enable it, so its compact RR transcript and
  // grading surface remain unchanged.
  wire [15:0] aru_f_dbg = {
    aru.F15, aru.F14, aru.F13, aru.F12, aru.F11, aru.F10, aru.F9, aru.F8,
    aru.F7, aru.F6, aru.F5, aru.F4, aru.F3, aru.F2, aru.F1, aru.F0
  };
  wire [19:0] aru_sr_dbg = {
    aru.SR19, aru.SR18, aru.SR17, aru.SR16, aru.SR15,
    aru.SR14, aru.SR13, aru.SR12, aru.SR11, aru.SR10,
    aru.SR9, aru.SR8, aru.SR7, aru.SR6, aru.SR5,
    aru.SR4, aru.SR3, aru.SR2, aru.SR1, aru.SR0
  };
  wire [19:0] aru_pp_dbg = {
    aru.PP19, aru.PP18, aru.PP17, aru.PP16, aru.PP15,
    aru.PP14, aru.PP13, aru.PP12, aru.PP11, aru.PP10,
    aru.PP9, aru.PP8, aru.PP7, aru.PP6, aru.PP5,
    aru.PP4, aru.PP3, aru.PP2, aru.PP1, aru.PP0
  };
  wire [7:0] aru_u10_d_dbg = {
    aru.Net_U10_D7, aru.Net_U10_D6, aru.Net_U10_D5, aru.Net_U10_D4,
    aru.Net_U10_D3, aru.Net_U10_D2, aru.Net_U10_D1, aru.Net_U10_D0
  };
  wire [7:0] aru_u11_d_dbg = {
    aru.Net_U11_D7, aru.Net_U11_D6, aru.Net_U11_D5, aru.Net_U11_D4,
    aru.Net_U11_D3, aru.Net_U11_D2, aru.Net_U11_D1, aru.Net_U11_D0
  };
  wire [7:0] aru_u10_q_dbg = {
    aru.Net_U10_Q7, aru.Net_U10_Q6, aru.Net_U10_Q5, aru.Net_U10_Q4,
    aru.Net_U10_Q3, aru.Net_U10_Q2, aru.Net_U10_Q1, aru.Net_U10_Q0
  };
  wire [7:0] aru_u11_q_dbg = {
    aru.Net_U11_Q7, aru.Net_U11_Q6, aru.Net_U11_Q5, aru.Net_U11_Q4,
    aru.Net_U11_Q3, aru.Net_U11_Q2, aru.Net_U11_Q1, aru.Net_U11_Q0
  };
  wire [15:0] aru_rr_input_dbg = {
    aru.PP18, aru.PP17, aru.PP16, aru.PP15,
    aru.PP14, aru.PP13, aru.PP12, aru.PP11,
    aru.PP10, aru.PP9, aru.PP8, aru.PP7,
    aru.PP6, aru.PP5, aru.PP4, aru.PP3
  };
  wire [15:0] aru_rr_dbg = {
    aru.U43.q[2], aru.U43.q[3], aru.U43.q[0], aru.U43.q[1],
    aru.U43.q[6], aru.U43.q[7], aru.U43.q[4], aru.U43.q[5],
    aru.U44.q[2], aru.U44.q[3], aru.U44.q[0], aru.U44.q[1],
    aru.U44.q[6], aru.U44.q[7], aru.U44.q[4], aru.U44.q[5]
  };

  integer opi = -1;
  integer frames = 0;
  integer trace_aperture = 0;
  integer write_wcsa = 30;
  integer single_target_window = 0;
  reg trace_aperture_armed = 1'b0;
  initial begin
    if ($value$plusargs("TRACE_APERTURE=%d", trace_aperture) && trace_aperture) begin
      if (trace_aperture > 1) trace_aperture_armed = 1'b1;
      $display("AP_HEADER time_ps,tag,wcsa,op,s0,s1,m0_n,m1_n,as0,as1_n,aruck,cp,xfer,zero_n,f,sr,u10d,u11d,u10q,u11q,pp,rrin,rr");
    end
  end

  function automatic trace_row_visible;
    input [6:0] addr;
    begin
      trace_row_visible = (addr >= 7'd124) || (addr <= 7'd3);
    end
  endfunction

  task automatic log_aperture;
    input [8*20-1:0] tag;
    begin
      if (trace_aperture && trace_aperture_armed && trace_row_visible(wcsa)) begin
        $display("AP %0t,%0s,%0d,%0d,%b,%b,%b,%b,%b,%b,%b,%b,%b,%b,%h,%h,%h,%h,%h,%h,%h,%h,%h",
          $time, tag, wcsa, opi, s0, s1, m0_n, m1_n, as0, as1_n,
          aruck, aru.Net_U10_CP, xfer_ck, zero_n, aru_f_dbg, aru_sr_dbg,
          aru_u10_d_dbg, aru_u11_d_dbg, aru_u10_q_dbg, aru_u11_q_dbg,
          aru_pp_dbg, aru_rr_input_dbg, aru_rr_dbg);
      end
    end
  endtask

  always @(wcsa) log_aperture("wcsa");
  always @(aru.Net_U10_CP) log_aperture("shared_cp");
  always @(xfer_ck) log_aperture("xfer_ck");
  always @(s0 or s1 or m0_n or m1_n or as0 or as1_n)
    log_aperture("tc_controls");
  always @(aru_f_dbg) log_aperture("f");
  always @(aru_sr_dbg) log_aperture("sr");
  always @(aru_u10_d_dbg or aru_u11_d_dbg) log_aperture("product_d");
  always @(aru_u10_q_dbg or aru_u11_q_dbg) log_aperture("product_q");
  always @(aru_pp_dbg) log_aperture("pp");
  always @(aru_rr_dbg) log_aperture("rr");

  // Operand schedule: four E8x inputs (or the call-5 table via +OPSET).
  reg [15:0] ops [0:3];
  reg [15:0] op0_override = 16'h0000;
  integer opset = 0;
  initial begin
    if ($value$plusargs("OPSET=%d", opset) && opset == 2) begin
      ops[0] = 16'h3333; ops[1] = 16'hCCCC; ops[2] = 16'h3FFF; ops[3] = 16'hC000;
    end else begin
      ops[0] = 16'h5555; ops[1] = 16'hAAAA; ops[2] = 16'h6666; ops[3] = 16'h9999;
    end
    // Probe-only controls for mapping the register-write lead time under a
    // real WCS image.  Defaults preserve the diagnostic regression exactly.
    if ($value$plusargs("OP0=%h", op0_override)) ops[0] = op0_override;
    if ($value$plusargs("WRITE_WCSA=%d", write_wcsa)) begin end
    if ($value$plusargs("SINGLE_TARGET_WINDOW=%d", single_target_window)) begin end
  end

  initial begin
    #(40 * 9 * 32.55); // settle: several frames
    forever begin
      @(posedge ms6);
      #8;
      frames = frames + 1;
      // Read the ARU result register once per frame (Z80-side read).
      rd_freg_n = 1'b0; #40;
      $display("RR f=%0d a=%0d op=%0d v=%h sat=%b", frames, wcsa, opi, dab, sat);
      rd_freg_n = 1'b1; #10;
      // Advance the operand at the start of each 128-frame loop pass,
      // writing it into the ARU register file over the DAB.
      if (wcsa == write_wcsa[6:0] && (frames > 128 * (opi + 1) || opi < 0)) begin
        opi = opi + 1;
        if (opi > 3) $finish;
        // The diagnostic's write path (8080 -> XREG -> DAB) complements the
        // data; the ROM expected-value tables are calibrated to that, and
        // the readback path is transparent. Established by A/B on call 1:
        // complement-write reproduces the ROM table bit-exactly (raw write
        // reproduces table(~op)).
        dab_drv = (R7_DAB_WRITE_COMPLEMENT_REQUIRED != 0) ? ~ops[opi] : ops[opi];
        dab_oe = 1'b1; #30;
        wstb_n = 1'b0; #60; wstb_n = 1'b1; #20; dab_oe = 1'b0;
        if (trace_aperture && opi == 0) trace_aperture_armed = 1'b1;
        $display("OPW op=%0d v=%h at f=%0d wcsa=%0d", opi, ops[opi], frames, wcsa);
      end
    end
  end

  // For the lead-time probe, stop just after the first logical-step-126 XFER
  // following the manual write.  Ordinary regression runs leave this off.
  always @(posedge xfer_ck) begin
    if (single_target_window && opi >= 0 && wcsa == 7'd1) begin
      #12;
      $display("TARGET_XFER op=%0d src=%h wcsa=%0d f=%h pp=%h rrin=%h rr=%h",
        opi, ops[opi], wcsa, aru_f_dbg, aru_pp_dbg, aru_rr_input_dbg, aru_rr_dbg);
      $finish;
    end
  end

  initial begin
    #(700 * 128 * 9 * 32.55 / 100); // hard stop safety (~900 frames)
    $finish;
  end
endmodule
