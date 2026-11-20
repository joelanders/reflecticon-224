`timescale 1ns/1ps
// Auto-generated from KiCad netlist
// Source: ../schematics/netlists/fpc.net

module fpc_board_netlist #(parameter FPC_U6_INIT_FILE = "rom/u6_74s287_init.hex")(DAB0, DAB1, DAB2, DAB3, DAB4, DAB5, DAB6, DAB7, DAB8, DAB9, DAB10, DAB11, DAB12, DAB13, DAB14, DAB15, FPC_CK, FPC_DBUG, RESET_n, RD_AD_n, WR_DA_n, O2, AD0, AD1, AD2, AD3, AD4, AD5, AD6, AD7, AD8, AD9, AD10, AD11, IGA0, IGA1, CH1, OUTA, OUTB, OUTC, OUTD, SDAA, SDAB, SDAC, SDAD);
  inout DAB0, DAB1, DAB2, DAB3, DAB4, DAB5, DAB6, DAB7, DAB8, DAB9, DAB10, DAB11, DAB12, DAB13, DAB14, DAB15, FPC_CK, FPC_DBUG, RESET_n, RD_AD_n, WR_DA_n, O2, AD0, AD1, AD2, AD3, AD4, AD5, AD6, AD7, AD8, AD9, AD10, AD11, IGA0, IGA1, CH1, OUTA, OUTB, OUTC, OUTD, SDAA, SDAB, SDAC, SDAD;
  // Net declarations
  wire n_5V; // +5V
  wire Untitled_Sheet_U16_CP; // /Untitled Sheet/U16_CP
  wire BUSY; // BUSY
  wire CH1_slash; // CH1{slash}
  wire CNV_CK; // CNV CK
  wire DA0; // DA0
  wire DA1; // DA1
  wire DA2; // DA2
  wire DA3; // DA3
  wire DA4; // DA4
  wire DA5; // DA5
  wire DA6; // DA6
  wire DA7; // DA7
  wire DA8; // DA8
  wire DA9; // DA9
  wire DA10; // DA10
  wire DA11; // DA11
  wire DA11_INT; // DA11-INT
  wire Earth; // Earth
  wire Net_U1_L; // Net-(U1-L)
  wire Net_U1_P; // Net-(U1-P)
  wire Net_U1_QA; // Net-(U1-QA)
  wire Net_U1_QC; // Net-(U1-QC)
  wire Net_U2_QB; // Net-(U2-QB)
  wire Net_U3A_J; // Net-(U3A-J)
  wire Net_U3A_Q; // Net-(U3A-Q)
  wire Net_U3B_K; // Net-(U3B-~{K})
  wire Net_U4_I1a; // Net-(U4-I1a)
  wire Net_U4_I1c; // Net-(U4-I1c)
  wire Net_U4_Za; // Net-(U4-Za)
  wire Net_U6_A; // Net-(U6-A)
  wire Net_U6_B; // Net-(U6-B)
  wire Net_U6_C; // Net-(U6-C)
  wire Net_U6_D; // Net-(U6-D)
  wire Net_U6_E; // Net-(U6-E)
  wire Net_U6_G; // Net-(U6-G)
  wire Net_U6_H; // Net-(U6-H)
  wire Net_U7_CEP; // Net-(U7-CEP)
  wire Net_U7_CET; // Net-(U7-CET)
  wire Net_U7_TC; // Net-(U7-TC)
  wire Net_U12_Pad2; // Net-(U12-Pad2)
  wire Net_U12_Pad10; // Net-(U12-Pad10)
  wire Net_U13_Pad8; // Net-(U13-Pad8)
  wire Net_U14_Pad2; // Net-(U14-Pad2)
  wire Net_U16_CEP; // Net-(U16-CEP)
  wire Net_U16_PE; // Net-(U16-~{PE})
  wire Net_U17_Pad2; // Net-(U17-Pad2)
  wire Net_U18_D1; // Net-(U18-D1)
  wire Net_U18_D2; // Net-(U18-D2)
  wire Net_U18_D3; // Net-(U18-D3)
  wire Net_U23_P0; // Net-(U23-P0)
  wire Net_U23_P1; // Net-(U23-P1)
  wire Net_U23_P2; // Net-(U23-P2)
  wire Net_U23_P3; // Net-(U23-P3)
  wire Net_U23_S0; // Net-(U23-S0)
  wire Net_U23_S1; // Net-(U23-S1)
  wire Net_U24_P0; // Net-(U24-P0)
  wire Net_U24_P1; // Net-(U24-P1)
  wire Net_U24_P2; // Net-(U24-P2)
  wire Net_U24_P3; // Net-(U24-P3)
  wire Net_U25A_I0; // Net-(U25A-I0)
  wire Net_U25A_I1; // Net-(U25A-I1)
  wire Net_U25A_I2; // Net-(U25A-I2)
  wire Net_U25A_I3; // Net-(U25A-I3)
  wire Net_U25A_OE; // Net-(U25A-~{OE})
  wire Net_U25B_I0; // Net-(U25B-I0)
  wire Net_U25B_I1; // Net-(U25B-I1)
  wire Net_U25B_I2; // Net-(U25B-I2)
  wire Net_U25B_I3; // Net-(U25B-I3)
  wire Net_U26A_I0; // Net-(U26A-I0)
  wire Net_U26A_I1; // Net-(U26A-I1)
  wire Net_U26A_I2; // Net-(U26A-I2)
  wire Net_U26A_I3; // Net-(U26A-I3)
  wire Net_U26B_I0; // Net-(U26B-I0)
  wire Net_U26B_I1; // Net-(U26B-I1)
  wire Net_U26B_I2; // Net-(U26B-I2)
  wire Net_U26B_I3; // Net-(U26B-I3)
  wire Net_U27_S0; // Net-(U27-S0)
  wire Net_U34_Dsl; // Net-(U34-Dsl)
  wire Net_U34_P0; // Net-(U34-P0)
  wire Net_U34_P1; // Net-(U34-P1)
  wire Net_U34_P2; // Net-(U34-P2)
  wire Net_U34_P3; // Net-(U34-P3)
  wire Net_U35_P0; // Net-(U35-P0)
  wire Net_U35_P1; // Net-(U35-P1)
  wire Net_U35_P2; // Net-(U35-P2)
  wire Net_U35_P3; // Net-(U35-P3)
  wire Net_U36_E; // Net-(U36-~{E})
  wire Net_U40_QA; // Net-(U40-QA)
  wire Net_U40_QB; // Net-(U40-QB)
  wire Net_U40_QC; // Net-(U40-QC)
  wire Net_U40_QD; // Net-(U40-QD)
  wire Net_U41_QA; // Net-(U41-QA)
  wire Net_U41_QB; // Net-(U41-QB)
  wire Net_U41_QC; // Net-(U41-QC)
  wire Net_U41_QD; // Net-(U41-QD)
  wire Net_U42_I1a; // Net-(U42-I1a)
  wire Net_U42_I1c; // Net-(U42-I1c)
  wire Net_U43_P; // Net-(U43-P)
  wire Net_U43_TC; // Net-(U43-TC)
  wire OGA0; // OGA0
  wire OGA1; // OGA1
  wire STBGN; // STBGN
  wire STC; // STC
  wire STROBE_n; // STROBE_n
  wire unconnected_U1_QD_Pad11; // unconnected-(U1-QD-Pad11)
  wire unconnected_U1_TC_Pad15; // unconnected-(U1-TC-Pad15)
  wire unconnected_U2_QA_Pad14; // unconnected-(U2-QA-Pad14)
  wire unconnected_U2_QC_Pad12; // unconnected-(U2-QC-Pad12)
  wire unconnected_U2_QD_Pad11; // unconnected-(U2-QD-Pad11)
  wire unconnected_U3A_Q_Pad7; // unconnected-(U3A-~{Q}-Pad7)
  wire unconnected_U3B_Q_Pad9; // unconnected-(U3B-~{Q}-Pad9)
  wire unconnected_U7_D0_Pad3; // unconnected-(U7-D0-Pad3)
  wire unconnected_U7_D1_Pad4; // unconnected-(U7-D1-Pad4)
  wire unconnected_U7_D2_Pad5; // unconnected-(U7-D2-Pad5)
  wire unconnected_U7_D3_Pad6; // unconnected-(U7-D3-Pad6)
  wire unconnected_U8_D0_Pad3; // unconnected-(U8-D0-Pad3)
  wire unconnected_U8_D1_Pad4; // unconnected-(U8-D1-Pad4)
  wire unconnected_U8_D2_Pad5; // unconnected-(U8-D2-Pad5)
  wire unconnected_U8_D3_Pad6; // unconnected-(U8-D3-Pad6)
  wire unconnected_U16_D3_Pad6; // unconnected-(U16-D3-Pad6)
  wire unconnected_U16_Q0_Pad14; // unconnected-(U16-Q0-Pad14)
  wire unconnected_U16_Q1_Pad13; // unconnected-(U16-Q1-Pad13)
  wire unconnected_U16_Q3_Pad11; // unconnected-(U16-Q3-Pad11)
  wire unconnected_U16_TC_Pad15; // unconnected-(U16-TC-Pad15)
  wire unconnected_U18_Q0_Pad2; // unconnected-(U18-Q0-Pad2)
  wire unconnected_U18_Q1_Pad6; // unconnected-(U18-~{Q1}-Pad6)
  wire unconnected_U18_Q3_Pad14; // unconnected-(U18-~{Q3}-Pad14)
  wire unconnected_U23_Dsr_Pad2; // unconnected-(U23-Dsr-Pad2)
  wire unconnected_U24_Dsr_Pad2; // unconnected-(U24-Dsr-Pad2)
  wire unconnected_U27_Dsr_Pad2; // unconnected-(U27-Dsr-Pad2)
  wire unconnected_U28_Dsr_Pad2; // unconnected-(U28-Dsr-Pad2)
  wire unconnected_U34_Dsr_Pad2; // unconnected-(U34-Dsr-Pad2)
  wire unconnected_U35_Dsl_Pad7; // unconnected-(U35-Dsl-Pad7)
  wire unconnected_U35_Dsr_Pad2; // unconnected-(U35-Dsr-Pad2)
  wire unconnected_U35_Q1_Pad14; // unconnected-(U35-Q1-Pad14)
  wire unconnected_U35_Q2_Pad13; // unconnected-(U35-Q2-Pad13)
  wire unconnected_U35_Q3_Pad12; // unconnected-(U35-Q3-Pad12)
  wire unconnected_U38_Dsr_Pad2; // unconnected-(U38-Dsr-Pad2)
  wire unconnected_U39_Dsr_Pad2; // unconnected-(U39-Dsr-Pad2)
  wire unconnected_U40_TC_Pad15; // unconnected-(U40-TC-Pad15)
  wire unconnected_U41_TC_Pad15; // unconnected-(U41-TC-Pad15)
  wire unconnected_U43_QC_Pad12; // unconnected-(U43-QC-Pad12)
  wire unconnected_U43_QD_Pad11; // unconnected-(U43-QD-Pad11)

  // Common rail assumptions
  assign n_5V = 1'b1;
  assign Earth = 1'b0;

  // Floating TTL input pins read high (overridable per pin)
  assign unconnected_U7_D0_Pad3 = 1'b1; // U7.3
  assign unconnected_U7_D1_Pad4 = 1'b1; // U7.4
  assign unconnected_U7_D2_Pad5 = 1'b1; // U7.5
  assign unconnected_U7_D3_Pad6 = 1'b1; // U7.6
  assign unconnected_U8_D0_Pad3 = 1'b1; // U8.3
  assign unconnected_U8_D1_Pad4 = 1'b1; // U8.4
  assign unconnected_U8_D2_Pad5 = 1'b1; // U8.5
  assign unconnected_U8_D3_Pad6 = 1'b1; // U8.6
  assign unconnected_U16_D3_Pad6 = 1'b1; // U16.6
  assign unconnected_U23_Dsr_Pad2 = 1'b1; // U23.2
  assign unconnected_U24_Dsr_Pad2 = 1'b1; // U24.2
  assign unconnected_U27_Dsr_Pad2 = 1'b1; // U27.2
  assign unconnected_U28_Dsr_Pad2 = 1'b1; // U28.2
  assign unconnected_U34_Dsr_Pad2 = 1'b1; // U34.2
  assign unconnected_U35_Dsl_Pad7 = 1'b1; // U35.7
  assign unconnected_U35_Dsr_Pad2 = 1'b1; // U35.2
  assign unconnected_U38_Dsr_Pad2 = 1'b1; // U38.2
  assign unconnected_U39_Dsr_Pad2 = 1'b1; // U39.2

  ttl_sn74s163n U1 (.p_1(n_5V), .p_2(Untitled_Sheet_U16_CP), .p_3(Earth), .p_4(n_5V), .p_5(Earth), .p_6(Earth), .p_7(Net_U1_P), .p_8(Earth), .p_9(Net_U1_L), .p_10(Net_U1_P), .p_11(unconnected_U1_QD_Pad11), .p_12(Net_U1_QC), .p_13(BUSY), .p_14(Net_U1_QA), .p_15(unconnected_U1_TC_Pad15), .p_16(n_5V)); // 74LS163_3 -> 74LS163 74LS163
  ttl_74ls00 U12 (.p_1(Net_U7_TC), .p_2(Net_U12_Pad2), .p_3(Net_U7_CEP), .p_7(Earth), .p_8(Net_U23_S1), .p_9(Net_U1_L), .p_10(Net_U12_Pad10), .p_11(Net_U3B_K), .p_12(Net_U2_QB), .p_13(Net_U1_QA), .p_14(n_5V)); // 74LS00 74LS00
  ttl_74ls86 U13 (.p_7(Earth), .p_8(Net_U13_Pad8), .p_9(DA10), .p_10(DA11_INT), .p_14(n_5V)); // 74LS86 74LS86
  ttl_74ls02 U14 (.p_1(Net_U3A_J), .p_2(Net_U14_Pad2), .p_3(Net_U1_L), .p_4(Net_U43_P), .p_5(Net_U43_TC), .p_6(Net_U13_Pad8), .p_7(Earth), .p_11(BUSY), .p_12(Net_U3A_Q), .p_13(Net_U23_S0), .p_14(n_5V)); // 74LS02 LS02
  ttl_74ls04 U15 (.p_1(FPC_DBUG), .p_2(Net_U12_Pad2), .p_3(Net_U36_E), .p_4(Net_U14_Pad2), .p_5(DA11_INT), .p_6(DA11), .p_7(Earth), .p_8(Net_U1_L), .p_9(Net_U23_S0), .p_10(Net_U12_Pad10), .p_11(Net_U43_P), .p_14(n_5V)); // 74LS04 74LS04
  ttl_sn74s163n U16 (.p_1(n_5V), .p_2(Untitled_Sheet_U16_CP), .p_3(IGA0), .p_4(IGA1), .p_5(n_5V), .p_6(unconnected_U16_D3_Pad6), .p_7(Net_U16_CEP), .p_8(Earth), .p_9(Net_U16_PE), .p_10(Net_U16_CEP), .p_11(unconnected_U16_Q3_Pad11), .p_12(Net_U16_CEP), .p_13(unconnected_U16_Q1_Pad13), .p_14(unconnected_U16_Q0_Pad14), .p_15(unconnected_U16_TC_Pad15), .p_16(n_5V)); // 74LS163_1 -> 74LS163 74LS163
  ttl_74ls04 U17 (.p_1(Net_U6_G), .p_2(Net_U17_Pad2), .p_7(Earth), .p_8(Net_U16_PE), .p_9(Net_U27_S0), .p_12(Net_U42_I1c), .p_13(Net_U42_I1a), .p_14(n_5V)); // 74LS04 74LS04
  ttl_74ls175 U18 (.p_1(n_5V), .p_2(unconnected_U18_Q0_Pad2), .p_3(STC), .p_4(STBGN), .p_5(Net_U18_D1), .p_6(unconnected_U18_Q1_Pad6), .p_7(STBGN), .p_8(Earth), .p_9(Untitled_Sheet_U16_CP), .p_10(CH1), .p_11(CH1_slash), .p_12(Net_U18_D2), .p_13(Net_U18_D3), .p_14(unconnected_U18_Q3_Pad14), .p_15(CNV_CK), .p_16(n_5V)); // 74LS175 74LS175
  ttl_sn74s163n U2 (.p_1(n_5V), .p_2(Untitled_Sheet_U16_CP), .p_3(Earth), .p_4(n_5V), .p_5(Earth), .p_6(n_5V), .p_7(n_5V), .p_8(Earth), .p_9(Net_U1_L), .p_10(BUSY), .p_11(unconnected_U2_QD_Pad11), .p_12(unconnected_U2_QC_Pad12), .p_13(Net_U2_QB), .p_14(unconnected_U2_QA_Pad14), .p_15(Net_U1_P), .p_16(n_5V)); // 74LS163_3 -> 74LS163 74LS163
  ttl_74ls194a U23 (.p_1(n_5V), .p_2(unconnected_U23_Dsr_Pad2), .p_3(Net_U23_P0), .p_4(Net_U23_P1), .p_5(Net_U23_P2), .p_6(Net_U23_P3), .p_7(DA7), .p_8(Earth), .p_9(Net_U23_S0), .p_10(Net_U23_S1), .p_11(Untitled_Sheet_U16_CP), .p_12(DA8), .p_13(DA9), .p_14(DA10), .p_15(DA11_INT), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U24 (.p_1(n_5V), .p_2(unconnected_U24_Dsr_Pad2), .p_3(Net_U24_P0), .p_4(Net_U24_P1), .p_5(Net_U24_P2), .p_6(Net_U24_P3), .p_7(DA3), .p_8(Earth), .p_9(Net_U23_S0), .p_10(Net_U23_S1), .p_11(Untitled_Sheet_U16_CP), .p_12(DA4), .p_13(DA5), .p_14(DA6), .p_15(DA7), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls244 U25 (.p_1(Net_U25A_OE), .p_2(Net_U25A_I0), .p_3(DAB4), .p_4(Net_U25A_I1), .p_5(DAB5), .p_6(Net_U25A_I2), .p_7(DAB6), .p_8(Net_U25A_I3), .p_9(DAB7), .p_10(Earth), .p_11(Net_U25B_I0), .p_12(DAB0), .p_13(Net_U25B_I1), .p_14(DAB1), .p_15(Net_U25B_I2), .p_16(DAB2), .p_17(Net_U25B_I3), .p_18(DAB3), .p_19(Net_U25A_OE), .p_20(n_5V)); // 74LS244 74LS244
  ttl_74ls244 U26 (.p_1(Net_U25A_OE), .p_2(Net_U26A_I0), .p_3(DAB12), .p_4(Net_U26A_I1), .p_5(DAB13), .p_6(Net_U26A_I2), .p_7(DAB14), .p_8(Net_U26A_I3), .p_9(DAB15), .p_10(Earth), .p_11(Net_U26B_I0), .p_12(DAB8), .p_13(Net_U26B_I1), .p_14(DAB9), .p_15(Net_U26B_I2), .p_16(DAB10), .p_17(Net_U26B_I3), .p_18(DAB11), .p_19(Net_U25A_OE), .p_20(n_5V)); // 74LS244 74LS244
  ttl_74ls194a U27 (.p_1(n_5V), .p_2(unconnected_U27_Dsr_Pad2), .p_3(AD11), .p_4(AD11), .p_5(AD11), .p_6(AD11), .p_7(Net_U26A_I0), .p_8(Earth), .p_9(Net_U27_S0), .p_10(Net_U16_CEP), .p_11(Untitled_Sheet_U16_CP), .p_12(Net_U26B_I3), .p_13(Net_U26B_I2), .p_14(Net_U26B_I1), .p_15(Net_U26B_I0), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U28 (.p_1(n_5V), .p_2(unconnected_U28_Dsr_Pad2), .p_3(AD11), .p_4(AD10), .p_5(AD9), .p_6(AD8), .p_7(Net_U25B_I0), .p_8(Earth), .p_9(Net_U27_S0), .p_10(Net_U16_CEP), .p_11(Untitled_Sheet_U16_CP), .p_12(Net_U26A_I3), .p_13(Net_U26A_I2), .p_14(Net_U26A_I1), .p_15(Net_U26A_I0), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls109 U3 (.p_1(n_5V), .p_2(Net_U3A_J), .p_3(Net_U36_E), .p_4(Untitled_Sheet_U16_CP), .p_5(n_5V), .p_6(Net_U3A_Q), .p_7(unconnected_U3A_Q_Pad7), .p_8(Earth), .p_9(unconnected_U3B_Q_Pad9), .p_10(STROBE_n), .p_11(n_5V), .p_12(Untitled_Sheet_U16_CP), .p_13(Net_U3B_K), .p_14(Net_U1_QC), .p_15(n_5V), .p_16(n_5V)); // 74LS109 74LS109
  ttl_74ls194a U34 (.p_1(n_5V), .p_2(unconnected_U34_Dsr_Pad2), .p_3(Net_U34_P0), .p_4(Net_U34_P1), .p_5(Net_U34_P2), .p_6(Net_U34_P3), .p_7(Net_U34_Dsl), .p_8(Earth), .p_9(Net_U23_S0), .p_10(Net_U23_S1), .p_11(Untitled_Sheet_U16_CP), .p_12(DA0), .p_13(DA1), .p_14(DA2), .p_15(DA3), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U35 (.p_1(n_5V), .p_2(unconnected_U35_Dsr_Pad2), .p_3(Net_U35_P0), .p_4(Net_U35_P1), .p_5(Net_U35_P2), .p_6(Net_U35_P3), .p_7(unconnected_U35_Dsl_Pad7), .p_8(Earth), .p_9(Net_U23_S0), .p_10(Net_U23_S1), .p_11(Untitled_Sheet_U16_CP), .p_12(unconnected_U35_Q3_Pad12), .p_13(unconnected_U35_Q2_Pad13), .p_14(unconnected_U35_Q1_Pad14), .p_15(Net_U34_Dsl), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls377 U36 (.p_1(Net_U36_E), .p_2(Net_U34_P0), .p_3(DAB7), .p_4(DAB6), .p_5(Net_U34_P1), .p_6(Net_U34_P2), .p_7(DAB5), .p_8(DAB4), .p_9(Net_U34_P3), .p_10(Earth), .p_11(Untitled_Sheet_U16_CP), .p_12(Net_U35_P0), .p_13(DAB3), .p_14(DAB2), .p_15(Net_U35_P1), .p_16(Net_U35_P2), .p_17(DAB1), .p_18(DAB0), .p_19(Net_U35_P3), .p_20(n_5V)); // 74LS377 74LS377
  ttl_74ls377 U37 (.p_1(Net_U36_E), .p_2(Net_U23_P0), .p_3(DAB15), .p_4(DAB14), .p_5(Net_U23_P1), .p_6(Net_U23_P2), .p_7(DAB13), .p_8(DAB12), .p_9(Net_U23_P3), .p_10(Earth), .p_11(Untitled_Sheet_U16_CP), .p_12(Net_U24_P0), .p_13(DAB11), .p_14(DAB10), .p_15(Net_U24_P1), .p_16(Net_U24_P2), .p_17(DAB9), .p_18(DAB8), .p_19(Net_U24_P3), .p_20(n_5V)); // 74LS377 74LS377
  ttl_74ls194a U38 (.p_1(n_5V), .p_2(unconnected_U38_Dsr_Pad2), .p_3(AD7), .p_4(AD6), .p_5(AD5), .p_6(AD4), .p_7(Net_U25A_I0), .p_8(Earth), .p_9(Net_U27_S0), .p_10(Net_U16_CEP), .p_11(Untitled_Sheet_U16_CP), .p_12(Net_U25B_I3), .p_13(Net_U25B_I2), .p_14(Net_U25B_I1), .p_15(Net_U25B_I0), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls194a U39 (.p_1(n_5V), .p_2(unconnected_U39_Dsr_Pad2), .p_3(AD3), .p_4(AD2), .p_5(AD1), .p_6(AD0), .p_7(Earth), .p_8(Earth), .p_9(Net_U27_S0), .p_10(Net_U16_CEP), .p_11(Untitled_Sheet_U16_CP), .p_12(Net_U25A_I3), .p_13(Net_U25A_I2), .p_14(Net_U25A_I1), .p_15(Net_U25A_I0), .p_16(n_5V)); // 74LS194 74LS194
  ttl_74ls157 U4 (.p_1(FPC_DBUG), .p_2(RESET_n), .p_3(Net_U4_I1a), .p_4(Net_U4_Za), .p_5(FPC_CK), .p_6(O2), .p_7(Untitled_Sheet_U16_CP), .p_8(Earth), .p_9(Net_U25A_OE), .p_10(Net_U4_I1c), .p_11(RD_AD_n), .p_12(Net_U36_E), .p_13(Net_U4_I1c), .p_14(WR_DA_n), .p_15(Earth), .p_16(n_5V)); // 74LS157 74LS157
  ttl_sn74s163n U40 (.p_1(n_5V), .p_2(Untitled_Sheet_U16_CP), .p_3(SDAA), .p_4(SDAB), .p_5(SDAC), .p_6(SDAD), .p_7(Earth), .p_8(Earth), .p_9(Net_U36_E), .p_10(Earth), .p_11(Net_U40_QD), .p_12(Net_U40_QC), .p_13(Net_U40_QB), .p_14(Net_U40_QA), .p_15(unconnected_U40_TC_Pad15), .p_16(n_5V)); // 74LS163_2 -> 74LS163 74LS163
  ttl_sn74s163n U41 (.p_1(n_5V), .p_2(Untitled_Sheet_U16_CP), .p_3(Net_U40_QA), .p_4(Net_U40_QB), .p_5(Net_U40_QC), .p_6(Net_U40_QD), .p_7(Earth), .p_8(Earth), .p_9(Net_U1_L), .p_10(Earth), .p_11(Net_U41_QD), .p_12(Net_U41_QC), .p_13(Net_U41_QB), .p_14(Net_U41_QA), .p_15(unconnected_U41_TC_Pad15), .p_16(n_5V)); // 74LS163_3 -> 74LS163 74LS163
  ttl_74ls157 U42 (.p_1(FPC_DBUG), .p_2(Net_U41_QA), .p_3(Net_U42_I1a), .p_4(OUTA), .p_5(Net_U41_QB), .p_6(Net_U42_I1a), .p_7(OUTB), .p_8(Earth), .p_9(OUTC), .p_10(Net_U42_I1c), .p_11(Net_U41_QC), .p_12(OUTD), .p_13(Net_U42_I1c), .p_14(Net_U41_QD), .p_15(STROBE_n), .p_16(n_5V)); // 74LS157 74LS157
  ttl_sn74s163n U43 (.p_1(n_5V), .p_2(Untitled_Sheet_U16_CP), .p_3(Earth), .p_4(Earth), .p_5(n_5V), .p_6(n_5V), .p_7(Net_U43_P), .p_8(Earth), .p_9(Net_U1_L), .p_10(n_5V), .p_11(unconnected_U43_QD_Pad11), .p_12(unconnected_U43_QC_Pad12), .p_13(OGA1), .p_14(OGA0), .p_15(Net_U43_TC), .p_16(n_5V)); // 74LS163_3 -> 74LS163 74LS163
  ttl_74ls02 U46 (.p_7(Earth), .p_14(n_5V)); // 74LS02 74LS02
  ttl_74ls20 U5 (.p_1(Net_U6_G), .p_2(Net_U42_I1a), .p_4(Net_U6_A), .p_5(Net_U6_B), .p_6(Net_U4_I1a), .p_7(Earth), .p_8(Net_U4_I1c), .p_9(Net_U7_CET), .p_10(Net_U17_Pad2), .p_12(n_5V), .p_13(Net_U6_E), .p_14(n_5V)); // 74LS20 74LS20
  ttl_74s287 #(.INIT_FILE(FPC_U6_INIT_FILE), .DEFAULT_WORD(4'hx)) U6 (.p_1(Net_U6_G), .p_2(Net_U42_I1a), .p_3(Net_U6_E), .p_4(Net_U6_D), .p_5(Net_U6_A), .p_6(Net_U6_B), .p_7(Net_U6_C), .p_8(Earth), .p_9(Net_U27_S0), .p_10(Net_U18_D3), .p_11(Net_U18_D2), .p_12(Net_U18_D1), .p_13(Earth), .p_14(Earth), .p_15(Net_U6_H), .p_16(n_5V)); // 74LS257 -> 74S287 S287
  ttl_sn74s163n U7 (.p_1(Net_U4_Za), .p_2(Untitled_Sheet_U16_CP), .p_3(unconnected_U7_D0_Pad3), .p_4(unconnected_U7_D1_Pad4), .p_5(unconnected_U7_D2_Pad5), .p_6(unconnected_U7_D3_Pad6), .p_7(Net_U7_CEP), .p_8(Earth), .p_9(n_5V), .p_10(Net_U7_CET), .p_11(Net_U6_H), .p_12(Net_U6_G), .p_13(Net_U42_I1a), .p_14(Net_U6_E), .p_15(Net_U7_TC), .p_16(n_5V)); // 74LS163 74LS163
  ttl_sn74s163n U8 (.p_1(Net_U4_Za), .p_2(Untitled_Sheet_U16_CP), .p_3(unconnected_U8_D0_Pad3), .p_4(unconnected_U8_D1_Pad4), .p_5(unconnected_U8_D2_Pad5), .p_6(unconnected_U8_D3_Pad6), .p_7(Net_U7_CEP), .p_8(Earth), .p_9(n_5V), .p_10(n_5V), .p_11(Net_U6_D), .p_12(Net_U6_C), .p_13(Net_U6_B), .p_14(Net_U6_A), .p_15(Net_U7_CET), .p_16(n_5V)); // 74LS163 74LS163
endmodule

