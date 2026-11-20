// Lexicon 224X DSP (T&C + ARU + DMEM + XREG + FPC) as a row machine.
//
// One rising edge of row_clock = one WCS row = 292.97 ns. Each edge runs
// one microinstruction to completion, in the order written below. The
// hardware overlaps each microinstruction across three rows; README.md
// explains why this sequential order gives the same values.
module lexicon224x_dsp
    import lexicon224x::*;
#(
    // 1: the XREG input bank drives the bus on XREG-source rows, as the
    // schematic ink shows. 0 reproduces the
    // traced netlist, whose flipped XREG banks never drive it; for the
    // gate-level comparison only.
    parameter bit XREG_DRIVES_BUS = 1
)
(
    input  logic        row_clock,
    input  logic        run,                    // RUN (HALT/ high): low holds the program counter
    input  logic [11:0] adc_left, adc_right,    // AIN converter codes (12-bit two's complement)
    input  logic [1:0]  gain_left, gain_right,  // AIN gain-range pins
    input  logic [15:0] xreg_from_cpu,          // XREG input latch (written by the CPU)

    output logic        dac_capture = 0,        // the output holds took a sample this row
    output logic [3:0]  dac_channels,           // which holds (bit 0 = A)
    output logic [11:0] dac_code_out,           // offset-binary DAC code
    output logic [3:0]  dac_gain,               // 12..15 (gain pins = bits 1:0)
    output logic [15:0] xreg_to_cpu = 0         // last word captured by WR_XREG
);
    timeunit 1ns;
    timeprecision 1ns;

    // ---- Program ----------------------------------------------------
    microinstruction_t wcs [128];
    logic [6:0]   pc = 0;

    // ---- Arithmetic unit --------------------------------------------
    logic [15:0] R [4];    // register file: the only operands a multiply can use
    value_t      X = '0;   // the current multiplicand (sample * 8)
    // The reference starts with a cleared partial-product register. It is
    // stored inverted, so its first clock adds -1: ACC starts at -1.
    value_t      ACC = -20'sd1;  // accumulator, saturating to 19 bits
    logic [15:0] RR = 0;   // result register: ACC[18:3] saved by XFER (unknown until then)

    // ---- Delay memory -----------------------------------------------
    logic [15:0] memory [65536];
    logic [15:0] cpc = 1;  // current position counter: +1 per pass

    // ---- End-of-pass bookkeeping ------------------------------------
    logic after_reset = 0; // the previous row was an OPER RESET
    logic advance_cpc = 0; // CPC advances before this row's memory access

    // ---- Converters ---------------------------------------------------
    converter_t fpc = CONVERTER_AT_FIRST_ROW;

    always_ff @(posedge row_clock) begin : execute_row
        microinstruction_t mi;
        oper_t o;
        logic oper;
        logic [15:0] bus, address, position;
        logic [15:0] registers [4];
        converter_t fpc_next;
        value_t x, acc;

        mi = wcs[pc];
        o = mi.low;
        oper = mi.op == OPER;

        // 1. SEQUENCE. A RESET row marks the end of a pass: the row after
        //    it is the pass's last, then the program counter returns to 0.
        //    With RUN low the counter holds, and the same row executes again.
        pc <= after_reset ? 7'd0 : run ? pc + 7'd1 : pc;
        after_reset <= oper && o.reset;

        // 2. DATA BUS. Every row has exactly one bus value. OPER chooses a
        //    driver (source 0 selects none); MEMW always puts RR on the bus;
        //    MEMR puts the memory word there (step 3); NOP leaves it
        //    undriven, which the register file reads as zero.
        case (mi.op)
            OPER:    bus = o.source == FROM_XREG ? (XREG_DRIVES_BUS ? xreg_from_cpu : 16'h0000)
                         : o.source == FROM_ADC  ? fpc.input_sample
                         : o.source == FROM_RR   ? RR
                         : 16'h0000;
            MEMW:    bus = RR;
            default: bus = 16'h0000;
        endcase

        // 3. DELAY MEMORY. The address is CPC minus the offset (the field
        //    holds OFST/, the complement). CPC advances once per pass, just
        //    before the first row of the new pass accesses memory.
        position = advance_cpc ? cpc + 16'd1 : cpc;
        cpc <= position;
        advance_cpc <= after_reset;
        address = position + mi.low + 16'd1;
        if (mi.op == MEMW) memory[address] <= bus;
        if (mi.op == MEMR) bus = memory[address];

        // 4. CONVERTER CLOCK. WR_DA offers the bus word to the DAC; the ADC
        //    side takes one step. An RD_AD row's register then receives the
        //    converter's new word, because the clock falls mid-row, between
        //    the DAC's sample of the bus and the register write.
        fpc_next = converter_clock(fpc, adc_left, adc_right, gain_left, gain_right,
                                   after_reset, oper && o.wr_da, channel_mask(o), bus);
        fpc <= fpc_next;
        dac_capture  <= |(output_selects(fpc_next) & ~output_selects(fpc));
        dac_channels <= output_selects(fpc_next) & ~output_selects(fpc);
        dac_code_out <= dac_code(fpc_next);
        dac_gain     <= fpc_next.output_gain;
        if (oper && o.source == FROM_ADC) bus = fpc_next.input_sample;

        // 5. REGISTER WRITE. Every row writes R[WA] from the bus, and the
        //    multiplicand is read afterwards: R[RA] sees this row's write.
        registers = R;
        registers[mi.wa] = bus;
        R[mi.wa] <= bus;
        if (oper && o.wr_xreg) xreg_to_cpu <= bus;

        // 6. MULTIPLY-ACCUMULATE.
        //    XFER saves the sum of everything accumulated so far (through
        //    the previous row). ZERO then clears it, so this row's product
        //    starts the next sum. keep_shifting reuses the last
        //    multiplicand scaled by 1/64: extra precision for tiny gains.
        x = (oper && o.keep_shifting) ? shift_two(shift_two(shift_two(X)))
                                      : from_sample(registers[mi.ra]);
        if (mi.xfer) RR <= to_sample(ACC);
        acc = mi.zero ? '0 : ACC;
        ACC <= multiply_accumulate(acc, x, mi.coefficient, mi.negative);
        X <= x;
    end

    // Memories start zeroed, matching the reference machines' prepared
    // start (not a claim about power-on DRAM).
    initial begin
        foreach (wcs[a]) wcs[a] = '0;
        foreach (memory[a]) memory[a] = 16'h0000;
        foreach (R[n]) R[n] = 16'h0000;
    end

    task automatic load_word(input int address, input logic [31:0] word);
        wcs[address] = microinstruction_t'(word);
    endtask
endmodule
