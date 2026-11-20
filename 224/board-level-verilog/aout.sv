// Digital boundary of the AUDIO OUTPUT board. OUTA..OUTD enable four sample
// holds. DA is offset-binary DAC data; OGA selects its analog gain range.
// Reconstructing signed samples is an observation of those pins, not a model
// of the DAC's analog settling, output filters or amplifiers.
module AOUT (
    input backplane_signals::bus_word DA, OGA,
    input logic [3:0] OUT
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    localparam time nanosecond = 576;
    bus_word held_sample [4];
    bus_word captured_select, captured_dac, captured_gain;
    time capture_time;
    longint unsigned capture_count;
    logic [3:0] previous_outputs;

    int unsigned initialization_generation /* verilator public_flat_rw */ = 0;
    int unsigned storage_generation = '1;
    task initialize();
        initialization_generation++;
    endtask
    always @(initialization_generation) begin
        if (storage_generation != initialization_generation) begin
            storage_generation = initialization_generation;
            foreach (held_sample[channel]) held_sample[channel] = '0;
            captured_select = '0;
            captured_dac = '0;
            captured_gain = '0;
            capture_time = 0;
            capture_count = 0;
            previous_outputs = 0;
        end
    end

    task automatic capture_outputs(input logic [3:0] selected_outputs, input int generation);
        logic signed [15:0] signed_mantissa;
        bus_word sample_word;
        // FPC controls publish 12 ns after their clock; the established audio
        // observation is 60 ns after that clock. DA settles in the meantime.
        #(48 * nanosecond);
        if (generation == initialization_generation) begin
            sample_word = '0;
            if (DA.known[11:0] == 12'hfff && DA.high_z[11:0] == 0 &&
                OGA.known[1:0] == 2'b11 && OGA.high_z[1:0] == 0) begin
                signed_mantissa = {{4{~DA.value[11]}}, ~DA.value[11], DA.value[10:0]};
                sample_word = known_word(signed_mantissa <<< (4 - int'(OGA.value[1:0])));
            end
            foreach (held_sample[channel])
                if (selected_outputs[channel]) held_sample[channel] = sample_word;
            captured_select = '{value: {12'b0, selected_outputs}, known: 16'h000f, high_z: 0};
            captured_dac = DA;
            captured_gain = OGA;
            capture_time = $time;
            capture_count++;
        end
    endtask

    // Copy the edge's channel mask before the detached process begins.
    // Otherwise the process could observe previous_outputs after its update.
    task automatic schedule_capture(input logic [3:0] selected_outputs, input int generation);
        fork
            capture_outputs(selected_outputs, generation);
        join_none
    endtask

    always @(OUT) begin : sample_switches
        if (|(OUT & ~previous_outputs))
            schedule_capture(OUT & ~previous_outputs, initialization_generation);
        previous_outputs = OUT;
    end
    initial initialize();
endmodule
