`timescale 1ns/1ps

// Smallest schematic-shaped witness for the 224X v8.2.1 T&C signature table.
//
// U14 is the low PC nibble and U1 is the high PC nibble on schematic 060-02475.
// The service table clocks the HP 5004A from rising DAB_RSTB/ in the interval
// between consecutive falling RESETD edges.  Its +5V signature, FP54, proves
// that this aperture contains 30 selected
// clock edges.  The PRE sample is intentionally taken at the selected edge,
// before the LS163 clock-to-Q propagation delay; POST is a negative-control
// phase sampled after that delay.
module tb_hp5004a_tc_u1_p14;
  reg dab_rstb_n_drv = 1'b0;
  reg pc_mr_n_drv = 1'b0;
  wire dab_rstb_n = dab_rstb_n_drv;
  wire pc_mr_n = pc_mr_n_drv;
  wire hi = 1'b1;
  wire lo = 1'b0;
  wire pc0;
  wire pc1;
  wire pc2;
  wire pc3;
  wire pc4;
  wire pc5;
  wire pc6;
  wire low_rco;

  // Generated T&C U14 connectivity, reduced only at its board-visible pins.
  ttl_sn74s163n U14 (
    .p_1(pc_mr_n),
    .p_2(dab_rstb_n),
    .p_3(lo), .p_4(lo), .p_5(lo), .p_6(lo),
    .p_7(hi), .p_8(lo), .p_9(hi), .p_10(hi),
    .p_11(pc3), .p_12(pc2), .p_13(pc1), .p_14(pc0),
    .p_15(low_rco), .p_16(hi)
  );

  // Generated T&C U1 connectivity.  Pin 10 is U14's terminal-count output;
  // pin 14 is the manual's 3U9F pilot node (HIGH_SPEED1_PC4).
  ttl_sn74s163n U1 (
    .p_1(pc_mr_n),
    .p_2(dab_rstb_n),
    .p_3(lo), .p_4(lo), .p_5(lo), .p_6(lo),
    .p_7(hi), .p_8(lo), .p_9(hi), .p_10(low_rco),
    .p_11(), .p_12(pc6), .p_13(pc5), .p_14(pc4),
    .p_15(), .p_16(hi)
  );

  task pulse_without_measurement;
    begin
      #25 dab_rstb_n_drv = 1'b1;
      #25 dab_rstb_n_drv = 1'b0;
    end
  endtask

  integer i;
  initial begin
    // LS163 clear is synchronous.  This represents the RESET interval before
    // the analyzer START edge and makes the initial PC state explicit.
    pulse_without_measurement();
    pc_mr_n_drv = 1'b1;

    for (i = 0; i < 30; i = i + 1) begin
      #25 dab_rstb_n_drv = 1'b1;
      $display(
        "HP_SAMPLE phase=pre index=%0d rail=1 ground=0 p10=%b p14=%b p13=%b p12=%b u14p16=1 u14p14=%b u14p13=%b u14p12=%b u14p11=%b u14p10=1 u14p9=1 u14p8=0 u14p7=1 low=%0d high=%0d",
        i, low_rco, pc4, pc5, pc6, pc0, pc1, pc2, pc3, U14.q, U1.q
      );
      #13;
      $display(
        "HP_SAMPLE phase=post index=%0d rail=1 ground=0 p10=%b p14=%b p13=%b p12=%b u14p16=1 u14p14=%b u14p13=%b u14p12=%b u14p11=%b u14p10=1 u14p9=1 u14p8=0 u14p7=1 low=%0d high=%0d",
        i, low_rco, pc4, pc5, pc6, pc0, pc1, pc2, pc3, U14.q, U1.q
      );
      #12 dab_rstb_n_drv = 1'b0;
    end

    $finish;
  end
endmodule
