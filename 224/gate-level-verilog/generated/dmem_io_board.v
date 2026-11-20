`timescale 1ns/1ps
// Auto-generated from KiCad netlist
// Source: ../schematics/netlists/dmem-io.net

module dmem_io_board_netlist(A14, MEMAC, DAB_RSTB, MS1, MC, RESET_n, IORC_n, IOWC_n, XACK_n, CPCCLR, ROW_SEL, RAS_n, CAS0_n, CAS1_n, DPORT0_n, DPORT1_n, DPORT2_n, DPORT3_n, DPORT4_n, DPORT5_n, WRL_XREG_n, WRH_XREG_n, RDL_XREG_n, RDH_XREG_n, ADR0_n, ADR1_n, ADR2_n, ADR3_n, ADR4_n, ADR5_n, ADR6_n, ADR7_n);
  inout A14, MEMAC, DAB_RSTB, MS1, MC, RESET_n, IORC_n, IOWC_n, XACK_n, CPCCLR, ROW_SEL, RAS_n, CAS0_n, CAS1_n, DPORT0_n, DPORT1_n, DPORT2_n, DPORT3_n, DPORT4_n, DPORT5_n, WRL_XREG_n, WRH_XREG_n, RDL_XREG_n, RDH_XREG_n, ADR0_n, ADR1_n, ADR2_n, ADR3_n, ADR4_n, ADR5_n, ADR6_n, ADR7_n;
  // Net declarations
  wire n_5V; // +5V
  wire n_30ns; // /30ns
  wire n_60ns; // /60ns
  wire n_120ns; // /120ns
  wire n_150ns; // /150ns
  wire GND; // GND
  wire HALT_slash; // HALT{slash}
  wire HR1_slash; // HR1{slash}
  wire HR2_slash; // HR2{slash}
  wire Net_R1_Pad2; // Net-(R1-Pad2)
  wire Net_R2_Pad2; // Net-(R2-Pad2)
  wire Net_R6_Pad2; // Net-(R6-Pad2)
  wire Net_U43_Pad3; // Net-(U43-Pad3)
  wire Net_U43_Pad5; // Net-(U43-Pad5)
  wire Net_U43_Pad10; // Net-(U43-Pad10)
  wire Net_U44_Pad6; // Net-(U44-Pad6)
  wire Net_U44_Pad13; // Net-(U44-Pad13)
  wire Net_U46A_C; // Net-(U46A-C)
  wire Net_U46A_Q; // Net-(U46A-Q)
  wire Net_U46B_C; // Net-(U46B-C)
  wire Net_U46B_D; // Net-(U46B-D)
  wire Net_U46B_Q; // Net-(U46B-~{Q})
  wire Net_U46B_R; // Net-(U46B-~{R})
  wire Net_U47A_D; // Net-(U47A-D)
  wire Net_U52_Pad12; // Net-(U52-Pad12)
  wire Net_U53_Pad2; // Net-(U53-Pad2)
  wire Net_U53_Pad9; // Net-(U53-Pad9)
  wire Net_U53_Pad11; // Net-(U53-Pad11)
  wire Net_U53_Pad12; // Net-(U53-Pad12)
  wire Net_U55_Y2; // Net-(U55-~{Y2})
  wire Net_U55_Y4; // Net-(U55-~{Y4})
  wire Net_U55_Y5; // Net-(U55-~{Y5})
  wire Net_U55_Y6; // Net-(U55-~{Y6})
  wire Net_U55_Y7; // Net-(U55-~{Y7})
  wire Net_U57_G1; // Net-(U57-G1)
  wire Net_U58_Pad4; // Net-(U58-Pad4)
  wire Net_U60_Pad6; // Net-(U60-Pad6)
  wire U54B_OUT; // U54B_OUT
  wire unconnected_U43_Pad1; // unconnected-(U43-Pad1)
  wire unconnected_U43_Pad2; // unconnected-(U43-Pad2)
  wire unconnected_U43_Pad12; // unconnected-(U43-Pad12)
  wire unconnected_U43_Pad13; // unconnected-(U43-Pad13)
  wire unconnected_U44_Pad1; // unconnected-(U44-Pad1)
  wire unconnected_U44_Pad2; // unconnected-(U44-Pad2)
  wire unconnected_U44_Pad3; // unconnected-(U44-Pad3)
  wire unconnected_U45_Pad1; // unconnected-(U45-Pad1)
  wire unconnected_U45_Pad2; // unconnected-(U45-Pad2)
  wire unconnected_U45_Pad3; // unconnected-(U45-Pad3)
  wire unconnected_U45_Pad4; // unconnected-(U45-Pad4)
  wire unconnected_U45_Pad5; // unconnected-(U45-Pad5)
  wire unconnected_U45_Pad6; // unconnected-(U45-Pad6)
  wire unconnected_U46A_Q_Pad6; // unconnected-(U46A-~{Q}-Pad6)
  wire unconnected_U46A_R_Pad1; // unconnected-(U46A-~{R}-Pad1)
  wire unconnected_U46B_Q_Pad9; // unconnected-(U46B-Q-Pad9)
  wire unconnected_U55_Y3_Pad12; // unconnected-(U55-~{Y3}-Pad12)
  wire unconnected_U57_Y0_Pad15; // unconnected-(U57-~{Y0}-Pad15)
  wire unconnected_U57_Y1_Pad14; // unconnected-(U57-~{Y1}-Pad14)
  wire unconnected_U57_Y2_Pad13; // unconnected-(U57-~{Y2}-Pad13)
  wire unconnected_U57_Y3_Pad12; // unconnected-(U57-~{Y3}-Pad12)
  wire unconnected_U57_Y4_Pad11; // unconnected-(U57-~{Y4}-Pad11)
  wire unconnected_U57_Y5_Pad10; // unconnected-(U57-~{Y5}-Pad10)
  wire unconnected_U59_Pad1; // unconnected-(U59-Pad1)
  wire unconnected_U59_Pad3; // unconnected-(U59-Pad3)
  wire unconnected_U59_Pad5; // unconnected-(U59-Pad5)
  wire unconnected_U59_Pad7; // unconnected-(U59-Pad7)
  wire unconnected_U59_Pad13; // unconnected-(U59-Pad13)
  wire unconnected_U59_Pad15; // unconnected-(U59-Pad15)
  wire unconnected_U59_Pad17; // unconnected-(U59-Pad17)
  wire unconnected_U59_Pad19; // unconnected-(U59-Pad19)
  wire unconnected_U61_Pad1; // unconnected-(U61-Pad1)
  wire unconnected_U61_Pad6; // unconnected-(U61-Pad6)
  wire unconnected_U61_Pad8; // unconnected-(U61-Pad8)
  wire unconnected_U61_Pad9; // unconnected-(U61-Pad9)
  wire unconnected_U61_Pad11; // unconnected-(U61-Pad11)
  wire unconnected_U61_Pad12; // unconnected-(U61-Pad12)
  wire unconnected_U61_Pad14; // unconnected-(U61-Pad14)
  wire unconnected_U61_Pad18; // unconnected-(U61-Pad18)
  wire unconnected_U61_Pad19; // unconnected-(U61-Pad19)

  // Common rail assumptions
  assign n_5V = 1'b1;
  assign GND = 1'b0;

  // Floating TTL input pins read high (overridable per pin)
  assign unconnected_U43_Pad1 = 1'b1; // U43.1
  assign unconnected_U43_Pad2 = 1'b1; // U43.2
  assign unconnected_U43_Pad13 = 1'b1; // U43.13
  assign unconnected_U44_Pad1 = 1'b1; // U44.1
  assign unconnected_U44_Pad2 = 1'b1; // U44.2
  assign unconnected_U45_Pad1 = 1'b1; // U45.1
  assign unconnected_U45_Pad2 = 1'b1; // U45.2
  assign unconnected_U45_Pad4 = 1'b1; // U45.4
  assign unconnected_U45_Pad5 = 1'b1; // U45.5
  assign unconnected_U46A_R_Pad1 = 1'b1; // U46.1
  assign unconnected_U59_Pad1 = 1'b1; // U59.1
  assign unconnected_U59_Pad13 = 1'b1; // U59.13
  assign unconnected_U59_Pad15 = 1'b1; // U59.15
  assign unconnected_U59_Pad17 = 1'b1; // U59.17
  assign unconnected_U59_Pad19 = 1'b1; // U59.19
  assign unconnected_U61_Pad1 = 1'b0; // U61.1
  assign unconnected_U61_Pad6 = 1'b1; // U61.6
  assign unconnected_U61_Pad8 = 1'b1; // U61.8
  assign unconnected_U61_Pad11 = 1'b1; // U61.11
  assign unconnected_U61_Pad19 = 1'b0; // U61.19

  ideal_resistor R1 (.p_1(CAS1_n), .p_2(Net_R1_Pad2)); // R 33R
  ideal_resistor R2 (.p_1(CAS0_n), .p_2(Net_R2_Pad2)); // R 33R
  ideal_resistor R3 (.p_1(n_5V), .p_2(Net_U46B_R)); // R 1k
  ideal_resistor R4 (.p_1(RAS_n), .p_2(Net_U47A_D)); // R 33R
  ideal_resistor R6 (.p_1(n_5V), .p_2(Net_R6_Pad2)); // R 1k
  ttl_74ls10 #(.TPLH(3.0), .TPHL(3.0)) U43 (.p_1(unconnected_U43_Pad1), .p_2(unconnected_U43_Pad2), .p_3(Net_U43_Pad3), .p_4(Net_U43_Pad10), .p_5(Net_U43_Pad5), .p_6(Net_R2_Pad2), .p_7(GND), .p_8(Net_R1_Pad2), .p_9(Net_U43_Pad5), .p_10(Net_U43_Pad10), .p_11(GND), .p_12(unconnected_U43_Pad12), .p_13(unconnected_U43_Pad13), .p_14(n_5V)); // 74LS10 74LS10
  ttl_74ls00 #(.TPLH(3.0), .TPHL(3.0)) U44 (.p_1(unconnected_U44_Pad1), .p_2(unconnected_U44_Pad2), .p_3(unconnected_U44_Pad3), .p_4(n_150ns), .p_5(n_150ns), .p_6(Net_U44_Pad6), .p_8(Net_U44_Pad13), .p_9(Net_U44_Pad6), .p_10(Net_U46A_C), .p_11(Net_U43_Pad10), .p_12(n_60ns), .p_13(Net_U44_Pad13)); // 74LS00 74LS00
  ttl_74ls08 #(.TPLH(4.75), .TPHL(4.75)) U45 (.p_1(unconnected_U45_Pad1), .p_2(unconnected_U45_Pad2), .p_3(unconnected_U45_Pad3), .p_4(unconnected_U45_Pad4), .p_5(unconnected_U45_Pad5), .p_6(unconnected_U45_Pad6), .p_7(GND), .p_8(Net_U47A_D), .p_9(n_30ns), .p_10(Net_U46A_Q), .p_11(ROW_SEL), .p_12(n_60ns), .p_13(n_30ns), .p_14(n_5V)); // 74LS08 74LS08
  ttl_74ls74 #(.TPD(9.0)) U46 (.p_1(unconnected_U46A_R_Pad1), .p_2(GND), .p_3(Net_U46A_C), .p_4(n_120ns), .p_5(Net_U46A_Q), .p_6(unconnected_U46A_Q_Pad6), .p_7(GND), .p_8(Net_U46B_Q), .p_9(unconnected_U46B_Q_Pad9), .p_10(Net_U46B_R), .p_11(Net_U46B_C), .p_12(Net_U46B_D), .p_13(Net_U46B_R), .p_14(n_5V)); // 74LS74 74LS74
  ttl_74ls03 U52 (.p_8(Net_R6_Pad2), .p_9(IORC_n), .p_10(IOWC_n), .p_11(XACK_n), .p_12(Net_U52_Pad12), .p_13(Net_R6_Pad2)); // 74LS03 74LS03
  ttl_74ls00 #(.TPLH(3.0), .TPHL(3.0)) U53 (.p_1(Net_U55_Y7), .p_2(Net_U53_Pad2), .p_3(Net_U53_Pad12), .p_4(Net_U53_Pad12), .p_5(Net_U55_Y6), .p_6(Net_U53_Pad2), .p_7(GND), .p_8(HALT_slash), .p_9(Net_U53_Pad9), .p_10(Net_U55_Y4), .p_11(Net_U53_Pad11), .p_12(Net_U53_Pad12), .p_13(RESET_n), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls20 U54 (.p_1(n_5V), .p_2(Net_U55_Y5), .p_4(Net_U53_Pad11), .p_5(HALT_slash), .p_6(Net_U53_Pad9), .p_8(U54B_OUT), .p_9(ADR4_n), .p_10(ADR5_n), .p_12(ADR7_n), .p_13(ADR6_n)); // 74LS20 74LS20
  ttl_74ls138 U55 (.p_1(ADR0_n), .p_2(ADR1_n), .p_3(ADR2_n), .p_4(U54B_OUT), .p_5(IOWC_n), .p_6(ADR3_n), .p_7(Net_U55_Y7), .p_8(GND), .p_9(Net_U55_Y6), .p_10(Net_U55_Y5), .p_11(Net_U55_Y4), .p_12(unconnected_U55_Y3_Pad12), .p_13(Net_U55_Y2), .p_14(WRL_XREG_n), .p_15(WRH_XREG_n), .p_16(n_5V)); // 74LS138_1 -> 74LS138 74LS138
  ttl_74ls138 U56 (.p_1(ADR0_n), .p_2(ADR1_n), .p_3(ADR2_n), .p_4(U54B_OUT), .p_5(IORC_n), .p_6(ADR3_n), .p_7(DPORT0_n), .p_8(GND), .p_9(DPORT1_n), .p_10(DPORT2_n), .p_11(DPORT3_n), .p_12(DPORT4_n), .p_13(DPORT5_n), .p_14(RDL_XREG_n), .p_15(RDH_XREG_n), .p_16(n_5V)); // 74LS138_1 -> 74LS138 74LS138
  ttl_74ls138 U57 (.p_1(ADR0_n), .p_2(ADR1_n), .p_3(ADR2_n), .p_4(U54B_OUT), .p_5(IORC_n), .p_6(Net_U57_G1), .p_7(HR1_slash), .p_8(GND), .p_9(HR2_slash), .p_10(unconnected_U57_Y5_Pad10), .p_11(unconnected_U57_Y4_Pad11), .p_12(unconnected_U57_Y3_Pad12), .p_13(unconnected_U57_Y2_Pad13), .p_14(unconnected_U57_Y1_Pad14), .p_15(unconnected_U57_Y0_Pad15), .p_16(n_5V)); // 74LS138_1 -> 74LS138 74LS138
  ttl_74ls04 #(.TPLH(3.0), .TPHL(3.0)) U58 (.p_3(Net_U43_Pad5), .p_4(Net_U58_Pad4), .p_5(ADR3_n), .p_6(Net_U57_G1), .p_7(GND), .p_8(Net_U43_Pad3), .p_9(GND), .p_10(Net_U52_Pad12), .p_11(U54B_OUT), .p_12(CPCCLR), .p_13(Net_U55_Y2), .p_14(n_5V)); // 74LS04 74LS04
  dlg308_delay_module U59 (.p_1(unconnected_U59_Pad1), .p_2(Net_U46A_Q), .p_3(unconnected_U59_Pad3), .p_4(n_30ns), .p_5(unconnected_U59_Pad5), .p_6(n_60ns), .p_7(unconnected_U59_Pad7), .p_8(n_60ns), .p_9(n_150ns), .p_10(GND), .p_11(n_120ns), .p_12(n_120ns), .p_13(unconnected_U59_Pad13), .p_14(n_60ns), .p_15(unconnected_U59_Pad15), .p_16(n_60ns), .p_17(unconnected_U59_Pad17), .p_18(n_30ns), .p_19(unconnected_U59_Pad19), .p_20(n_5V)); // 74LS244_Split DLG308
  ttl_74ls244 #(.TPLH(12.0), .TPHL(12.0)) U61 (.p_1(unconnected_U61_Pad1), .p_2(A14), .p_3(Net_U46B_C), .p_4(MEMAC), .p_5(Net_U60_Pad6), .p_6(unconnected_U61_Pad6), .p_7(Net_U46A_C), .p_8(unconnected_U61_Pad8), .p_9(unconnected_U61_Pad9), .p_10(GND), .p_11(unconnected_U61_Pad11), .p_12(unconnected_U61_Pad12), .p_13(DAB_RSTB), .p_14(unconnected_U61_Pad14), .p_15(MS1), .p_16(Net_U43_Pad5), .p_17(MC), .p_18(unconnected_U61_Pad18), .p_19(unconnected_U61_Pad19), .p_20(n_5V)); // 74LS244_Split 74LS244_Split
endmodule

