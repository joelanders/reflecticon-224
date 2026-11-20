// Random multiply-accumulate chains through the SystemVerilog machine
// (dsp.sv), for tests/arith_exhaustive.py. Same chain file as
// tests/arith_exhaustive.cpp: uint32 programs, then per program a 512-byte
// CPU-order WCS image and 128 little-endian XREG input words, one per row.
// The programs run back to back, 128 rows each (no RESET: the program
// counter wraps onto the next image).
//
// After each row: RR, ACC (this row's product included), the XREG output and
// the four registers, folded into one checksum per program:
//     w1 = RR | (ACC & 0xFFFFF) << 16 | XREG out << 40,  w2 = R0 | R1 << 16 | R2 << 32 | R3 << 48
//     sum += w1 * weight(2r) + w2 * weight(2r + 1)
//
//   +chain=FILE [+dump=PROGRAM]
module arith_chain;
    timeunit 1ns;
    timeprecision 1ns;
    import lexicon224x::*;

    logic row_clock = 0;
    logic [15:0] xreg_from_cpu = 0;
    logic dac_capture;
    logic [3:0] dac_channels, dac_gain;
    logic [11:0] dac_code_out;
    logic [15:0] xreg_to_cpu;

    lexicon224x_dsp dsp(
        .row_clock, .run(1'b1), .adc_left(12'd0), .adc_right(12'd0), .gain_left(2'd0), .gain_right(2'd0),
        .xreg_from_cpu, .dac_capture, .dac_channels, .dac_code_out, .dac_gain, .xreg_to_cpu);

    function automatic longint unsigned weight(longint unsigned i);
        return (i * 64'h9E3779B97F4A7C15 + 64'h632BE59BD9B4E019) | 64'd1;
    endfunction

    initial begin
        string file;
        int fd, dump;
        logic [31:0] programs, word;
        logic [7:0] block [768];

        if (!$value$plusargs("chain=%s", file)) $fatal(1, "+chain=FILE required");
        if (!$value$plusargs("dump=%d", dump)) dump = -1;
        fd = $fopen(file, "rb");
        if (fd == 0) $fatal(1, "cannot open %s", file);
        programs = 0;
        for (int b = 0; b < 4; b++) programs |= 32'($fgetc(fd)) << (8 * b);

        for (int p = 0; p < int'(programs); p++) begin
            longint unsigned sum;
            foreach (block[b]) block[b] = 8'($fgetc(fd));
            for (int a = 0; a < 128; a++) begin
                word = 0;
                for (int lane = 0; lane < 4; lane++)
                    word |= 32'(block[(a ^ 127) * 4 + lane] ^ 8'hff) << (8 * lane);
                dsp.load_word(a, word);
            end
            sum = 0;
            for (int r = 0; r < 128; r++) begin
                longint unsigned w1, w2;
                xreg_from_cpu = {block[513 + 2 * r], block[512 + 2 * r]};
                #1 row_clock = 1;
                #1 row_clock = 0;
                w1 = {8'd0, xreg_to_cpu, 4'd0, 20'(dsp.ACC), dsp.RR};
                w2 = {dsp.R[3], dsp.R[2], dsp.R[1], dsp.R[0]};
                if (p == dump) $display("%0d %016h %016h", r, w1, w2);
                sum += w1 * weight(longint'(2 * r)) + w2 * weight(longint'(2 * r + 1));
            end
            if (dump < 0) $display("%0d %016h", p, sum);
        end
        $fclose(fd);
        $finish;
    end
endmodule
