`timescale 1ns/1ps

// Minimal primitive set to get a first CLI simulation loop working.
// All modules expose p_1..p_24 so generated netlists can connect any observed pin.

module ttl_74ls00(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // SN74S00N datasheet max: tPLH=4.5ns, tPHL=5ns (rounded conservatively for rise/fall pair form).
  parameter real TPLH = 4.5;
  parameter real TPHL = 5.0;
  assign #(TPLH, TPHL) p_3  = ~(p_1  & p_2);
  assign #(TPLH, TPHL) p_6  = ~(p_4  & p_5);
  assign #(TPLH, TPHL) p_8  = ~(p_9  & p_10);
  assign #(TPLH, TPHL) p_11 = ~(p_12 & p_13);
endmodule

module ttl_74ls02(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 4.5;
  parameter real TPHL = 5.0;
  assign #(TPLH, TPHL) p_1  = ~(p_2  | p_3);
  assign #(TPLH, TPHL) p_4  = ~(p_5  | p_6);
  assign #(TPLH, TPHL) p_10 = ~(p_8  | p_9);
  assign #(TPLH, TPHL) p_13 = ~(p_11 | p_12);
endmodule

module ttl_74ls20(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 8.0;
  parameter real TPHL = 8.0;
  assign #(TPLH, TPHL) p_6 = ~(p_1 & p_2 & p_4 & p_5);
  assign #(TPLH, TPHL) p_8 = ~(p_9 & p_10 & p_12 & p_13);
endmodule

module ttl_74ls109(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPD = 20.0;
  reg q1 = 1'b0;
  reg q2 = 1'b0;

  function automatic jkbar_next;
    input j;
    input kbar;
    input qcur;
    begin
      case ({j, kbar})
        2'b01: jkbar_next = qcur;
        2'b00: jkbar_next = 1'b0;
        2'b11: jkbar_next = 1'b1;
        2'b10: jkbar_next = ~qcur;
        default: jkbar_next = 1'bx;
      endcase
    end
  endfunction

  // FF1: /R=1, J=2, /K=3, CLK=4, /S=5, Q=6, /Q=7
  always @(negedge p_1 or negedge p_5 or posedge p_4) begin
    if (p_1 === 1'b0 && p_5 === 1'b1) q1 <= #(TPD) 1'b0;
    else if (p_1 === 1'b1 && p_5 === 1'b0) q1 <= #(TPD) 1'b1;
    else if (p_1 === 1'b0 && p_5 === 1'b0) q1 <= #(TPD) 1'bx;
    else if (p_1 === 1'b1 && p_5 === 1'b1) q1 <= #(TPD) jkbar_next(p_2, p_3, q1);
    else q1 <= #(TPD) 1'bx;
  end

  // FF2: /R=15, J=14, /K=13, CLK=12, /S=11, Q=10, /Q=9
  always @(negedge p_15 or negedge p_11 or posedge p_12) begin
    if (p_15 === 1'b0 && p_11 === 1'b1) q2 <= #(TPD) 1'b0;
    else if (p_15 === 1'b1 && p_11 === 1'b0) q2 <= #(TPD) 1'b1;
    else if (p_15 === 1'b0 && p_11 === 1'b0) q2 <= #(TPD) 1'bx;
    else if (p_15 === 1'b1 && p_11 === 1'b1) q2 <= #(TPD) jkbar_next(p_14, p_13, q2);
    else q2 <= #(TPD) 1'bx;
  end

  assign p_6  = q1;
  assign p_7  = ~q1;
  assign p_10 = q2;
  assign p_9  = ~q2;
endmodule

module ttl_74s287(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // 74S287/82S129-style 256x4 bipolar PROM:
  // - address pins A..H land on p_5, p_6, p_7, p_4, p_3, p_2, p_1, p_15
  // - outputs D01..D04 land on p_12, p_11, p_10, p_9
  // - enables S1/S2 are active low on p_13/p_14
  parameter INIT_FILE = "";
  parameter [3:0] DEFAULT_WORD = 4'hx;
  parameter real TAA = 25.0;
  parameter real TOHZ = 20.0;

  reg [3:0] mem [0:255];
  integer i;

  initial begin
    for (i = 0; i < 256; i = i + 1)
      mem[i] = DEFAULT_WORD;
    if (INIT_FILE != "")
      $readmemh(INIT_FILE, mem);
  end

  wire [7:0] addr = {p_15, p_1, p_2, p_3, p_4, p_7, p_6, p_5};
  wire [3:0] raw_word = mem[addr];

  function automatic [3:0] tri_word;
    input g1_n;
    input g2_n;
    input [3:0] word;
    begin
      if (g1_n === 1'b0 && g2_n === 1'b0)
        tri_word = word;
      else if (g1_n === 1'b1 || g2_n === 1'b1)
        tri_word = 4'hz;
      else
        tri_word = 4'hx;
    end
  endfunction

  wire [3:0] q = tri_word(p_13, p_14, raw_word);

  assign #(TAA, TAA, TOHZ) p_12 = q[0];
  assign #(TAA, TAA, TOHZ) p_11 = q[1];
  assign #(TAA, TAA, TOHZ) p_10 = q[2];
  assign #(TAA, TAA, TOHZ) p_9  = q[3];
endmodule

module ttl_sn74f374n(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPD = 8.0;
  // Setup time: with TSU > 0 the register takes D as it was TSU before the
  // clock edge, so data arriving later than that is missed.
  parameter real TSU = 0.0;
  reg [7:0] q = 8'h00;
  wire [7:0] d = {p_18, p_17, p_14, p_13, p_8, p_7, p_4, p_3};

  generate
    if (TSU > 0.0) begin : with_setup
      wire [7:0] d_setup;
      assign #(TSU) d_setup = d;
      always @(posedge p_11) q <= #(TPD) d_setup;
    end else begin : no_setup
      always @(posedge p_11) q <= #(TPD) {p_18, p_17, p_14, p_13, p_8, p_7, p_4, p_3};
    end
  endgenerate

  wire [7:0] q_drive = (p_1 === 1'b0) ? q : ((p_1 === 1'b1) ? 8'hzz : 8'hxx);
  assign p_2  = q_drive[0];
  assign p_5  = q_drive[1];
  assign p_6  = q_drive[2];
  assign p_9  = q_drive[3];
  assign p_12 = q_drive[4];
  assign p_15 = q_drive[5];
  assign p_16 = q_drive[6];
  assign p_19 = q_drive[7];
endmodule

module ttl_sn74s163n(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter integer TPD = 12;
  reg [3:0] q = 4'h0;

  always @(posedge p_2) begin
    if (p_1 === 1'b0)
      q <= #(TPD) 4'h0; // synchronous clear
    else if (p_9 === 1'b0)
      q <= #(TPD) {p_6, p_5, p_4, p_3}; // synchronous load
    else if (p_7 === 1'b1 && p_10 === 1'b1)
      q <= #(TPD) q + 4'h1;
  end

  assign p_14 = q[0];
  assign p_13 = q[1];
  assign p_12 = q[2];
  assign p_11 = q[3];
  assign p_15 = (p_10 === 1'b1) ? ((q == 4'hf) ? 1'b1 : 1'b0) :
                (p_10 === 1'b0 ? 1'b0 : 1'bx);
endmodule

module ttl_74ls175(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // SN74LS175 datasheet max:
  // - clear->Q/Qn up to 30ns
  // - clock->Q/Qn up to 25ns
  // This model uses per-source conservative delays.
  parameter real CLR_TPD = 20.0;
  parameter real CLK_TPD = 20.0;
  reg [3:0] q = 4'b0000;

  // 74LS175: async clear active low, capture D on rising clock.
  always @(negedge p_1 or posedge p_9) begin
    if (p_1 === 1'b0) begin
      q <= #(CLR_TPD) 4'b0000;
    end else if (p_1 === 1'b1) begin
      q[0] <= #(CLK_TPD) p_4;
      q[1] <= #(CLK_TPD) p_5;
      q[2] <= #(CLK_TPD) p_12;
      q[3] <= #(CLK_TPD) p_13;
    end else begin
      q <= #(CLK_TPD) 4'bxxxx;
    end
  end

  assign p_2  = q[0];
  assign p_3  = ~q[0];
  assign p_7  = q[1];
  assign p_6  = ~q[1];
  assign p_10 = q[2];
  assign p_11 = ~q[2];
  assign p_15 = q[3];
  assign p_14 = ~q[3];
endmodule

module ttl_74ls14(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // SN74LS14 datasheet max propagation delay is 25ns.
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;

  assign #(TPLH, TPHL) p_2  = ~p_1;
  assign #(TPLH, TPHL) p_4  = ~p_3;
  assign #(TPLH, TPHL) p_6  = ~p_5;
  assign #(TPLH, TPHL) p_8  = ~p_9;
  assign #(TPLH, TPHL) p_10 = ~p_11;
  assign #(TPLH, TPHL) p_12 = ~p_13;
endmodule

module ttl_74ls194a(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real CLR_TPD = 20.0;
  parameter real CLK_TPD = 20.0;
  reg [3:0] q = 4'b0000;

  function automatic ttl_data_or_zero;
    input value;
    begin
      case (value)
        1'b0: ttl_data_or_zero = 1'b0;
        1'b1: ttl_data_or_zero = 1'b1;
        1'bz: ttl_data_or_zero = 1'b0;
        default: ttl_data_or_zero = 1'bx;
      endcase
    end
  endfunction

  // SN54/74LS194A: "All data and mode control inputs are edge-triggered, responding
  // only to the LOW to HIGH transition of the Clock (CP)."  -> POSEDGE.
  //
  // 2026-07-13: this was `negedge p_11`, copied from a public SPICE macro rather than
  // the datasheet part (the old comment said so outright). It was the only clocked prim
  // in this library whose edge contradicted its datasheet. Net_U10_CP is ONE wire clocking
  // 14 ARU chips -- the '377s, the '175 and the five '163 accumulators all on posedge --
  // so a negedge '194 had the operand shifter sampling the AS ring on the opposite side of
  // its update from the '195 coefficient serializers. That rotated the multiply's radix-4
  // WEIGHTS one state off the coefficient DIGITS: effective coeff = ROR2(true).
  // E8x never caught it: the only ROR2 fixed points in 6 bits are {0,21,42,63} and E8x's
  // multiply anchors are exactly 21 and 42 -- it is PROVABLY BLIND to this defect class.
  // Floating data inputs are still coerced low (see ttl_data_or_zero) to match the ARU
  // netlist's omitted pins.
  always @(negedge p_1 or posedge p_11) begin
    if (p_1 === 1'b0) begin
      q <= #(CLR_TPD) 4'b0000;
    end else if (p_1 === 1'b1) begin
      case ({p_10, p_9})
        2'b00: q <= #(CLK_TPD) q;
        2'b01: q <= #(CLK_TPD) {q[2], q[1], q[0], ttl_data_or_zero(p_2)};
        2'b10: q <= #(CLK_TPD) {ttl_data_or_zero(p_7), q[3], q[2], q[1]};
        2'b11: q <= #(CLK_TPD) {
          ttl_data_or_zero(p_6),
          ttl_data_or_zero(p_5),
          ttl_data_or_zero(p_4),
          ttl_data_or_zero(p_3)
        };
        default: q <= #(CLK_TPD) 4'bxxxx;
      endcase
    end else begin
      q <= #(CLK_TPD) 4'bxxxx;
    end
  end

  assign p_12 = q[3];
  assign p_13 = q[2];
  assign p_14 = q[1];
  assign p_15 = q[0];
endmodule

module ttl_74ls670(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // Transparent 4x4 register-file model. The E51..E7F firmware register
  // harness writes all four addresses back-to-back and rules out any durable
  // cross-address corruption during the normal /GW aperture.
  parameter real READ_TPLH = 25.0;
  parameter real READ_TPHL = 25.0;
  parameter real TOHZ = 30.0;
  parameter real WRITE_TPD = 25.0;

  reg [3:0] mem [0:3];
  reg [3:0] selected_word;
  reg [3:0] read_drive;
  integer i;

  wire [1:0] read_addr = {p_4, p_5};
  wire [1:0] write_addr = {p_13, p_14};
  wire [3:0] write_word = {p_3, p_2, p_1, p_15};

  initial begin
    for (i = 0; i < 4; i = i + 1)
      mem[i] = 4'b0000;
    read_drive = 4'bzzzz;
  end

  // Transparent write path while /GW is low.
  always @* begin
    if (p_12 === 1'b0) begin
      case (write_addr)
        2'b00: mem[0] <= #(WRITE_TPD) write_word;
        2'b01: mem[1] <= #(WRITE_TPD) write_word;
        2'b10: mem[2] <= #(WRITE_TPD) write_word;
        2'b11: mem[3] <= #(WRITE_TPD) write_word;
        default: begin
          mem[0] <= #(WRITE_TPD) 4'bxxxx;
          mem[1] <= #(WRITE_TPD) 4'bxxxx;
          mem[2] <= #(WRITE_TPD) 4'bxxxx;
          mem[3] <= #(WRITE_TPD) 4'bxxxx;
        end
      endcase

    end
  end

  always @* begin
    case (read_addr)
      2'b00: selected_word = mem[0];
      2'b01: selected_word = mem[1];
      2'b10: selected_word = mem[2];
      2'b11: selected_word = mem[3];
      default: selected_word = 4'bxxxx;
    endcase

    if (p_12 === 1'b0 && read_addr === write_addr) begin
      selected_word = write_word;
    end
    // (2026-07-09, A/B tested): clause 2 (cross-address
    // {1'b1,p_14} feedthrough) REMOVED — non-physical 74LS670 primitive
    // artifact (real part = four independent latches; datasheet + git
    // archaeology + removal A/B all say bench-artifact). Clause 1
    // (same-address write-through transparency) retained.

    if (p_11 === 1'b1) begin
      read_drive = 4'bzzzz;
    end else if (p_11 === 1'b0) begin
      read_drive = selected_word;
    end else begin
      read_drive = 4'bxxxx;
    end
  end

  assign #(READ_TPLH, READ_TPHL, TOHZ) p_10 = read_drive[0];
  assign #(READ_TPLH, READ_TPHL, TOHZ) p_9  = read_drive[1];
  assign #(READ_TPLH, READ_TPHL, TOHZ) p_7  = read_drive[2];
  assign #(READ_TPLH, READ_TPHL, TOHZ) p_6  = read_drive[3];
endmodule

module ttl_74ls74(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // SN74S74N datasheet max for CLR/PRE/CLK -> Q/Qn: up to 40ns.
  parameter real TPD = 40.0;
  reg q1 = 1'b0;
  reg q2 = 1'b0;

  // FF1: CLR_n=1, D=2, CLK=3, PRE_n=4, Q=5, Qn=6
  always @(negedge p_1 or negedge p_4 or posedge p_3) begin
    if (p_1 === 1'b0 && p_4 === 1'b1) q1 <= #(TPD) 1'b0;
    else if (p_1 === 1'b1 && p_4 === 1'b0) q1 <= #(TPD) 1'b1;
    else if (p_1 === 1'b0 && p_4 === 1'b0) q1 <= #(TPD) 1'bx;
    else if (p_1 === 1'b1 && p_4 === 1'b1) q1 <= #(TPD) p_2;
    else q1 <= #(TPD) 1'bx;
  end

  // FF2: CLR_n=13, D=12, CLK=11, PRE_n=10, Q=9, Qn=8
  always @(negedge p_13 or negedge p_10 or posedge p_11) begin
    if (p_13 === 1'b0 && p_10 === 1'b1) q2 <= #(TPD) 1'b0;
    else if (p_13 === 1'b1 && p_10 === 1'b0) q2 <= #(TPD) 1'b1;
    else if (p_13 === 1'b0 && p_10 === 1'b0) q2 <= #(TPD) 1'bx;
    else if (p_13 === 1'b1 && p_10 === 1'b1) q2 <= #(TPD) p_12;
    else q2 <= #(TPD) 1'bx;
  end

  assign p_5 = q1;
  assign p_6 = ~q1;
  assign p_9 = q2;
  assign p_8 = ~q2;
endmodule

module ttl_74ls27(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // 74LS27: triple 3-input NOR
  // SN74LS27 datasheet max: tPLH/tPHL = 15ns.
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;
  assign #(TPLH, TPHL) p_12 = ~(p_1 | p_2 | p_13);
  assign #(TPLH, TPHL) p_6  = ~(p_3 | p_4 | p_5);
  assign #(TPLH, TPHL) p_8  = ~(p_9 | p_10 | p_11);
endmodule

module ttl_sn74s112an(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter integer TPD = 8;
  reg q1 = 1'b0;
  reg q2 = 1'b0;

  // FF1: CLK=1 (negedge), J=3, K=2, CLR_n=15, PRE_n=4, Q=5, Qn=6
  always @(negedge p_1 or negedge p_15 or negedge p_4) begin
    if (p_15 === 1'b0 && p_4 === 1'b1) q1 <= #(TPD) 1'b0;
    else if (p_15 === 1'b1 && p_4 === 1'b0) q1 <= #(TPD) 1'b1;
    else if (p_15 === 1'b0 && p_4 === 1'b0) q1 <= #(TPD) 1'bx;
    else if (p_15 === 1'b1 && p_4 === 1'b1) begin
      if (p_3 === 1'b0 && p_2 === 1'b0) q1 <= #(TPD) q1;
      else if (p_3 === 1'b0 && p_2 === 1'b1) q1 <= #(TPD) 1'b0;
      else if (p_3 === 1'b1 && p_2 === 1'b0) q1 <= #(TPD) 1'b1;
      else if (p_3 === 1'b1 && p_2 === 1'b1) q1 <= #(TPD) ~q1;
      else q1 <= #(TPD) 1'bx;
    end else begin
      q1 <= #(TPD) 1'bx;
    end
  end

  // FF2: CLK=13 (negedge), J=11, K=12, CLR_n=14, PRE_n=10, Q=9, Qn=7
  always @(negedge p_13 or negedge p_14 or negedge p_10) begin
    if (p_14 === 1'b0 && p_10 === 1'b1) q2 <= #(TPD) 1'b0;
    else if (p_14 === 1'b1 && p_10 === 1'b0) q2 <= #(TPD) 1'b1;
    else if (p_14 === 1'b0 && p_10 === 1'b0) q2 <= #(TPD) 1'bx;
    else if (p_14 === 1'b1 && p_10 === 1'b1) begin
      if (p_11 === 1'b0 && p_12 === 1'b0) q2 <= #(TPD) q2;
      else if (p_11 === 1'b0 && p_12 === 1'b1) q2 <= #(TPD) 1'b0;
      else if (p_11 === 1'b1 && p_12 === 1'b0) q2 <= #(TPD) 1'b1;
      else if (p_11 === 1'b1 && p_12 === 1'b1) q2 <= #(TPD) ~q2;
      else q2 <= #(TPD) 1'bx;
    end else begin
      q2 <= #(TPD) 1'bx;
    end
  end

  assign p_5 = q1;
  assign p_6 = ~q1;
  assign p_9 = q2;
  assign p_7 = ~q2;
endmodule

module ttl_74ls03(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLZ = 15.0;
  parameter real TPHL = 15.0;

  // Open-collector outputs actively pull low; logic-high becomes high impedance.
  function automatic oc_nand2;
    input a;
    input b;
    begin
      if (a === 1'b1 && b === 1'b1) oc_nand2 = 1'b0;
      else if (a === 1'b0 || b === 1'b0) oc_nand2 = 1'bz;
      else oc_nand2 = 1'bx;
    end
  endfunction

  assign #(TPLZ, TPHL, TPLZ) p_3  = oc_nand2(p_1,  p_2);
  assign #(TPLZ, TPHL, TPLZ) p_6  = oc_nand2(p_4,  p_5);
  assign #(TPLZ, TPHL, TPLZ) p_8  = oc_nand2(p_9,  p_10);
  assign #(TPLZ, TPHL, TPLZ) p_11 = oc_nand2(p_12, p_13);
endmodule

module ttl_74ls133(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;
  wire [12:0] inputs = {p_15, p_14, p_13, p_12, p_11, p_10, p_7, p_6, p_5, p_4, p_3, p_2, p_1};
  assign #(TPLH, TPHL) p_9 = ~&inputs;
endmodule

module ttl_74ls174(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real CLR_TPD = 25.0;
  parameter real CLK_TPD = 25.0;
  reg [5:0] q = 6'b000000;

  always @(negedge p_1 or posedge p_9) begin
    if (p_1 === 1'b0)
      q <= #(CLR_TPD) 6'b000000;
    else if (p_1 === 1'b1)
      q <= #(CLK_TPD) {p_14, p_13, p_11, p_6, p_4, p_3};
    else
      q <= #(CLK_TPD) 6'bxxxxxx;
  end

  assign p_2  = q[0];
  assign p_5  = q[1];
  assign p_7  = q[2];
  assign p_10 = q[3];
  assign p_12 = q[4];
  assign p_15 = q[5];
endmodule

module ttl_74ls04(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;
  assign #(TPLH, TPHL) p_2  = ~p_1;
  assign #(TPLH, TPHL) p_4  = ~p_3;
  assign #(TPLH, TPHL) p_6  = ~p_5;
  assign #(TPLH, TPHL) p_8  = ~p_9;
  assign #(TPLH, TPHL) p_10 = ~p_11;
  assign #(TPLH, TPHL) p_12 = ~p_13;
endmodule

module ttl_74ls08(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;
  assign #(TPLH, TPHL) p_3  = (p_1  & p_2);
  assign #(TPLH, TPHL) p_6  = (p_4  & p_5);
  assign #(TPLH, TPHL) p_8  = (p_9  & p_10);
  assign #(TPLH, TPHL) p_11 = (p_12 & p_13);
endmodule

module ttl_74ls10(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;
  assign #(TPLH, TPHL) p_12 = ~(p_1 & p_2 & p_13);
  assign #(TPLH, TPHL) p_6  = ~(p_3 & p_4 & p_5);
  assign #(TPLH, TPHL) p_8  = ~(p_9 & p_10 & p_11);
endmodule

module ttl_74ls139(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // SN74LS139A datasheet, switching characteristics at 25 C, from the
  // enable input (2 levels): tPLH 16 typ / 24 max, tPHL 21 typ / 32 max
  // (from select: 13/20 and 22/33 at 2 levels, 18/29 and 25/38 at 3).
  // Typicals from the enable path (2026-09-25): the HP 5004A ARU tables
  // print RDRREG/ (U47 G -> Y1 -> U48A -> U48C) asserted one analyzer
  // sample earlier than a flat 25 ns gave; U43.1/U44.1 match at
  // TPHL <= 22 and differ at >= 23, TPLH is immaterial there. The old
  // flat 25.0 had no datasheet source.
  parameter real TPLH = 16.0;
  parameter real TPHL = 21.0;

  wire [1:0] sel_a = {p_3, p_2};
  wire [1:0] sel_b = {p_13, p_14};

  wire ena_a = (p_1 === 1'b0);
  wire ena_b = (p_15 === 1'b0);
  wire inv_a = (p_1 === 1'bx || p_1 === 1'bz || sel_a[0] === 1'bx || sel_a[0] === 1'bz || sel_a[1] === 1'bx || sel_a[1] === 1'bz);
  wire inv_b = (p_15 === 1'bx || p_15 === 1'bz || sel_b[0] === 1'bx || sel_b[0] === 1'bz || sel_b[1] === 1'bx || sel_b[1] === 1'bz);

  assign #(TPLH, TPHL) p_4  = inv_a ? 1'bx : (ena_a ? (sel_a == 2'b00 ? 1'b0 : 1'b1) : 1'b1);
  assign #(TPLH, TPHL) p_5  = inv_a ? 1'bx : (ena_a ? (sel_a == 2'b01 ? 1'b0 : 1'b1) : 1'b1);
  assign #(TPLH, TPHL) p_6  = inv_a ? 1'bx : (ena_a ? (sel_a == 2'b10 ? 1'b0 : 1'b1) : 1'b1);
  assign #(TPLH, TPHL) p_7  = inv_a ? 1'bx : (ena_a ? (sel_a == 2'b11 ? 1'b0 : 1'b1) : 1'b1);

  assign #(TPLH, TPHL) p_12 = inv_b ? 1'bx : (ena_b ? (sel_b == 2'b00 ? 1'b0 : 1'b1) : 1'b1);
  assign #(TPLH, TPHL) p_11 = inv_b ? 1'bx : (ena_b ? (sel_b == 2'b01 ? 1'b0 : 1'b1) : 1'b1);
  assign #(TPLH, TPHL) p_10 = inv_b ? 1'bx : (ena_b ? (sel_b == 2'b10 ? 1'b0 : 1'b1) : 1'b1);
  assign #(TPLH, TPHL) p_9  = inv_b ? 1'bx : (ena_b ? (sel_b == 2'b11 ? 1'b0 : 1'b1) : 1'b1);
endmodule

module ttl_74ls155(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 25.0;
  parameter real TPHL = 25.0;

  function automatic ena_a;
    input e1;
    input e2;
    begin
      // Section A is enabled when Ea1 is high and Ea2 is low.
      if (e1 === 1'b1 && e2 === 1'b0) ena_a = 1'b1;
      else if (e1 === 1'b0 || e2 === 1'b1) ena_a = 1'b0;
      else ena_a = 1'bx;
    end
  endfunction

  function automatic ena_b;
    input e1;
    input e2;
    begin
      // Section B uses two active-low enables.
      if (e1 === 1'b0 && e2 === 1'b0) ena_b = 1'b1;
      else if (e1 === 1'b1 || e2 === 1'b1) ena_b = 1'b0;
      else ena_b = 1'bx;
    end
  endfunction

  function automatic decode_out;
    input en;
    input [1:0] sel;
    input [1:0] target;
    begin
      if (en === 1'b0) begin
        decode_out = 1'b1;
      end else if (en === 1'b1) begin
        if ((sel[1] === 1'b0 || sel[1] === 1'b1) &&
            (sel[0] === 1'b0 || sel[0] === 1'b1))
          decode_out = (sel == target) ? 1'b0 : 1'b1;
        else
          decode_out = 1'bx;
      end else begin
        decode_out = 1'bx;
      end
    end
  endfunction

  wire [1:0] sel = {p_3, p_13};
  wire en_a = ena_a(p_1, p_2);
  wire en_b = ena_b(p_14, p_15);

  assign #(TPLH, TPHL) p_7  = decode_out(en_a, sel, 2'b00);
  assign #(TPLH, TPHL) p_6  = decode_out(en_a, sel, 2'b01);
  assign #(TPLH, TPHL) p_5  = decode_out(en_a, sel, 2'b10);
  assign #(TPLH, TPHL) p_4  = decode_out(en_a, sel, 2'b11);
  assign #(TPLH, TPHL) p_9  = decode_out(en_b, sel, 2'b00);
  assign #(TPLH, TPHL) p_10 = decode_out(en_b, sel, 2'b01);
  assign #(TPLH, TPHL) p_11 = decode_out(en_b, sel, 2'b10);
  assign #(TPLH, TPHL) p_12 = decode_out(en_b, sel, 2'b11);
endmodule

module ttl_74ls244(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 15.0;
  parameter real TPHL = 15.0;
  parameter real TOHZ = 15.0;

  function automatic tri_buf;
    input oe_n;
    input d;
    begin
      if (oe_n === 1'b0) tri_buf = d;
      else if (oe_n === 1'b1) tri_buf = 1'bz;
      else tri_buf = 1'bx;
    end
  endfunction

  assign #(TPLH, TPHL, TOHZ) p_18 = tri_buf(p_1,  p_2);
  assign #(TPLH, TPHL, TOHZ) p_16 = tri_buf(p_1,  p_4);
  assign #(TPLH, TPHL, TOHZ) p_14 = tri_buf(p_1,  p_6);
  assign #(TPLH, TPHL, TOHZ) p_12 = tri_buf(p_1,  p_8);
  assign #(TPLH, TPHL, TOHZ) p_9  = tri_buf(p_19, p_11);
  assign #(TPLH, TPHL, TOHZ) p_7  = tri_buf(p_19, p_13);
  assign #(TPLH, TPHL, TOHZ) p_5  = tri_buf(p_19, p_15);
  assign #(TPLH, TPHL, TOHZ) p_3  = tri_buf(p_19, p_17);
endmodule

module ttl_74ls195(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real CLR_TPD = 20.0;
  parameter real CLK_TPD = 20.0;
  reg [3:0] q = 4'b0000;

  function automatic jkbar_step;
    input q_prev;
    input j;
    input k_n;
    begin
      if (j === 1'b0 && k_n === 1'b0) begin
        jkbar_step = 1'b0;
      end else if (j === 1'b1 && k_n === 1'b1) begin
        jkbar_step = 1'b1;
      end else if (j === 1'b0 && k_n === 1'b1) begin
        jkbar_step = q_prev;
      end else if (j === 1'b1 && k_n === 1'b0) begin
        if (q_prev === 1'b0) jkbar_step = 1'b1;
        else if (q_prev === 1'b1) jkbar_step = 1'b0;
        else jkbar_step = 1'bx;
      end else if (q_prev === 1'b0) begin
        if (j === 1'b0) jkbar_step = 1'b0;
        else if (j === 1'b1) jkbar_step = 1'b1;
        else jkbar_step = 1'bx;
      end else if (q_prev === 1'b1) begin
        if (k_n === 1'b0) jkbar_step = 1'b0;
        else if (k_n === 1'b1) jkbar_step = 1'b1;
        else jkbar_step = 1'bx;
      end else begin
        jkbar_step = 1'bx;
      end
    end
  endfunction

  always @(negedge p_1 or posedge p_10) begin
    if (p_1 === 1'b0) begin
      q <= #(CLR_TPD) 4'b0000;
    end else if (p_1 === 1'b1) begin
      if (p_9 === 1'b0)
        q <= #(CLK_TPD) {p_7, p_6, p_5, p_4};
      else if (p_9 === 1'b1)
        q <= #(CLK_TPD) {q[2], q[1], q[0], jkbar_step(q[0], p_2, p_3)};
      else
        q <= #(CLK_TPD) 4'bxxxx;
    end else begin
      q <= #(CLK_TPD) 4'bxxxx;
    end
  end

  assign p_15 = q[0];
  assign p_14 = q[1];
  assign p_13 = q[2];
  assign p_12 = q[3];
  assign p_11 = ~q[3];
endmodule

module ttl_74ls157(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 20.0;
  parameter real TPHL = 20.0;

  function automatic mux2;
    input e_n;
    input s;
    input i0;
    input i1;
    begin
      if (e_n === 1'b1) mux2 = 1'b0;
      else if (e_n === 1'b0) begin
        if (s === 1'b0) mux2 = i0;
        else if (s === 1'b1) mux2 = i1;
        else mux2 = 1'bx;
      end else begin
        mux2 = 1'bx;
      end
    end
  endfunction

  // KiCad's DIP74LS157 symbol uses pin 1 as S and pin 15 as active-low E.
  assign #(TPLH, TPHL) p_4  = mux2(p_15, p_1, p_2,  p_3);
  assign #(TPLH, TPHL) p_7  = mux2(p_15, p_1, p_5,  p_6);
  assign #(TPLH, TPHL) p_9  = mux2(p_15, p_1, p_11, p_10);
  assign #(TPLH, TPHL) p_12 = mux2(p_15, p_1, p_14, p_13);
endmodule

module ttl_74ls86(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // S86 speed grade: the ARU schematic letters its XOR packages "S86"
  // (U42 clock/SAT gates, U5-U9 mask banks) and the 224A parts column
  // confirms 74S86. The ZERO//XFER-vs-internal-CK margins at the T&C+ARU
  // boundary are only a few ns and genuinely require S-grade delays
  // (LS-grade 15ns makes ZERO/'s release beat the U42A clock edge and
  // the 163 clear never fires). 7ns = 74S86 typical.
  parameter real TPLH = 7.0;
  parameter real TPHL = 7.0;
  assign #(TPLH, TPHL) p_3  = (p_1  ^ p_2);
  assign #(TPLH, TPHL) p_6  = (p_4  ^ p_5);
  assign #(TPLH, TPHL) p_8  = (p_9  ^ p_10);
  assign #(TPLH, TPHL) p_11 = (p_12 ^ p_13);
endmodule

module ttl_74ls377(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter integer TPD = 20;
  reg [7:0] q = 8'h00;

  always @(posedge p_11) begin
    if (p_1 === 1'b0)
      q <= #(TPD) {p_18, p_17, p_14, p_13, p_8, p_7, p_4, p_3};
    else if (p_1 === 1'bx || p_1 === 1'bz)
      q <= #(TPD) 8'hxx;
  end

  assign p_2  = q[0];
  assign p_5  = q[1];
  assign p_6  = q[2];
  assign p_9  = q[3];
  assign p_12 = q[4];
  assign p_15 = q[5];
  assign p_16 = q[6];
  assign p_19 = q[7];
endmodule

module ttl_74ls123(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPD = 35.0;
  parameter real TW1 = 280.0;
  parameter real TW2 = 280.0;

  reg q1 = 1'b0;
  reg q2 = 1'b0;
  integer pulse1_id = 0;
  integer pulse2_id = 0;

  // Delayed NBA values capture their serial when scheduled. Static task-local
  // variables are shared across retriggers and can let old timers end new pulses.
  // Validate at the output deadline too: a retrigger can arrive during TPD.
  integer expired1 = 0;
  integer expired2 = 0;
  task trigger_1;
    begin
      pulse1_id = pulse1_id + 1;
      q1 <= #(TPD) 1'b1;
      expired1 <= #(TW1 + TPD) pulse1_id;
    end
  endtask
  task trigger_2;
    begin
      pulse2_id = pulse2_id + 1;
      q2 <= #(TPD) 1'b1;
      expired2 <= #(TW2 + TPD) pulse2_id;
    end
  endtask
  always @(expired1)
    if (expired1 == pulse1_id && p_3 === 1'b1) q1 <= 1'b0;
  always @(expired2)
    if (expired2 == pulse2_id && p_11 === 1'b1) q2 <= 1'b0;

  // Channel 1 trigger modes:
  // - A low, B rising
  // - B high, A falling
  // - A low and B high when CLR rises
  always @(posedge p_2) begin
    if (p_1 === 1'b0 && p_3 === 1'b1)
      trigger_1;
  end

  always @(negedge p_1) begin
    if (p_2 === 1'b1 && p_3 === 1'b1)
      trigger_1;
  end

  always @(posedge p_3) begin
    if (p_1 === 1'b0 && p_2 === 1'b1)
      trigger_1;
  end

  always @(negedge p_3) begin
    pulse1_id = pulse1_id + 1;
    q1 <= #(TPD) 1'b0;
  end

  // Channel 2 trigger modes mirror channel 1.
  always @(posedge p_10) begin
    if (p_9 === 1'b0 && p_11 === 1'b1)
      trigger_2;
  end

  always @(negedge p_9) begin
    if (p_10 === 1'b1 && p_11 === 1'b1)
      trigger_2;
  end

  always @(posedge p_11) begin
    if (p_9 === 1'b0 && p_10 === 1'b1)
      trigger_2;
  end

  always @(negedge p_11) begin
    pulse2_id = pulse2_id + 1;
    q2 <= #(TPD) 1'b0;
  end

  // Datasheet pinout: 1Q=13, 1Q~=4, 2Q=5, 2Q~=12 (true-Q pins were swapped
  // until 2026-07-28; the complements were always correct).
  assign p_13 = q1;
  assign p_4 = ~q1;
  assign p_5 = q2;
  assign p_12 = ~q2;
endmodule

module ttl_am8304n(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPLH = 20.0;
  parameter real TPHL = 20.0;
  parameter real TOHZ = 20.0;

  wire use_explicit_g = (p_9 === 1'b0) || (p_9 === 1'b1);
  wire auto_enable_ctl = use_explicit_g ? 1'b1 : p_11;
  wire explicit_en = (p_9 === 1'b0);

  function automatic tri_dir;
    input en;
    input src;
    begin
      if (en === 1'b1) tri_dir = src;
      else if (en === 1'b0) tri_dir = 1'bz;
      else tri_dir = 1'bx;
    end
  endfunction

  // Page-1 symbols expose both G and DIR. Timing&Control's AMB304 symbol dropped G,
  // so when p_9 is absent this falls back to a simple enabled bidirectional bus switch.
  assign #(TPLH, TPHL, TOHZ) p_12 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_8) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_13 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_7) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_14 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_6) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_15 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_5) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_16 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_4) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_17 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_3) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_18 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_2) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_19 = use_explicit_g ? ((p_11 === 1'b1) ? tri_dir(explicit_en, p_1) : ((p_11 === 1'b0) ? 1'bz : 1'bx)) : 1'bz;

  assign #(TPLH, TPHL, TOHZ) p_8 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_12) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_7 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_13) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_6 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_14) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_5 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_15) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_4 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_16) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_3 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_17) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_2 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_18) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;
  assign #(TPLH, TPHL, TOHZ) p_1 = use_explicit_g ? ((p_11 === 1'b0) ? tri_dir(explicit_en, p_19) : ((p_11 === 1'b1) ? 1'bz : 1'bx)) : 1'bz;

  tranif0 amb304_0(p_1,  p_19, auto_enable_ctl);
  tranif0 amb304_1(p_2,  p_18, auto_enable_ctl);
  tranif0 amb304_2(p_3,  p_17, auto_enable_ctl);
  tranif0 amb304_3(p_4,  p_16, auto_enable_ctl);
  tranif0 amb304_4(p_5,  p_15, auto_enable_ctl);
  tranif0 amb304_5(p_6,  p_14, auto_enable_ctl);
  tranif0 amb304_6(p_7,  p_13, auto_enable_ctl);
  tranif0 amb304_7(p_8,  p_12, auto_enable_ctl);
endmodule

module ttl_mcm68b10(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TAA = 250.0;
  parameter real TWC = 250.0;
  parameter real TOHZ = 80.0;
  parameter real TEN = 60.0;
  // Opt-in data-book write-window endpoint.  Legacy generated-board users
  // retain the historical TWC-after-assertion schedule unless a wrapper
  // explicitly presents and checks a finite physical write window.
  parameter integer WRITE_ON_WINDOW_END = 0;

  parameter INIT_FILE = "";

  reg [7:0] mem [0:127];
  integer i;
  initial begin
    // Deterministic startup keeps board-level regressions debuggable.
    for (i = 0; i < 128; i = i + 1)
      mem[i] = 8'h00;
    if (INIT_FILE != "")
      $readmemh(INIT_FILE, mem);
  end

  wire cs_active = (p_10 === 1'b1) && (p_13 === 1'b1) &&
                   (p_11 === 1'b0) && (p_12 === 1'b0) &&
                   (p_14 === 1'b0) && (p_15 === 1'b0);
  wire reading = cs_active && (p_16 === 1'b1);
  wire writing = cs_active && (p_16 === 1'b0);
  wire [6:0] addr = {p_17, p_18, p_19, p_20, p_21, p_22, p_23};
  wire [7:0] data_in = {p_9, p_8, p_7, p_6, p_5, p_4, p_3, p_2};

  generate
    if (WRITE_ON_WINDOW_END) begin : physical_write_endpoint
      reg write_window_open = 1'b0;
      always @(posedge writing) write_window_open = 1'b1;
      always @(negedge writing) begin
        if (write_window_open) begin
          // MCM68B10 data/address hold is 10 ns minimum after write release.
          mem[addr] <= #(10.0) data_in;
          write_window_open = 1'b0;
        end
      end
    end else begin : legacy_write_schedule
      always @(writing or addr or data_in) begin
        if (writing)
          mem[addr] <= #(TWC) data_in;
      end
    end
  endgenerate

  // Address access (TAA) is a TRANSPORT delay on the cell value; the 224X
  // gates the WCS RAMs with a dynamic chip select, so the output stage must
  // gate quickly (TEN, chip-select access) or sub-250ns read windows would
  // be swallowed by an inertial TAA on the pin assigns.
  //
  // 2026-07-05 datasheet-conformance fix: tAA on the MCM68B10 is the
  // address-to-OUTPUT-PIN access time (measured with CS active), and tEN
  // (chip-select access) applies to CS transitions IN PARALLEL — the two do
  // not stack.  The previous shape (TAA transport on cell_d, then TEN again
  // on every pin assign) made address-to-pin 310ns > the 293ns DSP row, so
  // the pins presented data one full row later than a real part: the value
  // for the current address only reached the pins ~49ns AFTER the next
  // DAB_RSTB/ row boundary instead of ~11ns BEFORE it.  TEN/TOHZ now live
  // on the enable path (rise=TEN enable, fall=TOHZ disable); the data pins
  // follow cell_d directly, so addr-to-pin = TAA as specified.
  reg [7:0] cell_d = 8'h00;
  always @(addr) cell_d <= #(TAA) mem[addr];
  wire reading_d;
  assign #(TEN, TOHZ) reading_d = reading;
  wire [7:0] data_out = reading_d ? cell_d : 8'hzz;
  assign p_2 = data_out[0];
  assign p_3 = data_out[1];
  assign p_4 = data_out[2];
  assign p_5 = data_out[3];
  assign p_6 = data_out[4];
  assign p_7 = data_out[5];
  assign p_8 = data_out[6];
  assign p_9 = data_out[7];
endmodule

// ===== Merged from 00-dmem-board/hdl/prims.v =====

module ideal_resistor(
  inout p_1, inout p_2
);
  // rtran, not tran: a resistor passes a WEAKENED drive, so a pull-up to +5V
  // loses to any strong driver on the far side instead of x-ing the rail.
  rtran rlink(p_1, p_2);
endmodule

// Eight isolated resistors in a DIP-16 pack (the DMEM board's U17, 33 ohm x 8):
// resistor k joins pins k and 17-k.
module ideal_resistor_pack8(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6, inout p_7, inout p_8,
  inout p_9, inout p_10, inout p_11, inout p_12, inout p_13, inout p_14, inout p_15, inout p_16
);
  rtran r1(p_1, p_16);
  rtran r2(p_2, p_15);
  rtran r3(p_3, p_14);
  rtran r4(p_4, p_13);
  rtran r5(p_5, p_12);
  rtran r6(p_6, p_11);
  rtran r7(p_7, p_10);
  rtran r8(p_8, p_9);
endmodule

module ttl_74ls283(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter real TPD = 9.0;
  // Separate sum and carry-out delays (rise, fall); default to TPD.
  parameter real S_LH = TPD;
  parameter real S_HL = TPD;
  parameter real C_LH = TPD;
  parameter real C_HL = TPD;
  wire [3:0] a = {p_12, p_14, p_3, p_5};
  wire [3:0] b = {p_11, p_15, p_2, p_6};
  wire [4:0] sum = {1'b0, a} + {1'b0, b} + ((p_7 === 1'b1) ? 5'b00001 :
                                             (p_7 === 1'b0) ? 5'b00000 : 5'bxxxxx);

  assign #(S_LH, S_HL) p_4  = sum[0];
  assign #(S_LH, S_HL) p_1  = sum[1];
  assign #(S_LH, S_HL) p_13 = sum[2];
  assign #(S_LH, S_HL) p_10 = sum[3];
  assign #(C_LH, C_HL) p_9  = sum[4];
endmodule

module ttl_74ls393(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // Matched to the local SPICE macros:
  // - asynchronous clear is active high
  // - counting occurs on the high-to-low clock edge
  // - higher stages ripple from the previous output's falling edge
  parameter real CLR_TPD = 24.0;
  parameter real QA_TPD  = 13.0;
  parameter real RIPPLE_TPD = 9.0;

  reg [3:0] a_q = 4'b0000;
  reg [3:0] b_q = 4'b0000;

  always @(posedge p_2 or negedge p_1) begin
    if (p_2 === 1'b1)
      a_q[0] <= #(CLR_TPD) 1'b0;
    else if (p_2 === 1'b0)
      a_q[0] <= #(QA_TPD) ~a_q[0];
    else
      a_q[0] <= #(QA_TPD) 1'bx;
  end

  always @(posedge p_2 or negedge a_q[0]) begin
    if (p_2 === 1'b1)
      a_q[1] <= #(CLR_TPD) 1'b0;
    else
      a_q[1] <= #(RIPPLE_TPD) ~a_q[1];
  end

  always @(posedge p_2 or negedge a_q[1]) begin
    if (p_2 === 1'b1)
      a_q[2] <= #(CLR_TPD) 1'b0;
    else
      a_q[2] <= #(RIPPLE_TPD) ~a_q[2];
  end

  always @(posedge p_2 or negedge a_q[2]) begin
    if (p_2 === 1'b1)
      a_q[3] <= #(CLR_TPD) 1'b0;
    else
      a_q[3] <= #(RIPPLE_TPD) ~a_q[3];
  end

  always @(posedge p_12 or negedge p_13) begin
    if (p_12 === 1'b1)
      b_q[0] <= #(CLR_TPD) 1'b0;
    else if (p_12 === 1'b0)
      b_q[0] <= #(QA_TPD) ~b_q[0];
    else
      b_q[0] <= #(QA_TPD) 1'bx;
  end

  always @(posedge p_12 or negedge b_q[0]) begin
    if (p_12 === 1'b1)
      b_q[1] <= #(CLR_TPD) 1'b0;
    else
      b_q[1] <= #(RIPPLE_TPD) ~b_q[1];
  end

  always @(posedge p_12 or negedge b_q[1]) begin
    if (p_12 === 1'b1)
      b_q[2] <= #(CLR_TPD) 1'b0;
    else
      b_q[2] <= #(RIPPLE_TPD) ~b_q[2];
  end

  always @(posedge p_12 or negedge b_q[2]) begin
    if (p_12 === 1'b1)
      b_q[3] <= #(CLR_TPD) 1'b0;
    else
      b_q[3] <= #(RIPPLE_TPD) ~b_q[3];
  end

  assign p_3  = a_q[0];
  assign p_4  = a_q[1];
  assign p_5  = a_q[2];
  assign p_6  = a_q[3];
  assign p_11 = b_q[0];
  assign p_10 = b_q[1];
  assign p_9  = b_q[2];
  assign p_8  = b_q[3];
endmodule

`ifndef LEXICON_EXTERNAL_MK4164
module ttl_mk4164n(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  // The KiCad symbol exported for this board is a 7-row / 7-column multiplexed,
  // multi-supply DRAM pinout despite being labeled "4164". This placeholder
  // model follows the netlist pinout, not the marketing part number.
  parameter integer ADDR_BITS = 7;
  parameter real READ_TPD = 25.0;
  parameter real TOHZ = 20.0;
  parameter real WRITE_TPD = 20.0;
  localparam integer DEPTH = 1 << (ADDR_BITS * 2);

  reg mem [0:DEPTH - 1];
  reg [ADDR_BITS - 1:0] row_addr = {ADDR_BITS{1'b0}};
  reg [ADDR_BITS - 1:0] col_addr = {ADDR_BITS{1'b0}};
  reg row_valid = 1'b0;
  reg col_valid = 1'b0;
  integer idx;

  // 4116 strapping uses 7 address pins; 4164 strapping adds pin 9 as the
  // high bit (fed by AD7 through jumper J1 on the 224X DMEM board).
  wire [7:0] mux_pins = {p_9, p_13, p_10, p_11, p_12, p_6, p_7, p_5};
  wire [ADDR_BITS - 1:0] mux_addr = mux_pins[ADDR_BITS - 1:0];
  wire [ADDR_BITS * 2 - 1:0] full_addr = {row_addr, col_addr};
  wire access_valid = row_valid && col_valid;
  wire read_en = (p_4 === 1'b0) && (p_15 === 1'b0) && (p_3 === 1'b1) && access_valid;

  function automatic tri_mem;
    input enable;
    input data;
    begin
      if (enable === 1'b1) tri_mem = data;
      else if (enable === 1'b0) tri_mem = 1'bz;
      else tri_mem = 1'bx;
    end
  endfunction

  initial begin
    for (idx = 0; idx < DEPTH; idx = idx + 1)
      mem[idx] = 1'b0;
  end

  always @(negedge p_4) begin
    row_addr <= mux_addr;
    row_valid <= 1'b1;
    col_valid <= 1'b0;
  end

  always @(posedge p_4) begin
    row_valid <= 1'b0;
    col_valid <= 1'b0;
  end

  always @(negedge p_15) begin
    if (row_valid) begin
      col_addr <= mux_addr;
      col_valid <= 1'b1;
      if (p_3 === 1'b0)
        mem[{row_addr, mux_addr}] <= #(WRITE_TPD) p_2;
    end
  end

  always @(negedge p_3) begin
    if (row_valid && (p_15 === 1'b0)) begin
      col_addr <= mux_addr;
      col_valid <= 1'b1;
      mem[{row_addr, mux_addr}] <= #(WRITE_TPD) p_2;
    end
  end

  assign #(READ_TPD, READ_TPD, TOHZ) p_14 = tri_mem(read_en, mem[full_addr]);
endmodule
`endif


// 74LS138: 3-to-8 line decoder/demultiplexer, active-low outputs.
// DIP-16: A=1 B=2 C=3 G2A/=4 G2B/=5 G1=6 Y7=7 GND=8
//         Y6=9 Y5=10 Y4=11 Y3=12 Y2=13 Y1=14 Y0=15 VCC=16
module ttl_74ls138(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16
);
  parameter real TPD = 20.0;

  wire enabled = (p_6 === 1'b1) && (p_4 === 1'b0) && (p_5 === 1'b0);
  wire [2:0] sel = {p_3, p_2, p_1};

  function automatic decode_out;
    input en;
    input [2:0] s;
    input [2:0] line;
    begin
      if (en !== 1'b1) decode_out = 1'b1;
      else if (s === 3'bxxx || (^s) === 1'bx) decode_out = 1'bx;
      else decode_out = (s == line) ? 1'b0 : 1'b1;
    end
  endfunction

  assign #(TPD) p_15 = decode_out(enabled, sel, 3'd0);
  assign #(TPD) p_14 = decode_out(enabled, sel, 3'd1);
  assign #(TPD) p_13 = decode_out(enabled, sel, 3'd2);
  assign #(TPD) p_12 = decode_out(enabled, sel, 3'd3);
  assign #(TPD) p_11 = decode_out(enabled, sel, 3'd4);
  assign #(TPD) p_10 = decode_out(enabled, sel, 3'd5);
  assign #(TPD) p_9  = decode_out(enabled, sel, 3'd6);
  assign #(TPD) p_7  = decode_out(enabled, sel, 3'd7);
endmodule

// DMEM I/O U59, the DLG308 delay module, drawn with a stand-in LS244 symbol
// (the pin numbers below are the stand-in's). The tap net names give the delays: pin 18 -> /30ns, pins 14/16 -> /60ns,
// pin 12 -> /120ns, pin 9 -> /150ns, all from the input on pin 2.
// Tap re-entry pins (4, 6, 8, 11) chain segments on the board; modeling each
// tap from the single input is equivalent at the net level.
module dlg308_delay_module(
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20
);
  assign #(30)  p_18 = p_2;
  assign #(60)  p_14 = p_2;
  assign #(60)  p_16 = p_2;
  assign #(120) p_12 = p_2;
  assign #(150) p_9  = p_2;
endmodule
