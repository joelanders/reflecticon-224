// Static-WCS run: load a 512-byte CPU-order WCS image, run N rows, and
// write the DAC captures in the reference machines' event format.
//
// Stimulus matches the reference "static" mode: prepared-zero memory, and
// left ADC code +512 on rows 4096..6143.
module static_run;
    timeunit 1ns;
    timeprecision 1ns;

    // Reference time base, in 1/576 ns ticks, so traces compare line for line.
    localparam longint FIRST_ROW = 207531, ROW = 168750, DAC_OBSERVED = 178512;

    logic row_clock = 0;
    logic [11:0] adc_left = 0, adc_right = 0;
    logic dac_capture;
    logic [3:0] dac_channels, dac_gain;
    logic [11:0] dac_code_out;
    logic [15:0] xreg_to_cpu;

    lexicon224x_dsp dsp(
        .row_clock, .run(1'b1), .adc_left, .adc_right, .gain_left(2'd0), .gain_right(2'd0),
        .xreg_from_cpu(16'h0000),
        .dac_capture, .dac_channels, .dac_code_out, .dac_gain, .xreg_to_cpu);

    initial begin
        string image, events;
        longint rows;
        int fd, out;
        logic [7:0] bytes [512];
        logic [31:0] word;

        if (!$value$plusargs("image=%s", image)) $fatal(1, "+image=FILE required");
        if (!$value$plusargs("events=%s", events)) $fatal(1, "+events=FILE required");
        if (!$value$plusargs("rows=%d", rows)) rows = 4194304;

        fd = $fopen(image, "rb");
        if (fd == 0) $fatal(1, "cannot open %s", image);
        foreach (bytes[b]) bytes[b] = 8'($fgetc(fd));
        $fclose(fd);
        // CPU byte order: row a sits at offset (a ^ 127) * 4; the bus inverts each byte.
        for (int a = 0; a < 128; a++) begin
            word = 0;
            for (int lane = 0; lane < 4; lane++)
                word |= 32'(bytes[(a ^ 127) * 4 + lane] ^ 8'hff) << (8 * lane);
            dsp.load_word(a, word);
        end

        out = $fopen(events, "w");
        for (longint row = 0; row < rows; row++) begin
            // The pin read while CH1 is high: the right input (the references call it left).
            adc_right = (row >= 4096 && row < 6144) ? 12'd512 : 12'd0;
            #1 row_clock = 1;
            #1 row_clock = 0;
            // A capture is observed 60 ns after the converter clock, which is
            // in the next row: the reference run ends before the last one.
            if (dac_capture && row + 1 < rows)
                $fwrite(out, "A %0d %0d 15 0 %0d 4095 0 %0d 15 0\n",
                        FIRST_ROW + row * ROW + DAC_OBSERVED,
                        dac_channels, dac_code_out, dac_gain);
        end
        $fclose(out);
        $finish;
    end
endmodule
