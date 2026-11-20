`timescale 1ns/1ps
// T&C + FPC stitch: a tiny WCS image lets the structural T&C generate
// FPC_CK, WR_DA/, RD_AD/, RESET/, and SDA into the structural FPC board.
// The tb supplies only data-side fixtures: a DAB word during the WR_DA row and
// a prepared FPC read word at U25/U26 for the RD_AD row.
module tb_tc_fpc_output;
  reg mc = 1'b0;
  reg clk_new = 1'b0;
  wire mc_w = mc, clk_new_w = clk_new;
  wire hi = 1'b1;
  wire lo = 1'b0;

  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8, fpc_ck, aruck, as0, as1_n;
  wire dab_rstb, memac, xfer_ck;
  wire [6:0] wcsa;
  wire [15:0] ofst;
  wire wr_xreg_n, wr_da_n, rd_xreg_n, rd_ad_n, memw_n, rdrreg_n;
  wire wa0_n, wa1_n, ra0_n, ra1_n, csign_n, zero_n, reset_n, xfer, wcs_cs;
  wire s0, s1, m0_n, m1_n;
  wire sdaa, sdab, sdac, sdad;
  wire [3:0] out;

  tc_board_netlist tc(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck), .SAT(lo), .HALT(hi),
    .MWTC(hi), .MRDC(hi), .ADR0(hi), .ADR1(hi),
    .WCSA0(wcsa[0]), .WCSA1(wcsa[1]), .WCSA2(wcsa[2]), .WCSA3(wcsa[3]),
    .WCSA4(wcsa[4]), .WCSA5(wcsa[5]), .WCSA6(wcsa[6]),
    .OFST0(ofst[0]), .OFST1(ofst[1]), .OFST2(ofst[2]), .OFST3(ofst[3]),
    .OFST4(ofst[4]), .OFST5(ofst[5]), .OFST6(ofst[6]), .OFST7(ofst[7]),
    .OFST8(ofst[8]), .OFST9(ofst[9]), .OFST10(ofst[10]), .OFST11(ofst[11]),
    .OFST12(ofst[12]), .OFST13(ofst[13]), .OFST14(ofst[14]), .OFST15(ofst[15]),
    .WR_XREG_n(wr_xreg_n), .WR_DA_n(wr_da_n), .RD_XREG_n(rd_xreg_n),
    .RD_AD_n(rd_ad_n), .SDAA(sdaa), .SDAB(sdab), .SDAC(sdac), .SDAD(sdad),
    .MEMW_n(memw_n), .RDRREG_n(rdrreg_n),
    .WA0_n(wa0_n), .WA1_n(wa1_n), .RA0_n(ra0_n), .RA1_n(ra1_n),
    .CSIGN_n(csign_n), .ZERO_n(zero_n), .RESET_n(reset_n), .XFER(xfer),
    .WCS_CS(wcs_cs), .S0(s0), .S1(s1), .M0_n(m0_n), .M1_n(m1_n)
  );

  always #16.275 mc = ~mc;

  // CLK_NEW is DAB_RSTB/ (2026-07-05 net-merge patch): the T&C board now
  // self-clocks its MI latches from its own DAB_RSTB/ driver, so this tb no
  // longer drives tc.CLK_NEW. clk_new survives only as a follower feeding
  // the separate fpc_board_netlist's O2 port below.
  always @(tc.DAB_RSTB_slash) clk_new = tc.DAB_RSTB_slash;

  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;
  wire [15:0] dab;
  genvar gi;
  generate for (gi = 0; gi < 16; gi = gi + 1) begin : dab_bits
    assign dab[gi] = dab_oe ? dab_drv[gi] : 1'bz;
  end endgenerate

  fpc_board_netlist fpc(
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .FPC_CK(fpc_ck), .FPC_DBUG(lo), .RESET_n(reset_n),
    .RD_AD_n(rd_ad_n), .WR_DA_n(wr_da_n), .O2(clk_new_w),
    .OUTA(out[0]), .OUTB(out[1]), .OUTC(out[2]), .OUTD(out[3]),
    .SDAA(sdaa), .SDAB(sdab), .SDAC(sdac), .SDAD(sdad)
  );

  wire [15:0] captured_word = {
    fpc.Net_U23_P0, fpc.Net_U23_P1, fpc.Net_U23_P2, fpc.Net_U23_P3,
    fpc.Net_U24_P0, fpc.Net_U24_P1, fpc.Net_U24_P2, fpc.Net_U24_P3,
    fpc.Net_U34_P0, fpc.Net_U34_P1, fpc.Net_U34_P2, fpc.Net_U34_P3,
    fpc.Net_U35_P0, fpc.Net_U35_P1, fpc.Net_U35_P2, fpc.Net_U35_P3
  };
  wire [3:0] captured_sda = {
    fpc.Net_U40_QD, fpc.Net_U40_QC, fpc.Net_U40_QB, fpc.Net_U40_QA
  };

  task set_prepared_word_1357;
    begin
      force fpc.Net_U25A_I0 = 1'b0;
      force fpc.Net_U25A_I1 = 1'b1;
      force fpc.Net_U25A_I2 = 1'b1;
      force fpc.Net_U25A_I3 = 1'b1;
      force fpc.Net_U25B_I0 = 1'b0;
      force fpc.Net_U25B_I1 = 1'b1;
      force fpc.Net_U25B_I2 = 1'b0;
      force fpc.Net_U25B_I3 = 1'b1;
      force fpc.Net_U26A_I0 = 1'b0;
      force fpc.Net_U26A_I1 = 1'b0;
      force fpc.Net_U26A_I2 = 1'b1;
      force fpc.Net_U26A_I3 = 1'b1;
      force fpc.Net_U26B_I0 = 1'b0;
      force fpc.Net_U26B_I1 = 1'b0;
      force fpc.Net_U26B_I2 = 1'b0;
      force fpc.Net_U26B_I3 = 1'b1;
    end
  endtask

  integer frames = 0;
  initial begin
    set_prepared_word_1357();
    forever begin
      @(posedge ms6);
      #8;
      frames = frames + 1;
      if (frames > 180) $finish;
      $display("TF a=%0d f=%0d dab_rstb=%b fpc_ck=%b wr_da_n=%b rd_ad_n=%b reset_n=%b sda=%b%b%b%b dab=%h cap=%h sel=%h out=%b%b%b%b u42_en_n=%b",
               wcsa, frames, dab_rstb, fpc.Untitled_Sheet_U16_CP, wr_da_n, rd_ad_n, reset_n,
               sdad, sdac, sdab, sdaa, dab, captured_word, captured_sda,
               out[3], out[2], out[1], out[0], fpc.STROBE_n);
    end
  end

  always @(negedge wr_da_n) begin
    #2;
    if (frames > 8 && wr_da_n === 1'b0) begin
      dab_drv = 16'hA55A;
      dab_oe = 1'b1;
      #1;
      $display("TW a=%0d f=%0d dab_rstb=%b fpc_ck=%b wr_da_n=%b sda=%b%b%b%b dab=%h cap=%h sel=%h",
               wcsa, frames, dab_rstb, fpc.Untitled_Sheet_U16_CP, wr_da_n,
               sdad, sdac, sdab, sdaa, dab, captured_word, captured_sda);
    end
  end

  always @(posedge wr_da_n) begin
    #2;
    if (wr_da_n === 1'b1) dab_oe = 1'b0;
  end

  always @(posedge fpc.Untitled_Sheet_U16_CP) begin
    #35;
    if (frames > 8 && wr_da_n === 1'b0) begin
      $display("TCAP a=%0d f=%0d dab_rstb=%b wr_da_n=%b sda=%b%b%b%b dab=%h cap=%h sel=%h",
               wcsa, frames, dab_rstb, wr_da_n,
               sdad, sdac, sdab, sdaa, dab, captured_word, captured_sda);
    end
  end

  always @(negedge rd_ad_n) begin
    #45;
    if (frames > 8 && rd_ad_n === 1'b0) begin
      $display("TR a=%0d f=%0d dab_rstb=%b fpc_ck=%b rd_ad_n=%b dab=%h cap=%h sel=%h",
               wcsa, frames, dab_rstb, fpc.Untitled_Sheet_U16_CP, rd_ad_n, dab, captured_word, captured_sda);
    end
  end

  always @(out) begin
    #2;
    if (frames > 8 && out !== 4'b0000) begin
      $display("TO a=%0d f=%0d out=%b%b%b%b u42_en_n=%b cap=%h sel=%h",
               wcsa, frames, out[3], out[2], out[1], out[0],
               fpc.STROBE_n, captured_word, captured_sda);
    end
  end
endmodule
