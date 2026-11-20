`timescale 1ns/1ps
// Stitched DMEM board (timing generator + DRAM array) write/read test.
// Frames follow service-manual Fig 3.3: write frames drive DIN on the DAB
// during slots 3..8 with MFMW/ low; read frames sample DAB in slot 7 where
// Fig 3.3 marks DOUT VALID. Address = position counter (0 after power-up)
// + OFST/ + 1 (S283 carry-in tied high through R5: position - offset).
// Emits RW lines for tools/check_dmem_read_write.py.
module tb_dmem_read_write;

  real SLOT;
  initial SLOT = 32.55;

  reg memac = 1'b0, dab_rstb = 1'b0, memw_n = 1'b1;
  reg [15:0] ofst_n = 16'hffff;
  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;

  wire hi = 1'b1;
  wire zz = 1'bz;
  wire memac_w = memac, dab_rstb_w = dab_rstb, memw_n_w = memw_n;
  wire [15:0] ofst_n_w = ofst_n;
  wire ras_n, cas0_n, cas1_n, row_sel, cpcclr, xack_n;
  reg cpc_clr_boot = 1'b0; // firmware clears the position counters at boot
  wire cpc_clr_w = cpc_clr_boot ? 1'b1 : cpcclr;
  wire wrl_xreg_n, wrh_xreg_n, rdl_xreg_n, rdh_xreg_n;
  wire [15:0] dab;

  genvar gi;
  generate
    for (gi = 0; gi < 16; gi = gi + 1) begin : dabdrv
      assign dab[gi] = dab_oe ? dab_drv[gi] : 1'bz;
    end
  endgenerate

  dmem_io_board_netlist io(
    .A14(zz), .MEMAC(memac_w), .DAB_RSTB(dab_rstb_w), .MS1(zz), .MC(zz),
    .RESET_n(hi),
    .IORC_n(hi), .IOWC_n(hi),
    .ADR0_n(hi), .ADR1_n(hi), .ADR2_n(hi), .ADR3_n(hi),
    .ADR4_n(hi), .ADR5_n(hi), .ADR6_n(hi), .ADR7_n(hi),
    .XACK_n(xack_n), .CPCCLR(cpcclr), .ROW_SEL(row_sel),
    .RAS_n(ras_n), .CAS0_n(cas0_n), .CAS1_n(cas1_n),
    .WRL_XREG_n(wrl_xreg_n), .WRH_XREG_n(wrh_xreg_n),
    .RDL_XREG_n(rdl_xreg_n), .RDH_XREG_n(rdh_xreg_n)
  );

  dmem_board_netlist array(
    .OFST0_n(ofst_n_w[0]), .OFST1_n(ofst_n_w[1]), .OFST2_n(ofst_n_w[2]), .OFST3_n(ofst_n_w[3]), .OFST4_n(ofst_n_w[4]), .OFST5_n(ofst_n_w[5]), .OFST6_n(ofst_n_w[6]), .OFST7_n(ofst_n_w[7]), .OFST8_n(ofst_n_w[8]), .OFST9_n(ofst_n_w[9]), .OFST10_n(ofst_n_w[10]), .OFST11_n(ofst_n_w[11]), .OFST12_n(ofst_n_w[12]), .OFST13_n(ofst_n_w[13]), .OFST14_n(ofst_n_w[14]), .OFST15_n(ofst_n_w[15]),
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]), .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]), .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]), .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .DATA0(), .DATA1(), .DATA2(), .DATA3(), .DATA4(), .DATA5(), .DATA6(), .DATA7(),
    .MEMW_n(memw_n_w), .RESET_n(hi), .CPC_CLR(cpc_clr_w),
    .RAS_n(ras_n), .CAS0_n(cas0_n), .CAS1_n(cas1_n), .ROW_SEL(row_sel),
    .WRL_XREG_n(wrl_xreg_n), .WRH_XREG_n(wrh_xreg_n),
    .RDL_XREG_n(rdl_xreg_n), .RDH_XREG_n(rdh_xreg_n),
    .WR_XREG_n(hi), .RD_XREG_n(hi),
    .DPORT0(hi), .DPORT1(hi), .DPORT2(hi)
  );

  initial begin
    $dumpfile("out/tb_dmem_read_write.vcd");
    $dumpvars(1, dab, ras_n, cas0_n, row_sel, memw_n_w);
  end

  // One Fig 3.3 frame. kind: 0=idle, 1=write, 2=read.
  task frame(input integer kind, input [15:0] addr_ofst_n, input [15:0] wdata,
             input [15:0] expect_data, input [127:0] tag);
    begin
      memac = (kind != 0);
      ofst_n = addr_ofst_n;
      #(2*SLOT + 6.0);                    // slot 2 (+6 ns strobe skew)
      dab_rstb = 1'b1;
      if (kind == 1) memw_n = 1'b0;       // MFMW/ low slots 2..8
      #(SLOT - 6.0);                      // slot 3
      if (kind == 1) begin dab_drv = wdata; dab_oe = 1'b1; end
      #(4.5*SLOT);                        // mid slot 7: DOUT valid window
      if (kind == 2)
        $display("RW %0s expect %h got %h", tag, expect_data, dab);
      if (kind == 0)
        $display("RW %0s bus %b", tag, dab);
      #(1.5*SLOT);                        // slot 0 boundary
      dab_rstb = 1'b0;
      memw_n = 1'b1;
      dab_oe = 1'b0;
      #(2*SLOT);                          // slots 0..1
    end
  endtask

  initial begin
    #50;
    cpc_clr_boot = 1'b1;  // firmware boot: clear current position counters
    #50;
    cpc_clr_boot = 1'b0;
    #50;
    frame(0, 16'hffff, 16'h0, 16'h0, "settle1");
    frame(0, 16'hffff, 16'h0, 16'h0, "settle2");
    frame(1, 16'hedcb, 16'hA55A, 16'h0, "writeA");   // addr = 0xedcc
    frame(1, 16'h1233, 16'h3CC3, 16'h0, "writeB");   // addr = 0x1234
    frame(0, 16'hffff, 16'h0, 16'h0, "idle_bus");
    frame(2, 16'hedcb, 16'h0, 16'hA55A, "readA");
    frame(2, 16'h1233, 16'h0, 16'h3CC3, "readB");
    frame(2, 16'h7775, 16'h0, 16'h0000, "readC_unwritten");
    $finish;
  end

endmodule
