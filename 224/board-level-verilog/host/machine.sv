// Host adapter, not another hardware board. Its tasks submit 8080 byte
// transactions to SBC and inspect state; the board modules own execution.
module machine_host #(parameter bit maximum_delays = 0, parameter bit cpu_connected = 1) (
    input logic [11:0] adc_left, adc_right,
    input logic [1:0] gain_left, gain_right,
    input logic [4:0] levels_left_n, levels_right_n,
    output logic cpu_present,
    output logic transfer_complete,
    output logic [31:0] transfer_waits,
    output logic [7:0] read_value, read_known, read_high_z,
    output logic [63:0] audio_count, audio_time, wcs_writes, wcs_commit_time, reset_edges,
    output logic [63:0] audio_source_time,
    output logic [5:0] audio_source_drivers,
    output logic [15:0] audio_source_overlap,
    output logic [15:0] audio_value[3], audio_known[3], audio_high_z[3], held_value[4], held_known[4],
    output logic [6:0] instruction_address,
    output logic [31:0] instruction,
    output logic [5:0] dab_drivers,
    output logic [15:0] dab_value, dab_known, dab_high_z, dab_overlap
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    bus_word AD, IGA, LA_N, DA, OGA;
    logic CH1, STBGN, CNVCLK, STCNV_N;
    logic [3:0] OUT;
    assign AD = '{value: {4'b0, (CH1 ? adc_left : adc_right)}, known: 16'h0fff, high_z: 0};
    assign IGA = '{value: {14'b0, (CH1 ? gain_left : gain_right)}, known: 16'h0003, high_z: 0};
    assign LA_N = '{value: {11'b0, (CH1 ? levels_left_n : levels_right_n)}, known: 16'h001f, high_z: 0};
    BACKPLANE #(.maximum_delays(maximum_delays), .cpu_connected(cpu_connected)) boards(
        .HALT_N(1'b1), .AD, .IGA, .LA_N, .DA, .OGA, .OUT,
        .CH1, .STBGN, .CNVCLK, .STCNV_N);

    assign cpu_present = cpu_connected;
    assign transfer_complete = boards.sbc.completed;
    assign transfer_waits = boards.sbc.waits;
    assign read_value = boards.sbc.read_byte.value[7:0];
    assign read_known = boards.sbc.read_byte.known[7:0];
    assign read_high_z = boards.sbc.read_byte.high_z[7:0];
    assign audio_count = boards.aout.capture_count;
    assign audio_time = boards.aout.capture_time;
    assign audio_source_time = boards.fpc.conversion_capture_time;
    assign audio_source_drivers = boards.fpc.conversion_drivers;
    assign audio_source_overlap = boards.fpc.conversion_overlap;
    assign wcs_writes = boards.tc.wcs_write_count;
    assign wcs_commit_time = boards.tc.last_wcs_commit;
    assign reset_edges = boards.dmem.reset_edges;
    assign instruction_address = boards.tc.PC;
    assign instruction = boards.tc.executing_instruction;
    assign dab_drivers = boards.DAB.drivers;
    assign dab_value = boards.DAB.sample.value;
    assign dab_known = boards.DAB.sample.known;
    assign dab_high_z = boards.DAB.sample.high_z;
    assign dab_overlap = boards.DAB.overlap;
    bus_word audio_words[3];
    assign audio_words = '{boards.aout.captured_select, boards.aout.captured_dac, boards.aout.captured_gain};
    for (genvar index = 0; index < 3; index++) begin
        assign audio_value[index] = audio_words[index].value;
        assign audio_known[index] = audio_words[index].known;
        assign audio_high_z[index] = audio_words[index].high_z;
    end
    for (genvar channel = 0; channel < 4; channel++) begin
        assign held_value[channel] = boards.aout.held_sample[channel].value;
        assign held_known[channel] = boards.aout.held_sample[channel].known;
    end

    task begin_transfer(input int kind, input logic [15:0] address, input logic [7:0] value); /* verilator public */
        boards.sbc.begin_transfer(kind, address, value);
    endtask
    function logic [7:0] peek_wcs(input logic [15:0] address); /* verilator public */
        logic [6:0] physical_row;
        physical_row = ~address[8:2];
        return ~boards.tc.wcs[physical_row][8 * address[1:0] +: 8];
    endfunction
    task load_word(input int address, input logic [31:0] word_value); /* verilator public */
        boards.tc.load_word(address, word_value);
    endtask
endmodule
