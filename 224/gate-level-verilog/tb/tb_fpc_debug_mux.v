`timescale 1ns/1ps
// FPC self-test/debug multiplexer.
//
// In normal backplane service FPC_DBUG is held low by the T&C/ARU/DMEM set
// and U4 passes external RESET/, FPC_CK, RD_AD/, and WR_DA/. The manual's
// removed-boards self-test says FPC_DBUG floats high and U4 substitutes O2
// plus U5-decoded local controls. This tb proves that schematic boundary.
module tb_fpc_debug_mux;
  reg fpc_ck = 1'b0;
  reg fpc_dbug = 1'b1;
  reg reset_n = 1'b1;
  reg rd_ad_n = 1'b1;
  reg wr_da_n = 1'b1;
  reg o2 = 1'b0;
  reg [3:0] sda = 4'h0;

  wire fpc_ck_w, fpc_dbug_w, reset_n_w, rd_ad_n_w, wr_da_n_w, o2_w;
  wire [3:0] sda_w;
  wire [3:0] out;
  wire [15:0] dab;

  assign fpc_ck_w = fpc_ck;
  assign fpc_dbug_w = fpc_dbug;
  assign reset_n_w = reset_n;
  assign rd_ad_n_w = rd_ad_n;
  assign wr_da_n_w = wr_da_n;
  assign o2_w = o2;
  assign sda_w = sda;
  assign dab = 16'hzzzz;

  fpc_board_netlist dut(
    .DAB0(dab[0]), .DAB1(dab[1]), .DAB2(dab[2]), .DAB3(dab[3]),
    .DAB4(dab[4]), .DAB5(dab[5]), .DAB6(dab[6]), .DAB7(dab[7]),
    .DAB8(dab[8]), .DAB9(dab[9]), .DAB10(dab[10]), .DAB11(dab[11]),
    .DAB12(dab[12]), .DAB13(dab[13]), .DAB14(dab[14]), .DAB15(dab[15]),
    .FPC_CK(fpc_ck_w), .FPC_DBUG(fpc_dbug_w), .RESET_n(reset_n_w),
    .RD_AD_n(rd_ad_n_w), .WR_DA_n(wr_da_n_w), .O2(o2_w),
    .OUTA(out[0]), .OUTB(out[1]), .OUTC(out[2]), .OUTD(out[3]),
    .SDAA(sda_w[0]), .SDAB(sda_w[1]), .SDAC(sda_w[2]), .SDAD(sda_w[3])
  );

  wire [3:0] u7_q = {
    dut.Net_U6_H, dut.Net_U6_G, dut.Net_U42_I1a, dut.Net_U6_E
  };
  wire [3:0] u8_q = {
    dut.Net_U6_D, dut.Net_U6_C, dut.Net_U6_B, dut.Net_U6_A
  };
  wire [7:0] cnt = {u7_q, u8_q};

  integer phase = 0;
  integer ext_cp_edges = 0;
  integer o2_cp_edges = 0;
  integer count_changes = 0;
  integer read_low = 0;
  integer write_low = 0;
  integer reset_low = 0;
  integer u42_enabled = 0;
  integer out_nonzero = 0;
  integer last_cnt = -1;
  integer i;

  always @(posedge dut.Untitled_Sheet_U16_CP) begin
    if (phase == 1)
      ext_cp_edges = ext_cp_edges + 1;
    else if (phase == 2)
      o2_cp_edges = o2_cp_edges + 1;
  end

  task pulse_ext_ck;
    begin
      fpc_ck = 1'b1; #80;
      fpc_ck = 1'b0; #80;
    end
  endtask

  task pulse_o2;
    begin
      o2 = 1'b1; #80;
      o2 = 1'b0; #80;
    end
  endtask

  task sample_cycle;
    input integer cyc;
    begin
      if (last_cnt >= 0 && cnt !== last_cnt[7:0])
        count_changes = count_changes + 1;
      last_cnt = cnt;
      if (dut.Net_U25A_OE === 1'b0)
        read_low = read_low + 1;
      if (dut.Net_U36_E === 1'b0)
        write_low = write_low + 1;
      if (dut.Net_U4_Za === 1'b0)
        reset_low = reset_low + 1;
      if (dut.Net_U3A_Q === 1'b0)
        u42_enabled = u42_enabled + 1;
      if (out !== 4'b0000)
        out_nonzero = out_nonzero + 1;
      $display("DMUX cyc=%0d cnt=%02h cp=%b reset_n=%b read_oe_n=%b write_load_n=%b i1a=%b i1c=%b u42_en_n=%b out=%b%b%b%b",
               cyc, cnt, dut.Untitled_Sheet_U16_CP, dut.Net_U4_Za,
               dut.Net_U25A_OE, dut.Net_U36_E,
               dut.Net_U4_I1a, dut.Net_U4_I1c, dut.Net_U3A_Q,
               out[3], out[2], out[1], out[0]);
    end
  endtask

  initial begin
    #200;
    $display("DMUX start cnt=%02h cp=%b reset_n=%b read_oe_n=%b write_load_n=%b",
             cnt, dut.Untitled_Sheet_U16_CP, dut.Net_U4_Za,
             dut.Net_U25A_OE, dut.Net_U36_E);

    phase = 1;
    last_cnt = cnt;
    for (i = 0; i < 8; i = i + 1)
      pulse_ext_ck();
    #80;
    $display("DMUX ext_ck_ignored cnt=%02h cp=%b ext_cp_edges=%0d",
             cnt, dut.Untitled_Sheet_U16_CP, ext_cp_edges);

    phase = 2;
    last_cnt = cnt;
    for (i = 0; i < 160; i = i + 1) begin
      pulse_o2();
      sample_cycle(i);
    end

    $display("DMUX summary ext_cp_edges=%0d o2_cp_edges=%0d count_changes=%0d read_low=%0d write_low=%0d reset_low=%0d u42_enabled=%0d out_nonzero=%0d final_cnt=%02h",
             ext_cp_edges, o2_cp_edges, count_changes,
             read_low, write_low, reset_low, u42_enabled, out_nonzero, cnt);
    $finish;
  end
endmodule
