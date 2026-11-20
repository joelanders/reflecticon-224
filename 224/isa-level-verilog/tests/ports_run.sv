// Port trace for the gate-level comparison (tests/gate_level.py).
//
// Runs a WCS image and writes the same per-board records as the
// gate-level bench (tests/gate_bench.py), so its
// gate-level six-board stitch can be compared with the row machine:
//   fetch    address and word, per row
//   regs     the four registers (74LS670 word order) and RR, at each ARUCK
//   mem      each DRAM access: bank, write, chip address {row, column}, data
//   xreg     XREG output-register loads
//   adc      the converter's input word after each clock
//   pending  the DAC waiting latch after each WR_DA
//   dac      each DAC capture: code, channels, gain
//
// The row machine executes whole instructions; this testbench places each
// effect in the hardware row where the boards show it. For instruction k
// (fetched in hardware row k), the register write, XFER and memory access
// appear in row k+1. That placement, and the "undriven" marking of a
// register written by a NOP row, are trace bookkeeping here, not model state.
module ports_run;
    timeunit 1ns;
    timeprecision 1ps;
    import lexicon224x::*;

    localparam real ROW = 292.96875, MASTER = ROW / 9.0;
    localparam real FIRST = 490.297;  // Go's first row marker, for readable diffs

    logic row_clock = 0;
    logic [11:0] adc_left = 0, adc_right = 0;
    logic [1:0] gain = 0;
    logic dac_capture;
    logic [3:0] dac_channels, dac_gain;
    logic [11:0] dac_code_out;
    logic [15:0] xreg_to_cpu;

    // The netlist's traced XREG banks never drive the bus (a known trace
    // defect; see dsp.sv), so compare with that variant.
    logic run = 0;
    lexicon224x_dsp dsp(
        .row_clock, .run, .adc_left, .adc_right, .gain_left(gain), .gain_right(gain),
        .xreg_from_cpu(16'h0000),
        .dac_capture, .dac_channels, .dac_code_out, .dac_gain, .xreg_to_cpu);

    // The HDL bench's converter input: a hash of the program pass (advanced
    // at each fetch of address 0), with the CH1 pin XORed by 0x555 (the bench
    // calls it left; it is the right input, channel 2).
    function automatic logic [11:0] adc_hash(input int pass);
        logic [31:0] x;
        x = 32'(pass) * 32'h9e3779b1 + 32'h7f4a7c15;
        x ^= x >> 15;
        return x[11:0];
    endfunction

    // 74LS670 word for a WA/RA field: U54 inverts, the address pins cross.
    function automatic int chip_word(input logic [1:0] field);
        logic [1:0] inverted;
        inverted = ~field;
        return int'({inverted[0], inverted[1]});
    endfunction

    function automatic string bits16(input logic [15:0] v, input bit undriven);
        return undriven ? "zzzzzzzzzzzzzzzz" : $sformatf("%016b", v);
    endfunction

    int out;
    logic [15:0] R_before [4], RR_before;
    bit undriven [4], undriven_before [4], undriven_bus, netlist_xreg_race;

    task automatic regs_line(input real at, input bit after_write);
        string line;
        line = $sformatf("%.3f,regs", at);
        for (int word = 0; word < 4; word++)
            for (int field = 0; field < 4; field++)
                if (chip_word(2'(field)) == word)
                    line = {line, ",", bits16(after_write ? dsp.R[field] : R_before[field],
                                              after_write ? undriven[field] : undriven_before[field])};
        $fwrite(out, "%s,%016b\n", line, RR_before);
    endtask

    initial begin
        string image, trace;
        int rows, fd, pass;
        logic [7:0] bytes [512];
        logic [31:0] word;
        microinstruction_t mi;
        oper_t o;
        logic [15:0] position, sum;
        real frame, next;

        if (!$value$plusargs("image=%s", image)) $fatal(1, "+image=FILE required");
        if (!$value$plusargs("trace=%s", trace)) $fatal(1, "+trace=FILE required");
        if (!$value$plusargs("rows=%d", rows)) rows = 1500;
        netlist_xreg_race = $test$plusargs("netlist_xreg_race");
        fd = $fopen(image, "rb");
        if (fd == 0) $fatal(1, "cannot open %s", image);
        foreach (bytes[b]) bytes[b] = 8'($fgetc(fd));
        $fclose(fd);
        for (int a = 0; a < 128; a++) begin
            word = 0;
            for (int lane = 0; lane < 4; lane++)
                word |= 32'(bytes[(a ^ 127) * 4 + lane] ^ 8'hff) << (8 * lane);
            dsp.load_word(a, word);
        end
        // The HDL bench pulses CPC CLR before RUN: the counter starts at 0.
        dsp.cpc = 0;
        // Before the first row the instruction register holds an all-zero
        // word, a NOP with WA = 0: its row leaves R[0] written from an
        // undriven bus.
        foreach (undriven[n]) undriven[n] = n == 0;

        out = $fopen(trace, "w");
        $fwrite(out, "time_ns,kind,a,b,c,d,e\n");
        $fwrite(out, "0.000,xreg,xxxxxxxxxxxxxxxx,,,,\n");
        $fwrite(out, "0.000,adc,%016b,%08b,,,\n", dsp.fpc.input_sample, dsp.fpc.count);
        $fwrite(out, "0.000,pending,%016b,%04b,,,\n", dsp.fpc.waiting, dsp.fpc.waiting_channels);
        pass = -1;
        // The bench powers up with RUN low: the T&C executes three rows with
        // the program counter held at 0 before RUN rises (and before its
        // first recorded fetch).
        for (int row = 0; row < rows + 3; row++) begin
            run = row >= 3;
            frame = FIRST + row * ROW;
            next = frame + ROW;
            mi = dsp.wcs[dsp.pc];
            o = mi.low;
            if (run && dsp.pc == 0) pass++;
            adc_left = adc_hash(pass);
            adc_right = adc_hash(pass) ^ 12'h555;
            gain = 2'(pass);
            if (run) $fwrite(out, "%.3f,fetch,%0d,%08h,,,\n", frame + 102.156, dsp.pc, word_of(mi));
            // NOP rows and OPER source 0 drive nothing (the netlist shows z).
            undriven_bus = mi.op == NOP || (mi.op == OPER && o.source == SELECTS_NOTHING);
            R_before = dsp.R;
            undriven_before = undriven;
            RR_before = dsp.RR;
            position = dsp.advance_cpc ? dsp.cpc + 16'd1 : dsp.cpc;
            sum = position + mi.low + 16'd1;

            #1 row_clock = 1;
            #1 row_clock = 0;

            undriven[mi.wa] = undriven_bus;
            // Row k+1 in hardware terms: register write, then two more ARUCKs.
            regs_line(next, 0);
            regs_line(next + 3 * MASTER, 1);
            regs_line(next + 6 * MASTER, 1);
            if (mi.op == MEMW || mi.op == MEMR)
                $fwrite(out, "%.3f,mem,0,%0d,%016b,%016b,%016b/%016b\n", next + 10.0,
                        mi.op == MEMW, {sum[7:0], sum[15:8]}, dsp.memory[sum], position, mi.low);
            if (mi.op == OPER && o.wr_xreg)
                // Known race (see notes): with RR as the source, the netlist's
                // RR driver releases before the XREG capture edge, so the
                // gate-level XREG saves an undriven bus. The reference
                // machines (2 ns margin) and the row machine save RR.
                $fwrite(out, "%.3f,xreg,%s,,,,\n", next + 101.0, bits16(dsp.xreg_to_cpu,
                        undriven_bus || (netlist_xreg_race && o.source == FROM_RR)));
            $fwrite(out, "%.3f,adc,%016b,%08b,,,\n", frame + 270.0, dsp.fpc.input_sample, dsp.fpc.count);
            if (mi.op == OPER && o.wr_da)
                $fwrite(out, "%.3f,pending,%s,%04b,,,\n", frame + 270.0,
                        bits16(dsp.fpc.waiting, undriven_bus), dsp.fpc.waiting_channels);
            if (dac_capture)
                $fwrite(out, "%.3f,dac,%012b,%04b,%04b,,\n", frame + 310.0,
                        dac_code_out, dac_channels, dac_gain);
        end
        $fclose(out);
        $finish;
    end

    function automatic logic [31:0] word_of(input microinstruction_t mi);
        return mi;
    endfunction
endmodule
