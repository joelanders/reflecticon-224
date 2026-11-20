`timescale 1ns/1ps
// Compact E50/E96 T&C run-control probe.
//
// This is the first structural piece needed before the delayed-XREG/DMEM
// value test.  The firmware slice writes only WCS[126:127], halts the DSP,
// then uses OUT 03h as a diagnostic step pulse.  This bench keeps the scope to
// the generated T&C board: park the real WCS counter with HALT low at row 127,
// release it for one frame, and record which rows assert the DAB/write control
// strobes under the actual board phasing.
module tb_tc_single_step;

  reg mc = 1'b0;
  reg halt_drv = 1'b1;
  wire mc_w = mc;
  wire halt = halt_drv;
  wire hi = 1'b1;
  wire lo = 1'b0;
  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8, fpc_ck, aruck, as0, as1_n;
  wire dab_rstb, memac, xfer_ck;
  wire [6:0] wcsa;
  wire [15:0] ofst;
  wire wr_xreg_n, wr_da_n, rd_xreg_n, rd_ad_n, memw_n, rdrreg_n;
  wire wa0_n, wa1_n, ra0_n, ra1_n, csign_n, zero_n, reset_n, xfer, wcs_cs;
  wire s0, s1, m0_n, m1_n;

  tc_board_netlist tc(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck), .SAT(lo), .HALT(halt),
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
    .CSIGN_n(csign_n), .ZERO_n(zero_n), .RESET_n(reset_n), .XFER(xfer),
    .WCS_CS(wcs_cs), .S0(s0), .S1(s1), .M0_n(m0_n), .M1_n(m1_n)
  );

  wire tc_dab_wstb_n = tc.DAB_WSTB_n;

  always #16.275 mc = ~mc;

  task automatic wait_frames(input integer n);
    integer k;
    begin
      for (k = 0; k < n; k = k + 1)
        @(posedge ms0);
      #80;
    end
  endtask

  task automatic wait_for_step(input [6:0] step);
    integer guard;
    begin
      guard = 0;
      while (wcsa !== step && guard < 1024) begin
        @(posedge ms6);
        #8;
        guard = guard + 1;
      end
      $display("E50_WAIT step=%0d guard=%0d wcsa=%0d halt=%b", step, guard, wcsa, halt);
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

  task automatic load_e50_wcs;
    integer row;
    begin
      for (row = 0; row < 128; row = row + 1)
        poke_wcs_row(row[6:0], 8'h00, 8'h00, 8'h00, 8'h00);
      poke_wcs_row(7'd126, 8'h00, 8'h00, 8'hfc, 8'h7c);
      poke_wcs_row(7'd127, 8'hff, 8'hdf, 8'hfe, 8'h7d);
    end
  endtask

  reg active = 1'b0;
  integer edge_total = 0;
  reg [127:0] wstb_mask = 128'h0;
  reg [127:0] rd_xreg_mask = 128'h0;
  reg [127:0] rdrreg_mask = 128'h0;
  reg [127:0] memw_mask = 128'h0;
  reg [127:0] wr_xreg_mask = 128'h0;
  reg [127:0] rd_ad_mask = 128'h0;
  reg [127:0] reset_mask = 128'h0;

  always @(negedge tc_dab_wstb_n) begin : e50_control_watch
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
        if (reset_n === 1'b0)
          reset_mask[row] = 1'b1;
        if (rd_xreg_n === 1'b0 || rdrreg_n === 1'b0 || memw_n === 1'b0 ||
            wr_xreg_n === 1'b0 || rd_ad_n === 1'b0 || reset_n === 1'b0) begin
          $display("E50EDGE a=%0d halt=%b rd_xreg=%b rdr=%b memw=%b wr_xreg=%b rd_ad=%b reset=%b ofst=%04h wa=%b%b ra=%b%b csign=%b zero=%b xfer=%b s=%b%b m=%b%b dab_rstb=%b memac=%b",
            wcsa, halt, rd_xreg_n, rdrreg_n, memw_n, wr_xreg_n, rd_ad_n,
            reset_n, ofst, wa1_n, wa0_n, ra1_n, ra0_n, csign_n, zero_n,
            xfer, s1, s0, m1_n, m0_n, dab_rstb, memac);
        end
      end
    end
  end

  task automatic sample_phase(input [63:0] tag, input integer n);
    integer i;
    begin
      for (i = 0; i < n; i = i + 1) begin
        @(posedge ms6);
        #8;
        $display("E50SAMPLE tag=%0s i=%0d halt=%b wcsa=%0d ofst=%04h memac=%b memw=%b rdr=%b rd_xreg=%b reset=%b xfer=%b csign=%b",
          tag, i, halt, wcsa, ofst, memac, memw_n, rdrreg_n, rd_xreg_n,
          reset_n, xfer, csign_n);
      end
    end
  endtask

  initial begin
    $dumpfile("out/tb_tc_single_step.vcd");
    $dumpvars(0, tc);

    load_e50_wcs();
    #(40 * 9 * 32.55);
    wait_frames(4);

    wait_for_step(7'd127);
    halt_drv = 1'b0;
    sample_phase("hold", 4);

    edge_total = 0;
    wstb_mask = 128'h0;
    rd_xreg_mask = 128'h0;
    rdrreg_mask = 128'h0;
    memw_mask = 128'h0;
    wr_xreg_mask = 128'h0;
    rd_ad_mask = 128'h0;
    reset_mask = 128'h0;
    active = 1'b1;

    @(posedge ms0);
    #2;
    halt_drv = 1'b1;
    sample_phase("release", 3);
    @(posedge ms0);
    #2;
    halt_drv = 1'b0;
    sample_phase("rehold", 4);

    active = 1'b0;
    $display("E50CTRL total=%0d wstb=%032h rd_xreg=%032h rdr=%032h memw=%032h wr_xreg=%032h rd_ad=%032h reset=%032h",
      edge_total, wstb_mask, rd_xreg_mask, rdrreg_mask, memw_mask,
      wr_xreg_mask, rd_ad_mask, reset_mask);
    $display("E50_DONE");
    $finish;
  end
endmodule
