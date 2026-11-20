`timescale 1ns/1ps
// Compact E50/E96 T&C + DMEM step probe.
//
// This is not a full E50 value replay: the physical CPU-DATA -> DSP-DAB
// ingress/result-owner path is still not present.  The helper below stands in
// for the result-register DAB owner only while the real E50 row family asserts
// RDRREG/ and MEMW/.  Everything else in the write path is structural: T&C
// row decode, HALT park/release, DMEM-IO CPCCLR from port 5, MEMAC/DAB_RSTB,
// RESET/, OFST/, RAS/CAS, the current-position counters, and the DRAM array.
module tb_tc_dmem_single_step;

  reg mc = 1'b0;
  reg use_io_halt = 1'b0;
  wire mc_w = mc;
  wire halt;
  wire io_halt;
  wire hi = 1'b1;
  wire lo = 1'b0;
  wire zz = 1'bz;

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

  reg [7:0] adr_n_drv = 8'hff;
  reg iorc_drv = 1'b1;
  reg iowc_drv = 1'b1;
  wire [7:0] adr_n = adr_n_drv;
  wire iorc_n = iorc_drv;
  wire iowc_n = iowc_drv;
  wire ras_n, cas0_n, cas1_n, row_sel, cpcclr, xack_n;
  wire dport0_n, dport1_n, dport2_n, dport3_n, dport4_n, dport5_n;
  wire wrl_xreg_n, wrh_xreg_n, rdl_xreg_n, rdh_xreg_n;
  assign io_halt = io.HALT_slash;
  assign halt = use_io_halt ? io_halt : 1'b1;

  dmem_io_board_netlist io(
    .A14(zz), .MEMAC(memac), .DAB_RSTB(dab_rstb), .MS1(ms1), .MC(mc_w),
    .RESET_n(hi), .IORC_n(iorc_n), .IOWC_n(iowc_n), .XACK_n(xack_n),
    .CPCCLR(cpcclr), .ROW_SEL(row_sel), .RAS_n(ras_n), .CAS0_n(cas0_n),
    .CAS1_n(cas1_n), .DPORT0_n(dport0_n), .DPORT1_n(dport1_n),
    .DPORT2_n(dport2_n), .DPORT3_n(dport3_n), .DPORT4_n(dport4_n),
    .DPORT5_n(dport5_n), .WRL_XREG_n(wrl_xreg_n), .WRH_XREG_n(wrh_xreg_n),
    .RDL_XREG_n(rdl_xreg_n), .RDH_XREG_n(rdh_xreg_n),
    .ADR0_n(adr_n[0]), .ADR1_n(adr_n[1]), .ADR2_n(adr_n[2]), .ADR3_n(adr_n[3]),
    .ADR4_n(adr_n[4]), .ADR5_n(adr_n[5]), .ADR6_n(adr_n[6]), .ADR7_n(adr_n[7])
  );

  reg [15:0] owner_word = 16'h0000;
  reg owner_enable = 1'b0;
  wire owner_drive = owner_enable && (rdrreg_n === 1'b0) && (memw_n === 1'b0);
  wire [15:0] dab;
  genvar gi;
  generate
    for (gi = 0; gi < 16; gi = gi + 1) begin : dab_drive
      assign dab[gi] = owner_drive ? owner_word[gi] : 1'bz;
    end
  endgenerate

  reg cpc_clr_boot = 1'b0;
  wire cpc_clr_w = cpc_clr_boot ? 1'b1 : cpcclr;

  dmem_board_netlist dmem(
    .OFST0_n(ofst[0]), .OFST1_n(ofst[1]), .OFST2_n(ofst[2]), .OFST3_n(ofst[3]),
    .OFST4_n(ofst[4]), .OFST5_n(ofst[5]), .OFST6_n(ofst[6]), .OFST7_n(ofst[7]),
    .OFST8_n(ofst[8]), .OFST9_n(ofst[9]), .OFST10_n(ofst[10]), .OFST11_n(ofst[11]),
    .OFST12_n(ofst[12]), .OFST13_n(ofst[13]), .OFST14_n(ofst[14]), .OFST15_n(ofst[15]),
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .DATA0(), .DATA1(), .DATA2(), .DATA3(),
    .DATA4(), .DATA5(), .DATA6(), .DATA7(),
    .MEMW_n(memw_n), .RESET_n(reset_n), .CPC_CLR(cpc_clr_w),
    .RAS_n(ras_n), .CAS0_n(cas0_n), .CAS1_n(cas1_n), .ROW_SEL(row_sel),
    .WRL_XREG_n(wrl_xreg_n), .WRH_XREG_n(wrh_xreg_n),
    .RDL_XREG_n(rdl_xreg_n), .RDH_XREG_n(rdh_xreg_n),
    .WR_XREG_n(hi), .RD_XREG_n(hi),
    .DPORT0(dport0_n), .DPORT1(dport1_n), .DPORT2(dport2_n)
  );

  wire tc_dab_wstb_n = tc.DAB_WSTB_n;
  wire [15:0] cpc = {
    dmem.U65.b_q[3:0], dmem.U65.a_q[3:0],
    dmem.U51.b_q[3:0], dmem.U51.a_q[3:0]
  };
  wire [15:0] dmem_addr = {
    dmem.Untitled_Sheet_A15, dmem.Untitled_Sheet_A14,
    dmem.Untitled_Sheet_A13, dmem.Untitled_Sheet_A12,
    dmem.Untitled_Sheet_A11, dmem.Untitled_Sheet_A10,
    dmem.Untitled_Sheet_A9, dmem.Untitled_Sheet_A8,
    dmem.Untitled_Sheet_A7, dmem.Untitled_Sheet_A6,
    dmem.Untitled_Sheet_A5, dmem.Untitled_Sheet_A4,
    dmem.Untitled_Sheet_A3, dmem.Untitled_Sheet_A2,
    dmem.Untitled_Sheet_A1, dmem.Untitled_Sheet_A0
  };
  wire [15:0] dram_addr = {dmem.U20.row_addr, dmem.U20.col_addr};

  reg active = 1'b0;
  reg [63:0] active_tag = 64'h0;
  integer active_edges = 0;
  integer active_memacs = 0;
  reg [127:0] active_wstb_mask = 128'h0;
  reg [127:0] active_memw_mask = 128'h0;
  reg [127:0] active_rdr_mask = 128'h0;
  reg [127:0] active_owner_mask = 128'h0;

  always #16.275 mc = ~mc;


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
      #160;
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
      $display("E50D_WAIT step=%0d guard=%0d wcsa=%0d halt=%b cpc=%04h",
        step, guard, wcsa, halt, cpc);
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

  task automatic set_diag_addr(input [7:0] port);
    begin
      adr_n_drv = 8'hff;
      adr_n_drv[0] = ~port[0];
      adr_n_drv[1] = ~port[1];
      adr_n_drv[2] = ~port[2];
      adr_n_drv[3] = ~port[3];
    end
  endtask

  task automatic pulse_port_write(input [7:0] port, input [7:0] value);
    begin
      set_diag_addr(port);
      iorc_drv = 1'b1;
      iowc_drv = 1'b1;
      #80;
      $display("E50D_PORT_SETUP port=%0d value=%02h wcsa=%0d halt=%b iohalt=%b adr_n=%02h cpcclr=%b cpc=%04h",
        port, value, wcsa, halt, io_halt, adr_n, cpcclr, cpc);
      iowc_drv = 1'b0;
      #100;
      $display("E50D_PORT port=%0d value=%02h wcsa=%0d halt=%b iohalt=%b adr_n=%02h cpcclr=%b dports=%b%b%b%b%b%b cpc=%04h",
        port, value, wcsa, halt, io_halt, adr_n, cpcclr, dport5_n, dport4_n, dport3_n,
        dport2_n, dport1_n, dport0_n, cpc);
      #80;
      iowc_drv = 1'b1;
      adr_n_drv = 8'hff;
      #220;
      $display("E50D_PORT_DONE port=%0d value=%02h wcsa=%0d halt=%b iohalt=%b cpcclr=%b cpc=%04h",
        port, value, wcsa, halt, io_halt, cpcclr, cpc);
    end
  endtask

  always @(negedge tc_dab_wstb_n) begin
    if (active) begin
      #12;
      active_edges = active_edges + 1;
      active_wstb_mask[wcsa] = 1'b1;
      if (memac === 1'b1)
        active_memacs = active_memacs + 1;
      if (memw_n === 1'b0)
        active_memw_mask[wcsa] = 1'b1;
      if (rdrreg_n === 1'b0)
        active_rdr_mask[wcsa] = 1'b1;
      if (owner_drive === 1'b1)
        active_owner_mask[wcsa] = 1'b1;
      $display("E50D_STB tag=%0s row=%0d halt=%b cpc=%04h ofst=%04h sum=%04h dram=%04h dab=%04h known=%0d owner=%b memac=%b memw=%b rdr=%b rd_xreg=%b wr_xreg=%b reset=%b",
        active_tag, wcsa, halt, cpc, ofst, dmem_addr, dram_addr, dab,
        known16(dab), owner_drive, memac, memw_n, rdrreg_n, rd_xreg_n,
        wr_xreg_n, reset_n);
    end
  end

  always @(negedge cas0_n) begin
    if (active) begin
      #4;
      $display("E50D_MA tag=%0s row=%0d halt=%b cpc=%04h sum=%04h dram=%04h ofst=%04h dab=%04h known=%0d owner=%b memw=%b rdr=%b cas0=%b cas1=%b row_sel=%b",
        active_tag, wcsa, halt, cpc, dmem_addr, dram_addr, ofst, dab,
        known16(dab), owner_drive, memw_n, rdrreg_n, cas0_n, cas1_n, row_sel);
    end
  end

  task automatic run_step(input [63:0] tag, input [15:0] word);
    begin
      wait_for_step(7'd127);
      owner_word = word;
      owner_enable = 1'b1;
      active_tag = tag;
      active_edges = 0;
      active_memacs = 0;
      active_wstb_mask = 128'h0;
      active_memw_mask = 128'h0;
      active_rdr_mask = 128'h0;
      active_owner_mask = 128'h0;
      active = 1'b1;

      pulse_port_write(8'h03, word[7:0]);
      repeat (12) begin
        @(posedge ms6);
        #12;
      end

      active = 1'b0;
      owner_enable = 1'b0;
      $display("E50D_SUM tag=%0s word=%04h edges=%0d memacs=%0d wstb=%032h memw=%032h rdr=%032h owner=%032h cpc=%04h",
        tag, word, active_edges, active_memacs, active_wstb_mask,
        active_memw_mask, active_rdr_mask, active_owner_mask, cpc);
    end
  endtask

  initial begin
    $dumpfile("out/tb_tc_dmem_single_step.vcd");
    $dumpvars(0, tb_tc_dmem_single_step);

    load_e50_wcs();
    iorc_drv = 1'b1;
    iowc_drv = 1'b1;
    adr_n_drv = 8'hff;
    #(40 * 9 * 32.55);
    cpc_clr_boot = 1'b1;
    #120;
    cpc_clr_boot = 1'b0;
    wait_frames(4);

    wait_for_step(7'd127);
    use_io_halt = 1'b1;
    pulse_port_write(8'h02, 8'h00);
    pulse_port_write(8'h00, 8'h00);
    pulse_port_write(8'h05, 8'h00);
    pulse_port_write(8'h06, 8'h55);
    pulse_port_write(8'h07, 8'h55);
    run_step("warm", 16'h5555);
    pulse_port_write(8'h05, 8'h80);
    pulse_port_write(8'h06, 8'haa);
    pulse_port_write(8'h07, 8'haa);
    run_step("final", 16'haaaa);

    $display("E50D_DONE cpc=%04h", cpc);
    $finish;
  end
endmodule
