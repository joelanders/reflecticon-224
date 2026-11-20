// 060-01320, FLOATING POINT CONVERTER.
// Sheet 1: ADC encoding, output normalization, gain and sample strobes.
// Sheet 2: input headroom registers and CPU readback.
module FPC (
    input logic FPC_CK, RESET_N, RD_AD_N, WR_DA_N,
    input logic SDAA, SDAB, SDAC, SDAD,
    input backplane_signals::bus_word AD, IGA, LA_N,
    input logic HR1_N, HR2_N,
    cpu_bus.fpc DAT_N,
    output logic CH1, STBGN, CNVCLK, STCNV_N,
    output backplane_signals::bus_word DA, OGA,
    output logic [3:0] OUT,
    dab_bus.fpc DAB
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    localparam time nanosecond = 576;

    // SHEET 1 — INPUT CONVERTER.
    // The timing ROM scans two channels in 50-clock intervals. Its published
    // pins trail the counter; the gain counter decides how the LS194s shift.
    logic [7:0] input_count;
    logic [3:0] input_controls;
    bus_word input_gain, input_sample;

    // U18 (74LS175): STBGN is Q1; STCNV/ is /Q0, STBGN one clock later.
    assign CH1 = input_controls[2];
    assign STBGN = input_controls[1];
    assign CNVCLK = input_controls[3];
    assign STCNV_N = ~input_controls[0];
    assign DAB.fpc_drive = RD_AD_N ? released_bus : input_sample;

    function automatic logic [3:0] timing_rom(input logic [7:0] address);
        int position, half_position;
        logic gain_strobe, channel_one, conversion_clock, parallel_load;
        if (address >= 100) return 4'b0001;
        position = (int'(address) + 1) % 100;
        half_position = position % 50;
        gain_strobe = (half_position + 12) % 50 < 13;
        channel_one = position >= 4 && position < 54;
        conversion_clock = half_position >= 5 && half_position <= 41 && (half_position - 5) % 3 == 0;
        parallel_load = address % 50 >= 38 && address % 50 < 40;
        return {parallel_load, conversion_clock, channel_one, gain_strobe};
    endfunction

    function automatic bus_word shift_input(
        input bus_word sample_word, adc_word, input logic [1:0] movement
    );
        bus_word register_inputs;
        if (movement == 0) return sample_word;
        case (movement)
            // Right shifts are four separate nibbles; each serial wire is high.
            1: register_inputs = '{
                value: {1'b1, sample_word.value[15:13], 1'b1, sample_word.value[11:9],
                        1'b1, sample_word.value[7:5], 1'b1, sample_word.value[3:1]},
                known: {1'b1, sample_word.known[15:13], 1'b1, sample_word.known[11:9],
                        1'b1, sample_word.known[7:5], 1'b1, sample_word.known[3:1]},
                high_z: {1'b0, sample_word.high_z[15:13], 1'b0, sample_word.high_z[11:9],
                         1'b0, sample_word.high_z[7:5], 1'b0, sample_word.high_z[3:1]}};
            2: register_inputs = '{value: {sample_word.value[14:0], 1'b0},
                known: {sample_word.known[14:0], 1'b1}, high_z: {sample_word.high_z[14:0], 1'b0}};
            3: register_inputs = '{value: {{4{adc_word.value[11]}}, adc_word.value[11:0]},
                known: {{4{adc_word.known[11]}}, adc_word.known[11:0]},
                high_z: {{4{adc_word.high_z[11]}}, adc_word.high_z[11:0]}};
            default: register_inputs = '0;
        endcase
        // The inherited LS194 input policy resolves a disconnected wire low.
        register_inputs.value &= register_inputs.known & ~register_inputs.high_z;
        register_inputs.known |= register_inputs.high_z;
        register_inputs.high_z = 0;
        return register_inputs;
    endfunction

    // OUTPUT CONVERTER — INPUT LATCH, NORMALIZER, AND GAIN COUNTER.
    // WR DA/ fills one latch while another sample is being converted. BUSY
    // prevents replacing the conversion. Consuming and writing on the same
    // clock leaves NEW DAT/ asserted for the newly written sample.
    bus_word waiting_sample, waiting_select, conversion_sample;
    logic [3:0] conversion_select, conversion_gain;
    logic [4:0] conversion_age;
    logic BUSY, NEW_DAT_N; // Printed labels: BUSY, NEW DAT/.
    // Observation metadata follows the same two latches. It identifies the
    // DAB owner at WR_DA, rather than the unrelated bus owner at DAC capture.
    time waiting_capture_time, conversion_capture_time;
    logic [5:0] waiting_drivers, conversion_drivers;
    logic [15:0] waiting_overlap, conversion_overlap;

    localparam logic [4:0] sample_clock = 9, completion_clock = 22, release_clock = 23;
    assign OUT = conversion_age >= sample_clock && conversion_age < release_clock ? conversion_select : 0;
    assign DA = '{
        value: {4'b0, (~conversion_sample.value[15] & conversion_sample.known[15]), conversion_sample.value[14:4]},
        known: {4'b0, conversion_sample.known[15:4]}, high_z: {4'b0, conversion_sample.high_z[15:4]}};
    assign OGA = '{value: {14'b0, conversion_gain[1:0]}, known: 16'h0003, high_z: 0};

    // SHEET 2 — HEADROOM REGISTERS.
    // A low level-detector pin records a peak until a CPU read releases the
    // register. HR1/ and HR2/ also start its delayed initialization interval.
    bus_word headroom_peak[2];
    bit headroom_initializing[2];
    int unsigned headroom_read_version[2];

    function automatic bus_word headroom_byte(input bus_word peak_word);
        return '{value: {8'b0, 3'b111, peak_word.value[0], peak_word.value[1],
                    peak_word.value[2], peak_word.value[3], peak_word.value[4]},
                 known: {8'b0, 3'b111, peak_word.known[0], peak_word.known[1],
                    peak_word.known[2], peak_word.known[3], peak_word.known[4]}, high_z: 0};
    endfunction
    always_comb begin
        DAT_N.fpc_drive = released_byte;
        if (!HR1_N) DAT_N.fpc_drive = headroom_byte(headroom_peak[0]);
        else if (!HR2_N) DAT_N.fpc_drive = headroom_byte(headroom_peak[1]);
    end

    task automatic store_headroom(input int channel, input bus_word sample_word, input int generation);
        #(20 * nanosecond);
        if (generation == initialization_generation) headroom_peak[channel] = sample_word;
    endtask
    task automatic sample_headroom(input int channel, input bus_word detector_pins, input int generation);
        bus_word sample_word;
        logic_level detector_pin;
        sample_word = headroom_peak[channel];
        for (int index = 0; index < 5; index++) begin
            detector_pin = word_bit(detector_pins, index);
            if (headroom_initializing[channel]) begin
                sample_word.value[index] = 1;
                sample_word.known[index] = 1;
            end else if (detector_pin != high) begin
                sample_word.value[index] = 0;
                sample_word.known[index] = detector_pin == low;
            end
        end
        fork
            store_headroom(channel, sample_word, generation);
        join_none
    endtask

    task automatic release_headroom_initialization(input int channel, version, generation);
        #(1100 * nanosecond);
        if (generation == initialization_generation && version == headroom_read_version[channel])
            headroom_initializing[channel] = 0;
    endtask
    task automatic initialize_headroom(input int channel, version, generation);
        #(35 * nanosecond);
        if (generation == initialization_generation && version == headroom_read_version[channel]) begin
            if (!headroom_initializing[channel]) begin
                headroom_initializing[channel] = 1;
                fork
                    store_headroom(channel, '{value: 16'h001f, known: 16'h001f, high_z: 0}, generation);
                join_none
            end
            fork
                release_headroom_initialization(channel, version, generation);
            join_none
        end
    endtask
    task automatic headroom_read_released(input int channel, generation);
        headroom_read_version[channel]++;
        fork
            initialize_headroom(channel, headroom_read_version[channel], generation);
        join_none
    endtask
    always @(posedge HR1_N) headroom_read_released(0, initialization_generation);
    always @(posedge HR2_N) headroom_read_released(1, initialization_generation);

    int unsigned initialization_generation /* verilator public_flat_rw */ = 0;
    int unsigned storage_generation = '1;
    task initialize();
        initialization_generation++;
    endtask

    always @(initialization_generation) begin : initialize_storage
        if (storage_generation != initialization_generation) begin
            storage_generation = initialization_generation;
            // Match the existing machine's first-marker startup witness:
            // one input clock has occurred and an empty output conversion began.
            input_count = 1;
            input_controls = 0;
            input_gain = '{value: 0, known: 16'h000f, high_z: 0};
            input_sample = zero_word;
            waiting_sample = zero_word;
            waiting_select = '{value: 0, known: 16'h000f, high_z: 0};
            conversion_sample = zero_word;
            conversion_select = 0;
            conversion_gain = 12; // The counter's unused upper two bits load high.
            conversion_age = 0;
            BUSY = 1;
            NEW_DAT_N = 1;
            waiting_capture_time = 0; conversion_capture_time = 0;
            waiting_drivers = 0; conversion_drivers = 0;
            waiting_overlap = 0; conversion_overlap = 0;
            foreach (headroom_peak[channel]) begin
                headroom_peak[channel] = '{value: 0, known: 16'h001f, high_z: 0};
                headroom_initializing[channel] = 0;
                headroom_read_version[channel] = 0;
            end
        end
    end

    task automatic convert_sample(input int generation);
        logic [3:0] rom_pins, gain_after_clock, select_after_clock;
        logic [7:0] count_after_clock;
        logic [4:0] age_after_clock;
        logic busy_after_clock, new_data_after_clock;
        bus_word input_gain_after_clock, input_after_clock;
        bus_word sample_after_clock, waiting_sample_after_clock, waiting_select_after_clock;
        time write_time_after_clock, conversion_time_after_clock;
        logic [5:0] write_drivers_after_clock, conversion_drivers_after_clock;
        logic [15:0] write_overlap_after_clock, conversion_overlap_after_clock;

        rom_pins = timing_rom(input_count);
        count_after_clock = !RESET_N ? 8'b0 : input_count == 255 ? input_count : input_count + 8'd1;
        input_gain_after_clock = input_gain;
        input_after_clock = input_gain.known[2] && !input_gain.high_z[2]
            ? shift_input(input_sample, AD, {input_gain.value[2], rom_pins[3]}) : bus_word'('0);
        if (rom_pins[3])
            input_gain_after_clock = '{value: {12'b0, 2'b11, IGA.value[1:0]},
                known: {12'b0, 2'b11, IGA.known[1:0]}, high_z: {14'b0, IGA.high_z[1:0]}};
        else if (input_gain.known[2] && !input_gain.high_z[2] && input_gain.value[2])
            input_gain_after_clock = input_gain.known == 15 && input_gain.high_z == 0
                ? '{value: {12'b0, (input_gain.value[3:0] + 4'd1)}, known: 16'h000f, high_z: 0} : bus_word'('0);

        sample_after_clock = conversion_sample;
        waiting_sample_after_clock = waiting_sample;
        waiting_select_after_clock = waiting_select;
        gain_after_clock = conversion_gain;
        select_after_clock = conversion_select;
        age_after_clock = conversion_age;
        busy_after_clock = BUSY;
        new_data_after_clock = !NEW_DAT_N;
        write_time_after_clock = waiting_capture_time;
        conversion_time_after_clock = conversion_capture_time;
        write_drivers_after_clock = waiting_drivers;
        conversion_drivers_after_clock = conversion_drivers;
        write_overlap_after_clock = waiting_overlap;
        conversion_overlap_after_clock = conversion_overlap;

        if (!BUSY && !NEW_DAT_N) begin
            assert (waiting_select.known == 15 && waiting_select.high_z == 0)
                else $fatal(1, "FPC conversion begins with unresolved channel selection");
            sample_after_clock = waiting_sample;
            sample_after_clock.value &= sample_after_clock.known;
            sample_after_clock.known |= sample_after_clock.high_z;
            sample_after_clock.high_z = 0;
            select_after_clock = waiting_select.value[3:0];
            gain_after_clock = 12;
            age_after_clock = 0;
            busy_after_clock = 1;
            new_data_after_clock = 0;
            conversion_time_after_clock = waiting_capture_time;
            conversion_drivers_after_clock = waiting_drivers;
            conversion_overlap_after_clock = waiting_overlap;
        end else if (BUSY) begin
            age_after_clock++;
            if (conversion_gain < 15) begin
                if (conversion_sample.known[15:14] != 2'b11) sample_after_clock = '0;
                else if (conversion_sample.value[15] == conversion_sample.value[14]) begin
                    sample_after_clock = '{value: {conversion_sample.value[14:0], 1'b1},
                        known: {conversion_sample.known[14:0], 1'b1}, high_z: 0};
                    gain_after_clock++;
                end
            end
            if (age_after_clock == completion_clock) busy_after_clock = 0;
        end else if (conversion_age == completion_clock) age_after_clock++;

        // A simultaneous write goes into the waiting latch after the old word
        // has been chosen above. This ordering is the double-buffer circuit.
        if (!WR_DA_N) begin
            waiting_sample_after_clock = DAB.sample;
            waiting_select_after_clock = '{value: {12'b0, SDAD, SDAC, SDAB, SDAA}, known: 16'h000f, high_z: 0};
            new_data_after_clock = 1;
            write_time_after_clock = $time;
            write_drivers_after_clock = DAB.drivers;
            write_overlap_after_clock = DAB.overlap;
        end

        #(12 * nanosecond);
        if (generation == initialization_generation) begin
            input_count = count_after_clock;
            input_gain = input_gain_after_clock;
            waiting_select = waiting_select_after_clock;
            conversion_select = select_after_clock;
            conversion_gain = gain_after_clock;
            conversion_age = age_after_clock;
            BUSY = busy_after_clock;
        end
        #(8 * nanosecond);
        if (generation == initialization_generation) begin
            // Capture the detector pins before CH1 changes the AIN mux.
            if (input_controls[2] != rom_pins[1]) sample_headroom(rom_pins[1] ? 1 : 0, LA_N, generation);
            input_controls = {rom_pins[2:0], input_controls[1]};
            input_sample = input_after_clock;
            conversion_sample = sample_after_clock;
            waiting_sample = waiting_sample_after_clock;
            NEW_DAT_N = !new_data_after_clock;
            waiting_capture_time = write_time_after_clock;
            conversion_capture_time = conversion_time_after_clock;
            waiting_drivers = write_drivers_after_clock;
            conversion_drivers = conversion_drivers_after_clock;
            waiting_overlap = write_overlap_after_clock;
            conversion_overlap = conversion_overlap_after_clock;
        end
    endtask

    always @(posedge FPC_CK) begin
        fork
            convert_sample(initialization_generation);
        join_none
    end
    initial initialize();
endmodule
