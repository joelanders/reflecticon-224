`timescale 1ns/1ps
// Conformance stimulus for transcription Table 1 (T&C master timing).
// Injects MC at the PLL boundary (32.55 ns period) and emits EDGE lines for
// tools/check_tc_time_slots.py. The chain self-starts from power-on state; the
// checker grades steady-state frames only.
module tb_tc_time_slots;

  reg mc = 1'b0;
  wire mc_w = mc;
  wire sat = 1'b0;
  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8, fpc_ck, aruck, as0, as1_n;
  wire dab_rstb, memac, xfer_ck;

  tc_board_netlist dut(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck), .SAT(sat)
  );

  always #16.275 mc = ~mc;

  initial begin
    $dumpfile("out/tb_tc_time_slots.vcd");
    $dumpvars(0, dut);
    #(30 * 9 * 32.55);   // settle
    $display("WINDOW start %0.2f", $realtime);
    #(20 * 9 * 32.55);   // 20 graded frames
    $display("WINDOW end %0.2f", $realtime);
    $finish;
  end

  `define EDGE(sig, name) \
    always @(sig) $display("EDGE name %s %0.2f", \
      sig === 1'b1 ? "rise" : sig === 1'b0 ? "fall" : "x", $realtime);

  always @(ms0) $display("EDGE MS0 %s %0.2f", ms0 === 1'b1 ? "rise" : ms0 === 1'b0 ? "fall" : "x", $realtime);
  always @(ms1) $display("EDGE MS1 %s %0.2f", ms1 === 1'b1 ? "rise" : ms1 === 1'b0 ? "fall" : "x", $realtime);
  always @(ms2) $display("EDGE MS2 %s %0.2f", ms2 === 1'b1 ? "rise" : ms2 === 1'b0 ? "fall" : "x", $realtime);
  always @(ms4) $display("EDGE MS4 %s %0.2f", ms4 === 1'b1 ? "rise" : ms4 === 1'b0 ? "fall" : "x", $realtime);
  always @(ms6) $display("EDGE MS6 %s %0.2f", ms6 === 1'b1 ? "rise" : ms6 === 1'b0 ? "fall" : "x", $realtime);
  always @(ms7) $display("EDGE MS7 %s %0.2f", ms7 === 1'b1 ? "rise" : ms7 === 1'b0 ? "fall" : "x", $realtime);
  always @(ms8) $display("EDGE MS8 %s %0.2f", ms8 === 1'b1 ? "rise" : ms8 === 1'b0 ? "fall" : "x", $realtime);
  always @(aruck) $display("EDGE ARUCK %s %0.2f", aruck === 1'b1 ? "rise" : aruck === 1'b0 ? "fall" : "x", $realtime);
  always @(as0) $display("EDGE AS0 %s %0.2f", as0 === 1'b1 ? "rise" : as0 === 1'b0 ? "fall" : "x", $realtime);
  always @(as1_n) $display("EDGE AS1N %s %0.2f", as1_n === 1'b1 ? "rise" : as1_n === 1'b0 ? "fall" : "x", $realtime);
  always @(dab_rstb) $display("EDGE RSTB %s %0.2f", dab_rstb === 1'b1 ? "rise" : dab_rstb === 1'b0 ? "fall" : "x", $realtime);
  always @(fpc_ck) $display("EDGE FPCCK %s %0.2f", fpc_ck === 1'b1 ? "rise" : fpc_ck === 1'b0 ? "fall" : "x", $realtime);

endmodule
