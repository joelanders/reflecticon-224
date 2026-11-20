`timescale 1ns/1ps

// Full generated T&C-board witness for the 224X v8.2.1 HP 5004A table.
//
// The service setup uses falling RESETD (U19.9) for both START and STOP, so
// one aperture runs between consecutive falling RESETD edges.  CLOCK is rising
// DAB_RSTB/ (U20.6).  Unlike the small
// U14/U1 counter island, U11 serializes coefficient bits fetched from a real
// WCS image.  The image is supplied to the generated board's four MCM68B10
// INIT_FILE paths by the checker.
module tb_hp5004a_tc_u11;
  // Exposed only for the U48.1 service-signature timing sensitivity probe.
  // The regression default remains the primitive's 25 ns LS139 delay.
  parameter real U47_TPD = 25.0;
  reg mc = 1'b0;
  wire mc_w = mc;
  wire hi = 1'b1;
  wire lo = 1'b0;
  wire ms0, ms1, ms2, ms4, ms6, ms7, ms8;
  wire fpc_ck, aruck, as0, as1_n, dab_rstb, memac, xfer_ck;
  wire [6:0] wcsa;

  tc_board_netlist dut(
    .MC(mc_w), .MS0(ms0), .MS1(ms1), .MS2(ms2), .MS4(ms4),
    .MS6(ms6), .MS7(ms7), .MS8(ms8), .FPC_CK(fpc_ck),
    .ARUCK(aruck), .AS0(as0), .AS1_n(as1_n),
    .DAB_RSTB(dab_rstb), .MEMAC(memac), .XFER_CK(xfer_ck),
    .SAT(lo), .HALT(hi), .MWTC(hi), .MRDC(hi), .ADR0(hi), .ADR1(hi),
    .WCSA0(wcsa[0]), .WCSA1(wcsa[1]), .WCSA2(wcsa[2]),
    .WCSA3(wcsa[3]), .WCSA4(wcsa[4]), .WCSA5(wcsa[5]),
    .WCSA6(wcsa[6])
  );
  defparam dut.U47.TPLH = U47_TPD;
  defparam dut.U47.TPHL = U47_TPD;

  always #16.275 mc = ~mc;

  integer aperture = -1;
  integer sample_index = 0;
  integer complete_apertures = 0;
  reg measuring = 1'b0;
  time settle_after = 20000;

  // START and STOP share RESETD and the same falling-edge selection in the
  // scan.  Treat each falling edge as the boundary between adjacent analyzer
  // apertures; this also exposes whether the full board naturally supplies the
  // 30 selected clocks implied by the manual's +5V signature.
  always @(negedge dut.RESETD) begin
    if ($time >= settle_after) begin
      if (measuring) begin
        $display(
          "HP_GATE edge=stop aperture=%0d time=%0t samples=%0d wcsa=%0d",
          aperture, $time, sample_index, wcsa
        );
        complete_apertures = complete_apertures + 1;
      end
      if (complete_apertures == 3) begin
        measuring = 1'b0;
        #100 $finish;
      end else begin
        aperture = aperture + 1;
        sample_index = 0;
        measuring = 1'b1;
        $display("HP_GATE edge=start aperture=%0d time=%0t wcsa=%0d", aperture, $time, wcsa);
      end
    end
  end

  always @(posedge dut.DAB_RSTB_slash) begin
    if (measuring) begin
      $display(
        "HP_SAMPLE phase=pre aperture=%0d index=%0d time=%0t wcsa=%0d rail=1 ground=0 u5p1=%b u5p3=%b u5p4=%b u5p5=%b u5p6=%b u5p7=0 u5p8=0 u5p9=0 u5p10=0 u5p11=%b u5p12=%b u5p13=%b u5p14=%b u5p16=1 u10p1=1 u10p4=%b u10p5=%b u10p6=%b u10p7=%b u10p8=0 u10p11=%b u10p13=%b u10p16=1 u11p1=1 u11p4=%b u11p5=%b u11p6=%b u11p7=%b u11p8=0 u11p11=%b u11p13=%b u11p16=1 u15p1=0 u15p2=%b u15p3=%b u15p4=%b u15p5=%b u15p6=%b u15p7=%b u15p8=%b u15p9=%b u15p11=0 u15p12=0 u15p14=0 u15p15=0 u15p16=%b u15p17=%b u15p18=%b u15p19=%b u15p20=%b u15p21=%b u15p22=%b u15p23=%b u15p24=1 u16p1=%b u16p2=%b u16p3=%b u16p4=%b u16p5=%b u16p6=%b u16p7=%b u16p8=%b u16p9=%b u16p10=%b u16p11=%b u16p20=%b u17p1=%b u17p3=%b u17p4=%b u17p5=%b u17p6=%b u17p7=0 u17p8=0 u17p9=0 u17p10=0 u17p11=%b u17p12=%b u17p13=%b u17p14=%b u17p16=1 u18p1=%b u18p3=%b u18p4=%b u18p5=%b u18p6=%b u18p7=0 u18p8=0 u18p9=0 u18p10=0 u18p11=%b u18p12=%b u18p13=%b u18p14=%b u18p16=1 u19p2=%b u19p3=%b u19p4=%b u19p5=%b u19p6=%b u19p7=%b u19p8=%b u19p9=%b u19p10=0 u19p12=%b u19p13=%b u19p14=%b u19p15=%b u19p16=%b u19p17=%b u19p18=%b u19p19=%b u19p20=1 u20p8=0 u20p9=%b u20p10=%b u20p14=%b u20p16=1 u2p1=0 u2p2=%b u2p3=%b u2p4=%b u2p5=%b u2p6=%b u2p7=%b u2p8=%b u2p9=%b u2p11=0 u2p12=0 u2p14=0 u2p15=0 u2p16=%b u2p17=%b u2p18=%b u2p19=%b u2p20=%b u2p21=%b u2p22=%b u2p23=%b u2p24=1 u3p1=%b u3p2=%b u3p3=%b u3p4=%b u3p5=%b u3p6=%b u3p7=%b u3p8=%b u3p9=%b u3p10=%b u3p11=%b u3p20=%b u4p1=%b u4p3=%b u4p4=%b u4p5=%b u4p6=%b u4p7=0 u4p8=0 u4p9=0 u4p10=0 u4p11=%b u4p12=%b u4p13=%b u4p14=%b u4p16=1 resetd=%b",
        aperture, sample_index, $time, wcsa,
        dut.ADR_SEL,
        dut.HIGH_SPEED1_MI28, dut.HIGH_SPEED1_MI29,
        dut.HIGH_SPEED1_MI30, dut.HIGH_SPEED1_MI31,
        dut.C5_slash, dut.C4_slash, dut.C3_slash, dut.C2_slash,
        dut.C1_slash, dut.C3_slash, dut.C5_slash,
        dut.Net_U10_P3, dut.M1_n, dut.Net_U10_P3,
        dut.C0_slash, dut.C2_slash, dut.C4_slash,
        dut.Net_U11_P3, dut.M0_n, dut.Net_U11_P3,
        dut.HIGH_SPEED1_MI16, dut.HIGH_SPEED1_MI17,
        dut.HIGH_SPEED1_MI18, dut.HIGH_SPEED1_MI19,
        dut.HIGH_SPEED1_MI20, dut.HIGH_SPEED1_MI21,
        dut.HIGH_SPEED1_MI22, dut.HIGH_SPEED1_MI23,
        dut.Net_U15_RW,
        dut.WCSA6, dut.WCSA5, dut.WCSA4, dut.WCSA3,
        dut.WCSA2, dut.WCSA1, dut.WCSA0,
        dut.HIGH_SPEED1_MI16, dut.HIGH_SPEED1_MI17,
        dut.HIGH_SPEED1_MI18, dut.HIGH_SPEED1_MI19,
        dut.HIGH_SPEED1_MI20, dut.HIGH_SPEED1_MI21,
        dut.HIGH_SPEED1_MI22, dut.HIGH_SPEED1_MI23,
        dut.Net_U16_G, dut.U16.p_10, dut.Net_U15_RW, dut.U16.p_20,
        dut.ADR_SEL,
        dut.HIGH_SPEED1_MI16, dut.HIGH_SPEED1_MI17,
        dut.HIGH_SPEED1_MI18, dut.HIGH_SPEED1_MI19,
        dut.WA1_n, dut.WA0_n, dut.MEMAC, dut.Net_U17_QA,
        dut.ADR_SEL,
        dut.HIGH_SPEED1_MI20, dut.HIGH_SPEED1_MI21,
        dut.HIGH_SPEED1_MI22, dut.HIGH_SPEED1_MI23,
        dut.Net_U18_QD, dut.PROT, dut.Net_U18_QB, dut.Net_U18_QA,
        dut.DP, dut.HIGH_SPEED1_DO_OUT, dut.Net_U18_QA,
        dut.RA0_n, dut.RA1_n, dut.Net_U18_QB,
        dut.RESET_n, dut.RESETD,
        dut.Net_U19_Q4, dut.Net_U18_QD,
        dut.Net_U19_D5, dut.XFER,
        dut.HIGH_SPEED1_Q6_OUT, dut.Net_U19_D6,
        dut.U19.p_18, dut.U19.p_19,
        dut.CSIGN_n, dut.U20.p_10, dut.U20.p_14,
        dut.HIGH_SPEED1_MI24, dut.HIGH_SPEED1_MI25,
        dut.HIGH_SPEED1_MI26, dut.HIGH_SPEED1_MI27,
        dut.HIGH_SPEED1_MI28, dut.HIGH_SPEED1_MI29,
        dut.HIGH_SPEED1_MI30, dut.HIGH_SPEED1_MI31,
        dut.Net_U2_RW,
        dut.WCSA6, dut.WCSA5, dut.WCSA4, dut.WCSA3,
        dut.WCSA2, dut.WCSA1, dut.WCSA0,
        dut.HIGH_SPEED1_MI24, dut.HIGH_SPEED1_MI25,
        dut.HIGH_SPEED1_MI26, dut.HIGH_SPEED1_MI27,
        dut.HIGH_SPEED1_MI28, dut.HIGH_SPEED1_MI29,
        dut.HIGH_SPEED1_MI30, dut.HIGH_SPEED1_MI31,
        dut.Net_U3_G, dut.U3.p_10, dut.Net_U2_RW, dut.U3.p_20,
        dut.ADR_SEL,
        dut.HIGH_SPEED1_MI24, dut.HIGH_SPEED1_MI25,
        dut.HIGH_SPEED1_MI26, dut.HIGH_SPEED1_MI27,
        dut.C1_slash, dut.C0_slash, dut.Net_U19_D6, dut.Net_U19_D5,
        dut.RESETD
      );
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u28p1=%b u28p3=%b u28p4=%b u28p6=%b u28p7=%b u28p8=%b u28p9=%b u28p10=%b u28p15=%b u28p16=%b",
        aperture, sample_index, dut.U28.p_1, dut.U28.p_3, dut.U28.p_4,
        dut.U28.p_6, dut.U28.p_7, dut.U28.p_8, dut.U28.p_9,
        dut.U28.p_10, dut.U28.p_15, dut.U28.p_16);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u29p1=%b u29p2=%b u29p3=%b u29p4=%b u29p5=%b u29p6=%b u29p7=%b u29p8=%b u29p9=%b u29p11=%b u29p12=%b u29p14=%b u29p15=%b u29p16=%b u29p17=%b u29p18=%b u29p19=%b u29p20=%b u29p21=%b u29p22=%b u29p23=%b u29p24=%b",
        aperture, sample_index, dut.U29.p_1, dut.U29.p_2, dut.U29.p_3,
        dut.U29.p_4, dut.U29.p_5, dut.U29.p_6, dut.U29.p_7, dut.U29.p_8,
        dut.U29.p_9, dut.U29.p_11, dut.U29.p_12, dut.U29.p_14,
        dut.U29.p_15, dut.U29.p_16, dut.U29.p_17, dut.U29.p_18,
        dut.U29.p_19, dut.U29.p_20, dut.U29.p_21, dut.U29.p_22,
        dut.U29.p_23, dut.U29.p_24);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u30p1=%b u30p2=%b u30p3=%b u30p4=%b u30p5=%b u30p6=%b u30p7=%b u30p8=%b u30p9=%b u30p10=%b u30p11=%b u30p20=%b",
        aperture, sample_index, dut.U30.p_1, dut.U30.p_2, dut.U30.p_3,
        dut.U30.p_4, dut.U30.p_5, dut.U30.p_6, dut.U30.p_7, dut.U30.p_8,
        dut.U30.p_9, dut.U30.p_10, dut.U30.p_11, dut.U30.p_20);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u31p1=%b u31p2=%b u31p3=%b u31p4=%b u31p5=%b u31p6=%b u31p7=%b u31p8=%b u31p9=%b u31p10=%b u31p12=%b u31p13=%b u31p14=%b u31p15=%b u31p16=%b u31p17=%b u31p18=%b u31p19=%b u31p20=%b",
        aperture, sample_index, dut.U31.p_1, dut.U31.p_2, dut.U31.p_3,
        dut.U31.p_4, dut.U31.p_5, dut.U31.p_6, dut.U31.p_7, dut.U31.p_8,
        dut.U31.p_9, dut.U31.p_10, dut.U31.p_12, dut.U31.p_13,
        dut.U31.p_14, dut.U31.p_15, dut.U31.p_16, dut.U31.p_17,
        dut.U31.p_18, dut.U31.p_19, dut.U31.p_20);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u32p1=%b u32p2=%b u32p3=%b u32p4=%b u32p5=%b u32p6=%b u32p7=%b u32p8=%b u32p9=%b u32p10=%b u32p11=%b u32p12=%b u32p13=%b u32p14=%b",
        aperture, sample_index, dut.U32.p_1, dut.U32.p_2, dut.U32.p_3,
        dut.U32.p_4, dut.U32.p_5, dut.U32.p_6, dut.U32.p_7, dut.U32.p_8,
        dut.U32.p_9, dut.U32.p_10, dut.U32.p_11, dut.U32.p_12,
        dut.U32.p_13, dut.U32.p_14);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u33p3=%b u33p4=%b u33p7=%b u33p14=%b u34p1=%b u34p2=%b u34p3=%b u34p4=%b u34p5=%b u34p6=%b u34p7=%b u34p9=%b u34p13=%b u34p14=%b",
        aperture, sample_index, dut.U33.p_3, dut.U33.p_4, dut.U33.p_7,
        dut.U33.p_14, dut.U34.p_1, dut.U34.p_2, dut.U34.p_3, dut.U34.p_4,
        dut.U34.p_5, dut.U34.p_6, dut.U34.p_7, dut.U34.p_9,
        dut.U34.p_13, dut.U34.p_14);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u42p1=%b u42p3=%b u42p4=%b u42p6=%b u42p7=%b u42p8=%b u42p9=%b u42p10=%b u42p12=%b u42p13=%b u42p15=%b u42p16=%b",
        aperture, sample_index, dut.U42.p_1, dut.U42.p_3, dut.U42.p_4,
        dut.U42.p_6, dut.U42.p_7, dut.U42.p_8, dut.U42.p_9,
        dut.U42.p_10, dut.U42.p_12, dut.U42.p_13, dut.U42.p_15,
        dut.U42.p_16);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u43p1=%b u43p2=%b u43p3=%b u43p4=%b u43p5=%b u43p6=%b u43p7=%b u43p8=%b u43p9=%b u43p11=%b u43p12=%b u43p14=%b u43p15=%b u43p16=%b u43p17=%b u43p18=%b u43p19=%b u43p20=%b u43p21=%b u43p22=%b u43p23=%b u43p24=%b",
        aperture, sample_index, dut.U43.p_1, dut.U43.p_2, dut.U43.p_3,
        dut.U43.p_4, dut.U43.p_5, dut.U43.p_6, dut.U43.p_7, dut.U43.p_8,
        dut.U43.p_9, dut.U43.p_11, dut.U43.p_12, dut.U43.p_14,
        dut.U43.p_15, dut.U43.p_16, dut.U43.p_17, dut.U43.p_18,
        dut.U43.p_19, dut.U43.p_20, dut.U43.p_21, dut.U43.p_22,
        dut.U43.p_23, dut.U43.p_24);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u44p1=%b u44p2=%b u44p3=%b u44p4=%b u44p5=%b u44p6=%b u44p7=%b u44p8=%b u44p9=%b u44p10=%b u44p11=%b u44p20=%b",
        aperture, sample_index, dut.U44.p_1, dut.U44.p_2, dut.U44.p_3,
        dut.U44.p_4, dut.U44.p_5, dut.U44.p_6, dut.U44.p_7, dut.U44.p_8,
        dut.U44.p_9, dut.U44.p_10, dut.U44.p_11, dut.U44.p_20);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u45p1=%b u45p2=%b u45p3=%b u45p4=%b u45p5=%b u45p6=%b u45p7=%b u45p8=%b u45p9=%b u45p10=%b u45p12=%b u45p13=%b u45p14=%b u45p15=%b u45p16=%b u45p17=%b u45p18=%b u45p19=%b u45p20=%b",
        aperture, sample_index, dut.U45.p_1, dut.U45.p_2, dut.U45.p_3,
        dut.U45.p_4, dut.U45.p_5, dut.U45.p_6, dut.U45.p_7, dut.U45.p_8,
        dut.U45.p_9, dut.U45.p_10, dut.U45.p_12, dut.U45.p_13,
        dut.U45.p_14, dut.U45.p_15, dut.U45.p_16, dut.U45.p_17,
        dut.U45.p_18, dut.U45.p_19, dut.U45.p_20);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u46p1=%b u46p2=%b u46p4=%b u46p5=%b u46p6=%b u46p7=%b u46p8=%b u46p9=%b u46p10=%b u46p11=%b u46p12=%b u46p14=%b u46p16=%b",
        aperture, sample_index, dut.U46.p_1, dut.U46.p_2, dut.U46.p_4,
        dut.U46.p_5, dut.U46.p_6, dut.U46.p_7, dut.U46.p_8, dut.U46.p_9,
        dut.U46.p_10, dut.U46.p_11, dut.U46.p_12, dut.U46.p_14,
        dut.U46.p_16);
      $display("HP_EXTRA phase=pre aperture=%0d index=%0d u47p1=%b u47p2=%b u47p3=%b u47p5=%b u47p6=%b u47p7=%b u47p8=%b u47p13=%b u47p14=%b u47p16=%b u48p1=%b u48p2=%b u48p3=%b u48p4=%b u48p7=%b u48p8=%b u48p9=%b u48p12=%b u48p14=%b memwn=%b rdrregn=%b u49p2=%b u49p3=%b u49p4=%b u49p7=%b u49p8=%b u49p9=%b u49p10=%b u49p11=%b u49p13=%b u49p14=%b",
        aperture, sample_index, dut.U47.p_1, dut.U47.p_2, dut.U47.p_3,
        dut.U47.p_5, dut.U47.p_6, dut.U47.p_7, dut.U47.p_8, dut.U47.p_13,
        dut.U47.p_14, dut.U47.p_16, dut.U48.p_1, dut.U48.p_2,
        dut.U48.p_3, dut.U48.p_4, dut.U48.p_7, dut.U48.p_8,
        dut.U48.p_9, dut.U48.p_12, dut.U48.p_14, dut.MEMW_n,
        dut.RDRREG_n, dut.U49.p_2,
        dut.U49.p_3, dut.U49.p_4, dut.U49.p_7, dut.U49.p_8,
        dut.U49.p_9, dut.U49.p_10, dut.U49.p_11, dut.U49.p_13,
        dut.U49.p_14);
      sample_index = sample_index + 1;
    end
  end

  initial begin
    #200000;
    $display("HP_TIMEOUT complete=%0d measuring=%0d resetd=%b wcsa=%0d", complete_apertures, measuring, dut.RESETD, wcsa);
    $finish;
  end
endmodule
