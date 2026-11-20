`timescale 1ns/1ps
// E51..E7F T&C control-row probe.
//
// The E51/E7F firmware harness tests ARU register write/readback through a
// compact five-row WCS program.  This bench keeps the scope deliberately
// small: it runs those rows through the structural T&C and records which rows
// assert the bus/source control strobes.  It does not drive DAB or grade ARU
// values; the output is a row contract for the later T&C+ARU+DMEM replay.
module tb_tc_control_outputs;

  reg mc = 1'b0;
  wire mc_w = mc;
  wire hi = 1'b1;
  wire lo = 1'b0;
  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8, fpc_ck, aruck, as0, as1_n;
  wire dab_rstb, memac, xfer_ck;
  wire [6:0] wcsa;
  wire [15:0] ofst;
  wire wr_xreg_n, wr_da_n, rd_xreg_n, rd_ad_n, memw_n, rdrreg_n;
  wire wa0_n, wa1_n, ra0_n, ra1_n, csign_n, zero_n, xfer, wcs_cs;
  wire s0, s1, m0_n, m1_n;

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
    .RD_AD_n(rd_ad_n), .MEMW_n(memw_n), .RDRREG_n(rdrreg_n),
    .WA0_n(wa0_n), .WA1_n(wa1_n), .RA0_n(ra0_n), .RA1_n(ra1_n),
    .CSIGN_n(csign_n), .ZERO_n(zero_n), .XFER(xfer), .WCS_CS(wcs_cs),
    .S0(s0), .S1(s1), .M0_n(m0_n), .M1_n(m1_n)
  );

  wire tc_dab_wstb_n = tc.DAB_WSTB_n;

  always #16.275 mc = ~mc;

  task automatic wait_frames(input integer n);
    integer k;
    begin
      for (k = 0; k < n; k = k + 1)
        @(posedge ms0);
      #300;
    end
  endtask

  task automatic wait_for_step(input [6:0] step);
    integer guard;
    begin
      guard = 0;
      while (wcsa !== step && guard < 512) begin
        @(posedge ms6);
        #8;
        guard = guard + 1;
      end
      #20;
    end
  endtask

  task automatic poke_wcs_row(
    input [6:0] step,
    input [7:0] b0, input [7:0] b1, input [7:0] b2, input [7:0] b3
  );
    begin
      tc.U43.mem[step] = b0; if (wcsa === step) tc.U43.cell_d = b0;
      tc.U29.mem[step] = b1; if (wcsa === step) tc.U29.cell_d = b1;
      tc.U15.mem[step] = b2; if (wcsa === step) tc.U15.cell_d = b2;
      tc.U2.mem[step]  = b3; if (wcsa === step) tc.U2.cell_d  = b3;
    end
  endtask

  task automatic load_e51_wcs(input [7:0] b2);
    integer row;
    begin
      for (row = 0; row < 128; row = row + 1)
        poke_wcs_row(row[6:0], 8'hff, 8'hff, 8'hff, 8'hff);
      poke_wcs_row(7'd123, 8'hff, 8'hff, 8'hff, 8'hff);
      poke_wcs_row(7'd124, 8'hb7, 8'hef, 8'hfe, 8'hff);
      poke_wcs_row(7'd125, 8'hf7, 8'hff, 8'hfe, 8'hfe);
      poke_wcs_row(7'd126, 8'hff, 8'hdf, b2,    8'h7d);
      poke_wcs_row(7'd127, 8'hff, 8'hff, 8'hff, 8'hff);
    end
  endtask

  reg active = 1'b0;
  reg [7:0] active_b2 = 8'h00;
  integer active_idx = 0;
  integer edge_total = 0;
  reg [127:0] wstb_mask = 128'h0;
  reg [127:0] rd_xreg_mask = 128'h0;
  reg [127:0] rdrreg_mask = 128'h0;
  reg [127:0] memw_mask = 128'h0;
  reg [127:0] wr_xreg_mask = 128'h0;
  reg [127:0] rd_ad_mask = 128'h0;

  always @(negedge tc_dab_wstb_n) begin : control_watch
    integer row;
    begin
      if (active) begin
        #12;
        row = wcsa;
        edge_total = edge_total + 1;
        wstb_mask[row] = 1'b1;
        if (rd_xreg_n === 1'b0)
          rd_xreg_mask[row] = 1'b1;
        if (rdrreg_n === 1'b0)
          rdrreg_mask[row] = 1'b1;
        if (memw_n === 1'b0)
          memw_mask[row] = 1'b1;
        if (wr_xreg_n === 1'b0)
          wr_xreg_mask[row] = 1'b1;
        if (rd_ad_n === 1'b0)
          rd_ad_mask[row] = 1'b1;
        if (rd_xreg_n === 1'b0 || rdrreg_n === 1'b0 || memw_n === 1'b0 ||
            wr_xreg_n === 1'b0 || rd_ad_n === 1'b0) begin
          $display("E51EDGE idx=%0d b2=%02h a=%0d rd_xreg=%b rdr=%b memw=%b wr_xreg=%b rd_ad=%b wa=%b%b ra=%b%b csign=%b zero=%b s=%b%b m=%b%b dab_rstb=%b memac=%b",
            active_idx, active_b2, wcsa, rd_xreg_n, rdrreg_n, memw_n,
            wr_xreg_n, rd_ad_n, wa1_n, wa0_n, ra1_n, ra0_n,
            csign_n, zero_n, s1, s0, m1_n, m0_n, dab_rstb, memac);
        end
      end
    end
  end

  task automatic run_case(input integer idx, input [7:0] b2);
    integer guard;
    begin
      load_e51_wcs(b2);
      wait_frames(2);
      wait_for_step(7'd123);
      active_idx = idx;
      active_b2 = b2;
      edge_total = 0;
      wstb_mask = 128'h0;
      rd_xreg_mask = 128'h0;
      rdrreg_mask = 128'h0;
      memw_mask = 128'h0;
      wr_xreg_mask = 128'h0;
      rd_ad_mask = 128'h0;
      active = 1'b1;
      guard = 0;
      while (edge_total < 128 && guard < 512) begin
        @(posedge ms6);
        #20;
        guard = guard + 1;
      end
      active = 1'b0;
      $display("E51CTRL idx=%0d b2=%02h total=%0d guard=%0d wstb=%032h rd_xreg=%032h rdr=%032h memw=%032h wr_xreg=%032h rd_ad=%032h",
        idx, b2, edge_total, guard, wstb_mask, rd_xreg_mask, rdrreg_mask,
        memw_mask, wr_xreg_mask, rd_ad_mask);
    end
  endtask

  initial begin
    $dumpfile("out/tb_tc_control_outputs.vcd");
    $dumpvars(0, tc);

    #(40 * 9 * 32.55);
    wait_frames(4);

    run_case(0, 8'hc2);
    run_case(1, 8'hc6);
    run_case(2, 8'hca);
    run_case(3, 8'hce);
    run_case(4, 8'hde);
    run_case(5, 8'hee);
    run_case(6, 8'hfe);
    $finish;
  end
endmodule
