// Lexicon 224X DSP: the microinstruction, the arithmetic, and the converters.
//
// This package holds everything a row needs that is not machine state:
// the WCS word layout, the multiply-accumulate equations, and the
// floating-point converter's per-row clock. The machine that uses them is
// in dsp.sv. See README.md for the programmer's model these implement.
package lexicon224x;
    timeunit 1ns;
    timeprecision 1ns;

    // ------------------------------------------------------------------
    // THE MICROINSTRUCTION
    //
    // One 32-bit WCS word per row, in the polarity the T&C's
    // microinstruction register holds (the complement of the SBC byte view and of the
    // program dumps). The packed struct lists fields from bit 31 down.
    // ------------------------------------------------------------------
    typedef enum logic [1:0] {NOP = 2'd0, OPER = 2'd1, MEMW = 2'd2, MEMR = 2'd3} operation_t;

    typedef struct packed {
        logic [5:0]  coefficient;  // 31:26  magnitude in 1/32 steps: 0 .. 63/32
        logic        zero;         // 25     clear ACC (after XFER, before this product)
        logic        xfer;         // 24     RR := ACC
        logic        negative;     // 23     subtract this row's product
        logic        protect;      // 22     protect bit: SBC access to the WCS (see emulator/wcs_access.hpp)
        logic [1:0]  ra;           // 21:20  register that supplies the multiplicand
        logic [1:0]  wa;           // 19:18  register that captures the data bus
        operation_t  op;           // 17:16
        logic [15:0] low;          // 15:0   MEMR/MEMW: OFST/ (complemented offset)
                                   //        OPER: the fields in oper_t
    } microinstruction_t;

    // OPER's use of the low half. Which of the four DAB drivers speaks is
    // the only "source operand" the machine has.
    // Source 0 selects nothing: the read-select decoder's 2Y0/ is unwired
    // (224X 060-02475 U47 pin 12; 224 060-01317 U27 2Y0).
    typedef enum logic [1:0] {SELECTS_NOTHING = 2'd0, FROM_RR = 2'd1, FROM_XREG = 2'd2, FROM_ADC = 2'd3} source_t;

    typedef struct packed {
        logic [1:0] unused_15_14;
        source_t    source;        // 13:12  what drives the data bus this row
        logic       output_a;      // 11     DAC channel selects (SDAA..SDAD)
        logic       output_b;      // 10
        logic       output_c;      // 9
        logic       output_d;      // 8
        logic       wr_da;         // 7      offer the bus word to the DAC
        logic       wr_xreg;       // 6      copy the bus word to XREG for the CPU
        logic       unused_5;      // 5      TEST (diagnostics)
        logic       keep_shifting; // 4      multiplicand := previous one / 64
        logic       reset;         // 3      end of pass (see dsp.sv)
        logic [2:0] unused_2_0;
    } oper_t;

    function automatic logic [3:0] channel_mask(input oper_t o);
        return {o.output_d, o.output_c, o.output_b, o.output_a};  // bit 0 = A
    endfunction

    // ------------------------------------------------------------------
    // THE ARITHMETIC
    //
    // Numbers in the ARU are 20-bit two's complement. A 16-bit sample
    // enters as sample * 8 (three guard bits below it). ACC saturates to
    // the 19-bit range, and RR takes ACC bits 18:3 -- the sample format
    // again. A coefficient is c/32 for c = 0..63.
    // ------------------------------------------------------------------
    typedef logic signed [19:0] value_t;

    localparam value_t ACC_MAX = 20'sh3FFFF;  // +2^18 - 1
    localparam value_t ACC_MIN = 20'shC0000;  // -2^18

    function automatic value_t from_sample(input logic [15:0] sample);
        return {sample[15], sample, 3'b000};
    endfunction

    function automatic logic [15:0] to_sample(input value_t v);
        return v[18:3];  // truncation, no rounding
    endfunction

    // The multiplicand shift register moves two places per step. Bits 19
    // and 18 both hold the sign, so this is x >>> 2.
    function automatic value_t shift_two(input value_t x);
        return {x[19:18], x[19:2]};
    endfunction

    // A partial product covers two coefficient bits: the higher selects
    // x, the lower selects x/2 (both terms truncate; the sum wraps at 20 bits).
    function automatic value_t partial(input value_t x, input logic high_bit, input logic low_bit);
        value_t direct, half, x_half;
        // A separate statement keeps >>> signed; inside ?: with an
        // unsigned '0 it would become a logical shift.
        x_half = x >>> 1;
        direct = high_bit ? x : '0;
        half = low_bit ? x_half : '0;
        return direct + half;
    endfunction

    // ACC +/- p, saturating. Overflow is detected from the adder's two top
    // bits and clamps toward the addend's sign. (Subtraction is the
    // one's complement plus a carry, as the adder does it.)
    function automatic value_t accumulate(input value_t acc, input value_t p, input logic negative);
        logic [19:0] addend, sum;
        addend = negative ? ~p : p;
        sum = acc + addend + {19'b0, negative};
        if (sum[19] != sum[18]) return addend[19] ? ACC_MIN : ACC_MAX;
        return sum;
    endfunction

    // One row's product: x * c/32 in three partial products, most
    // significant first, each added (and saturated) on its own clock.
    //   c5*x + c4*x/2,   then c3*x/4 + c2*x/8,   then c1*x/16 + c0*x/32
    function automatic value_t multiply_accumulate(input value_t acc, input value_t x,
                                                   input logic [5:0] c, input logic negative);
        value_t x4, x16;
        x4 = shift_two(x);
        x16 = shift_two(x4);
        acc = accumulate(acc, partial(x, c[5], c[4]), negative);
        acc = accumulate(acc, partial(x4, c[3], c[2]), negative);
        acc = accumulate(acc, partial(x16, c[1], c[0]), negative);
        return acc;
    endfunction

    // ------------------------------------------------------------------
    // THE FLOATING-POINT CONVERTER (FPC), one clock per row
    //
    // Input side: a timing ROM scans the two ADC channels in 50-row
    // halves. It loads a 12-bit ADC code and shifts it left by the
    // channel's gain range, so RD_AD/ sees a 16-bit sample.
    // Output side: WR_DA/ fills a waiting latch; when the previous
    // conversion is done the word moves in, is normalized (mantissa +
    // gain), and after 9 rows the DAC and the selected holds see it.
    // ------------------------------------------------------------------
    typedef struct packed {
        // input converter
        logic [7:0]  count;          // position in the ROM scan; RESET restarts it
        logic [3:0]  controls;       // {CNVCLK, CH1, STBGN, previous STBGN}
        logic [3:0]  input_gain;     // gain counter; bit 2 enables shifting
        logic [15:0] input_sample;   // what RD_AD/ puts on the bus
        // output converter
        logic [15:0] waiting;        // WR_DA/ latch
        logic [3:0]  waiting_channels;
        logic        new_data;       // NEW DAT/ asserted: a word is waiting
        logic [15:0] converting;     // being normalized for the DAC
        logic [3:0]  channels;
        logic [3:0]  output_gain;    // 12..15; the DAC's gain pins are bits 1:0
        logic [4:0]  age;            // rows since this conversion began
        logic        busy;
    } converter_t;

    localparam logic [4:0] OUTPUT_ROW = 9, DONE_ROW = 22, RELEASE_ROW = 23;

    localparam converter_t CONVERTER_AT_FIRST_ROW = '{
        count: 8'd1, controls: 4'd0, input_gain: 4'd0, input_sample: 16'd0,
        waiting: 16'd0, waiting_channels: 4'd0, new_data: 1'b0,
        converting: 16'd0, channels: 4'd0, output_gain: 4'd12, age: 5'd0, busy: 1'b1};

    // The FPC timing ROM (U6): {parallel load, conversion clock, CH1, gain strobe}.
    function automatic logic [3:0] timing_rom(input logic [7:0] address);
        int position, half_position;
        logic gain_strobe, channel_one, conversion_clock, parallel_load;
        if (address >= 100) return 4'b0001;
        position = (int'(address) + 1) % 100;
        half_position = position % 50;
        gain_strobe = (half_position + 12) % 50 < 13;
        channel_one = position >= 4 && position < 54;
        conversion_clock = half_position >= 5 && half_position <= 41 && (half_position - 5) % 3 == 0;
        parallel_load = int'(address) % 50 >= 38 && int'(address) % 50 < 40;
        return {parallel_load, conversion_clock, channel_one, gain_strobe};
    endfunction

    // The input LS194s: hold, nibble-wise right shift (ones in), left shift, or load.
    function automatic logic [15:0] move_input(input logic [15:0] s, input logic [11:0] adc,
                                               input logic [1:0] mode);
        case (mode)
            2'd0: return s;
            2'd1: return {1'b1, s[15:13], 1'b1, s[11:9], 1'b1, s[7:5], 1'b1, s[3:1]};
            2'd2: return {s[14:0], 1'b0};
            default: return {{4{adc[11]}}, adc};
        endcase
    endfunction

    // Output pins OUTA..OUTD: the selected holds are open from row 9 to row 22.
    function automatic logic [3:0] output_selects(input converter_t c);
        return (c.age >= OUTPUT_ROW && c.age < RELEASE_ROW) ? c.channels : 4'b0;
    endfunction

    // DAC data: offset binary, top 12 bits of the normalized mantissa.
    function automatic logic [11:0] dac_code(input converter_t c);
        return {~c.converting[15], c.converting[14:4]};
    endfunction

    function automatic converter_t converter_clock(
        input converter_t c,
        input logic [11:0] adc_left, adc_right, input logic [1:0] gain_left, gain_right,
        input logic restart,                    // previous row was RESET
        input logic write_dac, input logic [3:0] write_channels, input logic [15:0] bus
    );
        converter_t n;
        logic [3:0] rom;
        logic [11:0] adc;
        logic [1:0] adc_gain;
        n = c;

        // Input: the ADC mux follows CH1 as it was before this clock; the load
        // while CH1 is high takes channel 2, the right input (Fig. 3.5).
        rom = timing_rom(c.count);
        adc = c.controls[2] ? adc_right : adc_left;
        adc_gain = c.controls[2] ? gain_right : gain_left;
        n.count = restart ? 8'd0 : (c.count == 8'd255 ? c.count : c.count + 8'd1);
        n.input_sample = move_input(c.input_sample, adc, {c.input_gain[2], rom[3]});
        if (rom[3]) n.input_gain = {2'b11, adc_gain};
        else if (c.input_gain[2]) n.input_gain = c.input_gain + 4'd1;
        n.controls = {rom[2:0], c.controls[1]};

        // Output: start a waiting word, or continue normalizing the current one.
        if (!c.busy && c.new_data) begin
            n.converting = c.waiting;
            n.channels = c.waiting_channels;
            n.output_gain = 4'd12;
            n.age = 5'd0;
            n.busy = 1'b1;
            n.new_data = 1'b0;
        end else if (c.busy) begin
            n.age = c.age + 5'd1;
            if (c.output_gain < 4'd15 && c.converting[15] == c.converting[14]) begin
                n.converting = {c.converting[14:0], 1'b1};
                n.output_gain = c.output_gain + 4'd1;
            end
            if (n.age == DONE_ROW) n.busy = 1'b0;
        end else if (c.age == DONE_ROW) n.age = c.age + 5'd1;

        // WR_DA/ refills the waiting latch after the old word was taken.
        if (write_dac) begin
            n.waiting = bus;
            n.waiting_channels = write_channels;
            n.new_data = 1'b1;
        end
        return n;
    endfunction

endpackage
