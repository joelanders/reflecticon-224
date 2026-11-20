`timescale 1ns/1ps

// Mostek MK4164-15 timing model for the 224X DMEM board's corrected 8+8
// address mode.  This is deliberately separate from the historical 25 ns
// diagnostic primitive: experiments must opt in until every board fixture
// meets the part's input timing requirements.
`ifdef LEXICON_MK4164_CORRECTED_NETLIST_ALIAS
module ttl_mk4164n_4164mode(
`elsif LEXICON_MK4164_BOARD_ALIAS
module ttl_mk4164n(
`else
module ttl_mk4164n_4164mode_datasheet(
`endif
  inout p_1, inout p_2, inout p_3, inout p_4, inout p_5, inout p_6,
  inout p_7, inout p_8, inout p_9, inout p_10, inout p_11, inout p_12,
  inout p_13, inout p_14, inout p_15, inout p_16, inout p_17, inout p_18,
  inout p_19, inout p_20, inout p_21, inout p_22, inout p_23, inout p_24
);
  parameter integer ADDR_BITS = 8;
  parameter real TRAC_MAX = 150.0;
  parameter real TCAC_MAX = 75.0;
  parameter real TOHZ_MAX = 40.0;
  parameter real WRITE_TPD = 20.0;
  localparam integer DEPTH = 1 << (ADDR_BITS * 2);

  reg mem [0:DEPTH - 1];
  reg [ADDR_BITS - 1:0] row_addr = {ADDR_BITS{1'b0}};
  reg [ADDR_BITS - 1:0] col_addr = {ADDR_BITS{1'b0}};
  reg row_valid = 1'b0;
  reg col_valid = 1'b0;
  reg dout_active = 1'b0;
  reg dout_data = 1'b0;
  integer idx;

  wire [7:0] mux_pins = {p_9, p_13, p_10, p_11, p_12, p_6, p_7, p_5};
  wire [ADDR_BITS - 1:0] mux_addr = mux_pins[ADDR_BITS - 1:0];
  wire [ADDR_BITS * 2 - 1:0] full_addr = {row_addr, col_addr};
  wire ras_selected = (p_4 === 1'b0) && row_valid;
  wire cas_selected = (p_15 === 1'b0) && col_valid;
  wire ras_ready;
  wire cas_ready;
  wire access_ready = ras_ready || cas_ready;
  wire read_qualified =
    (p_3 === 1'b1) && ras_selected && cas_selected && access_ready;

  // The two maximum access specifications are independent upper bounds.  A
  // valid read may become available through either bound, but RAS and CAS
  // must still be asserted when it does.
  assign #(TRAC_MAX, 0) ras_ready = ras_selected;
  assign #(TCAC_MAX, 0) cas_ready = cas_selected;

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

  // A read may become valid only while RAS, CAS, and read mode are all
  // active.  Once it does, the MK4164's output latch is controlled by CAS:
  // releasing RAS must not invalidate DOUT while CAS remains asserted.  This
  // is the part's documented indefinite-DOUT-hold/hidden-refresh behavior and
  // is also required by the 224X Fig. 3.3 MS7/MS8 DOUT-valid aperture.
  always @(posedge read_qualified) begin
    dout_data <= mem[full_addr];
    dout_active <= 1'b1;
  end

  // CAS returning high disables the output buffer.  Entering write mode also
  // retires a prior read output; the continuous assignment below applies the
  // specified high-impedance turn-off delay to either cause.
  always @(posedge p_15 or negedge p_3)
    dout_active <= 1'b0;

  assign #(0, 0, TOHZ_MAX) p_14 = tri_mem(dout_active, dout_data);
endmodule
