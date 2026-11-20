`timescale 1ns/1ps
// Auto-generated from KiCad netlist
// Source: ../schematics/netlists/aru.net

module bb_C_Polarized(p_1, p_2);
  inout p_1, p_2;
endmodule

module aru_board_netlist(DAB0, DAB1, DAB2, DAB3, DAB4, DAB5, DAB6, DAB7, DAB8, DAB9, DAB10, DAB11, DAB12, DAB13, DAB14, DAB15, DAB_WSTB_n, S0, S1, M0, M1, CSIGN_n, ZERO_n, XFER_CK, RDRREG_n, ARUCKE, WA0_n, WA1_n, RA0_n, RA1_n, SAT);
  inout DAB0, DAB1, DAB2, DAB3, DAB4, DAB5, DAB6, DAB7, DAB8, DAB9, DAB10, DAB11, DAB12, DAB13, DAB14, DAB15, DAB_WSTB_n, S0, S1, M0, M1, CSIGN_n, ZERO_n, XFER_CK, RDRREG_n, ARUCKE, WA0_n, WA1_n, RA0_n, RA1_n, SAT;
  // Net declarations
  wire n_5V; // +5V
  wire AC0; // /AC0
  wire AC1; // /AC1
  wire AC2; // /AC2
  wire AC3; // /AC3
  wire AC4; // /AC4
  wire AC5; // /AC5
  wire AC6; // /AC6
  wire AC7; // /AC7
  wire AC8; // /AC8
  wire AC9; // /AC9
  wire AC10; // /AC10
  wire AC11; // /AC11
  wire AC12; // /AC12
  wire AC13; // /AC13
  wire AC14; // /AC14
  wire AC15; // /AC15
  wire AC16; // /AC16
  wire AC17; // /AC17
  wire AC18; // /AC18
  wire AC19; // /AC19
  wire F0; // /F0
  wire F1; // /F1
  wire F2; // /F2
  wire F3; // /F3
  wire F4; // /F4
  wire F5; // /F5
  wire F6; // /F6
  wire F7; // /F7
  wire F8; // /F8
  wire F9; // /F9
  wire F10; // /F10
  wire F11; // /F11
  wire F12; // /F12
  wire F13; // /F13
  wire F14; // /F14
  wire F15; // /F15
  wire PP0; // /PP0
  wire PP1; // /PP1
  wire PP2; // /PP2
  wire PP3; // /PP3
  wire PP4; // /PP4
  wire PP5; // /PP5
  wire PP6; // /PP6
  wire PP7; // /PP7
  wire PP8; // /PP8
  wire PP9; // /PP9
  wire PP10; // /PP10
  wire PP11; // /PP11
  wire PP12; // /PP12
  wire PP13; // /PP13
  wire PP14; // /PP14
  wire PP15; // /PP15
  wire PP16; // /PP16
  wire PP17; // /PP17
  wire PP18; // /PP18
  wire PP19; // /PP19
  wire RA0; // /RA0
  wire RA1; // /RA1
  wire SR0; // /SR0
  wire SR1; // /SR1
  wire SR2; // /SR2
  wire SR3; // /SR3
  wire SR4; // /SR4
  wire SR5; // /SR5
  wire SR6; // /SR6
  wire SR7; // /SR7
  wire SR8; // /SR8
  wire SR9; // /SR9
  wire SR10; // /SR10
  wire SR11; // /SR11
  wire SR12; // /SR12
  wire SR13; // /SR13
  wire SR14; // /SR14
  wire SR15; // /SR15
  wire SR16; // /SR16
  wire SR17; // /SR17
  wire SR18; // /SR18
  wire SR19; // /SR19
  wire WA0; // /WA0
  wire WA1; // /WA1
  wire GNDREF; // GNDREF
  wire Net_R1_Pad2; // Net-(R1-Pad2)
  wire Net_U2_Pad10; // Net-(U2-Pad10)
  wire Net_U2_Pad12; // Net-(U2-Pad12)
  wire Net_U10_CP; // Net-(U10-CP)
  wire Net_U10_D0; // Net-(U10-D0)
  wire Net_U10_D1; // Net-(U10-D1)
  wire Net_U10_D2; // Net-(U10-D2)
  wire Net_U10_D3; // Net-(U10-D3)
  wire Net_U10_D4; // Net-(U10-D4)
  wire Net_U10_D5; // Net-(U10-D5)
  wire Net_U10_D6; // Net-(U10-D6)
  wire Net_U10_D7; // Net-(U10-D7)
  wire Net_U10_Q0; // Net-(U10-Q0)
  wire Net_U10_Q1; // Net-(U10-Q1)
  wire Net_U10_Q2; // Net-(U10-Q2)
  wire Net_U10_Q3; // Net-(U10-Q3)
  wire Net_U10_Q4; // Net-(U10-Q4)
  wire Net_U10_Q5; // Net-(U10-Q5)
  wire Net_U10_Q6; // Net-(U10-Q6)
  wire Net_U10_Q7; // Net-(U10-Q7)
  wire Net_U11_D0; // Net-(U11-D0)
  wire Net_U11_D1; // Net-(U11-D1)
  wire Net_U11_D2; // Net-(U11-D2)
  wire Net_U11_D3; // Net-(U11-D3)
  wire Net_U11_D4; // Net-(U11-D4)
  wire Net_U11_D5; // Net-(U11-D5)
  wire Net_U11_D6; // Net-(U11-D6)
  wire Net_U11_D7; // Net-(U11-D7)
  wire Net_U11_Q0; // Net-(U11-Q0)
  wire Net_U11_Q1; // Net-(U11-Q1)
  wire Net_U11_Q2; // Net-(U11-Q2)
  wire Net_U11_Q3; // Net-(U11-Q3)
  wire Net_U11_Q4; // Net-(U11-Q4)
  wire Net_U11_Q5; // Net-(U11-Q5)
  wire Net_U11_Q6; // Net-(U11-Q6)
  wire Net_U11_Q7; // Net-(U11-Q7)
  wire Net_U12_D0; // Net-(U12-D0)
  wire Net_U12_D1; // Net-(U12-D1)
  wire Net_U12_D2; // Net-(U12-D2)
  wire Net_U12_D3; // Net-(U12-D3)
  wire Net_U12_Q0; // Net-(U12-Q0)
  wire Net_U12_Q1; // Net-(U12-Q1)
  wire Net_U12_Q2; // Net-(U12-Q2)
  wire Net_U12_Q3; // Net-(U12-Q3)
  wire Net_U13_A1; // Net-(U13-A1)
  wire Net_U13_A2; // Net-(U13-A2)
  wire Net_U13_A3; // Net-(U13-A3)
  wire Net_U13_A4; // Net-(U13-A4)
  wire Net_U13_B1; // Net-(U13-B1)
  wire Net_U13_B2; // Net-(U13-B2)
  wire Net_U13_B3; // Net-(U13-B3)
  wire Net_U13_B4; // Net-(U13-B4)
  wire Net_U13_C4; // Net-(U13-C4)
  wire Net_U14_Pad10; // Net-(U14-Pad10)
  wire Net_U19_B1; // Net-(U19-B1)
  wire Net_U19_B2; // Net-(U19-B2)
  wire Net_U19_B3; // Net-(U19-B3)
  wire Net_U19_B4; // Net-(U19-B4)
  wire Net_U19_C0; // Net-(U19-C0)
  wire Net_U19_C4; // Net-(U19-C4)
  wire Net_U19_S1; // Net-(U19-S1)
  wire Net_U19_S2; // Net-(U19-S2)
  wire Net_U19_S3; // Net-(U19-S3)
  wire Net_U19_S4; // Net-(U19-S4)
  wire Net_U20_B1; // Net-(U20-B1)
  wire Net_U20_B2; // Net-(U20-B2)
  wire Net_U20_B3; // Net-(U20-B3)
  wire Net_U20_B4; // Net-(U20-B4)
  wire Net_U20_C4; // Net-(U20-C4)
  wire Net_U20_S1; // Net-(U20-S1)
  wire Net_U20_S2; // Net-(U20-S2)
  wire Net_U20_S3; // Net-(U20-S3)
  wire Net_U20_S4; // Net-(U20-S4)
  wire Net_U21_B1; // Net-(U21-B1)
  wire Net_U21_B2; // Net-(U21-B2)
  wire Net_U21_B3; // Net-(U21-B3)
  wire Net_U21_B4; // Net-(U21-B4)
  wire Net_U21_C4; // Net-(U21-C4)
  wire Net_U21_S1; // Net-(U21-S1)
  wire Net_U21_S2; // Net-(U21-S2)
  wire Net_U21_S3; // Net-(U21-S3)
  wire Net_U21_S4; // Net-(U21-S4)
  wire Net_U22_B1; // Net-(U22-B1)
  wire Net_U22_B2; // Net-(U22-B2)
  wire Net_U22_B3; // Net-(U22-B3)
  wire Net_U22_B4; // Net-(U22-B4)
  wire Net_U22_C4; // Net-(U22-C4)
  wire Net_U22_S1; // Net-(U22-S1)
  wire Net_U22_S2; // Net-(U22-S2)
  wire Net_U22_S3; // Net-(U22-S3)
  wire Net_U22_S4; // Net-(U22-S4)
  wire Net_U23_B1; // Net-(U23-B1)
  wire Net_U23_B2; // Net-(U23-B2)
  wire Net_U23_B3; // Net-(U23-B3)
  wire Net_U23_B4; // Net-(U23-B4)
  wire Net_U23_S1; // Net-(U23-S1)
  wire Net_U23_S2; // Net-(U23-S2)
  wire Net_U23_S3; // Net-(U23-S3)
  wire Net_U23_S4; // Net-(U23-S4)
  wire Net_U24_A1; // Net-(U24-A1)
  wire Net_U24_A2; // Net-(U24-A2)
  wire Net_U24_A3; // Net-(U24-A3)
  wire Net_U24_A4; // Net-(U24-A4)
  wire Net_U24_B1; // Net-(U24-B1)
  wire Net_U24_B2; // Net-(U24-B2)
  wire Net_U24_B3; // Net-(U24-B3)
  wire Net_U24_B4; // Net-(U24-B4)
  wire Net_U24_C0; // Net-(U24-C0)
  wire Net_U25_A1; // Net-(U25-A1)
  wire Net_U25_A2; // Net-(U25-A2)
  wire Net_U25_A3; // Net-(U25-A3)
  wire Net_U25_A4; // Net-(U25-A4)
  wire Net_U25_B1; // Net-(U25-B1)
  wire Net_U25_B2; // Net-(U25-B2)
  wire Net_U25_B3; // Net-(U25-B3)
  wire Net_U25_B4; // Net-(U25-B4)
  wire Net_U25_C0; // Net-(U25-C0)
  wire Net_U25_C4; // Net-(U25-C4)
  wire Net_U27_Pad10; // Net-(U27-Pad10)
  wire Net_U33_I1a; // Net-(U33-I1a)
  wire Net_U38_A1; // Net-(U38-A1)
  wire Net_U38_A2; // Net-(U38-A2)
  wire Net_U38_A3; // Net-(U38-A3)
  wire Net_U38_A4; // Net-(U38-A4)
  wire Net_U38_B1; // Net-(U38-B1)
  wire Net_U38_B2; // Net-(U38-B2)
  wire Net_U38_B3; // Net-(U38-B3)
  wire Net_U38_B4; // Net-(U38-B4)
  wire Net_U39_A1; // Net-(U39-A1)
  wire Net_U39_A2; // Net-(U39-A2)
  wire Net_U39_A3; // Net-(U39-A3)
  wire Net_U39_A4; // Net-(U39-A4)
  wire Net_U39_B1; // Net-(U39-B1)
  wire Net_U39_B2; // Net-(U39-B2)
  wire Net_U39_B3; // Net-(U39-B3)
  wire Net_U39_B4; // Net-(U39-B4)
  wire unconnected_U2_Pad1; // unconnected-(U2-Pad1)
  wire unconnected_U2_Pad2; // unconnected-(U2-Pad2)
  wire unconnected_U2_Pad3; // unconnected-(U2-Pad3)
  wire unconnected_U2_Pad4; // unconnected-(U2-Pad4)
  wire unconnected_U3_Dsl_Pad7; // unconnected-(U3-Dsl-Pad7)
  wire unconnected_U3_P2_Pad5; // unconnected-(U3-P2-Pad5)
  wire unconnected_U3_P3_Pad6; // unconnected-(U3-P3-Pad6)
  wire unconnected_U3_Q2_Pad13; // unconnected-(U3-Q2-Pad13)
  wire unconnected_U3_Q3_Pad12; // unconnected-(U3-Q3-Pad12)
  wire unconnected_U4_Dsl_Pad7; // unconnected-(U4-Dsl-Pad7)
  wire unconnected_U4_P2_Pad5; // unconnected-(U4-P2-Pad5)
  wire unconnected_U4_P3_Pad6; // unconnected-(U4-P3-Pad6)
  wire unconnected_U4_Q2_Pad13; // unconnected-(U4-Q2-Pad13)
  wire unconnected_U4_Q3_Pad12; // unconnected-(U4-Q3-Pad12)
  wire unconnected_U12_Q0_Pad3; // unconnected-(U12-~{Q0}-Pad3)
  wire unconnected_U12_Q1_Pad6; // unconnected-(U12-~{Q1}-Pad6)
  wire unconnected_U12_Q2_Pad11; // unconnected-(U12-~{Q2}-Pad11)
  wire unconnected_U12_Q3_Pad14; // unconnected-(U12-~{Q3}-Pad14)
  wire unconnected_U15_Dsl_Pad7; // unconnected-(U15-Dsl-Pad7)
  wire unconnected_U16_Dsl_Pad7; // unconnected-(U16-Dsl-Pad7)
  wire unconnected_U17_Dsl_Pad7; // unconnected-(U17-Dsl-Pad7)
  wire unconnected_U18_Dsl_Pad7; // unconnected-(U18-Dsl-Pad7)
  wire unconnected_U23_C4_Pad9; // unconnected-(U23-C4-Pad9)
  wire unconnected_U24_C4_Pad9; // unconnected-(U24-C4-Pad9)
  wire unconnected_U42_Pad8; // unconnected-(U42-Pad8)
  wire unconnected_U42_Pad9; // unconnected-(U42-Pad9)
  wire unconnected_U42_Pad10; // unconnected-(U42-Pad10)
  wire unconnected_U42_Pad11; // unconnected-(U42-Pad11)
  wire unconnected_U42_Pad12; // unconnected-(U42-Pad12)
  wire unconnected_U42_Pad13; // unconnected-(U42-Pad13)
  wire unconnected_U45_TC_Pad15; // unconnected-(U45-TC-Pad15)
  wire unconnected_U46_TC_Pad15; // unconnected-(U46-TC-Pad15)
  wire unconnected_U47_TC_Pad15; // unconnected-(U47-TC-Pad15)
  wire unconnected_U48_TC_Pad15; // unconnected-(U48-TC-Pad15)
  wire unconnected_U49_TC_Pad15; // unconnected-(U49-TC-Pad15)

  // Common rail assumptions
  assign n_5V = 1'b1;
  assign GNDREF = 1'b0;

  // Floating TTL input pins read high (overridable per pin)
  assign unconnected_U2_Pad1 = 1'b1; // U2.1
  assign unconnected_U2_Pad3 = 1'b1; // U2.3
  assign unconnected_U3_Dsl_Pad7 = 1'b1; // U3.7
  assign unconnected_U3_P2_Pad5 = 1'b1; // U3.5
  assign unconnected_U3_P3_Pad6 = 1'b1; // U3.6
  assign unconnected_U4_Dsl_Pad7 = 1'b1; // U4.7
  assign unconnected_U4_P2_Pad5 = 1'b1; // U4.5
  assign unconnected_U4_P3_Pad6 = 1'b1; // U4.6
  assign unconnected_U15_Dsl_Pad7 = 1'b1; // U15.7
  assign unconnected_U16_Dsl_Pad7 = 1'b1; // U16.7
  assign unconnected_U17_Dsl_Pad7 = 1'b1; // U17.7
  assign unconnected_U18_Dsl_Pad7 = 1'b1; // U18.7
  assign unconnected_U42_Pad9 = 1'b1; // U42.9
  assign unconnected_U42_Pad10 = 1'b1; // U42.10
  assign unconnected_U42_Pad12 = 1'b1; // U42.12
  assign unconnected_U42_Pad13 = 1'b1; // U42.13

  bb_C_Polarized C1 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 22uF/15V
  bb_C_Polarized C12 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C13 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C2 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 22uF/15V
  bb_C_Polarized C24 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C25 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C3 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C34 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C35 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  bb_C_Polarized C41 (.p_1(n_5V), .p_2(GNDREF)); // C_Polarized 4.7uF/15V
  ideal_resistor R1 (.p_1(n_5V), .p_2(Net_R1_Pad2)); // R_US 1K
  ideal_resistor R2 (.p_1(ARUCKE), .p_2(GNDREF)); // R_US 380
  ideal_resistor R3 (.p_1(n_5V), .p_2(ARUCKE)); // R_US 220
  ideal_resistor R4 (.p_1(n_5V), .p_2(CSIGN_n)); // R_US 220
  ideal_resistor R5 (.p_1(CSIGN_n), .p_2(GNDREF)); // R_US 330
  ttl_74ls377 #(.TPD(6)) U10 (.p_1(GNDREF), .p_2(Net_U10_Q0), .p_3(Net_U10_D0), .p_4(Net_U10_D1), .p_5(Net_U10_Q1), .p_6(Net_U10_Q2), .p_7(Net_U10_D2), .p_8(Net_U10_D3), .p_9(Net_U10_Q3), .p_10(GNDREF), .p_11(Net_U10_CP), .p_12(Net_U10_Q4), .p_13(Net_U10_D4), .p_14(Net_U10_D5), .p_15(Net_U10_Q5), .p_16(Net_U10_Q6), .p_17(Net_U10_D6), .p_18(Net_U10_D7), .p_19(Net_U10_Q7), .p_20(n_5V)); // 74LS377 74LS377
  ttl_74ls377 #(.TPD(6)) U11 (.p_1(GNDREF), .p_2(Net_U11_Q0), .p_3(Net_U11_D0), .p_4(Net_U11_D1), .p_5(Net_U11_Q1), .p_6(Net_U11_Q2), .p_7(Net_U11_D2), .p_8(Net_U11_D3), .p_9(Net_U11_Q3), .p_10(GNDREF), .p_11(Net_U10_CP), .p_12(Net_U11_Q4), .p_13(Net_U11_D4), .p_14(Net_U11_D5), .p_15(Net_U11_Q5), .p_16(Net_U11_Q6), .p_17(Net_U11_D6), .p_18(Net_U11_D7), .p_19(Net_U11_Q7), .p_20(n_5V)); // 74LS377 74LS377
  ttl_74ls175 #(.CLK_TPD(11.5)) U12 (.p_1(n_5V), .p_2(Net_U12_Q0), .p_3(unconnected_U12_Q0_Pad3), .p_4(Net_U12_D0), .p_5(Net_U12_D1), .p_6(unconnected_U12_Q1_Pad6), .p_7(Net_U12_Q1), .p_8(GNDREF), .p_9(Net_U10_CP), .p_10(Net_U12_Q2), .p_11(unconnected_U12_Q2_Pad11), .p_12(Net_U12_D2), .p_13(Net_U12_D3), .p_14(unconnected_U12_Q3_Pad14), .p_15(Net_U12_Q3), .p_16(n_5V)); // 74LS175 74LS175
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U13 (.p_1(Net_U12_D2), .p_2(Net_U13_B2), .p_3(Net_U13_A2), .p_4(Net_U12_D3), .p_5(Net_U13_A1), .p_6(Net_U13_B1), .p_7(n_5V), .p_8(GNDREF), .p_9(Net_U13_C4), .p_10(Net_U12_D0), .p_11(Net_U13_B4), .p_12(Net_U13_A4), .p_13(Net_U12_D1), .p_14(Net_U13_A3), .p_15(Net_U13_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls00 U14 (.p_1(SR3), .p_2(Net_U14_Pad10), .p_3(Net_U13_A3), .p_4(SR4), .p_5(Net_U14_Pad10), .p_6(Net_U13_A4), .p_7(GNDREF), .p_8(Net_U13_A1), .p_9(SR1), .p_10(Net_U14_Pad10), .p_11(Net_U13_A2), .p_12(SR2), .p_13(Net_U14_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls194a U15 (.p_1(n_5V), .p_2(SR19), .p_3(F15), .p_4(F14), .p_5(F12), .p_6(F10), .p_7(unconnected_U15_Dsl_Pad7), .p_8(GNDREF), .p_9(S0), .p_10(S1), .p_11(Net_U10_CP), .p_12(SR13), .p_13(SR15), .p_14(SR17), .p_15(SR19), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U16 (.p_1(n_5V), .p_2(SR18), .p_3(F15), .p_4(F13), .p_5(F11), .p_6(F9), .p_7(unconnected_U16_Dsl_Pad7), .p_8(GNDREF), .p_9(S0), .p_10(S1), .p_11(Net_U10_CP), .p_12(SR12), .p_13(SR14), .p_14(SR16), .p_15(SR18), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U17 (.p_1(n_5V), .p_2(SR12), .p_3(F7), .p_4(F5), .p_5(F3), .p_6(F1), .p_7(unconnected_U17_Dsl_Pad7), .p_8(GNDREF), .p_9(S0), .p_10(S1), .p_11(Net_U10_CP), .p_12(SR4), .p_13(SR6), .p_14(SR8), .p_15(SR10), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U18 (.p_1(n_5V), .p_2(SR13), .p_3(F8), .p_4(F6), .p_5(F4), .p_6(F2), .p_7(unconnected_U18_Dsl_Pad7), .p_8(GNDREF), .p_9(S0), .p_10(S1), .p_11(Net_U10_CP), .p_12(SR5), .p_13(SR7), .p_14(SR9), .p_15(SR11), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U19 (.p_1(Net_U19_S2), .p_2(Net_U19_B2), .p_3(AC1), .p_4(Net_U19_S1), .p_5(AC0), .p_6(Net_U19_B1), .p_7(Net_U19_C0), .p_8(GNDREF), .p_9(Net_U19_C4), .p_10(Net_U19_S4), .p_11(Net_U19_B4), .p_12(AC3), .p_13(Net_U19_S3), .p_14(AC2), .p_15(Net_U19_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls04 U2 (.p_1(unconnected_U2_Pad1), .p_2(unconnected_U2_Pad2), .p_3(unconnected_U2_Pad3), .p_4(unconnected_U2_Pad4), .p_5(Net_U2_Pad10), .p_6(Net_U19_C0), .p_7(GNDREF), .p_8(Net_U33_I1a), .p_9(Net_U23_B4), .p_10(Net_U2_Pad10), .p_11(CSIGN_n), .p_12(Net_U2_Pad12), .p_13(CSIGN_n), .p_14(n_5V)); // 74LS04 74LS04
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U20 (.p_1(Net_U20_S2), .p_2(Net_U20_B2), .p_3(AC5), .p_4(Net_U20_S1), .p_5(AC4), .p_6(Net_U20_B1), .p_7(Net_U19_C4), .p_8(GNDREF), .p_9(Net_U20_C4), .p_10(Net_U20_S4), .p_11(Net_U20_B4), .p_12(AC7), .p_13(Net_U20_S3), .p_14(AC6), .p_15(Net_U20_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U21 (.p_1(Net_U21_S2), .p_2(Net_U21_B2), .p_3(AC9), .p_4(Net_U21_S1), .p_5(AC8), .p_6(Net_U21_B1), .p_7(Net_U20_C4), .p_8(GNDREF), .p_9(Net_U21_C4), .p_10(Net_U21_S4), .p_11(Net_U21_B4), .p_12(AC11), .p_13(Net_U21_S3), .p_14(AC10), .p_15(Net_U21_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U22 (.p_1(Net_U22_S2), .p_2(Net_U22_B2), .p_3(AC13), .p_4(Net_U22_S1), .p_5(AC12), .p_6(Net_U22_B1), .p_7(Net_U21_C4), .p_8(GNDREF), .p_9(Net_U22_C4), .p_10(Net_U22_S4), .p_11(Net_U22_B4), .p_12(AC15), .p_13(Net_U22_S3), .p_14(AC14), .p_15(Net_U22_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U23 (.p_1(Net_U23_S2), .p_2(Net_U23_B2), .p_3(AC17), .p_4(Net_U23_S1), .p_5(AC16), .p_6(Net_U23_B1), .p_7(Net_U22_C4), .p_8(GNDREF), .p_9(unconnected_U23_C4_Pad9), .p_10(Net_U23_S4), .p_11(Net_U23_B4), .p_12(AC19), .p_13(Net_U23_S3), .p_14(AC18), .p_15(Net_U23_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U24 (.p_1(Net_U10_D2), .p_2(Net_U24_B2), .p_3(Net_U24_A2), .p_4(Net_U10_D3), .p_5(Net_U24_A1), .p_6(Net_U24_B1), .p_7(Net_U24_C0), .p_8(GNDREF), .p_9(unconnected_U24_C4_Pad9), .p_10(Net_U10_D0), .p_11(Net_U24_B4), .p_12(Net_U24_A4), .p_13(Net_U10_D1), .p_14(Net_U24_A3), .p_15(Net_U24_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U25 (.p_1(Net_U11_D2), .p_2(Net_U25_B2), .p_3(Net_U25_A2), .p_4(Net_U11_D3), .p_5(Net_U25_A1), .p_6(Net_U25_B1), .p_7(Net_U25_C0), .p_8(GNDREF), .p_9(Net_U25_C4), .p_10(Net_U11_D0), .p_11(Net_U25_B4), .p_12(Net_U25_A4), .p_13(Net_U11_D1), .p_14(Net_U25_A3), .p_15(Net_U25_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls00 U26 (.p_1(SR11), .p_2(Net_U14_Pad10), .p_3(Net_U25_A3), .p_4(SR9), .p_5(Net_U14_Pad10), .p_6(Net_U25_A1), .p_7(GNDREF), .p_8(Net_U25_A4), .p_9(SR12), .p_10(Net_U14_Pad10), .p_11(Net_U25_A2), .p_12(SR10), .p_13(Net_U14_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls00 U27 (.p_1(SR9), .p_2(Net_U27_Pad10), .p_3(Net_U25_B2), .p_4(SR10), .p_5(Net_U27_Pad10), .p_6(Net_U25_B3), .p_7(GNDREF), .p_8(Net_U25_B1), .p_9(SR8), .p_10(Net_U27_Pad10), .p_11(Net_U25_B4), .p_12(SR11), .p_13(Net_U27_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls00 U28 (.p_1(SR3), .p_2(Net_U27_Pad10), .p_3(Net_U13_B4), .p_4(SR1), .p_5(Net_U27_Pad10), .p_6(Net_U13_B2), .p_7(GNDREF), .p_8(Net_U13_B3), .p_9(SR2), .p_10(Net_U27_Pad10), .p_11(Net_U13_B1), .p_12(SR0), .p_13(Net_U27_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls670 U29 (.p_1(DAB14), .p_2(DAB13), .p_3(DAB12), .p_4(RA0), .p_5(RA1), .p_6(F12), .p_7(F13), .p_8(GNDREF), .p_9(F14), .p_10(F15), .p_11(GNDREF), .p_12(DAB_WSTB_n), .p_13(WA0), .p_14(WA1), .p_15(DAB15), .p_16(n_5V)); // 74LS670 74LS670
  ttl_74ls194a U3 (.p_1(n_5V), .p_2(SR4), .p_3(GNDREF), .p_4(GNDREF), .p_5(unconnected_U3_P2_Pad5), .p_6(unconnected_U3_P3_Pad6), .p_7(unconnected_U3_Dsl_Pad7), .p_8(GNDREF), .p_9(S0), .p_10(S1), .p_11(Net_U10_CP), .p_12(unconnected_U3_Q3_Pad12), .p_13(unconnected_U3_Q2_Pad13), .p_14(SR0), .p_15(SR2), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls670 U30 (.p_1(DAB10), .p_2(DAB9), .p_3(DAB8), .p_4(RA0), .p_5(RA1), .p_6(F8), .p_7(F9), .p_8(GNDREF), .p_9(F10), .p_10(F11), .p_11(GNDREF), .p_12(DAB_WSTB_n), .p_13(WA0), .p_14(WA1), .p_15(DAB11), .p_16(n_5V)); // 74LS670 74LS670
  ttl_74ls670 U31 (.p_1(DAB6), .p_2(DAB5), .p_3(DAB4), .p_4(RA0), .p_5(RA1), .p_6(F4), .p_7(F5), .p_8(GNDREF), .p_9(F6), .p_10(F7), .p_11(GNDREF), .p_12(DAB_WSTB_n), .p_13(WA0), .p_14(WA1), .p_15(DAB7), .p_16(n_5V)); // 74LS670 74LS670
  ttl_74ls670 U32 (.p_1(DAB2), .p_2(DAB1), .p_3(DAB0), .p_4(RA0), .p_5(RA1), .p_6(F0), .p_7(F1), .p_8(GNDREF), .p_9(F2), .p_10(F3), .p_11(GNDREF), .p_12(DAB_WSTB_n), .p_13(WA0), .p_14(WA1), .p_15(DAB3), .p_16(n_5V)); // 74LS670 74LS670
  ttl_74ls157 #(.TPLH(6.6), .TPHL(4.6)) U33 (.p_1(SAT), .p_2(Net_U19_S1), .p_3(Net_U33_I1a), .p_4(PP0), .p_5(Net_U19_S2), .p_6(Net_U33_I1a), .p_7(PP1), .p_8(GNDREF), .p_9(PP2), .p_10(Net_U33_I1a), .p_11(Net_U19_S3), .p_12(PP3), .p_13(Net_U33_I1a), .p_14(Net_U19_S4), .p_15(GNDREF), .p_16(n_5V)); // 74LS157 74LS157
  ttl_74ls157 #(.TPLH(6.6), .TPHL(4.6)) U34 (.p_1(SAT), .p_2(Net_U20_S1), .p_3(Net_U33_I1a), .p_4(PP4), .p_5(Net_U20_S2), .p_6(Net_U33_I1a), .p_7(PP5), .p_8(GNDREF), .p_9(PP6), .p_10(Net_U33_I1a), .p_11(Net_U20_S3), .p_12(PP7), .p_13(Net_U33_I1a), .p_14(Net_U20_S4), .p_15(GNDREF), .p_16(n_5V)); // 74LS157 74LS157
  ttl_74ls157 #(.TPLH(6.6), .TPHL(4.6)) U35 (.p_1(SAT), .p_2(Net_U21_S1), .p_3(Net_U33_I1a), .p_4(PP8), .p_5(Net_U21_S2), .p_6(Net_U33_I1a), .p_7(PP9), .p_8(GNDREF), .p_9(PP10), .p_10(Net_U33_I1a), .p_11(Net_U21_S3), .p_12(PP11), .p_13(Net_U33_I1a), .p_14(Net_U21_S4), .p_15(GNDREF), .p_16(n_5V)); // 74LS157 74LS157
  ttl_74ls157 #(.TPLH(6.6), .TPHL(4.6)) U36 (.p_1(SAT), .p_2(Net_U22_S1), .p_3(Net_U33_I1a), .p_4(PP12), .p_5(Net_U22_S2), .p_6(Net_U33_I1a), .p_7(PP13), .p_8(GNDREF), .p_9(PP14), .p_10(Net_U33_I1a), .p_11(Net_U22_S3), .p_12(PP15), .p_13(Net_U33_I1a), .p_14(Net_U22_S4), .p_15(GNDREF), .p_16(n_5V)); // 74LS157 74LS157
  ttl_74ls157 #(.TPLH(6.6), .TPHL(4.6)) U37 (.p_1(SAT), .p_2(Net_U23_S1), .p_3(Net_U33_I1a), .p_4(PP16), .p_5(Net_U23_S2), .p_6(Net_U33_I1a), .p_7(PP17), .p_8(GNDREF), .p_9(PP18), .p_10(Net_U23_B4), .p_11(Net_U23_S3), .p_12(PP19), .p_13(Net_U23_B4), .p_14(Net_U23_S4), .p_15(GNDREF), .p_16(n_5V)); // 74LS157 74LS157
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U38 (.p_1(Net_U10_D6), .p_2(Net_U38_B2), .p_3(Net_U38_A2), .p_4(Net_U10_D7), .p_5(Net_U38_A1), .p_6(Net_U38_B1), .p_7(Net_U25_C4), .p_8(GNDREF), .p_9(Net_U24_C0), .p_10(Net_U10_D4), .p_11(Net_U38_B4), .p_12(Net_U38_A4), .p_13(Net_U10_D5), .p_14(Net_U38_A3), .p_15(Net_U38_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 #(.S_LH(6.6), .S_HL(6.6), .C_LH(5.3), .C_HL(5.0)) U39 (.p_1(Net_U11_D6), .p_2(Net_U39_B2), .p_3(Net_U39_A2), .p_4(Net_U11_D7), .p_5(Net_U39_A1), .p_6(Net_U39_B1), .p_7(Net_U13_C4), .p_8(GNDREF), .p_9(Net_U25_C0), .p_10(Net_U11_D4), .p_11(Net_U39_B4), .p_12(Net_U39_A4), .p_13(Net_U11_D5), .p_14(Net_U39_A3), .p_15(Net_U39_B3), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls194a U4 (.p_1(n_5V), .p_2(SR5), .p_3(F0), .p_4(GNDREF), .p_5(unconnected_U4_P2_Pad5), .p_6(unconnected_U4_P3_Pad6), .p_7(unconnected_U4_Dsl_Pad7), .p_8(GNDREF), .p_9(S0), .p_10(S1), .p_11(Net_U10_CP), .p_12(unconnected_U4_Q3_Pad12), .p_13(unconnected_U4_Q2_Pad13), .p_14(SR1), .p_15(SR3), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls00 U40 (.p_1(SR5), .p_2(Net_U27_Pad10), .p_3(Net_U39_B2), .p_4(SR7), .p_5(Net_U27_Pad10), .p_6(Net_U39_B4), .p_7(GNDREF), .p_8(Net_U39_B1), .p_9(SR4), .p_10(Net_U27_Pad10), .p_11(Net_U39_B3), .p_12(SR6), .p_13(Net_U27_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls00 U41 (.p_1(SR7), .p_2(Net_U14_Pad10), .p_3(Net_U39_A3), .p_4(SR8), .p_5(Net_U14_Pad10), .p_6(Net_U39_A4), .p_7(GNDREF), .p_8(Net_U39_A1), .p_9(SR5), .p_10(Net_U14_Pad10), .p_11(Net_U39_A2), .p_12(SR6), .p_13(Net_U14_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls86 #(.TPLH(7.0), .TPHL(6.5)) U42 (.p_1(Net_U23_S4), .p_2(Net_U23_S3), .p_3(SAT), .p_4(Net_R1_Pad2), .p_5(ARUCKE), .p_6(Net_U10_CP), .p_7(GNDREF), .p_8(unconnected_U42_Pad8), .p_9(unconnected_U42_Pad9), .p_10(unconnected_U42_Pad10), .p_11(unconnected_U42_Pad11), .p_12(unconnected_U42_Pad12), .p_13(unconnected_U42_Pad13), .p_14(n_5V)); // 74LS86 74LS86
  ttl_sn74f374n #(.TPD(6.1), .TSU(2.0)) U43 (.p_1(RDRREG_n), .p_2(DAB13), .p_3(PP16), .p_4(PP15), .p_5(DAB12), .p_6(DAB15), .p_7(PP18), .p_8(PP17), .p_9(DAB14), .p_10(GNDREF), .p_11(XFER_CK), .p_12(DAB9), .p_13(PP12), .p_14(PP11), .p_15(DAB8), .p_16(DAB11), .p_17(PP14), .p_18(PP13), .p_19(DAB10), .p_20(n_5V)); // 74LS374 74LS374
  ttl_sn74f374n #(.TPD(6.1), .TSU(2.0)) U44 (.p_1(RDRREG_n), .p_2(DAB5), .p_3(PP8), .p_4(PP7), .p_5(DAB4), .p_6(DAB7), .p_7(PP10), .p_8(PP9), .p_9(DAB6), .p_10(GNDREF), .p_11(XFER_CK), .p_12(DAB1), .p_13(PP4), .p_14(PP3), .p_15(DAB0), .p_16(DAB3), .p_17(PP6), .p_18(PP5), .p_19(DAB2), .p_20(n_5V)); // 74LS374 74LS374
  ttl_sn74s163n #(.TPD(18)) U45 (.p_1(ZERO_n), .p_2(Net_U10_CP), .p_3(PP3), .p_4(PP2), .p_5(PP1), .p_6(PP0), .p_7(GNDREF), .p_8(GNDREF), .p_9(GNDREF), .p_10(GNDREF), .p_11(AC0), .p_12(AC1), .p_13(AC2), .p_14(AC3), .p_15(unconnected_U45_TC_Pad15), .p_16(n_5V)); // 74LS163 74LS163
  ttl_sn74s163n #(.TPD(18)) U46 (.p_1(ZERO_n), .p_2(Net_U10_CP), .p_3(PP7), .p_4(PP6), .p_5(PP5), .p_6(PP4), .p_7(GNDREF), .p_8(GNDREF), .p_9(GNDREF), .p_10(GNDREF), .p_11(AC4), .p_12(AC5), .p_13(AC6), .p_14(AC7), .p_15(unconnected_U46_TC_Pad15), .p_16(n_5V)); // 74LS163 74LS163
  ttl_sn74s163n #(.TPD(18)) U47 (.p_1(ZERO_n), .p_2(Net_U10_CP), .p_3(PP11), .p_4(PP10), .p_5(PP9), .p_6(PP8), .p_7(GNDREF), .p_8(GNDREF), .p_9(GNDREF), .p_10(GNDREF), .p_11(AC8), .p_12(AC9), .p_13(AC10), .p_14(AC11), .p_15(unconnected_U47_TC_Pad15), .p_16(n_5V)); // 74LS163 74LS163
  ttl_sn74s163n #(.TPD(18)) U48 (.p_1(ZERO_n), .p_2(Net_U10_CP), .p_3(PP15), .p_4(PP14), .p_5(PP13), .p_6(PP12), .p_7(GNDREF), .p_8(GNDREF), .p_9(GNDREF), .p_10(GNDREF), .p_11(AC12), .p_12(AC13), .p_13(AC14), .p_14(AC15), .p_15(unconnected_U48_TC_Pad15), .p_16(n_5V)); // 74LS163 74LS163
  ttl_sn74s163n #(.TPD(18)) U49 (.p_1(ZERO_n), .p_2(Net_U10_CP), .p_3(PP19), .p_4(PP18), .p_5(PP17), .p_6(PP16), .p_7(GNDREF), .p_8(GNDREF), .p_9(GNDREF), .p_10(GNDREF), .p_11(AC16), .p_12(AC17), .p_13(AC18), .p_14(AC19), .p_15(unconnected_U49_TC_Pad15), .p_16(n_5V)); // 74LS163 74LS163
  ttl_74ls86 #(.TPLH(7.0), .TPHL(6.5)) U5 (.p_1(Net_U12_Q3), .p_2(Net_U2_Pad10), .p_3(Net_U19_B1), .p_4(Net_U12_Q2), .p_5(Net_U2_Pad10), .p_6(Net_U19_B2), .p_7(GNDREF), .p_8(Net_U19_B3), .p_9(Net_U12_Q1), .p_10(Net_U2_Pad10), .p_11(Net_U19_B4), .p_12(Net_U12_Q0), .p_13(Net_U2_Pad10), .p_14(n_5V)); // 74LS86 74LS86
  ttl_74ls00 U50 (.p_1(SR12), .p_2(Net_U27_Pad10), .p_3(Net_U38_B1), .p_4(SR13), .p_5(Net_U27_Pad10), .p_6(Net_U38_B2), .p_7(GNDREF), .p_8(Net_U38_B3), .p_9(SR14), .p_10(Net_U27_Pad10), .p_11(Net_U38_B4), .p_12(SR15), .p_13(Net_U27_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls00 U51 (.p_1(SR13), .p_2(Net_U14_Pad10), .p_3(Net_U38_A1), .p_4(SR14), .p_5(Net_U14_Pad10), .p_6(Net_U38_A2), .p_7(GNDREF), .p_8(Net_U38_A3), .p_9(SR15), .p_10(Net_U14_Pad10), .p_11(Net_U38_A4), .p_12(SR16), .p_13(Net_U14_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls00 U52 (.p_1(SR17), .p_2(Net_U27_Pad10), .p_3(Net_U24_B2), .p_4(SR18), .p_5(Net_U27_Pad10), .p_6(Net_U24_B3), .p_7(GNDREF), .p_8(Net_U24_B4), .p_9(SR19), .p_10(Net_U27_Pad10), .p_11(Net_U24_B1), .p_12(SR16), .p_13(Net_U27_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls00 U53 (.p_1(SR17), .p_2(Net_U14_Pad10), .p_3(Net_U24_A1), .p_4(SR18), .p_5(Net_U14_Pad10), .p_6(Net_U24_A2), .p_7(GNDREF), .p_8(Net_U24_A4), .p_9(SR19), .p_10(Net_U14_Pad10), .p_11(Net_U24_A3), .p_12(SR19), .p_13(Net_U14_Pad10), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls14 U54 (.p_1(RA0_n), .p_2(RA0), .p_3(WA0_n), .p_4(WA0), .p_5(RA1_n), .p_6(RA1), .p_7(GNDREF), .p_8(WA1), .p_9(WA1_n), .p_10(Net_U27_Pad10), .p_11(M1), .p_12(Net_U14_Pad10), .p_13(M0), .p_14(n_5V)); // 74LS14 74LS14
  ttl_74ls86 #(.TPLH(7.0), .TPHL(6.5)) U6 (.p_1(Net_U11_Q7), .p_2(Net_U2_Pad10), .p_3(Net_U20_B1), .p_4(Net_U11_Q6), .p_5(Net_U2_Pad10), .p_6(Net_U20_B2), .p_7(GNDREF), .p_8(Net_U20_B3), .p_9(Net_U11_Q5), .p_10(Net_U2_Pad10), .p_11(Net_U20_B4), .p_12(Net_U11_Q4), .p_13(Net_U2_Pad10), .p_14(n_5V)); // 74LS86 74LS86
  ttl_74ls86 #(.TPLH(7.0), .TPHL(6.5)) U7 (.p_1(Net_U11_Q3), .p_2(Net_U2_Pad12), .p_3(Net_U21_B1), .p_4(Net_U11_Q2), .p_5(Net_U2_Pad12), .p_6(Net_U21_B2), .p_7(GNDREF), .p_8(Net_U21_B3), .p_9(Net_U11_Q1), .p_10(Net_U2_Pad12), .p_11(Net_U21_B4), .p_12(Net_U11_Q0), .p_13(Net_U2_Pad12), .p_14(n_5V)); // 74LS86 74LS86
  ttl_74ls86 #(.TPLH(20.0), .TPHL(13.0)) U8 (.p_1(Net_U10_Q7), .p_2(Net_U2_Pad12), .p_3(Net_U22_B1), .p_4(Net_U10_Q6), .p_5(Net_U2_Pad12), .p_6(Net_U22_B2), .p_7(GNDREF), .p_8(Net_U22_B3), .p_9(Net_U10_Q5), .p_10(Net_U2_Pad12), .p_11(Net_U22_B4), .p_12(Net_U10_Q4), .p_13(Net_U2_Pad12), .p_14(n_5V)); // 74LS86 74LS86
  ttl_74ls86 #(.TPLH(7.0), .TPHL(6.5)) U9 (.p_1(Net_U10_Q3), .p_2(Net_U2_Pad12), .p_3(Net_U23_B1), .p_4(Net_U10_Q2), .p_5(Net_U2_Pad12), .p_6(Net_U23_B2), .p_7(GNDREF), .p_8(Net_U23_B3), .p_9(Net_U10_Q1), .p_10(Net_U2_Pad12), .p_11(Net_U23_B4), .p_12(Net_U10_Q0), .p_13(Net_U2_Pad12), .p_14(n_5V)); // 74LS86 74LS86
endmodule

