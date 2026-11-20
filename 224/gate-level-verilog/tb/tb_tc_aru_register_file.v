`timescale 1ns/1ps
// E51..E7F T&C + ARU register-file value probe.
//
// This is deliberately narrower than a full firmware replay. The generated
// DMEM/XREG island still lacks an honest CPU-DATA -> DSP-DAB ingress source,
// so the bench owns DAB during the one row-0 write aperture. What is
// structural here is the T&C row decode, the real row-0 DAB_WSTB/ edge, the
// ARU 74LS670 register-file write, and the later RA-selected F-bus read.
module tb_tc_aru_register_file;

`include "out/backplane_contract.vh"

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
  wire sat;

  tc_board_netlist tc(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck), .SAT(sat), .HALT(hi),
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

  always #16.275 mc = ~mc;

  wire arucke_pin = (R7_ARUCKE_INVERTS_TC_ARUCK != 0) ? ~aruck : aruck;
  wire tc_dab_wstb_n = tc.DAB_WSTB_n;

  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;
  reg row0_write_enable = 1'b0;
  wire [15:0] dab;
  // Re-keyed 2026-07-05 (schematic-true CLK_NEW, one-row prefetch): the
  // row-126 write command (b0/b1/b2/b3 poked at WCS row 126) now executes
  // when wcsa==127 instead of wcsa==0 (was the idle row's WA=11 under the
  // old two-bug model). See the check-suite sweep note / e8x TEMPLATE.
  wire aru_dab_wstb_n = (row0_write_enable && wcsa === 7'd127) ? tc_dab_wstb_n : hi;
  genvar gi;
  generate
    for (gi = 0; gi < 16; gi = gi + 1) begin : dab_drive
      assign dab[gi] = dab_oe ? dab_drv[gi] : 1'bz;
    end
  endgenerate

  aru_board_netlist aru(
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .DAB_WSTB_n(aru_dab_wstb_n), .S0(s0), .S1(s1), .M0(m0_n), .M1(m1_n),
    .CSIGN_n(csign_n), .ZERO_n(zero_n), .XFER_CK(xfer_ck),
    .RDRREG_n(hi), .ARUCKE(arucke_pin),
    .WA0_n(wa0_n), .WA1_n(wa1_n), .RA0_n(ra0_n), .RA1_n(ra1_n), .SAT(sat)
  );

  wire [15:0] f_bus = {aru.F15, aru.F14, aru.F13, aru.F12,
                       aru.F11, aru.F10, aru.F9,  aru.F8,
                       aru.F7,  aru.F6,  aru.F5,  aru.F4,
                       aru.F3,  aru.F2,  aru.F1,  aru.F0};

  function automatic [15:0] rf_word(input integer idx);
    reg [3:0] n0;
    reg [3:0] n1;
    reg [3:0] n2;
    reg [3:0] n3;
    begin
      n0 = {aru.U32.mem[idx][0], aru.U32.mem[idx][1],
            aru.U32.mem[idx][2], aru.U32.mem[idx][3]};
      n1 = {aru.U31.mem[idx][0], aru.U31.mem[idx][1],
            aru.U31.mem[idx][2], aru.U31.mem[idx][3]};
      n2 = {aru.U30.mem[idx][0], aru.U30.mem[idx][1],
            aru.U30.mem[idx][2], aru.U30.mem[idx][3]};
      n3 = {aru.U29.mem[idx][0], aru.U29.mem[idx][1],
            aru.U29.mem[idx][2], aru.U29.mem[idx][3]};
      rf_word = {n3, n2, n1, n0};
    end
  endfunction

  function automatic integer known16(input [15:0] value);
    integer bi;
    begin
      known16 = 1;
      for (bi = 0; bi < 16; bi = bi + 1)
        if (value[bi] !== 1'b0 && value[bi] !== 1'b1)
          known16 = 0;
    end
  endfunction

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

  integer write_seen = 0;
  integer active_seq = -1;
  reg [7:0] active_b2 = 8'h00;
  reg [15:0] active_value = 16'h0000;

  always @(negedge aru_dab_wstb_n) begin
    if (row0_write_enable) begin
      #40;
      write_seen = write_seen + 1;
      $display("E51RF_W seq=%0d b2=%02h a=%0d value=%04h dab=%04h wa=%b%b ra=%b%b f=%04h rf0=%04h rf1=%04h rf2=%04h rf3=%04h known=%0d rdr=%b memw=%b rd_xreg=%b wr_xreg=%b",
        active_seq, active_b2, wcsa, active_value, dab, wa1_n, wa0_n,
        ra1_n, ra0_n, f_bus, rf_word(0), rf_word(1), rf_word(2), rf_word(3),
        known16(f_bus), rdrreg_n, memw_n, rd_xreg_n, wr_xreg_n);
    end
  end

  task automatic run_write(input integer seq_i, input [7:0] b2, input [15:0] value);
    integer start_seen;
    integer guard;
    begin
      load_e51_wcs(b2);
      wait_frames(1);
      wait_for_step(7'd125);
      active_seq = seq_i;
      active_b2 = b2;
      active_value = value;
      dab_drv = value;
      dab_oe = 1'b1;
      start_seen = write_seen;
      row0_write_enable = 1'b1;
      guard = 0;
      while (write_seen == start_seen && guard < 512) begin
        @(posedge ms6);
        #10;
        guard = guard + 1;
      end
      #100;
      row0_write_enable = 1'b0;
      dab_oe = 1'b0;
      $display("E51RF_WDONE seq=%0d b2=%02h value=%04h delta=%0d guard=%0d rf0=%04h rf1=%04h rf2=%04h rf3=%04h",
        seq_i, b2, value, write_seen - start_seen, guard,
        rf_word(0), rf_word(1), rf_word(2), rf_word(3));
    end
  endtask

  task automatic run_read(input integer seq_i, input [7:0] b2, input [15:0] expected);
    begin
      load_e51_wcs(b2);
      wait_frames(1);
      wait_for_step(7'd127);
      // Sample before WCSA advances: with the 74F157 address muxes U28/U42
      // (2026-08-31) it moves at MS6 + 101 ns.
      #60;
      $display("E51RF_R seq=%0d b2=%02h a=%0d expect=%04h f=%04h wa=%b%b ra=%b%b known=%0d rf0=%04h rf1=%04h rf2=%04h rf3=%04h rdr=%b memw=%b rd_xreg=%b wr_xreg=%b",
        seq_i, b2, wcsa, expected, f_bus, wa1_n, wa0_n, ra1_n, ra0_n,
        known16(f_bus), rf_word(0), rf_word(1), rf_word(2), rf_word(3),
        rdrreg_n, memw_n, rd_xreg_n, wr_xreg_n);
    end
  endtask

  initial begin
    $dumpfile("out/tb_tc_aru_register_file.vcd");
    $dumpvars(0, tb_tc_aru_register_file);

    #(40 * 9 * 32.55);
    wait_frames(4);

    run_write(0, 8'hc2, 16'h5555);
    run_write(1, 8'hc6, 16'h6666);
    run_write(2, 8'hca, 16'h7777);
    run_write(3, 8'hce, 16'heeee);
    run_read(4, 8'hce, 16'h5555);
    run_read(5, 8'hde, 16'h6666);
    run_read(6, 8'hee, 16'h7777);
    run_read(7, 8'hfe, 16'heeee);

    run_write(8, 8'hc2, 16'haaaa);
    run_write(9, 8'hc6, 16'h9999);
    run_write(10, 8'hca, 16'h8888);
    run_write(11, 8'hce, 16'h1111);
    run_read(12, 8'hce, 16'haaaa);
    run_read(13, 8'hde, 16'h9999);
    run_read(14, 8'hee, 16'h8888);
    run_read(15, 8'hfe, 16'h1111);

    $display("E51RF_DONE");
    $finish;
  end
endmodule
