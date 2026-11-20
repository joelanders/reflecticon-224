`timescale 1ns/1ps
// Auto-generated from KiCad netlist
// Source: ../schematics/netlists/tc.net

module bb_n_2N3906(p_1, p_2, p_3);
  inout p_1, p_2, p_3;
endmodule

module bb_C(p_1, p_2);
  inout p_1, p_2;
endmodule

module bb_D(p_1, p_2);
  inout p_1, p_2;
endmodule

module bb_L(p_1, p_2);
  inout p_1, p_2;
endmodule

module bb_LED(p_1, p_2);
  inout p_1, p_2;
endmodule

module bb_MC4044(p_1, p_2, p_3, p_4, p_5, p_6, p_7, p_8, p_9, p_10, p_11, p_12, p_13, p_14);
  inout p_1, p_2, p_3, p_4, p_5, p_6, p_7, p_8, p_9, p_10, p_11, p_12, p_13, p_14;
endmodule

module bb_Q_PNP_BCE(p_1, p_2, p_3);
  inout p_1, p_2, p_3;
endmodule

module tc_board_netlist(MC, MS0, MS1, MS2, MS4, MS6, MS7, MS8, FPC_CK, ARUCK, ARUCKE_n, AS0, AS1_n, DAB_RSTB, MEMAC, XFER_CK, SAT, DATA0, DATA1, DATA2, DATA3, DATA4, DATA5, DATA6, DATA7, WCS_DATA0_n, WCS_DATA1_n, WCS_DATA2_n, WCS_DATA3_n, WCS_DATA4_n, WCS_DATA5_n, WCS_DATA6_n, WCS_DATA7_n, WCSA0, WCSA1, WCSA2, WCSA3, WCSA4, WCSA5, WCSA6, OFST0, OFST1, OFST2, OFST3, OFST4, OFST5, OFST6, OFST7, OFST8, OFST9, OFST10, OFST11, OFST12, OFST13, OFST14, OFST15, WR_XREG_n, WR_DA_n, RD_XREG_n, RD_AD_n, SDAA, SDAB, SDAC, SDAD, MEMW_n, RDRREG_n, WA0_n, WA1_n, RA0_n, RA1_n, CSIGN_n, ZERO_n, RESET_n, XFER, WCS_CS, HALT, MWTC, MRDC, ADR0, ADR1, ADR2, ADR3, ADR4, ADR5, ADR6, ADR7, ADR8, ADR9, ADRA, ADRB, ADRC, ADRD, ADRE, ADRF, SBC_PHI2_ZD, XACK_n, GSTB, WSTB, ADR_SEL, HIGH_SPEED2_REQ, DPORT3_n, DPORT4_n, DPORT5_n, S0, S1, M0_n, M1_n, DAB_WSTB_n);
  inout MC, MS0, MS1, MS2, MS4, MS6, MS7, MS8, FPC_CK, ARUCK, ARUCKE_n, AS0, AS1_n, DAB_RSTB, MEMAC, XFER_CK, SAT, DATA0, DATA1, DATA2, DATA3, DATA4, DATA5, DATA6, DATA7, WCS_DATA0_n, WCS_DATA1_n, WCS_DATA2_n, WCS_DATA3_n, WCS_DATA4_n, WCS_DATA5_n, WCS_DATA6_n, WCS_DATA7_n, WCSA0, WCSA1, WCSA2, WCSA3, WCSA4, WCSA5, WCSA6, OFST0, OFST1, OFST2, OFST3, OFST4, OFST5, OFST6, OFST7, OFST8, OFST9, OFST10, OFST11, OFST12, OFST13, OFST14, OFST15, WR_XREG_n, WR_DA_n, RD_XREG_n, RD_AD_n, SDAA, SDAB, SDAC, SDAD, MEMW_n, RDRREG_n, WA0_n, WA1_n, RA0_n, RA1_n, CSIGN_n, ZERO_n, RESET_n, XFER, WCS_CS, HALT, MWTC, MRDC, ADR0, ADR1, ADR2, ADR3, ADR4, ADR5, ADR6, ADR7, ADR8, ADR9, ADRA, ADRB, ADRC, ADRD, ADRE, ADRF, SBC_PHI2_ZD, XACK_n, GSTB, WSTB, ADR_SEL, HIGH_SPEED2_REQ, DPORT3_n, DPORT4_n, DPORT5_n, S0, S1, M0_n, M1_n, DAB_WSTB_n;
  // Net declarations
  wire n_5V; // +5V
  wire HIGH_SPEED2_U8_D3; // /HIGH-SPEED2/U8_D3
  wire HIGH_SPEED2_U8_D7; // /HIGH-SPEED2/U8_D7
  wire HIGH_SPEED2_U9_D1; // /HIGH-SPEED2/U9_D1
  wire HIGH_SPEED2_U9_D2; // /HIGH-SPEED2/U9_D2
  wire HIGH_SPEED2_U9_Q4; // /HIGH-SPEED2/U9_Q4
  wire HIGH_SPEED2_U22_2K; // /HIGH-SPEED2/U22_2K
  wire HIGH_SPEED2_U22_CLK; // /HIGH-SPEED2/U22_CLK
  wire HIGH_SPEED2_U24_D; // /HIGH-SPEED2/U24_D
  wire HIGH_SPEED2_U28_D1; // /HIGH-SPEED2/U28_D1
  wire HIGH_SPEED2_U28_D2; // /HIGH-SPEED2/U28_D2
  wire HIGH_SPEED2_U36_1C; // /HIGH-SPEED2/U36_1C
  wire HIGH_SPEED2_U36_2C; // /HIGH-SPEED2/U36_2C
  wire HIGH_SPEED2_U36_3A; // /HIGH-SPEED2/U36_3A
  wire HIGH_SPEED2_U41_9; // /HIGH-SPEED2/U41_9
  wire HIGH_SPEED2_U50_OUT; // /HIGH-SPEED2/U50_OUT
  wire HIGH_SPEED2_U51_2A; // /HIGH-SPEED2/U51_2A
  wire HIGH_SPEED2_U52_CLR; // /HIGH-SPEED2/U52_CLR
  wire HIGH_SPEED2_U53_Q; // /HIGH-SPEED2/U53_Q
  wire HIGH_SPEED2_U368; // /HIGH-SPEED2/U368
  wire HIGH_SPEED1_DO_OUT; // /HIGH_SPEED1/DO_OUT
  wire HIGH_SPEED1_INA; // /HIGH_SPEED1/INA
  wire HIGH_SPEED1_MI0; // /HIGH_SPEED1/MI0
  wire HIGH_SPEED1_MI1; // /HIGH_SPEED1/MI1
  wire HIGH_SPEED1_MI2; // /HIGH_SPEED1/MI2
  wire HIGH_SPEED1_MI3; // /HIGH_SPEED1/MI3
  wire HIGH_SPEED1_MI4; // /HIGH_SPEED1/MI4
  wire HIGH_SPEED1_MI5; // /HIGH_SPEED1/MI5
  wire HIGH_SPEED1_MI6; // /HIGH_SPEED1/MI6
  wire HIGH_SPEED1_MI7; // /HIGH_SPEED1/MI7
  wire HIGH_SPEED1_MI8; // /HIGH_SPEED1/MI8
  wire HIGH_SPEED1_MI9; // /HIGH_SPEED1/MI9
  wire HIGH_SPEED1_MI10; // /HIGH_SPEED1/MI10
  wire HIGH_SPEED1_MI11; // /HIGH_SPEED1/MI11
  wire HIGH_SPEED1_MI12; // /HIGH_SPEED1/MI12
  wire HIGH_SPEED1_MI13; // /HIGH_SPEED1/MI13
  wire HIGH_SPEED1_MI14; // /HIGH_SPEED1/MI14
  wire HIGH_SPEED1_MI15; // /HIGH_SPEED1/MI15
  wire HIGH_SPEED1_MI16; // /HIGH_SPEED1/MI16
  wire HIGH_SPEED1_MI17; // /HIGH_SPEED1/MI17
  wire HIGH_SPEED1_MI18; // /HIGH_SPEED1/MI18
  wire HIGH_SPEED1_MI19; // /HIGH_SPEED1/MI19
  wire HIGH_SPEED1_MI20; // /HIGH_SPEED1/MI20
  wire HIGH_SPEED1_MI21; // /HIGH_SPEED1/MI21
  wire HIGH_SPEED1_MI22; // /HIGH_SPEED1/MI22
  wire HIGH_SPEED1_MI23; // /HIGH_SPEED1/MI23
  wire HIGH_SPEED1_MI24; // /HIGH_SPEED1/MI24
  wire HIGH_SPEED1_MI25; // /HIGH_SPEED1/MI25
  wire HIGH_SPEED1_MI26; // /HIGH_SPEED1/MI26
  wire HIGH_SPEED1_MI27; // /HIGH_SPEED1/MI27
  wire HIGH_SPEED1_MI28; // /HIGH_SPEED1/MI28
  wire HIGH_SPEED1_MI29; // /HIGH_SPEED1/MI29
  wire HIGH_SPEED1_MI30; // /HIGH_SPEED1/MI30
  wire HIGH_SPEED1_MI31; // /HIGH_SPEED1/MI31
  wire HIGH_SPEED1_OUTAB; // /HIGH_SPEED1/OUTAB
  wire HIGH_SPEED1_OUT_A1; // /HIGH_SPEED1/OUT_A1
  wire HIGH_SPEED1_PC0; // /HIGH_SPEED1/PC0
  wire HIGH_SPEED1_PC1; // /HIGH_SPEED1/PC1
  wire HIGH_SPEED1_PC2; // /HIGH_SPEED1/PC2
  wire HIGH_SPEED1_PC3; // /HIGH_SPEED1/PC3
  wire HIGH_SPEED1_PC4; // /HIGH_SPEED1/PC4
  wire HIGH_SPEED1_PC5; // /HIGH_SPEED1/PC5
  wire HIGH_SPEED1_PC6; // /HIGH_SPEED1/PC6
  wire HIGH_SPEED1_Q6_OUT; // /HIGH_SPEED1/Q6_OUT
  wire A28; // A28
  wire ARUCKE; // ARUCKE
  wire AS0_slash; // AS0{slash}
  wire C0_slash; // C0{slash}
  wire C1_slash; // C1{slash}
  wire C2_slash; // C2{slash}
  wire C3_slash; // C3{slash}
  wire C4_slash; // C4{slash}
  wire C5_slash; // C5{slash}
  wire CK; // CK
  wire DAB_RSTB_slash; // DAB_RSTB{slash}
  wire DP; // DP
  wire GNDPWR; // GNDPWR
  wire MEMR_slash; // MEMR{slash}
  wire Net_C32_Pad2; // Net-(C32-Pad2)
  wire Net_C45_Pad1; // Net-(C45-Pad1)
  wire Net_CR1_A; // Net-(CR1-A)
  wire Net_CR1_K; // Net-(CR1-K)
  wire Net_LED1_A; // Net-(LED1-A)
  wire Net_LED1_K; // Net-(LED1-K)
  wire Net_Q1_B; // Net-(Q1-B)
  wire Net_Q1_E; // Net-(Q1-E)
  wire Net_Q2_B; // Net-(Q2-B)
  wire Net_Q2_E; // Net-(Q2-E)
  wire Net_Q3_B; // Net-(Q3-B)
  wire Net_Q3_E; // Net-(Q3-E)
  wire Net_U1_CET; // Net-(U1-CET)
  wire Net_U1_MR; // Net-(U1-~{MR})
  wire Net_U2_RW; // Net-(U2-RW)
  wire Net_U3_G; // Net-(U3-G)
  wire Net_U8_D0; // Net-(U8-D0)
  wire Net_U8_D2; // Net-(U8-D2)
  wire Net_U8_D6; // Net-(U8-D6)
  wire Net_U10_P3; // Net-(U10-P3)
  wire Net_U11_P3; // Net-(U11-P3)
  wire Net_U13A_RCext; // Net-(U13A-RCext)
  wire Net_U13B_RCext; // Net-(U13B-RCext)
  wire Net_U15_RW; // Net-(U15-RW)
  wire Net_U16_G; // Net-(U16-G)
  wire Net_U17_QA; // Net-(U17-QA)
  wire Net_U18_QA; // Net-(U18-QA)
  wire Net_U18_QB; // Net-(U18-QB)
  wire Net_U18_QD; // Net-(U18-QD)
  wire Net_U19_D5; // Net-(U19-D5)
  wire Net_U19_D6; // Net-(U19-D6)
  wire Net_U19_Q4; // Net-(U19-Q4)
  wire Net_U21_2K; // Net-(U21-2K)
  wire Net_U22_1K; // Net-(U22-1K)
  wire Net_U23_Q1; // Net-(U23-~{Q1})
  wire Net_U24A_Q; // Net-(U24A-Q)
  wire Net_U24B_Q; // Net-(U24B-Q)
  wire Net_U25_1_CLR; // Net-(U25-1~CLR)
  wire Net_U25_2D; // Net-(U25-2D)
  wire Net_U26_2Q; // Net-(U26-2Q)
  wire Net_U27_AMP_IN; // Net-(U27-AMP_IN)
  wire Net_U27_D1; // Net-(U27-D1)
  wire Net_U27_DF; // Net-(U27-DF)
  wire Net_U27_PU; // Net-(U27-PU)
  wire Net_U27_V; // Net-(U27-V)
  wire Net_U29_RW; // Net-(U29-RW)
  wire Net_U30_G; // Net-(U30-G)
  wire Net_U33_Pad8; // Net-(U33-Pad8)
  wire Net_U33_Pad10; // Net-(U33-Pad10)
  wire Net_U35_Pad5; // Net-(U35-Pad5)
  wire Net_U36_1A; // Net-(U36-1A)
  wire Net_U36_3C; // Net-(U36-3C)
  wire Net_U39_1D; // Net-(U39-1D)
  wire Net_U40_1A; // Net-(U40-1A)
  wire Net_U43_RW; // Net-(U43-RW)
  wire Net_U44_G; // Net-(U44-G)
  wire Net_U47A_O1; // Net-(U47A-O1)
  wire Net_U47B_E; // Net-(U47B-E)
  wire Net_U51_2B; // Net-(U51-2B)
  wire Net_U52_Cp; // Net-(U52-Cp)
  wire Net_U52_D1; // Net-(U52-D1)
  wire Net_U52_Q1; // Net-(U52-~{Q1})
  wire Net_U53A_C; // Net-(U53A-C)
  wire Net_U53A_R; // Net-(U53A-~{R})
  wire Net_U54A_Q; // Net-(U54A-Q)
  wire Net_U54A_R; // Net-(U54A-~{R})
  wire Net_U54B_Q; // Net-(U54B-Q)
  wire PROT; // PROT
  wire RESET; // RESET
  wire RESETD; // RESETD
  wire TEST; // TEST
  wire U23_CLR; // U23_CLR
  wire U34C_OUT; // U34C_OUT
  wire U34D_OUT; // U34D_OUT
  wire unconnected_U1_D0_Pad3; // unconnected-(U1-D0-Pad3)
  wire unconnected_U1_D1_Pad4; // unconnected-(U1-D1-Pad4)
  wire unconnected_U1_D2_Pad5; // unconnected-(U1-D2-Pad5)
  wire unconnected_U1_D3_Pad6; // unconnected-(U1-D3-Pad6)
  wire unconnected_U1_Q3_Pad11; // unconnected-(U1-Q3-Pad11)
  wire unconnected_U1_TC_Pad15; // unconnected-(U1-TC-Pad15)
  wire unconnected_U4_RCO_Pad15; // unconnected-(U4-RCO-Pad15)
  wire unconnected_U5_RCO_Pad15; // unconnected-(U5-RCO-Pad15)
  wire unconnected_U9_D5_Pad14; // unconnected-(U9-D5-Pad14)
  wire unconnected_U9_Q5_Pad15; // unconnected-(U9-Q5-Pad15)
  wire unconnected_U10_J_Pad2; // unconnected-(U10-J-Pad2)
  wire unconnected_U10_Q0_Pad15; // unconnected-(U10-Q0-Pad15)
  wire unconnected_U10_Q1_Pad14; // unconnected-(U10-Q1-Pad14)
  wire unconnected_U10_Q3_Pad12; // unconnected-(U10-Q3-Pad12)
  wire unconnected_U10_K_Pad3; // unconnected-(U10-~{K}-Pad3)
  wire unconnected_U11_J_Pad2; // unconnected-(U11-J-Pad2)
  wire unconnected_U11_Q0_Pad15; // unconnected-(U11-Q0-Pad15)
  wire unconnected_U11_Q1_Pad14; // unconnected-(U11-Q1-Pad14)
  wire unconnected_U11_Q3_Pad12; // unconnected-(U11-Q3-Pad12)
  wire unconnected_U11_K_Pad3; // unconnected-(U11-~{K}-Pad3)
  wire unconnected_U13A_Q_Pad13; // unconnected-(U13A-Q-Pad13)
  wire unconnected_U13B_Q_Pad12; // unconnected-(U13B-~{Q}-Pad12)
  wire unconnected_U14_D0_Pad3; // unconnected-(U14-D0-Pad3)
  wire unconnected_U14_D1_Pad4; // unconnected-(U14-D1-Pad4)
  wire unconnected_U14_D2_Pad5; // unconnected-(U14-D2-Pad5)
  wire unconnected_U14_D3_Pad6; // unconnected-(U14-D3-Pad6)
  wire unconnected_U17_RCO_Pad15; // unconnected-(U17-RCO-Pad15)
  wire unconnected_U18_RCO_Pad15; // unconnected-(U18-RCO-Pad15)
  wire unconnected_U19_D7_Pad18; // unconnected-(U19-D7-Pad18)
  wire unconnected_U19_Q7_Pad19; // unconnected-(U19-Q7-Pad19)
  wire unconnected_U20_2_CLR_Pad14; // unconnected-(U20-2~CLR-Pad14)
  wire unconnected_U20_2_PRE_Pad10; // unconnected-(U20-2~PRE-Pad10)
  wire unconnected_U20_2_Q_Pad7; // unconnected-(U20-2~Q-Pad7)
  wire unconnected_U21_1_Q_Pad6; // unconnected-(U21-1~Q-Pad6)
  wire unconnected_U21_2_Q_Pad7; // unconnected-(U21-2~Q-Pad7)
  wire unconnected_U22_1_Q_Pad6; // unconnected-(U22-1~Q-Pad6)
  wire unconnected_U23_D3_Pad13; // unconnected-(U23-D3-Pad13)
  wire unconnected_U23_Q3_Pad15; // unconnected-(U23-Q3-Pad15)
  wire unconnected_U23_Q3_Pad14; // unconnected-(U23-~{Q3}-Pad14)
  wire unconnected_U24A_Q_Pad6; // unconnected-(U24A-~{Q}-Pad6)
  wire unconnected_U25_1Q_Pad5; // unconnected-(U25-1Q-Pad5)
  wire unconnected_U26_1CLK_Pad1; // unconnected-(U26-1CLK-Pad1)
  wire unconnected_U26_1J_Pad3; // unconnected-(U26-1J-Pad3)
  wire unconnected_U26_1K_Pad2; // unconnected-(U26-1K-Pad2)
  wire unconnected_U26_1Q_Pad5; // unconnected-(U26-1Q-Pad5)
  wire unconnected_U26_1_CLR_Pad15; // unconnected-(U26-1~CLR-Pad15)
  wire unconnected_U26_1_PRE_Pad4; // unconnected-(U26-1~PRE-Pad4)
  wire unconnected_U26_1_Q_Pad6; // unconnected-(U26-1~Q-Pad6)
  wire unconnected_U26_2_Q_Pad7; // unconnected-(U26-2~Q-Pad7)
  wire unconnected_U27_D2_Pad6; // unconnected-(U27-D2-Pad6)
  wire unconnected_U27_U2_Pad12; // unconnected-(U27-U2-Pad12)
  wire unconnected_U28_E_Pad15; // unconnected-(U28-E-Pad15)
  wire unconnected_U28_I0d_Pad14; // unconnected-(U28-I0d-Pad14)
  wire unconnected_U28_I1d_Pad13; // unconnected-(U28-I1d-Pad13)
  wire unconnected_U28_Zd_Pad12; // unconnected-(U28-Zd-Pad12)
  wire unconnected_U33_Pad1; // unconnected-(U33-Pad1)
  wire unconnected_U33_Pad2; // unconnected-(U33-Pad2)
  wire unconnected_U38_Pad3; // unconnected-(U38-Pad3)
  wire unconnected_U38_Pad4; // unconnected-(U38-Pad4)
  wire unconnected_U38_Pad5; // unconnected-(U38-Pad5)
  wire unconnected_U38_Pad6; // unconnected-(U38-Pad6)
  wire unconnected_U41_A_Pad3; // unconnected-(U41-A-Pad3)
  wire unconnected_U41_B_Pad4; // unconnected-(U41-B-Pad4)
  wire unconnected_U41_C_Pad5; // unconnected-(U41-C-Pad5)
  wire unconnected_U41_D_Pad6; // unconnected-(U41-D-Pad6)
  wire unconnected_U41_QA_Pad14; // unconnected-(U41-QA-Pad14)
  wire unconnected_U41_QB_Pad13; // unconnected-(U41-QB-Pad13)
  wire unconnected_U41_QC_Pad12; // unconnected-(U41-QC-Pad12)
  wire unconnected_U42_E_Pad15; // unconnected-(U42-E-Pad15)
  wire unconnected_U47A_O0_Pad4; // unconnected-(U47A-O0-Pad4)
  wire unconnected_U47B_O0_Pad12; // unconnected-(U47B-O0-Pad12)
  wire unconnected_U51_4A_Pad12; // unconnected-(U51-4A-Pad12)
  wire unconnected_U51_4B_Pad13; // unconnected-(U51-4B-Pad13)
  wire unconnected_U51_4Y_Pad11; // unconnected-(U51-4Y-Pad11)
  wire unconnected_U52_D2_Pad12; // unconnected-(U52-D2-Pad12)
  wire unconnected_U52_D3_Pad13; // unconnected-(U52-D3-Pad13)
  wire unconnected_U52_Q1_Pad7; // unconnected-(U52-Q1-Pad7)
  wire unconnected_U52_Q2_Pad10; // unconnected-(U52-Q2-Pad10)
  wire unconnected_U52_Q3_Pad15; // unconnected-(U52-Q3-Pad15)
  wire unconnected_U52_Q2_Pad11; // unconnected-(U52-~{Q2}-Pad11)
  wire unconnected_U52_Q3_Pad14; // unconnected-(U52-~{Q3}-Pad14)
  wire unconnected_U53A_Q_Pad5; // unconnected-(U53A-Q-Pad5)
  wire unconnected_U54A_Q_Pad6; // unconnected-(U54A-~{Q}-Pad6)
  wire unconnected_U56_QA_Pad14; // unconnected-(U56-QA-Pad14)
  wire unconnected_U56_QB_Pad13; // unconnected-(U56-QB-Pad13)
  wire unconnected_U56_QC_Pad12; // unconnected-(U56-QC-Pad12)

  // Common rail assumptions
  assign n_5V = 1'b1;
  assign GNDPWR = 1'b0;

  // Floating TTL input pins read high (overridable per pin)
  assign unconnected_U1_D0_Pad3 = 1'b1; // U1.3
  assign unconnected_U1_D1_Pad4 = 1'b1; // U1.4
  assign unconnected_U1_D2_Pad5 = 1'b1; // U1.5
  assign unconnected_U1_D3_Pad6 = 1'b1; // U1.6
  assign unconnected_U9_D5_Pad14 = 1'b1; // U9.14
  assign unconnected_U10_J_Pad2 = 1'b1; // U10.2
  assign unconnected_U10_K_Pad3 = 1'b1; // U10.3
  assign unconnected_U11_J_Pad2 = 1'b1; // U11.2
  assign unconnected_U11_K_Pad3 = 1'b1; // U11.3
  assign unconnected_U14_D0_Pad3 = 1'b1; // U14.3
  assign unconnected_U14_D1_Pad4 = 1'b1; // U14.4
  assign unconnected_U14_D2_Pad5 = 1'b1; // U14.5
  assign unconnected_U14_D3_Pad6 = 1'b1; // U14.6
  assign unconnected_U19_D7_Pad18 = 1'b1; // U19.18
  assign unconnected_U20_2_CLR_Pad14 = 1'b1; // U20.14
  assign unconnected_U20_2_PRE_Pad10 = 1'b1; // U20.10
  assign unconnected_U23_D3_Pad13 = 1'b1; // U23.13
  assign unconnected_U26_1CLK_Pad1 = 1'b1; // U26.1
  assign unconnected_U26_1J_Pad3 = 1'b1; // U26.3
  assign unconnected_U26_1K_Pad2 = 1'b1; // U26.2
  assign unconnected_U26_1_CLR_Pad15 = 1'b1; // U26.15
  assign unconnected_U26_1_PRE_Pad4 = 1'b1; // U26.4
  assign unconnected_U28_E_Pad15 = 1'b0; // U28.15
  assign unconnected_U28_I0d_Pad14 = 1'b1; // U28.14
  assign unconnected_U28_I1d_Pad13 = 1'b1; // U28.13
  assign unconnected_U33_Pad1 = 1'b1; // U33.1
  assign unconnected_U38_Pad3 = 1'b1; // U38.3
  assign unconnected_U38_Pad4 = 1'b1; // U38.4
  assign unconnected_U38_Pad5 = 1'b1; // U38.5
  assign unconnected_U41_A_Pad3 = 1'b1; // U41.3
  assign unconnected_U41_B_Pad4 = 1'b1; // U41.4
  assign unconnected_U41_C_Pad5 = 1'b1; // U41.5
  assign unconnected_U41_D_Pad6 = 1'b1; // U41.6
  assign unconnected_U42_E_Pad15 = 1'b0; // U42.15
  assign unconnected_U51_4A_Pad12 = 1'b1; // U51.12
  assign unconnected_U51_4B_Pad13 = 1'b1; // U51.13
  assign unconnected_U52_D2_Pad12 = 1'b1; // U52.12
  assign unconnected_U52_D3_Pad13 = 1'b1; // U52.13

  bb_C C11 (.p_1(GNDPWR), .p_2(Net_U13A_RCext)); // C 4.7
  bb_C C12 (.p_1(Net_CR1_A), .p_2(GNDPWR)); // C 5-30pF
  bb_C C13 (.p_1(Net_CR1_A), .p_2(GNDPWR)); // C 33pF
  bb_C C14 (.p_1(Net_Q1_B), .p_2(GNDPWR)); // C 0.01
  bb_C C15 (.p_1(n_5V), .p_2(GNDPWR)); // C 0.01
  bb_C C17 (.p_1(Net_CR1_A), .p_2(Net_Q2_B)); // C 120pF
  bb_C C18 (.p_1(GNDPWR), .p_2(Net_Q1_E)); // C 15pF
  bb_C C19 (.p_1(Net_Q1_E), .p_2(Net_CR1_A)); // C 10pF
  bb_C C31 (.p_1(Net_CR1_K), .p_2(GNDPWR)); // C 300pF
  bb_C C32 (.p_1(GNDPWR), .p_2(Net_C32_Pad2)); // C 1000pF
  bb_C C45 (.p_1(Net_C45_Pad1), .p_2(Net_CR1_K)); // C 680pF
  bb_C C46 (.p_1(Net_U26_2Q), .p_2(Net_Q3_B)); // C 30pF
  bb_C C9 (.p_1(GNDPWR), .p_2(Net_U13B_RCext)); // C 100pF
  bb_D CR1 (.p_1(Net_CR1_K), .p_2(Net_CR1_A)); // D MV209
  bb_L L1 (.p_1(Net_CR1_A), .p_2(GNDPWR)); // L 0.10uH
  bb_LED LED1 (.p_1(Net_LED1_K), .p_2(Net_LED1_A)); // LED LED
  bb_n_2N3906 Q1 (.p_1(Net_Q1_E), .p_2(Net_Q1_B), .p_3(Net_CR1_A)); // 2N3906 2N5910
  bb_n_2N3906 Q2 (.p_1(Net_Q2_E), .p_2(Net_Q2_B), .p_3(GNDPWR)); // 2N3906 2N5910
  bb_Q_PNP_BCE Q3 (.p_1(Net_Q3_B), .p_2(MC), .p_3(Net_Q3_E)); // Q_PNP_BCE 2n5910
  ideal_resistor R1 (.p_1(n_5V), .p_2(Net_LED1_A)); // R 390
  ideal_resistor R10 (.p_1(Net_CR1_K), .p_2(n_5V)); // R 1K
  ideal_resistor R11 (.p_1(Net_U27_DF), .p_2(Net_C32_Pad2)); // R 1K
  ideal_resistor R12 (.p_1(Net_U27_AMP_IN), .p_2(Net_C32_Pad2)); // R 1K
  ideal_resistor R13 (.p_1(Net_U27_AMP_IN), .p_2(Net_C45_Pad1)); // R 100
  ideal_resistor R14 (.p_1(Net_Q3_B), .p_2(Net_U26_2Q)); // R 1.2K
  ideal_resistor R15 (.p_1(Net_Q3_E), .p_2(Net_Q3_B)); // R 220
  ideal_resistor R16 (.p_1(n_5V), .p_2(Net_Q3_E)); // R 20
  ideal_resistor R3 (.p_1(Net_U13B_RCext), .p_2(n_5V)); // R 10K
  ideal_resistor R4 (.p_1(Net_U13A_RCext), .p_2(n_5V)); // R 120K
  ideal_resistor R5 (.p_1(Net_Q2_B), .p_2(Net_CR1_A)); // R 1K
  ideal_resistor R6 (.p_1(Net_Q1_B), .p_2(n_5V)); // R 1K
  ideal_resistor R7 (.p_1(GNDPWR), .p_2(Net_Q1_B)); // R 3K
  ideal_resistor R8 (.p_1(Net_Q2_E), .p_2(n_5V)); // R 270
  ideal_resistor R9 (.p_1(Net_Q1_E), .p_2(n_5V)); // R 270
  ttl_sn74s163n U1 (.p_1(Net_U1_MR), .p_2(DAB_RSTB_slash), .p_3(unconnected_U1_D0_Pad3), .p_4(unconnected_U1_D1_Pad4), .p_5(unconnected_U1_D2_Pad5), .p_6(unconnected_U1_D3_Pad6), .p_7(n_5V), .p_8(GNDPWR), .p_9(n_5V), .p_10(Net_U1_CET), .p_11(unconnected_U1_Q3_Pad11), .p_12(HIGH_SPEED1_PC6), .p_13(HIGH_SPEED1_PC5), .p_14(HIGH_SPEED1_PC4), .p_15(unconnected_U1_TC_Pad15), .p_16(n_5V)); // 74LS163 74LS163
  ttl_74ls195 U10 (.p_1(n_5V), .p_2(unconnected_U10_J_Pad2), .p_3(unconnected_U10_K_Pad3), .p_4(C1_slash), .p_5(C3_slash), .p_6(C5_slash), .p_7(Net_U10_P3), .p_8(GNDPWR), .p_9(AS1_n), .p_10(ARUCKE), .p_11(M1_n), .p_12(unconnected_U10_Q3_Pad12), .p_13(Net_U10_P3), .p_14(unconnected_U10_Q1_Pad14), .p_15(unconnected_U10_Q0_Pad15), .p_16(n_5V)); // 74LS195 74195
  ttl_74ls195 U11 (.p_1(n_5V), .p_2(unconnected_U11_J_Pad2), .p_3(unconnected_U11_K_Pad3), .p_4(C0_slash), .p_5(C2_slash), .p_6(C4_slash), .p_7(Net_U11_P3), .p_8(GNDPWR), .p_9(AS1_n), .p_10(ARUCKE), .p_11(M0_n), .p_12(unconnected_U11_Q3_Pad12), .p_13(Net_U11_P3), .p_14(unconnected_U11_Q1_Pad14), .p_15(unconnected_U11_Q0_Pad15), .p_16(n_5V)); // 74LS195 74195
  ttl_74ls86 U12 (.p_1(S0), .p_2(S1), .p_3(Net_U8_D2), .p_4(M0_n), .p_5(M1_n), .p_6(HIGH_SPEED2_U8_D7), .p_7(GNDPWR), .p_8(Net_U36_1A), .p_9(n_5V), .p_10(HIGH_SPEED2_U9_Q4), .p_14(n_5V)); // 74LS86 74LS86
  ttl_74ls123 U13 (.p_1(GNDPWR), .p_2(HIGH_SPEED2_U9_Q4), .p_3(n_5V), .p_4(HIGH_SPEED2_U36_1C), .p_5(Net_LED1_K), .p_6(GNDPWR), .p_7(Net_U13B_RCext), .p_8(GNDPWR), .p_9(CK), .p_10(n_5V), .p_11(n_5V), .p_12(unconnected_U13B_Q_Pad12), .p_13(unconnected_U13A_Q_Pad13), .p_14(GNDPWR), .p_15(Net_U13A_RCext), .p_16(n_5V)); // 74LS123 74LS123
  ttl_sn74s163n U14 (.p_1(Net_U1_MR), .p_2(DAB_RSTB_slash), .p_3(unconnected_U14_D0_Pad3), .p_4(unconnected_U14_D1_Pad4), .p_5(unconnected_U14_D2_Pad5), .p_6(unconnected_U14_D3_Pad6), .p_7(n_5V), .p_8(GNDPWR), .p_9(n_5V), .p_10(HALT), .p_11(HIGH_SPEED1_PC3), .p_12(HIGH_SPEED1_PC2), .p_13(HIGH_SPEED1_PC1), .p_14(HIGH_SPEED1_PC0), .p_15(Net_U1_CET), .p_16(n_5V)); // 74LS163 74LS163
  ttl_mcm68b10 #(.INIT_FILE("out/wcs_lane_b2.hex")) U15 (.p_1(GNDPWR), .p_2(HIGH_SPEED1_MI16), .p_3(HIGH_SPEED1_MI17), .p_4(HIGH_SPEED1_MI18), .p_5(HIGH_SPEED1_MI19), .p_6(HIGH_SPEED1_MI20), .p_7(HIGH_SPEED1_MI21), .p_8(HIGH_SPEED1_MI22), .p_9(HIGH_SPEED1_MI23), .p_10(WCS_CS), .p_11(GNDPWR), .p_12(GNDPWR), .p_13(WCS_CS), .p_14(GNDPWR), .p_15(GNDPWR), .p_16(Net_U15_RW), .p_17(WCSA6), .p_18(WCSA5), .p_19(WCSA4), .p_20(WCSA3), .p_21(WCSA2), .p_22(WCSA1), .p_23(WCSA0), .p_24(n_5V)); // MCM68B10 MCM68B10
  ttl_am8304n U16 (.p_1(HIGH_SPEED1_MI16), .p_2(HIGH_SPEED1_MI17), .p_3(HIGH_SPEED1_MI18), .p_4(HIGH_SPEED1_MI19), .p_5(HIGH_SPEED1_MI20), .p_6(HIGH_SPEED1_MI21), .p_7(HIGH_SPEED1_MI22), .p_8(HIGH_SPEED1_MI23), .p_9(Net_U16_G), .p_10(GNDPWR), .p_11(Net_U15_RW), .p_12(WCS_DATA7_n), .p_13(WCS_DATA6_n), .p_14(WCS_DATA5_n), .p_15(WCS_DATA4_n), .p_16(WCS_DATA3_n), .p_17(WCS_DATA2_n), .p_18(WCS_DATA1_n), .p_19(WCS_DATA0_n), .p_20(n_5V)); // AMB304_2 -> AMB304 AMB304
  ttl_sn74s163n U17 (.p_1(ADR_SEL), .p_2(DAB_RSTB_slash), .p_3(HIGH_SPEED1_MI16), .p_4(HIGH_SPEED1_MI17), .p_5(HIGH_SPEED1_MI18), .p_6(HIGH_SPEED1_MI19), .p_7(GNDPWR), .p_8(GNDPWR), .p_9(GNDPWR), .p_10(GNDPWR), .p_11(WA1_n), .p_12(WA0_n), .p_13(MEMAC), .p_14(Net_U17_QA), .p_15(unconnected_U17_RCO_Pad15), .p_16(n_5V)); // SN74S163N SN74S163N
  ttl_sn74s163n U18 (.p_1(ADR_SEL), .p_2(DAB_RSTB_slash), .p_3(HIGH_SPEED1_MI20), .p_4(HIGH_SPEED1_MI21), .p_5(HIGH_SPEED1_MI22), .p_6(HIGH_SPEED1_MI23), .p_7(GNDPWR), .p_8(GNDPWR), .p_9(GNDPWR), .p_10(GNDPWR), .p_11(Net_U18_QD), .p_12(PROT), .p_13(Net_U18_QB), .p_14(Net_U18_QA), .p_15(unconnected_U18_RCO_Pad15), .p_16(n_5V)); // SN74S163N SN74S163N
  ttl_74ls377 U19 (.p_1(AS0_slash), .p_2(DP), .p_3(HIGH_SPEED1_DO_OUT), .p_4(Net_U18_QA), .p_5(RA0_n), .p_6(RA1_n), .p_7(Net_U18_QB), .p_8(RESET_n), .p_9(RESETD), .p_10(GNDPWR), .p_11(ARUCKE), .p_12(Net_U19_Q4), .p_13(Net_U18_QD), .p_14(Net_U19_D5), .p_15(XFER), .p_16(HIGH_SPEED1_Q6_OUT), .p_17(Net_U19_D6), .p_18(unconnected_U19_D7_Pad18), .p_19(unconnected_U19_Q7_Pad19), .p_20(n_5V)); // 74LS377 74LS377
  ttl_mcm68b10 #(.INIT_FILE("out/wcs_lane_b3.hex")) U2 (.p_1(GNDPWR), .p_2(HIGH_SPEED1_MI24), .p_3(HIGH_SPEED1_MI25), .p_4(HIGH_SPEED1_MI26), .p_5(HIGH_SPEED1_MI27), .p_6(HIGH_SPEED1_MI28), .p_7(HIGH_SPEED1_MI29), .p_8(HIGH_SPEED1_MI30), .p_9(HIGH_SPEED1_MI31), .p_10(WCS_CS), .p_11(GNDPWR), .p_12(GNDPWR), .p_13(WCS_CS), .p_14(GNDPWR), .p_15(GNDPWR), .p_16(Net_U2_RW), .p_17(WCSA6), .p_18(WCSA5), .p_19(WCSA4), .p_20(WCSA3), .p_21(WCSA2), .p_22(WCSA1), .p_23(WCSA0), .p_24(n_5V)); // MCM68B10 MCM68B10
  ttl_sn74s112an U20 (.p_1(HIGH_SPEED2_U22_CLK), .p_2(MS8), .p_3(MS1), .p_4(n_5V), .p_5(DAB_RSTB), .p_6(DAB_RSTB_slash), .p_7(unconnected_U20_2_Q_Pad7), .p_8(GNDPWR), .p_9(CSIGN_n), .p_10(unconnected_U20_2_PRE_Pad10), .p_11(U34D_OUT), .p_12(U34C_OUT), .p_13(ARUCKE_n), .p_14(unconnected_U20_2_CLR_Pad14), .p_15(n_5V), .p_16(n_5V)); // SN74S112AN 74S112
  ttl_sn74s112an U21 (.p_1(HIGH_SPEED2_U22_CLK), .p_2(HIGH_SPEED2_U24_D), .p_3(HIGH_SPEED2_U22_2K), .p_4(n_5V), .p_5(GSTB), .p_6(unconnected_U21_1_Q_Pad6), .p_7(unconnected_U21_2_Q_Pad7), .p_8(GNDPWR), .p_9(WCS_CS), .p_10(n_5V), .p_11(MS2), .p_12(Net_U21_2K), .p_13(HIGH_SPEED2_U22_CLK), .p_14(n_5V), .p_15(n_5V), .p_16(n_5V)); // SN74S112AN 74S112
  ttl_sn74s112an U22 (.p_1(HIGH_SPEED2_U22_CLK), .p_2(Net_U22_1K), .p_3(ADR_SEL), .p_4(n_5V), .p_5(WSTB), .p_6(unconnected_U22_1_Q_Pad6), .p_7(ADR_SEL), .p_8(GNDPWR), .p_9(HIGH_SPEED2_U24_D), .p_10(n_5V), .p_11(Net_U22_1K), .p_12(HIGH_SPEED2_U22_2K), .p_13(HIGH_SPEED2_U22_CLK), .p_14(n_5V), .p_15(n_5V), .p_16(n_5V)); // SN74S112AN 74S112
  ttl_74ls175 #(.CLK_TPD(13.0), .CLR_TPD(13.0)) U23 (.p_1(U23_CLR), .p_2(AS1_n), .p_3(HIGH_SPEED2_U28_D1), .p_4(n_5V), .p_5(HIGH_SPEED2_U28_D1), .p_6(Net_U23_Q1), .p_7(HIGH_SPEED2_U28_D2), .p_8(GNDPWR), .p_9(ARUCK), .p_10(AS0), .p_11(AS0_slash), .p_12(HIGH_SPEED2_U28_D2), .p_13(unconnected_U23_D3_Pad13), .p_14(unconnected_U23_Q3_Pad14), .p_15(unconnected_U23_Q3_Pad15), .p_16(n_5V)); // 74LS175 74LS175
  ttl_74ls74 U24 (.p_1(n_5V), .p_2(HIGH_SPEED2_U24_D), .p_3(HIGH_SPEED2_U28_D2), .p_4(n_5V), .p_5(Net_U24A_Q), .p_6(unconnected_U24A_Q_Pad6), .p_7(GNDPWR), .p_8(S0), .p_9(Net_U24B_Q), .p_10(n_5V), .p_11(HIGH_SPEED2_U28_D2), .p_12(Net_U24A_Q), .p_13(n_5V), .p_14(n_5V)); // 74LS74 74LS74
  ttl_74ls74 #(.TPD(9.0)) U25 (.p_1(Net_U25_1_CLR), .p_2(RESET), .p_3(DAB_RSTB_slash), .p_4(n_5V), .p_5(unconnected_U25_1Q_Pad5), .p_6(RESET_n), .p_7(GNDPWR), .p_8(ARUCKE), .p_9(HIGH_SPEED2_U36_2C), .p_10(n_5V), .p_11(MC), .p_12(Net_U25_2D), .p_13(n_5V), .p_14(n_5V)); // SN74S74N 74S74
  ttl_sn74s112an U26 (.p_1(unconnected_U26_1CLK_Pad1), .p_2(unconnected_U26_1K_Pad2), .p_3(unconnected_U26_1J_Pad3), .p_4(unconnected_U26_1_PRE_Pad4), .p_5(unconnected_U26_1Q_Pad5), .p_6(unconnected_U26_1_Q_Pad6), .p_7(unconnected_U26_2_Q_Pad7), .p_8(GNDPWR), .p_9(Net_U26_2Q), .p_10(n_5V), .p_11(n_5V), .p_12(n_5V), .p_13(Net_Q2_E), .p_14(n_5V), .p_15(unconnected_U26_1_CLR_Pad15), .p_16(n_5V)); // SN74S112AN 74S112
  bb_MC4044 U27 (.p_1(A28), .p_2(Net_U27_D1), .p_3(Net_U27_V), .p_4(Net_U27_PU), .p_5(Net_U27_DF), .p_6(unconnected_U27_D2_Pad6), .p_7(GNDPWR), .p_8(Net_CR1_K), .p_9(Net_U27_AMP_IN), .p_10(Net_U27_DF), .p_11(Net_U27_D1), .p_12(unconnected_U27_U2_Pad12), .p_13(Net_U27_PU), .p_14(n_5V)); // MC4044 MC4044
  ttl_74ls157 #(.TPLH(9.0), .TPHL(9.0)) U28 (.p_1(ADR_SEL), .p_2(ADR6), .p_3(HIGH_SPEED1_PC4), .p_4(WCSA4), .p_5(ADR7), .p_6(HIGH_SPEED1_PC5), .p_7(WCSA5), .p_8(GNDPWR), .p_9(WCSA6), .p_10(HIGH_SPEED1_PC6), .p_11(ADR8), .p_12(unconnected_U28_Zd_Pad12), .p_13(unconnected_U28_I1d_Pad13), .p_14(unconnected_U28_I0d_Pad14), .p_15(unconnected_U28_E_Pad15), .p_16(n_5V)); // 74LS157 74F157
  ttl_mcm68b10 #(.INIT_FILE("out/wcs_lane_b1.hex")) U29 (.p_1(GNDPWR), .p_2(HIGH_SPEED1_MI8), .p_3(HIGH_SPEED1_MI9), .p_4(HIGH_SPEED1_MI10), .p_5(HIGH_SPEED1_MI11), .p_6(HIGH_SPEED1_MI12), .p_7(HIGH_SPEED1_MI13), .p_8(HIGH_SPEED1_MI14), .p_9(HIGH_SPEED1_MI15), .p_10(WCS_CS), .p_11(GNDPWR), .p_12(GNDPWR), .p_13(WCS_CS), .p_14(GNDPWR), .p_15(GNDPWR), .p_16(Net_U29_RW), .p_17(WCSA6), .p_18(WCSA5), .p_19(WCSA4), .p_20(WCSA3), .p_21(WCSA2), .p_22(WCSA1), .p_23(WCSA0), .p_24(n_5V)); // MCM68B10 MCM68B10
  ttl_am8304n U3 (.p_1(HIGH_SPEED1_MI24), .p_2(HIGH_SPEED1_MI25), .p_3(HIGH_SPEED1_MI26), .p_4(HIGH_SPEED1_MI27), .p_5(HIGH_SPEED1_MI28), .p_6(HIGH_SPEED1_MI29), .p_7(HIGH_SPEED1_MI30), .p_8(HIGH_SPEED1_MI31), .p_9(Net_U3_G), .p_10(GNDPWR), .p_11(Net_U2_RW), .p_12(WCS_DATA7_n), .p_13(WCS_DATA6_n), .p_14(WCS_DATA5_n), .p_15(WCS_DATA4_n), .p_16(WCS_DATA3_n), .p_17(WCS_DATA2_n), .p_18(WCS_DATA1_n), .p_19(WCS_DATA0_n), .p_20(n_5V)); // AMB304_3 -> AMB304 AMB304
  ttl_am8304n U30 (.p_1(HIGH_SPEED1_MI8), .p_2(HIGH_SPEED1_MI9), .p_3(HIGH_SPEED1_MI10), .p_4(HIGH_SPEED1_MI11), .p_5(HIGH_SPEED1_MI12), .p_6(HIGH_SPEED1_MI13), .p_7(HIGH_SPEED1_MI14), .p_8(HIGH_SPEED1_MI15), .p_9(Net_U30_G), .p_10(GNDPWR), .p_11(Net_U29_RW), .p_12(WCS_DATA7_n), .p_13(WCS_DATA6_n), .p_14(WCS_DATA5_n), .p_15(WCS_DATA4_n), .p_16(WCS_DATA3_n), .p_17(WCS_DATA2_n), .p_18(WCS_DATA1_n), .p_19(WCS_DATA0_n), .p_20(n_5V)); // AMB304_1 -> AMB304 AMB304
  ttl_sn74f374n U31 (.p_1(GNDPWR), .p_2(OFST9), .p_3(HIGH_SPEED1_MI9), .p_4(HIGH_SPEED1_MI11), .p_5(OFST11), .p_6(OFST13), .p_7(HIGH_SPEED1_MI13), .p_8(HIGH_SPEED1_MI15), .p_9(OFST15), .p_10(GNDPWR), .p_11(DAB_RSTB_slash), .p_12(OFST14), .p_13(HIGH_SPEED1_MI14), .p_14(HIGH_SPEED1_MI12), .p_15(OFST12), .p_16(OFST10), .p_17(HIGH_SPEED1_MI10), .p_18(HIGH_SPEED1_MI8), .p_19(OFST8), .p_20(n_5V)); // SN74F374N SN74F374N
  ttl_74ls08 U32 (.p_1(OFST8), .p_2(OFST8), .p_3(SDAD), .p_4(OFST10), .p_5(OFST10), .p_6(SDAB), .p_7(GNDPWR), .p_8(SDAA), .p_9(OFST11), .p_10(OFST11), .p_11(SDAC), .p_12(OFST9), .p_13(OFST9), .p_14(n_5V)); // 74LS08 74LS08
  ttl_74ls04 U33 (.p_1(unconnected_U33_Pad1), .p_2(unconnected_U33_Pad2), .p_3(Net_U47A_O1), .p_4(HIGH_SPEED1_OUTAB), .p_5(PROT), .p_6(Net_U51_2B), .p_7(GNDPWR), .p_8(Net_U33_Pad8), .p_9(Net_U19_Q4), .p_10(Net_U33_Pad10), .p_11(ADRE), .p_12(Net_U1_MR), .p_13(RESET), .p_14(n_5V)); // 74LS04 74LS04
  ttl_74ls08 U34 (.p_1(HIGH_SPEED1_OUTAB), .p_2(OFST4), .p_3(HIGH_SPEED1_DO_OUT), .p_4(HIGH_SPEED1_OUTAB), .p_5(OFST3), .p_6(RESET), .p_7(GNDPWR), .p_8(U34C_OUT), .p_9(Net_U33_Pad8), .p_10(AS0), .p_11(U34D_OUT), .p_12(AS0), .p_13(Net_U19_Q4), .p_14(n_5V)); // 74LS08 74LS08
  ttl_74ls08 U35 (.p_1(HIGH_SPEED2_REQ), .p_2(MS0), .p_3(Net_U22_1K), .p_4(MS0), .p_5(Net_U35_Pad5), .p_6(HIGH_SPEED2_U22_2K), .p_7(GNDPWR), .p_8(Net_U52_Cp), .p_9(HIGH_SPEED2_U368), .p_10(MS6), .p_11(Net_U21_2K), .p_12(MS8), .p_13(HIGH_SPEED2_U53_Q), .p_14(n_5V)); // 74LS08 74S08
  ttl_74ls10 #(.TPLH(3.0), .TPHL(3.0)) U36 (.p_1(Net_U36_1A), .p_2(Net_U36_1A), .p_3(AS0), .p_4(XFER), .p_5(HIGH_SPEED2_U36_2C), .p_6(XFER_CK), .p_7(GNDPWR), .p_8(HIGH_SPEED2_U368), .p_9(HIGH_SPEED2_U36_3A), .p_10(HALT), .p_11(Net_U36_3C), .p_12(HIGH_SPEED2_U8_D3), .p_13(HIGH_SPEED2_U36_1C), .p_14(n_5V)); // SN74S10N 74S10
  ttl_74ls04 U37 (.p_1(MC), .p_2(HIGH_SPEED2_U22_CLK), .p_3(MRDC), .p_4(Net_U53A_R), .p_5(MS7), .p_6(DAB_WSTB_n), .p_7(GNDPWR), .p_8(Net_U25_1_CLR), .p_9(MS6), .p_10(Net_U53A_C), .p_11(HIGH_SPEED2_U50_OUT), .p_12(Net_U35_Pad5), .p_13(HIGH_SPEED2_REQ), .p_14(n_5V)); // 74LS04 74S04
  ttl_74ls27 #(.TPLH(8.0), .TPHL(8.0)) U38 (.p_1(DP), .p_2(Net_U23_Q1), .p_3(unconnected_U38_Pad3), .p_4(unconnected_U38_Pad4), .p_5(unconnected_U38_Pad5), .p_6(unconnected_U38_Pad6), .p_7(GNDPWR), .p_8(Net_U25_2D), .p_9(MS8), .p_10(FPC_CK), .p_11(MS2), .p_12(S1), .p_13(Net_U24B_Q), .p_14(n_5V)); // 74LS27 74LS27
  ttl_sn74f374n U39 (.p_1(GNDPWR), .p_2(MS4), .p_3(Net_U39_1D), .p_4(MS4), .p_5(FPC_CK), .p_6(MS6), .p_7(FPC_CK), .p_8(MS6), .p_9(MS7), .p_10(GNDPWR), .p_11(MC), .p_12(MS8), .p_13(MS7), .p_14(MS8), .p_15(MS0), .p_16(MS1), .p_17(MS0), .p_18(MS1), .p_19(MS2), .p_20(n_5V)); // SN74F374N 74F374
  ttl_sn74s163n U4 (.p_1(ADR_SEL), .p_2(DAB_RSTB_slash), .p_3(HIGH_SPEED1_MI24), .p_4(HIGH_SPEED1_MI25), .p_5(HIGH_SPEED1_MI26), .p_6(HIGH_SPEED1_MI27), .p_7(GNDPWR), .p_8(GNDPWR), .p_9(GNDPWR), .p_10(GNDPWR), .p_11(C1_slash), .p_12(C0_slash), .p_13(Net_U19_D6), .p_14(Net_U19_D5), .p_15(unconnected_U4_RCO_Pad15), .p_16(n_5V)); // SN74S163N SN74S163N
  ttl_74ls00 U40 (.p_1(Net_U40_1A), .p_2(Net_U40_1A), .p_3(HIGH_SPEED2_U41_9), .p_4(ARUCKE), .p_5(n_5V), .p_6(ARUCKE_n), .p_7(GNDPWR), .p_8(ARUCK), .p_9(ARUCKE_n), .p_10(n_5V), .p_12(n_5V), .p_13(Net_U26_2Q), .p_14(n_5V)); // SN74S00N 74S00
  ttl_sn74s163n U41 (.p_1(n_5V), .p_2(MC), .p_3(unconnected_U41_A_Pad3), .p_4(unconnected_U41_B_Pad4), .p_5(unconnected_U41_C_Pad5), .p_6(unconnected_U41_D_Pad6), .p_7(n_5V), .p_8(GNDPWR), .p_9(HIGH_SPEED2_U41_9), .p_10(n_5V), .p_11(Net_U27_V), .p_12(unconnected_U41_QC_Pad12), .p_13(unconnected_U41_QB_Pad13), .p_14(unconnected_U41_QA_Pad14), .p_15(Net_U40_1A), .p_16(n_5V)); // SN74S163N 74S163
  ttl_74ls157 #(.TPLH(9.0), .TPHL(9.0)) U42 (.p_1(ADR_SEL), .p_2(ADR2), .p_3(HIGH_SPEED1_PC0), .p_4(WCSA0), .p_5(ADR3), .p_6(HIGH_SPEED1_PC1), .p_7(WCSA1), .p_8(GNDPWR), .p_9(WCSA2), .p_10(HIGH_SPEED1_PC2), .p_11(ADR4), .p_12(WCSA3), .p_13(HIGH_SPEED1_PC3), .p_14(ADR5), .p_15(unconnected_U42_E_Pad15), .p_16(n_5V)); // 74LS157 74F157
  ttl_mcm68b10 #(.INIT_FILE("out/wcs_lane_b0.hex")) U43 (.p_1(GNDPWR), .p_2(HIGH_SPEED1_MI0), .p_3(HIGH_SPEED1_MI1), .p_4(HIGH_SPEED1_MI2), .p_5(HIGH_SPEED1_MI3), .p_6(HIGH_SPEED1_MI4), .p_7(HIGH_SPEED1_MI5), .p_8(HIGH_SPEED1_MI6), .p_9(HIGH_SPEED1_MI7), .p_10(WCS_CS), .p_11(GNDPWR), .p_12(GNDPWR), .p_13(WCS_CS), .p_14(GNDPWR), .p_15(GNDPWR), .p_16(Net_U43_RW), .p_17(WCSA6), .p_18(WCSA5), .p_19(WCSA4), .p_20(WCSA3), .p_21(WCSA2), .p_22(WCSA1), .p_23(WCSA0), .p_24(n_5V)); // MCM68B10 MCM68B10
  ttl_am8304n U44 (.p_1(HIGH_SPEED1_MI0), .p_2(HIGH_SPEED1_MI1), .p_3(HIGH_SPEED1_MI2), .p_4(HIGH_SPEED1_MI3), .p_5(HIGH_SPEED1_MI4), .p_6(HIGH_SPEED1_MI5), .p_7(HIGH_SPEED1_MI6), .p_8(HIGH_SPEED1_MI7), .p_9(Net_U44_G), .p_10(GNDPWR), .p_11(Net_U43_RW), .p_12(WCS_DATA7_n), .p_13(WCS_DATA6_n), .p_14(WCS_DATA5_n), .p_15(WCS_DATA4_n), .p_16(WCS_DATA3_n), .p_17(WCS_DATA2_n), .p_18(WCS_DATA1_n), .p_19(WCS_DATA0_n), .p_20(n_5V)); // AMB304 AMB304
  ttl_sn74f374n U45 (.p_1(GNDPWR), .p_2(OFST1), .p_3(HIGH_SPEED1_MI1), .p_4(HIGH_SPEED1_MI3), .p_5(OFST3), .p_6(OFST5), .p_7(HIGH_SPEED1_MI5), .p_8(HIGH_SPEED1_MI7), .p_9(OFST7), .p_10(GNDPWR), .p_11(DAB_RSTB_slash), .p_12(OFST6), .p_13(HIGH_SPEED1_MI6), .p_14(HIGH_SPEED1_MI4), .p_15(OFST4), .p_16(OFST2), .p_17(HIGH_SPEED1_MI2), .p_18(HIGH_SPEED1_MI0), .p_19(OFST0), .p_20(n_5V)); // SN74F374N SN74F374N
  ttl_74ls155 U46 (.p_1(n_5V), .p_2(GSTB), .p_3(ADR1), .p_4(Net_U44_G), .p_5(Net_U30_G), .p_6(Net_U16_G), .p_7(Net_U3_G), .p_8(GNDPWR), .p_9(Net_U2_RW), .p_10(Net_U15_RW), .p_11(Net_U29_RW), .p_12(Net_U43_RW), .p_13(ADR0), .p_14(WSTB), .p_15(MWTC), .p_16(n_5V)); // 74LS155 74LS155
  ttl_74ls139 U47 (.p_1(GNDPWR), .p_2(Net_U17_QA), .p_3(MEMAC), .p_4(unconnected_U47A_O0_Pad4), .p_5(Net_U47A_O1), .p_6(MEMW_n), .p_7(MEMR_slash), .p_8(GNDPWR), .p_9(RD_AD_n), .p_10(RD_XREG_n), .p_11(HIGH_SPEED1_OUT_A1), .p_12(unconnected_U47B_O0_Pad12), .p_13(OFST13), .p_14(OFST12), .p_15(Net_U47B_E), .p_16(n_5V)); // 74LS139 74LS139
  ttl_74ls00 U48 (.p_1(MEMW_n), .p_2(HIGH_SPEED1_OUT_A1), .p_3(HIGH_SPEED1_INA), .p_4(HIGH_SPEED1_OUTAB), .p_5(DAB_RSTB), .p_6(Net_U47B_E), .p_7(GNDPWR), .p_8(RDRREG_n), .p_9(HIGH_SPEED1_INA), .p_10(DAB_RSTB), .p_11(ZERO_n), .p_12(HIGH_SPEED1_Q6_OUT), .p_13(AS0), .p_14(n_5V)); // SN74S00N SN74S00N
  ttl_74ls10 U49 (.p_1(MS7), .p_2(HIGH_SPEED1_OUTAB), .p_3(OFST5), .p_4(HIGH_SPEED1_OUTAB), .p_5(DAB_RSTB), .p_6(TEST), .p_7(GNDPWR), .p_8(WR_DA_n), .p_9(OFST7), .p_10(n_5V), .p_11(HIGH_SPEED1_OUTAB), .p_12(WR_XREG_n), .p_13(OFST6), .p_14(n_5V)); // 74LS10 74LS10
  ttl_sn74s163n U5 (.p_1(ADR_SEL), .p_2(DAB_RSTB_slash), .p_3(HIGH_SPEED1_MI28), .p_4(HIGH_SPEED1_MI29), .p_5(HIGH_SPEED1_MI30), .p_6(HIGH_SPEED1_MI31), .p_7(GNDPWR), .p_8(GNDPWR), .p_9(GNDPWR), .p_10(GNDPWR), .p_11(C5_slash), .p_12(C4_slash), .p_13(C3_slash), .p_14(C2_slash), .p_15(unconnected_U5_RCO_Pad15), .p_16(n_5V)); // SN74S163N SN74S163N
  ttl_74ls133 U50 (.p_1(Net_U33_Pad10), .p_2(ADR9), .p_3(ADRF), .p_4(ADRC), .p_5(ADRA), .p_6(Net_U52_Q1), .p_7(ADRD), .p_8(GNDPWR), .p_9(HIGH_SPEED2_U50_OUT), .p_10(ADRB), .p_11(n_5V), .p_12(Net_U52_D1), .p_13(n_5V), .p_14(n_5V), .p_15(n_5V), .p_16(n_5V)); // 74LS133 74LS133
  ttl_74ls00 U51 (.p_1(MRDC), .p_2(MWTC), .p_3(HIGH_SPEED2_U52_CLR), .p_4(HIGH_SPEED2_U51_2A), .p_5(Net_U51_2B), .p_6(Net_U36_3C), .p_7(GNDPWR), .p_8(HIGH_SPEED2_REQ), .p_9(HIGH_SPEED2_U53_Q), .p_10(HIGH_SPEED2_U50_OUT), .p_11(unconnected_U51_4Y_Pad11), .p_12(unconnected_U51_4A_Pad12), .p_13(unconnected_U51_4B_Pad13), .p_14(n_5V)); // SN74S00N 74S00
  ttl_74ls175 #(.CLK_TPD(13.0), .CLR_TPD(13.0)) U52 (.p_1(HIGH_SPEED2_U52_CLR), .p_2(Net_U52_D1), .p_3(HIGH_SPEED2_U36_3A), .p_4(n_5V), .p_5(Net_U52_D1), .p_6(Net_U52_Q1), .p_7(unconnected_U52_Q1_Pad7), .p_8(GNDPWR), .p_9(Net_U52_Cp), .p_10(unconnected_U52_Q2_Pad10), .p_11(unconnected_U52_Q2_Pad11), .p_12(unconnected_U52_D2_Pad12), .p_13(unconnected_U52_D3_Pad13), .p_14(unconnected_U52_Q3_Pad14), .p_15(unconnected_U52_Q3_Pad15), .p_16(n_5V)); // 74LS175 74LS175
  ttl_74ls74 U53 (.p_1(Net_U53A_R), .p_2(n_5V), .p_3(Net_U53A_C), .p_4(n_5V), .p_5(unconnected_U53A_Q_Pad5), .p_6(HIGH_SPEED2_U53_Q), .p_7(GNDPWR), .p_8(HIGH_SPEED2_U51_2A), .p_9(CK), .p_10(n_5V), .p_11(PROT), .p_12(HIGH_SPEED2_U51_2A), .p_13(RESETD), .p_14(n_5V)); // 74LS74 74LS74
  ttl_74ls74 U54 (.p_1(Net_U54A_R), .p_2(n_5V), .p_3(HIGH_SPEED2_REQ), .p_4(n_5V), .p_5(Net_U54A_Q), .p_6(unconnected_U54A_Q_Pad6), .p_7(GNDPWR), .p_8(Net_U54A_R), .p_9(Net_U54B_Q), .p_10(n_5V), .p_11(SBC_PHI2_ZD), .p_12(Net_U54A_Q), .p_13(n_5V), .p_14(n_5V)); // 74LS74 74LS74
  ttl_74ls03 U55 (.p_7(GNDPWR), .p_8(XACK_n), .p_9(Net_U54B_Q), .p_10(Net_U54B_Q), .p_14(n_5V)); // 74LS03 74LS03
  ttl_sn74s163n U56 (.p_1(n_5V), .p_2(MC), .p_3(GNDPWR), .p_4(GNDPWR), .p_5(GNDPWR), .p_6(n_5V), .p_7(n_5V), .p_8(GNDPWR), .p_9(U23_CLR), .p_10(n_5V), .p_11(U23_CLR), .p_12(unconnected_U56_QC_Pad12), .p_13(unconnected_U56_QB_Pad13), .p_14(unconnected_U56_QA_Pad14), .p_15(Net_U39_1D), .p_16(n_5V)); // SN74S163N 74S163
  ttl_74ls244 U6 (.p_1(DPORT4_n), .p_2(MEMW_n), .p_3(DATA3), .p_4(CSIGN_n), .p_5(DATA2), .p_6(RD_AD_n), .p_7(DATA1), .p_8(PROT), .p_9(DATA0), .p_10(GNDPWR), .p_11(WA0_n), .p_12(DATA7), .p_13(WA1_n), .p_14(DATA5), .p_15(RA0_n), .p_16(DATA4), .p_17(RA1_n), .p_18(DATA6), .p_19(DPORT4_n), .p_20(n_5V)); // 74LS244 74LS244
  ttl_sn74f374n U7 (.p_1(DPORT5_n), .p_2(DATA3), .p_3(SDAD), .p_4(SDAC), .p_5(DATA2), .p_6(DATA1), .p_7(SDAB), .p_8(SDAA), .p_9(DATA0), .p_10(GNDPWR), .p_11(MS8), .p_12(DATA7), .p_13(RDRREG_n), .p_14(WR_XREG_n), .p_15(DATA5), .p_16(DATA4), .p_17(WR_DA_n), .p_18(RD_XREG_n), .p_19(DATA6), .p_20(n_5V)); // 74LS374 74LS374
  ttl_sn74f374n U8 (.p_1(DPORT3_n), .p_2(DATA3), .p_3(Net_U8_D0), .p_4(HIGH_SPEED2_U9_D2), .p_5(DATA2), .p_6(DATA1), .p_7(Net_U8_D2), .p_8(HIGH_SPEED2_U8_D3), .p_9(DATA0), .p_10(GNDPWR), .p_11(MS2), .p_12(DATA7), .p_13(ZERO_n), .p_14(HIGH_SPEED2_U9_D1), .p_15(DATA5), .p_16(DATA4), .p_17(Net_U8_D6), .p_18(HIGH_SPEED2_U8_D7), .p_19(DATA6), .p_20(n_5V)); // 74LS374 74LS374
  ttl_74ls174 U9 (.p_1(n_5V), .p_2(HIGH_SPEED2_U9_D2), .p_3(Net_U8_D2), .p_4(HIGH_SPEED2_U9_D1), .p_5(Net_U8_D6), .p_6(HIGH_SPEED2_U9_D2), .p_7(Net_U8_D0), .p_8(GNDPWR), .p_9(ARUCK), .p_10(HIGH_SPEED2_U9_D1), .p_11(HIGH_SPEED2_U8_D7), .p_12(HIGH_SPEED2_U9_Q4), .p_13(SAT), .p_14(unconnected_U9_D5_Pad14), .p_15(unconnected_U9_Q5_Pad15), .p_16(n_5V)); // 74LS174 74LS174
endmodule

