`timescale 1ns/1ps
// Conformance stimulus for service-manual Fig 3.3 (DMEM Timing, 224X).
// Drives the structural Dmem-and-IO timing generator with the documented
// microinstruction frame (9 slots x 32.55 ns; DAB RSTB high slots 2..8,
// both edges ~6 ns after their slot boundary, inside Fig 3.3's <12 ns skew)
// and emits machine-readable EDGE lines for tools/check_dmem_io_timing.py.
module tb_dmem_io_timing;

  real SLOT;
  initial SLOT = 32.55;

  localparam integer NFRAMES = 8;
  integer f;

  reg a14 = 1'b0, memac = 1'b0, dab_rstb = 1'b0, ms1 = 1'b0, mc = 1'b0;
  reg reset_n = 1'b1;

  wire ras_n, cas0_n, cas1_n, row_sel, cpcclr, xack_n;
  wire wrl_xreg_n, wrh_xreg_n, rdl_xreg_n, rdh_xreg_n;

  // inout ports need net connections: shim regs/constants through wires
  wire hi = 1'b1;
  wire a14_w = a14, memac_w = memac, dab_rstb_w = dab_rstb;
  wire ms1_w = ms1, mc_w = mc, reset_n_w = reset_n;

  dmem_io_board_netlist dut(
    .A14(a14_w), .MEMAC(memac_w), .DAB_RSTB(dab_rstb_w), .MS1(ms1_w), .MC(mc_w),
    .RESET_n(reset_n_w),
    .IORC_n(hi), .IOWC_n(hi),
    .ADR0_n(hi), .ADR1_n(hi), .ADR2_n(hi), .ADR3_n(hi),
    .ADR4_n(hi), .ADR5_n(hi), .ADR6_n(hi), .ADR7_n(hi),
    .XACK_n(xack_n), .CPCCLR(cpcclr), .ROW_SEL(row_sel),
    .RAS_n(ras_n), .CAS0_n(cas0_n), .CAS1_n(cas1_n),
    .WRL_XREG_n(wrl_xreg_n), .WRH_XREG_n(wrh_xreg_n),
    .RDL_XREG_n(rdl_xreg_n), .RDH_XREG_n(rdh_xreg_n)
  );

  initial begin
    $dumpfile("out/tb_dmem_io_timing.vcd");
    $dumpvars(0, dut);
  end

  // Frame markers + MEMAC (last frames MEMAC low: CAS/ must stay high).
  initial begin
    #100;
    for (f = 0; f < NFRAMES; f = f + 1) begin
      $display("FRAME %0d start %0.2f MEMAC %b", f, $realtime, (f < 6));
      memac = (f < 6);
      #(9*SLOT);
    end
    $finish;
  end

  // DAB RSTB: rises 6 ns into slot 2, falls 6 ns into the next slot 0.
  initial begin
    #100;
    #(2*SLOT + 6.0);
    repeat (NFRAMES) begin
      dab_rstb = 1'b1;
      #(7*SLOT);
      dab_rstb = 1'b0;
      #(2*SLOT);
    end
  end

  always @(ras_n)
    $display("EDGE RAS %s %0.2f", ras_n === 1'b0 ? "fall" : ras_n === 1'b1 ? "rise" : "x", $realtime);
  always @(row_sel)
    $display("EDGE ROWSEL %s %0.2f", row_sel === 1'b1 ? "rise" : row_sel === 1'b0 ? "fall" : "x", $realtime);
  always @(cas0_n)
    $display("EDGE CAS0 %s %0.2f", cas0_n === 1'b0 ? "fall" : cas0_n === 1'b1 ? "rise" : "x", $realtime);
  always @(cas1_n)
    $display("EDGE CAS1 %s %0.2f", cas1_n === 1'b0 ? "fall" : cas1_n === 1'b1 ? "rise" : "x", $realtime);

endmodule
