`timescale 1ns/1ps
// FPC board alone: its data-bus interface.
//
// This is intentionally narrower than the full FPC conversion path: it runs
// the structural FPC board in normal mode, proves that RD_AD/ and WR_DA/ pass
// through U4, proves that U25/U26 drive DAB on reads, and proves that
// U36/U37/U40 capture DAB/SDA on FPC_CK while WR_DA/ is active.
module tb_fpc_bus_interface;
  reg fpc_ck = 1'b0;
  reg fpc_dbug = 1'b0;
  reg reset_n = 1'b1;
  reg rd_ad_n = 1'b1;
  reg wr_da_n = 1'b1;
  reg o2 = 1'b0;
  reg [3:0] sda = 4'h0;
  wire fpc_ck_w, fpc_dbug_w, reset_n_w, rd_ad_n_w, wr_da_n_w, o2_w;
  wire [3:0] sda_w;

  assign fpc_ck_w = fpc_ck;
  assign fpc_dbug_w = fpc_dbug;
  assign reset_n_w = reset_n;
  assign rd_ad_n_w = rd_ad_n;
  assign wr_da_n_w = wr_da_n;
  assign o2_w = o2;
  assign sda_w = sda;

  reg [15:0] dab_drv = 16'h0000;
  reg dab_oe = 1'b0;
  wire [15:0] dab;

  genvar gi;
  generate for (gi = 0; gi < 16; gi = gi + 1) begin : dab_bits
    assign dab[gi] = dab_oe ? dab_drv[gi] : 1'bz;
  end endgenerate

  fpc_board_netlist dut(
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .FPC_CK(fpc_ck_w), .FPC_DBUG(fpc_dbug_w), .RESET_n(reset_n_w),
    .RD_AD_n(rd_ad_n_w), .WR_DA_n(wr_da_n_w), .O2(o2_w),
    .SDAA(sda_w[0]), .SDAB(sda_w[1]), .SDAC(sda_w[2]), .SDAD(sda_w[3])
  );

  wire [15:0] captured_word = {
    dut.Net_U23_P0, dut.Net_U23_P1, dut.Net_U23_P2, dut.Net_U23_P3,
    dut.Net_U24_P0, dut.Net_U24_P1, dut.Net_U24_P2, dut.Net_U24_P3,
    dut.Net_U34_P0, dut.Net_U34_P1, dut.Net_U34_P2, dut.Net_U34_P3,
    dut.Net_U35_P0, dut.Net_U35_P1, dut.Net_U35_P2, dut.Net_U35_P3
  };
  wire [3:0] captured_sda = {
    dut.Net_U40_QD, dut.Net_U40_QC, dut.Net_U40_QB, dut.Net_U40_QA
  };

  task set_prepared_word_1357;
    begin
      force dut.Net_U25A_I0 = 1'b0;
      force dut.Net_U25A_I1 = 1'b1;
      force dut.Net_U25A_I2 = 1'b1;
      force dut.Net_U25A_I3 = 1'b1;
      force dut.Net_U25B_I0 = 1'b0;
      force dut.Net_U25B_I1 = 1'b1;
      force dut.Net_U25B_I2 = 1'b0;
      force dut.Net_U25B_I3 = 1'b1;
      force dut.Net_U26A_I0 = 1'b0;
      force dut.Net_U26A_I1 = 1'b0;
      force dut.Net_U26A_I2 = 1'b1;
      force dut.Net_U26A_I3 = 1'b1;
      force dut.Net_U26B_I0 = 1'b0;
      force dut.Net_U26B_I1 = 1'b0;
      force dut.Net_U26B_I2 = 1'b0;
      force dut.Net_U26B_I3 = 1'b1;
    end
  endtask

  task pulse_fpc_ck;
    begin
      #10 fpc_ck = 1'b1;
      #80 fpc_ck = 1'b0;
      #80;
    end
  endtask

  task show(input [160:0] label);
    begin
      $display("FP label=%0s cp=%b read_oe_n=%b write_load_n=%b dab=%h cap=%h sda=%h",
               label, dut.Untitled_Sheet_U16_CP, dut.Net_U25A_OE, dut.Net_U36_E,
               dab, captured_word, captured_sda);
    end
  endtask

  initial begin
    set_prepared_word_1357();
    #80;

    dab_drv = 16'hA55A;
    dab_oe = 1'b1;
    #80;
    show("write_bus_driven");

    sda = 4'hA;
    wr_da_n = 1'b0;
    #35;
    pulse_fpc_ck();
    wr_da_n = 1'b1;
    #80;
    show("write_captured");

    dab_oe = 1'b0;
    #40;
    rd_ad_n = 1'b0;
    #80;
    show("read_bus_driven");

    rd_ad_n = 1'b1;
    #80;
    show("bus_release");
    $finish;
  end
endmodule
