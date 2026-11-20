`timescale 1ns/1ps
// T&C + DMEM-IO run-latch contract probe.
//
// This isolates the diagnostic run-control glue from the E50 data path:
// DMEM I/O U55/U53/U54 decode OUT 00/01/02/03 into HALT/, and the generated
// T&C WCS address counter consumes that signal as its HALT/run enable.  The
// startup free-run lets the bench park at row 127 without inventing a T&C
// preset path; after that, HALT is driven only by the generated DMEM-IO latch.
module tb_tc_run_latch;

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
  wire tc_dab_rstb_n = tc.DAB_RSTB_slash;
  wire [3:0] wcsa_hi_d = {tc.U1.p_6, tc.U1.p_5, tc.U1.p_4, tc.U1.p_3};
  wire [3:0] wcsa_lo_d = {tc.U14.p_6, tc.U14.p_5, tc.U14.p_4, tc.U14.p_3};
  wire wcsa_hi_pe_n = tc.U1.p_9;
  wire wcsa_lo_pe_n = tc.U14.p_9;
  wire wcsa_hi_cep = tc.U1.p_7;
  wire wcsa_lo_cep = tc.U14.p_7;
  wire wcsa_hi_cet = tc.U1.p_10;
  wire wcsa_lo_cet = tc.U14.p_10;

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

  always #16.275 mc = ~mc;

  task automatic wait_frames(input integer n);
    integer k;
    begin
      for (k = 0; k < n; k = k + 1)
        @(posedge ms0);
      #100;
    end
  endtask

  task automatic wait_for_step(input [6:0] step, input [255:0] tag);
    integer guard;
    begin
      guard = 0;
      while (wcsa !== step && guard < 1024) begin
        @(posedge ms6);
        #8;
        guard = guard + 1;
      end
      $display("RUNL_WAIT tag=%0s step=%0d guard=%0d wcsa=%0d useio=%b halt=%b iohalt=%b",
        tag, step, guard, wcsa, use_io_halt, halt, io_halt);
      #20;
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

  task automatic print_port_state(input [255:0] tag, input [63:0] phase, input [7:0] port);
    begin
      $display("RUNL_PORT tag=%0s phase=%0s port=%0d wcsa=%0d useio=%b halt=%b iohalt=%b y0=%b y1=%b y2=%b y3=%b mode=%b%b net11=%b net9=%b mr=%b pehi=%b pelo=%b cephi=%b ceplo=%b cethi=%b cetlo=%b dhi=%b dlo=%b qhi=%h qlo=%h",
        tag, phase, port, wcsa, use_io_halt, halt, io_halt,
        io.Net_U55_Y7, io.Net_U55_Y6, io.Net_U55_Y5, io.Net_U55_Y4,
        io.Net_U53_Pad12, io.Net_U53_Pad2, io.Net_U53_Pad11, io.Net_U53_Pad9,
        tc.Net_U1_MR,
        wcsa_hi_pe_n, wcsa_lo_pe_n, wcsa_hi_cep, wcsa_lo_cep,
        wcsa_hi_cet, wcsa_lo_cet, wcsa_hi_d, wcsa_lo_d,
        tc.U1.q, tc.U14.q);
    end
  endtask

  task automatic pulse_port_write(input [255:0] tag, input [7:0] port);
    begin
      set_diag_addr(port);
      iorc_drv = 1'b1;
      iowc_drv = 1'b1;
      #70;
      print_port_state(tag, "setup", port);
      iowc_drv = 1'b0;
      #120;
      print_port_state(tag, "during", port);
      iowc_drv = 1'b1;
      #90;
      print_port_state(tag, "after", port);
      adr_n_drv = 8'hff;
      #140;
      print_port_state(tag, "idle", port);
    end
  endtask

  task automatic pulse_port_write_across_counter_clock(input [255:0] tag, input [7:0] port);
    begin
      set_diag_addr(port);
      iorc_drv = 1'b1;
      iowc_drv = 1'b1;
      #70;
      print_port_state(tag, "setup", port);
      iowc_drv = 1'b0;
      #80;
      print_port_state(tag, "armed", port);
      @(posedge tc_dab_rstb_n);
      #30;
      print_port_state(tag, "clocked", port);
      #100;
      iowc_drv = 1'b1;
      #90;
      print_port_state(tag, "after", port);
      adr_n_drv = 8'hff;
      #140;
      print_port_state(tag, "idle", port);
    end
  endtask

  task automatic sample_rows(input [255:0] tag, input integer n);
    integer k;
    begin
      for (k = 0; k < n; k = k + 1) begin
        @(posedge ms6);
        #8;
        $display("RUNL_SAMPLE tag=%0s i=%0d wcsa=%0d useio=%b halt=%b iohalt=%b mode=%b%b pehi=%b pelo=%b cephi=%b ceplo=%b cethi=%b cetlo=%b dhi=%b dlo=%b qhi=%h qlo=%h",
          tag, k, wcsa, use_io_halt, halt, io_halt,
          io.Net_U53_Pad12, io.Net_U53_Pad2,
          wcsa_hi_pe_n, wcsa_lo_pe_n, wcsa_hi_cep, wcsa_lo_cep,
          wcsa_hi_cet, wcsa_lo_cet, wcsa_hi_d, wcsa_lo_d,
          tc.U1.q, tc.U14.q);
      end
    end
  endtask

  initial begin
    $dumpfile("out/tb_tc_run_latch.vcd");
    $dumpvars(0, tb_tc_run_latch);

    iorc_drv = 1'b1;
    iowc_drv = 1'b1;
    adr_n_drv = 8'hff;
    use_io_halt = 1'b0;
    #(40 * 9 * 32.55);
    wait_frames(2);

    pulse_port_write("arm_single", 8'h00);
    wait_for_step(7'd127, "free_to_127");
    use_io_halt = 1'b1;
    #120;
    sample_rows("parked_127", 4);

    pulse_port_write("step_miss", 8'h03);
    sample_rows("after_step_miss", 4);

    pulse_port_write_across_counter_clock("step1_clocked", 8'h03);
    sample_rows("after_step1_clocked", 4);

    pulse_port_write_across_counter_clock("step2_clocked", 8'h03);
    sample_rows("after_step2_clocked", 4);

    pulse_port_write("arm_cont", 8'h01);
    pulse_port_write("release_cont", 8'h03);
    sample_rows("after_cont", 8);

    pulse_port_write("halt_after_cont", 8'h02);
    sample_rows("after_halt", 4);

    $display("RUNL_DONE");
    $finish;
  end
endmodule
