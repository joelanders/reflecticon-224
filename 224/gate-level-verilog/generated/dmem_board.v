`timescale 1ns/1ps
// Auto-generated from KiCad netlist
// Source: ../schematics/netlists/dmem.net

module bb_Conn_02x02_Counter_Clockwise(p_1, p_2, p_3, p_4);
  inout p_1, p_2, p_3, p_4;
endmodule

module dmem_board_netlist(OFST0_n, OFST1_n, OFST2_n, OFST3_n, OFST4_n, OFST5_n, OFST6_n, OFST7_n, OFST8_n, OFST9_n, OFST10_n, OFST11_n, OFST12_n, OFST13_n, OFST14_n, OFST15_n, DAB0, DAB1, DAB2, DAB3, DAB4, DAB5, DAB6, DAB7, DAB8, DAB9, DAB10, DAB11, DAB12, DAB13, DAB14, DAB15, DATA0, DATA1, DATA2, DATA3, DATA4, DATA5, DATA6, DATA7, MEMW_n, RESET_n, CPC_CLR, RAS_n, CAS0_n, CAS1_n, ROW_SEL, WRL_XREG_n, WRH_XREG_n, RDL_XREG_n, RDH_XREG_n, WR_XREG_n, RD_XREG_n, DPORT0, DPORT1, DPORT2);
  inout OFST0_n, OFST1_n, OFST2_n, OFST3_n, OFST4_n, OFST5_n, OFST6_n, OFST7_n, OFST8_n, OFST9_n, OFST10_n, OFST11_n, OFST12_n, OFST13_n, OFST14_n, OFST15_n, DAB0, DAB1, DAB2, DAB3, DAB4, DAB5, DAB6, DAB7, DAB8, DAB9, DAB10, DAB11, DAB12, DAB13, DAB14, DAB15, DATA0, DATA1, DATA2, DATA3, DATA4, DATA5, DATA6, DATA7, MEMW_n, RESET_n, CPC_CLR, RAS_n, CAS0_n, CAS1_n, ROW_SEL, WRL_XREG_n, WRH_XREG_n, RDL_XREG_n, RDH_XREG_n, WR_XREG_n, RD_XREG_n, DPORT0, DPORT1, DPORT2;
  // Net declarations
  wire n_5V; // +5V
  wire n_12V; // +12V
  wire n_5V_1; // -5V
  wire Untitled_Sheet_A0; // /Untitled Sheet/A0
  wire Untitled_Sheet_A1; // /Untitled Sheet/A1
  wire Untitled_Sheet_A2; // /Untitled Sheet/A2
  wire Untitled_Sheet_A3; // /Untitled Sheet/A3
  wire Untitled_Sheet_A4; // /Untitled Sheet/A4
  wire Untitled_Sheet_A5; // /Untitled Sheet/A5
  wire Untitled_Sheet_A6; // /Untitled Sheet/A6
  wire Untitled_Sheet_A7; // /Untitled Sheet/A7
  wire Untitled_Sheet_A8; // /Untitled Sheet/A8
  wire Untitled_Sheet_A9; // /Untitled Sheet/A9
  wire Untitled_Sheet_A10; // /Untitled Sheet/A10
  wire Untitled_Sheet_A11; // /Untitled Sheet/A11
  wire Untitled_Sheet_A12; // /Untitled Sheet/A12
  wire Untitled_Sheet_A13; // /Untitled Sheet/A13
  wire Untitled_Sheet_A14; // /Untitled Sheet/A14
  wire Untitled_Sheet_A15; // /Untitled Sheet/A15
  wire Untitled_Sheet_AD0; // /Untitled Sheet/AD0
  wire Untitled_Sheet_AD1; // /Untitled Sheet/AD1
  wire Untitled_Sheet_AD2; // /Untitled Sheet/AD2
  wire Untitled_Sheet_AD3; // /Untitled Sheet/AD3
  wire Untitled_Sheet_AD4; // /Untitled Sheet/AD4
  wire Untitled_Sheet_AD5; // /Untitled Sheet/AD5
  wire Untitled_Sheet_AD6; // /Untitled Sheet/AD6
  wire Untitled_Sheet_AD7; // /Untitled Sheet/AD7
  wire Untitled_Sheet_WR; // /Untitled Sheet/WR
  wire FPC_DBUG; // FPC DBUG
  wire Net_R7_Pad1; // Net-(R7-Pad1)
  wire Net_U18_Za; // Net-(U18-Za)
  wire Net_U18_Zb; // Net-(U18-Zb)
  wire Net_U18_Zc; // Net-(U18-Zc)
  wire Net_U18_Zd; // Net-(U18-Zd)
  wire Net_U36_Za; // Net-(U36-Za)
  wire Net_U36_Zb; // Net-(U36-Zb)
  wire Net_U36_Zc; // Net-(U36-Zc)
  wire Net_U36_Zd; // Net-(U36-Zd)
  wire Net_U38_Cp; // Net-(U38-Cp)
  wire Net_U39_OE; // Net-(U39-OE)
  wire Net_U49_A1; // Net-(U49-A1)
  wire Net_U49_A2; // Net-(U49-A2)
  wire Net_U49_A3; // Net-(U49-A3)
  wire Net_U49_A4; // Net-(U49-A4)
  wire Net_U49_C0; // Net-(U49-C0)
  wire Net_U49_C4; // Net-(U49-C4)
  wire Net_U50_A1; // Net-(U50-A1)
  wire Net_U50_A2; // Net-(U50-A2)
  wire Net_U50_A3; // Net-(U50-A3)
  wire Net_U50_A4; // Net-(U50-A4)
  wire Net_U50_C4; // Net-(U50-C4)
  wire Net_U63_A1; // Net-(U63-A1)
  wire Net_U63_A2; // Net-(U63-A2)
  wire Net_U63_A3; // Net-(U63-A3)
  wire Net_U63_A4; // Net-(U63-A4)
  wire Net_U63_C4; // Net-(U63-C4)
  wire Net_U64_A1; // Net-(U64-A1)
  wire Net_U64_A2; // Net-(U64-A2)
  wire Net_U64_A3; // Net-(U64-A3)
  wire Net_U64_A4; // Net-(U64-A4)
  wire RESET; // RESET
  wire unconnected_J1_Pin_4_Pad4; // unconnected-(J1-Pin_4-Pad4)
  wire unconnected_J2_Pin_4_Pad4; // unconnected-(J2-Pin_4-Pad4)
  wire unconnected_J3_Pin_1_Pad1; // unconnected-(J3-Pin_1-Pad1)
  wire unconnected_U61_Pad2; // unconnected-(U61-Pad2)
  wire unconnected_U61_Pad18; // unconnected-(U61-Pad18)
  wire unconnected_U64_C4_Pad9; // unconnected-(U64-C4-Pad9)

  // Common rail assumptions
  assign n_5V = 1'b1;
  assign FPC_DBUG = 1'b0; // sim tie_nets

  // Floating TTL input pins read high (overridable per pin)
  assign unconnected_U61_Pad2 = 1'b1; // U61.2

  bb_Conn_02x02_Counter_Clockwise J1 (.p_1(n_5V), .p_2(Untitled_Sheet_AD7), .p_3(Untitled_Sheet_AD7), .p_4(unconnected_J1_Pin_4_Pad4)); // Conn_02x02_Counter_Clockwise Conn_02x02_Counter_Clockwise
  bb_Conn_02x02_Counter_Clockwise J2 (.p_1(n_12V), .p_2(n_5V), .p_3(n_5V), .p_4(unconnected_J2_Pin_4_Pad4)); // Conn_02x02_Counter_Clockwise Conn_02x02_Counter_Clockwise
  bb_Conn_02x02_Counter_Clockwise J3 (.p_1(unconnected_J3_Pin_1_Pad1), .p_2(n_5V), .p_3(n_5V), .p_4(n_5V_1)); // Conn_02x02_Counter_Clockwise Conn_02x02_Counter_Clockwise
  ideal_resistor R5 (.p_1(n_5V), .p_2(Net_U49_C0)); // R_Small_US 1K
  ideal_resistor R7 (.p_1(Net_R7_Pad1), .p_2(Untitled_Sheet_WR)); // R_US 33R
  ttl_mk4164n #(.ADDR_BITS(8)) U1 (.p_1(n_5V), .p_2(DAB0), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB0), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U10 (.p_1(n_5V), .p_2(DAB9), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB9), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U11 (.p_1(n_5V), .p_2(DAB10), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB10), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U12 (.p_1(n_5V), .p_2(DAB11), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB11), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U13 (.p_1(n_5V), .p_2(DAB12), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB12), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U14 (.p_1(n_5V), .p_2(DAB13), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB13), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U15 (.p_1(n_5V), .p_2(DAB14), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB14), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U16 (.p_1(n_5V), .p_2(DAB15), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB15), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ideal_resistor_pack8 U17 (.p_1(Untitled_Sheet_AD7), .p_2(Untitled_Sheet_AD6), .p_3(Untitled_Sheet_AD3), .p_4(Untitled_Sheet_AD0), .p_5(Untitled_Sheet_AD4), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD5), .p_8(Untitled_Sheet_AD1), .p_9(Net_U36_Zb), .p_10(Net_U18_Zb), .p_11(Net_U36_Zc), .p_12(Net_U18_Za), .p_13(Net_U36_Za), .p_14(Net_U36_Zd), .p_15(Net_U18_Zc), .p_16(Net_U18_Zd)); // R_Pack08_Split 33R x 8
  ttl_74ls157 U18 (.p_1(ROW_SEL), .p_2(Untitled_Sheet_A12), .p_3(Untitled_Sheet_A4), .p_4(Net_U18_Za), .p_5(Untitled_Sheet_A13), .p_6(Untitled_Sheet_A5), .p_7(Net_U18_Zb), .p_8(FPC_DBUG), .p_9(Net_U18_Zc), .p_10(Untitled_Sheet_A6), .p_11(Untitled_Sheet_A14), .p_12(Net_U18_Zd), .p_13(Untitled_Sheet_A7), .p_14(Untitled_Sheet_A15), .p_15(FPC_DBUG), .p_16(n_5V)); // 74LS157 74LS157
  ttl_mk4164n #(.ADDR_BITS(8)) U2 (.p_1(n_5V), .p_2(DAB1), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB1), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U20 (.p_1(n_5V), .p_2(DAB0), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB0), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U21 (.p_1(n_5V), .p_2(DAB1), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB1), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U22 (.p_1(n_5V), .p_2(DAB2), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB2), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U23 (.p_1(n_5V), .p_2(DAB3), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB3), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U24 (.p_1(n_5V), .p_2(DAB4), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB4), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U25 (.p_1(n_5V), .p_2(DAB5), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB5), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U26 (.p_1(n_5V), .p_2(DAB6), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB6), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U27 (.p_1(n_5V), .p_2(DAB7), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB7), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U28 (.p_1(n_5V), .p_2(DAB8), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB8), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U29 (.p_1(n_5V), .p_2(DAB9), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB9), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U3 (.p_1(n_5V), .p_2(DAB2), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB2), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U30 (.p_1(n_5V), .p_2(DAB10), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB10), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U31 (.p_1(n_5V), .p_2(DAB11), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB11), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U32 (.p_1(n_5V), .p_2(DAB12), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB12), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U33 (.p_1(n_5V), .p_2(DAB13), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB13), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U34 (.p_1(n_5V), .p_2(DAB14), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB14), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U35 (.p_1(n_5V), .p_2(DAB15), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB15), .p_15(CAS0_n), .p_16(FPC_DBUG)); // MK4164N 4164
  ttl_74ls157 U36 (.p_1(ROW_SEL), .p_2(Untitled_Sheet_A8), .p_3(Untitled_Sheet_A0), .p_4(Net_U36_Za), .p_5(Untitled_Sheet_A9), .p_6(Untitled_Sheet_A1), .p_7(Net_U36_Zb), .p_8(FPC_DBUG), .p_9(Net_U36_Zc), .p_10(Untitled_Sheet_A2), .p_11(Untitled_Sheet_A10), .p_12(Net_U36_Zd), .p_13(Untitled_Sheet_A3), .p_14(Untitled_Sheet_A11), .p_15(FPC_DBUG), .p_16(n_5V)); // 74LS157 74LS157
  ttl_sn74f374n U38 (.p_1(RDL_XREG_n), .p_2(DATA0), .p_3(DAB0), .p_4(DAB1), .p_5(DATA1), .p_6(DATA2), .p_7(DAB2), .p_8(DAB3), .p_9(DATA3), .p_10(FPC_DBUG), .p_11(Net_U38_Cp), .p_12(DATA4), .p_13(DAB4), .p_14(DAB5), .p_15(DATA5), .p_16(DATA6), .p_17(DAB6), .p_18(DAB7), .p_19(DATA7), .p_20(n_5V)); // 74LS374 74LS374
  ttl_sn74f374n U39 (.p_1(Net_U39_OE), .p_2(DAB0), .p_3(DATA0), .p_4(DATA1), .p_5(DAB1), .p_6(DAB2), .p_7(DATA2), .p_8(DATA3), .p_9(DAB3), .p_10(FPC_DBUG), .p_11(WRL_XREG_n), .p_12(DAB4), .p_13(DATA4), .p_14(DATA5), .p_15(DAB5), .p_16(DAB6), .p_17(DATA6), .p_18(DATA7), .p_19(DAB7), .p_20(n_5V)); // 74LS374 74LS374
  ttl_mk4164n #(.ADDR_BITS(8)) U4 (.p_1(n_5V), .p_2(DAB3), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB3), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_sn74f374n U40 (.p_1(RDH_XREG_n), .p_2(DATA0), .p_3(DAB8), .p_4(DAB9), .p_5(DATA1), .p_6(DATA2), .p_7(DAB10), .p_8(DAB11), .p_9(DATA3), .p_10(FPC_DBUG), .p_11(Net_U38_Cp), .p_12(DATA4), .p_13(DAB12), .p_14(DAB13), .p_15(DATA5), .p_16(DATA6), .p_17(DAB14), .p_18(DAB15), .p_19(DATA7), .p_20(n_5V)); // 74LS374 74LS374
  ttl_sn74f374n U41 (.p_1(Net_U39_OE), .p_2(DAB8), .p_3(DATA0), .p_4(DATA1), .p_5(DAB9), .p_6(DAB10), .p_7(DATA2), .p_8(DATA3), .p_9(DAB11), .p_10(FPC_DBUG), .p_11(WRH_XREG_n), .p_12(DAB12), .p_13(DATA4), .p_14(DATA5), .p_15(DAB13), .p_16(DAB14), .p_17(DATA6), .p_18(DATA7), .p_19(DAB15), .p_20(n_5V)); // 74LS374 74LS374
  ttl_sn74f374n U42 (.p_1(DPORT2), .p_2(DATA7), .p_3(DATA7), .p_4(DATA6), .p_5(DATA6), .p_6(DATA5), .p_7(DATA5), .p_8(DATA4), .p_9(DATA4), .p_10(FPC_DBUG), .p_11(WRH_XREG_n), .p_12(DATA3), .p_13(DATA3), .p_14(DATA2), .p_15(DATA2), .p_16(DATA1), .p_17(DATA1), .p_18(DATA0), .p_19(DATA0), .p_20(n_5V)); // 74LS374 74LS374
  ttl_74ls244 U48 (.p_1(DPORT0), .p_2(OFST6_n), .p_3(DATA7), .p_4(OFST1_n), .p_5(DATA0), .p_6(OFST2_n), .p_7(DATA5), .p_8(OFST3_n), .p_9(DATA4), .p_10(FPC_DBUG), .p_11(OFST4_n), .p_12(DATA3), .p_13(OFST5_n), .p_14(DATA2), .p_15(OFST0_n), .p_16(DATA1), .p_17(OFST7_n), .p_18(DATA6), .p_19(DPORT0), .p_20(n_5V)); // 74LS244_Split_4 -> 74LS244_Split LS244
  ttl_74ls283 U49 (.p_1(Untitled_Sheet_A1), .p_2(OFST1_n), .p_3(Net_U49_A2), .p_4(Untitled_Sheet_A0), .p_5(Net_U49_A1), .p_6(OFST0_n), .p_7(Net_U49_C0), .p_8(FPC_DBUG), .p_9(Net_U49_C4), .p_10(Untitled_Sheet_A3), .p_11(OFST3_n), .p_12(Net_U49_A4), .p_13(Untitled_Sheet_A2), .p_14(Net_U49_A3), .p_15(OFST2_n), .p_16(n_5V)); // 74LS283 74LS283
  ttl_mk4164n #(.ADDR_BITS(8)) U5 (.p_1(n_5V), .p_2(DAB4), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB4), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_74ls283 U50 (.p_1(Untitled_Sheet_A5), .p_2(OFST5_n), .p_3(Net_U50_A2), .p_4(Untitled_Sheet_A4), .p_5(Net_U50_A1), .p_6(OFST4_n), .p_7(Net_U49_C4), .p_8(FPC_DBUG), .p_9(Net_U50_C4), .p_10(Untitled_Sheet_A7), .p_11(OFST7_n), .p_12(Net_U50_A4), .p_13(Untitled_Sheet_A6), .p_14(Net_U50_A3), .p_15(OFST6_n), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls393 U51 (.p_1(RESET), .p_2(CPC_CLR), .p_3(Net_U49_A1), .p_4(Net_U49_A2), .p_5(Net_U49_A3), .p_6(Net_U49_A4), .p_7(FPC_DBUG), .p_8(Net_U50_A4), .p_9(Net_U50_A3), .p_10(Net_U50_A2), .p_11(Net_U50_A1), .p_12(CPC_CLR), .p_13(Net_U49_A4), .p_14(n_5V)); // 74LS393_2 -> 74LS393 74LS393
  ttl_74ls04 U58 (.p_1(RESET_n), .p_2(RESET), .p_7(FPC_DBUG), .p_14(n_5V)); // 74LS04 LS04
  ttl_mk4164n #(.ADDR_BITS(8)) U6 (.p_1(n_5V), .p_2(DAB5), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB5), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_74ls244 U61 (.p_1(FPC_DBUG), .p_2(unconnected_U61_Pad2), .p_6(RD_XREG_n), .p_8(WR_XREG_n), .p_9(Net_R7_Pad1), .p_10(FPC_DBUG), .p_11(MEMW_n), .p_12(Net_U38_Cp), .p_14(Net_U39_OE), .p_18(unconnected_U61_Pad18), .p_19(FPC_DBUG), .p_20(n_5V)); // 74LS244_Split_1 -> 74LS244_Split LS244
  ttl_74ls244 U62 (.p_1(DPORT1), .p_2(OFST11_n), .p_3(DATA4), .p_4(OFST10_n), .p_5(DATA5), .p_6(OFST9_n), .p_7(DATA0), .p_8(OFST14_n), .p_9(DATA7), .p_10(FPC_DBUG), .p_11(OFST15_n), .p_12(DATA6), .p_13(OFST8_n), .p_14(DATA1), .p_15(OFST13_n), .p_16(DATA2), .p_17(OFST12_n), .p_18(DATA3), .p_19(DPORT1), .p_20(n_5V)); // 74LS244_Split_8 -> 74LS244_Split LS244
  ttl_74ls283 U63 (.p_1(Untitled_Sheet_A9), .p_2(OFST9_n), .p_3(Net_U63_A2), .p_4(Untitled_Sheet_A8), .p_5(Net_U63_A1), .p_6(OFST8_n), .p_7(Net_U50_C4), .p_8(FPC_DBUG), .p_9(Net_U63_C4), .p_10(Untitled_Sheet_A11), .p_11(OFST11_n), .p_12(Net_U63_A4), .p_13(Untitled_Sheet_A10), .p_14(Net_U63_A3), .p_15(OFST10_n), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls283 U64 (.p_1(Untitled_Sheet_A13), .p_2(OFST13_n), .p_3(Net_U64_A2), .p_4(Untitled_Sheet_A12), .p_5(Net_U64_A1), .p_6(OFST12_n), .p_7(Net_U63_C4), .p_8(FPC_DBUG), .p_9(unconnected_U64_C4_Pad9), .p_10(Untitled_Sheet_A15), .p_11(OFST15_n), .p_12(Net_U64_A4), .p_13(Untitled_Sheet_A14), .p_14(Net_U64_A3), .p_15(OFST14_n), .p_16(n_5V)); // 74LS283 74LS283
  ttl_74ls393 U65 (.p_1(Net_U50_A4), .p_2(CPC_CLR), .p_3(Net_U63_A1), .p_4(Net_U63_A2), .p_5(Net_U63_A3), .p_6(Net_U63_A4), .p_7(FPC_DBUG), .p_8(Net_U64_A4), .p_9(Net_U64_A3), .p_10(Net_U64_A2), .p_11(Net_U64_A1), .p_12(CPC_CLR), .p_13(Net_U63_A4), .p_14(n_5V)); // 74LS393_2 -> 74LS393 74LS393
  ttl_mk4164n #(.ADDR_BITS(8)) U7 (.p_1(n_5V), .p_2(DAB6), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB6), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U8 (.p_1(n_5V), .p_2(DAB7), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB7), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
  ttl_mk4164n #(.ADDR_BITS(8)) U9 (.p_1(n_5V), .p_2(DAB8), .p_3(Untitled_Sheet_WR), .p_4(RAS_n), .p_5(Untitled_Sheet_AD0), .p_6(Untitled_Sheet_AD2), .p_7(Untitled_Sheet_AD1), .p_8(n_5V), .p_9(Untitled_Sheet_AD7), .p_10(Untitled_Sheet_AD5), .p_11(Untitled_Sheet_AD4), .p_12(Untitled_Sheet_AD3), .p_13(Untitled_Sheet_AD6), .p_14(DAB8), .p_15(CAS1_n), .p_16(FPC_DBUG)); // MK4164N_1 -> MK4164N 4164
endmodule

