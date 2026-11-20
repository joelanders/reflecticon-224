`timescale 1ns/1ps
// Narrow run-control probe for the structural T&C WCS address counter.
// Current stitch benches tie HALT high. E50/E96 needs diagnostic halt/single-
// step behavior, so this proves the netlist-visible HALT input at least gates
// WCSA advancement before a larger single-step harness is built.
module tb_tc_halt;

  reg mc = 1'b0;
  reg halt_drv = 1'b1;
  wire mc_w = mc;
  wire halt = halt_drv;
  wire hi = 1'b1;
  wire lo = 1'b0;
  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8, fpc_ck, aruck, as0, as1_n;
  wire dab_rstb, memac, xfer_ck;
  wire [6:0] wcsa;

  tc_board_netlist dut(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck), .SAT(lo), .HALT(halt),
    .MWTC(hi), .MRDC(hi), .ADR0(hi), .ADR1(hi),
    .WCSA0(wcsa[0]), .WCSA1(wcsa[1]), .WCSA2(wcsa[2]), .WCSA3(wcsa[3]),
    .WCSA4(wcsa[4]), .WCSA5(wcsa[5]), .WCSA6(wcsa[6])
  );

  always #16.275 mc = ~mc;


  task automatic sample(input [63:0] tag, input integer n);
    integer i;
    begin
      for (i = 0; i < n; i = i + 1) begin
        @(posedge ms6);
        #8;
        $display("HALT_SAMPLE tag=%0s i=%0d halt=%b wcsa=%0d", tag, i, halt, wcsa);
      end
    end
  endtask

  initial begin
    $dumpfile("out/tb_tc_halt.vcd");
    $dumpvars(0, tb_tc_halt);
    #(12 * 9 * 32.55);
    sample("run_a", 6);
    halt_drv = 1'b0;
    sample("hold", 6);
    halt_drv = 1'b1;
    sample("run_b", 6);
    $display("HALT_DONE");
    $finish;
  end
endmodule
