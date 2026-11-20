// 060-02475, sheets 1 and 2: TIMING AND CONTROL, MODEL 224X.
//
// Sheet 1 generates clocks/strobes. Sheet 2 retains and decodes WCS words.
// The two sections share the labeled signals below. The running board does
// not accept software row commands: its timing processes run in simulation.
module TC #(
    parameter bit maximum_delays = 0,
    // The result register supplies MEMW rows. Drawn: U48A pin 1 = MEMW/
    // (060-02475 sheet 2; 600-DPI re-read 2026-09-25, both scans), so
    // RDRREG/ = NAND(NAND(MEMW/, 2Y1/), DAB_RSTB). Kept as a parameter only
    // so the old no-store bench can be reproduced.
    parameter bit assume_result_during_memory_write = 1
) (
    input logic HALT_N,
    input backplane_signals::logic_level SAT,
    input logic [15:0] ADR_N,
    input logic PHI2, MRDC_N, MWTC_N, DPORT3_N, DPORT4_N, DPORT5_N,
    cpu_bus.tc DAT_N,
    output logic ARUCK, DAB_WSTB_N, XFER_CK,
    output logic RA0_N, RA1_N, WA0_N, WA1_N,
    output logic S0, S1, M0_N, M1_N, CSIGN_N, ZERO_N,
    output logic RDRREG_N, RD_XREG_N, WR_XREG_N, RD_AD_N,
    output logic [15:0] OFST_N,
    output logic MEMAC, MEMW_N, DAB_RSTB,
    output logic FPC_CK, WR_DA_N, RESET_N,
    output logic SDAA, SDAB, SDAC, SDAD
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    localparam time nanosecond = 576;
    localparam time master_period = 18750;
    localparam time row_period = 9 * master_period;
    localparam time first_marker = 207531;

    // These are the existing 224X timing reductions, in exact 1/576 ns ticks.
    // They come from the earlier reference models of the machine. They are
    // not new measurements of individual chips or the PLL acquisition time.
    localparam time instruction_fetch = maximum_delays ? 59994 : 58842;
    localparam time write_sample = maximum_delays ? 152214 : 151062;
    localparam time converter_clock = 162384 - 32 * nanosecond;
    localparam time decoder_skew = maximum_delays ? 2 * nanosecond : 0;
    localparam time dab_read_fall = 51930 + decoder_skew;
    localparam time dab_read_rise = 89430 + decoder_skew;
    localparam time write_open = master_period + (maximum_delays ? 13 : 11) * nanosecond
        - 37 * nanosecond / 2;
    localparam time write_close = 2 * master_period + (maximum_delays ? 25 : 22) * nanosecond / 2
        - 37 * nanosecond / 2;

    // SHEET 2 — WCS RAM, PROGRAM COUNTER, AND MICROINSTRUCTION REGISTERS.
    // U2/U15/U29/U43 hold four bytes per word. MI0..MI31 are the SRAM data.
    logic [31:0] wcs [128];
    logic [6:0] PC;
    logic [31:0] MI, instruction_register, executing_instruction;
    logic [6:0] instruction_controls;
    logic sign_latch, counter_clear_n;
    logic [3:0] even_coefficient, odd_coefficient;
    logic protected_pair, resetd_n;
    time row_marker;
    longint unsigned rows_started;

    typedef enum logic [1:0] {idle_row, operation_row, memory_write_row, memory_read_row} row_operation;
    function automatic row_operation operation(input logic [31:0] instruction);
        return row_operation'(instruction[17:16]);
    endfunction

    function automatic logic [3:0] channel_select(input logic [31:0] instruction);
        // SDAA, SDAB, SDAC, SDAD connect to WCS bits 11, 10, 9, 8.
        return {instruction[8], instruction[9], instruction[10], instruction[11]};
    endfunction

    function automatic logic [3:0] load_coefficient(
        input logic [3:0] coefficient, input logic [31:0] instruction, input bit odd_bits
    );
        // U10/U11 retain one bit already in flight as three new bits enter.
        if (odd_bits)
            return {coefficient[2], instruction[31], instruction[29], instruction[27]};
        return {coefficient[2], instruction[30], instruction[28], instruction[26]};
    endfunction

    // SHEET 1 — CPU REQUEST, ACKNOWLEDGEMENT, AND WCS BUS OWNERSHIP.
    // A request crosses into the DSP clock domain at an eligible marker.
    // The CPU can finish its byte before the grant's arithmetic holds end.
    // Records retain those electrical tails while another byte is requested.
    localparam time cpu_period = 281250, sbc_oscillator_period = 31250;
    typedef struct packed {
        bit valid, scheduled, reading;
        logic [6:0] address;
        logic [1:0] lane;
        logic [7:0] data_byte;
        time requested_at, cpu_t1, displaced_start, displaced_end;
        time first_held_clock, last_held_clock;
    } wcs_access;
    wcs_access cpu_access[4];
    logic cpu_read_enable;
    logic [1:0] cpu_read_lane;
    assign cpu_read_lane = ~ADR_N[1:0];
    longint unsigned wcs_write_count;
    time last_wcs_commit;

    function automatic time periodic_edge(input time first_edge, period, minimum_time);
        return minimum_time <= first_edge ? first_edge
            : first_edge + ((minimum_time - first_edge + period - 1) / period) * period;
    endfunction

    task request_wcs(input bit reading);
        int slot;
        slot = -1;
        foreach (cpu_access[index])
            if (slot < 0 && (!cpu_access[index].valid ||
                (cpu_access[index].scheduled && cpu_access[index].last_held_clock < $time))) slot = index;
        assert (slot >= 0) else $fatal(1, "T&C WCS request storage exhausted");
        cpu_access[slot] = '0;
        cpu_access[slot].valid = 1;
        cpu_access[slot].reading = reading;
        cpu_access[slot].address = ADR_N[8:2];
        cpu_access[slot].lane = ~ADR_N[1:0];
        cpu_access[slot].data_byte = DAT_N.sample.value[7:0];
        cpu_access[slot].requested_at = $time;
        cpu_access[slot].cpu_t1 = $time - (reading ? cpu_period + 2 * sbc_oscillator_period : 2 * cpu_period);
    endtask
    always @(negedge MRDC_N) if (ADR_N[15:9] == 7'h5f) request_wcs(1);
    always @(negedge MWTC_N) if (ADR_N[15:9] == 7'h5f) request_wcs(0);
    always @(posedge MRDC_N or posedge MWTC_N)
        if (MRDC_N && MWTC_N) DAT_N.tc_ack = disconnected;

    task automatic publish_ack(input time at_time, input int generation);
        assert (at_time > $time);
        #(at_time - $time);
        if (generation == initialization_generation) DAT_N.tc_ack = low;
    endtask
    task automatic commit_cpu_write(input time at_time, input int slot, generation);
        assert (at_time > $time);
        #(at_time - $time);
        if (generation == initialization_generation) begin
            wcs[cpu_access[slot].address][8 * cpu_access[slot].lane +: 8] = cpu_access[slot].data_byte;
            wcs_write_count++;
            last_wcs_commit = $time;
        end
    endtask
    task automatic enable_cpu_read(input time enable_time, release_time, input int generation);
        assert (enable_time > $time && release_time > enable_time);
        #(enable_time - $time);
        if (generation == initialization_generation) cpu_read_enable = 1;
        #(release_time - enable_time);
        if (generation == initialization_generation) cpu_read_enable = 0;
    endtask

    task automatic grant_wcs(input int slot, generation);
        time capture, request_latched, ack_edge, ack_time, ready_edge, sampled_at;
        time select_release, grant_release, lane_enable, lane_release;
        time rise_delay, fall_delay;
        capture = $time + 9 * nanosecond / 2;
        request_latched = capture + 145 * nanosecond / 2;
        ack_edge = periodic_edge(cpu_access[slot].cpu_t1 + 2 * sbc_oscillator_period, cpu_period, request_latched);
        ack_time = ack_edge + 55 * nanosecond;
        ready_edge = cpu_access[slot].cpu_t1 + cpu_period + 3 * sbc_oscillator_period;
        while (ready_edge <= ack_time) ready_edge += cpu_period;
        sampled_at = ready_edge + cpu_period - sbc_oscillator_period;
        cpu_access[slot].scheduled = 1;
        cpu_access[slot].displaced_start = $time + row_period;
        cpu_access[slot].first_held_clock = $time + 2 * row_period + 3 * master_period;

        if (cpu_access[slot].reading) begin
            rise_delay = maximum_delays ? 9 * nanosecond / 2 : 3 * nanosecond;
            fall_delay = maximum_delays ? 5 * nanosecond : 3 * nanosecond;
            select_release = periodic_edge($time + 3 * master_period - 21 * nanosecond / 2 + fall_delay,
                row_period, sampled_at + fall_delay + 40 * nanosecond);
            grant_release = select_release + master_period;
            lane_enable = capture + 5 * master_period + 13 * nanosecond + decoder_skew;
            lane_release = grant_release + 25 * nanosecond;
            cpu_access[slot].displaced_end = periodic_edge($time, row_period, select_release + 1);
            cpu_access[slot].last_held_clock = grant_release + 14 * master_period + 15 * nanosecond / 2 - decoder_skew;
            fork
                enable_cpu_read(lane_enable, lane_release, generation);
            join_none
        end else begin
            select_release = capture + row_period + 3 * master_period - 12 * nanosecond + decoder_skew;
            cpu_access[slot].displaced_end = $time + 2 * row_period;
            cpu_access[slot].last_held_clock = $time + 3 * row_period;
            fork
                commit_cpu_write(select_release + 10 * nanosecond, slot, generation);
            join_none
        end
        fork
            publish_ack(ack_time, generation);
        join_none
    endtask

    function automatic bit arithmetic_held(input time edge_time);
        foreach (cpu_access[index])
            if (cpu_access[index].valid && cpu_access[index].scheduled &&
                edge_time >= cpu_access[index].first_held_clock &&
                edge_time <= cpu_access[index].last_held_clock) return 1;
        return 0;
    endfunction
    function automatic bit fetch_displaced(input time marker);
        foreach (cpu_access[index])
            if (cpu_access[index].valid && cpu_access[index].scheduled &&
                marker >= cpu_access[index].displaced_start && marker < cpu_access[index].displaced_end) return 1;
        return 0;
    endfunction

    // DIAGNOSTIC CIRCUITS — SERIAL ARITHMETIC HISTORY AND STATUS LATCHES.
    bus_word monitor_serial, monitor3, monitor3_sample;
    logic [7:0] monitor4, monitor5;
    logic_level saturation_samples[3], sat_n, sat_n_target, saturation;
    logic [2:0] shift_xor_samples, multiplier_xor_samples;
    int arithmetic_clock_index;
    bit saturation_pulse, pulse_rising;
    time retrigger_time;
    int unsigned sat_inverse_version, pulse_version;

    assign #(3 * nanosecond) saturation = invert_level(and_levels(sat_n, pin_level(!saturation_pulse)));
    always @(posedge ARUCK) begin
        saturation_samples[arithmetic_clock_index] = SAT;
        shift_xor_samples[arithmetic_clock_index] = S0 ^ S1;
        multiplier_xor_samples[arithmetic_clock_index] = M0_N ^ M1_N;
    end

    task automatic publish_sat_inverse(input logic_level value, input time delay_time,
                                       input int version, generation);
        assert (delay_time > 0);
        #(delay_time);
        if (generation == initialization_generation && version == sat_inverse_version) sat_n = value;
    endtask
    task automatic raise_saturation_pulse(input int generation);
        #(35 * nanosecond);
        if (generation == initialization_generation) begin
            saturation_pulse = 1;
            pulse_rising = 0;
        end
    endtask
    task automatic end_saturation_pulse(input int version, generation);
        #(186120035 * nanosecond);
        if (generation == initialization_generation && version == pulse_version) saturation_pulse = 0;
    endtask

    task automatic sample_arithmetic_monitor(input int edge_index, generation);
        logic_level old_saturation, sample_saturation, inverse_sample;
        time inverse_delay;
        old_saturation = word_bit(monitor_serial, 4);
        sample_saturation = saturation_samples[edge_index];
        monitor_serial = pack_byte('{pin_level(shift_xor_samples[edge_index]), word_bit(monitor_serial, 3),
            word_bit(monitor_serial, 0), pin_level(multiplier_xor_samples[edge_index]),
            sample_saturation, unknown, unknown, unknown});

        if (((old_saturation == low && sample_saturation != low) ||
             (old_saturation >= unknown && sample_saturation == high)) && $time >= retrigger_time) begin
            retrigger_time = $time + 1034000 * nanosecond;
            pulse_version++;
            if (!saturation_pulse && !pulse_rising) begin
                pulse_rising = 1;
                fork
                    raise_saturation_pulse(generation);
                join_none
            end
            fork
                end_saturation_pulse(pulse_version, generation);
            join_none
        end
        inverse_sample = invert_level(sample_saturation);
        if (inverse_sample != sat_n_target) begin
            sat_n_target = inverse_sample;
            sat_inverse_version++;
            inverse_delay = inverse_sample == unknown ? (sat_n == low ? 30 : 22) * nanosecond
                : (inverse_sample == high ? 30 : 22) * nanosecond;
            fork
                publish_sat_inverse(inverse_sample, inverse_delay, sat_inverse_version, generation);
            join_none
        end
    endtask

    always_comb begin : cpu_read_buffers
        DAT_N.tc_drive = released_byte;
        if (!DPORT3_N) DAT_N.tc_drive = monitor3;
        else if (!DPORT4_N) DAT_N.tc_drive = '{value: {8'b0, monitor4}, known: 16'h00ff, high_z: 0};
        else if (!DPORT5_N) DAT_N.tc_drive = '{value: {8'b0, monitor5}, known: 16'h00ff, high_z: 0};
        else if (!MRDC_N && cpu_read_enable && ADR_N[15:9] == 7'h5f)
            DAT_N.tc_drive = '{value: {8'b0, wcs[ADR_N[8:2]][8 * cpu_read_lane +: 8]}, known: 16'h00ff, high_z: 0};
    end

    // Host lifecycle/loading facilities, not connector pins. CPU-facing WCS
    // access will use the board's bus interface; static-WCS fixtures use these.
    int unsigned initialization_generation /* verilator public_flat_rw */ = 0;
    int unsigned storage_generation = '1;
    task initialize();
        initialization_generation++;
    endtask
    task load_word(input int address, input logic [31:0] instruction);
        assert (address >= 0 && address < 128);
        wcs[address] = instruction;
    endtask

    always @(initialization_generation) begin : initialize_storage
        if (storage_generation != initialization_generation) begin
            storage_generation = initialization_generation;
            foreach (wcs[address]) wcs[address] = 0;
            PC = 0;
            MI = 0;
            instruction_register = 0;
            executing_instruction = 0;
            instruction_controls = 0;
            sign_latch = 0;
            counter_clear_n = 1;
            even_coefficient = 0;
            odd_coefficient = 0;
            protected_pair = 0; resetd_n = 0;
            foreach (cpu_access[index]) cpu_access[index] = '0;
            cpu_read_enable = 0; wcs_write_count = 0; last_wcs_commit = 0;
            DAT_N.tc_ack = disconnected;
            monitor_serial = '{value: 0, known: 16'h001f, high_z: 0};
            monitor3 = '{value: 0, known: 16'h00ff, high_z: 0};
            monitor3_sample = monitor3;
            monitor4 = 0; monitor5 = 0; arithmetic_clock_index = 0;
            sat_n = high; sat_n_target = high;
            saturation_pulse = 0; pulse_rising = 0;
            sat_inverse_version = 0; pulse_version = 0; retrigger_time = $time + first_marker;
            rows_started = 0;
            row_marker = $time + first_marker;
            RA0_N = 1; RA1_N = 1; WA0_N = 1; WA1_N = 1;
            S0 = 1; S1 = 0; M0_N = 1; M1_N = 1; CSIGN_N = 0; ZERO_N = 1;
            DAB_WSTB_N = 1; XFER_CK = 0;
            RDRREG_N = 1; RD_XREG_N = 1; WR_XREG_N = 1; RD_AD_N = 1;
            MEMAC = 0; MEMW_N = 1; OFST_N = 0; DAB_RSTB = 1;
            FPC_CK <= 0; WR_DA_N = 1; RESET_N = 1;
            SDAA = 0; SDAB = 0; SDAC = 0; SDAD = 0;
            ARUCK <= 0;
        end
    end

    // SHEET 1 — CLOCK DRIVER AND MASTER STATE GENERATOR.
    // Model the locked nominal clock. One row spans nine master periods and
    // contains three arithmetic edges. Controls settle before the NBA edge.
    task automatic arithmetic_clock(input int edge_index, input bit load_operand, direct_bit, shifted_bit,
                                    coefficient_sign, clear_accumulator);
        arithmetic_clock_index = edge_index;
        S0 = !arithmetic_held($time);
        S1 = load_operand && S0;
        M1_N = ~direct_bit;
        M0_N = ~shifted_bit;
        CSIGN_N = coefficient_sign;
        ZERO_N = ~clear_accumulator;
        ARUCK <= 1;
    endtask

    // A marker latches the instruction that this row's circuits observe.
    // These timing processes stay alive between rows. Recreating a fork/join
    // tree every row allocated millions of simulator coroutine frames.
    event row_marker_edge;
    logic [31:0] row_instruction, row_fetched_instruction;
    logic [6:0] row_controls;
    logic [3:0] row_even_coefficient, row_odd_coefficient;
    bit row_coefficient_sign;

    task automatic begin_row(input int generation);
        row_instruction = instruction_register;
        executing_instruction = row_instruction;
        row_controls = instruction_controls;
        row_coefficient_sign = sign_latch;
        row_even_coefficient = load_coefficient(even_coefficient, row_instruction, 0);
        row_odd_coefficient = load_coefficient(odd_coefficient, row_instruction, 1);
        foreach (cpu_access[index])
            if (cpu_access[index].valid && !cpu_access[index].scheduled &&
                cpu_access[index].requested_at < $time &&
                (!HALT_N || (!protected_pair && !row_instruction[22]))) grant_wcs(index, generation);
        monitor4 = {row_instruction[22], operation(row_instruction) != memory_write_row,
            !(operation(row_instruction) == operation_row && row_instruction[13:12] == 3),
            row_coefficient_sign, row_controls[2:1], row_instruction[19:18]};
        RA0_N = ~row_controls[1]; RA1_N = ~row_controls[2];
        WA0_N = ~row_instruction[18]; WA1_N = ~row_instruction[19];
        row_marker = $time;
        rows_started++;

        -> row_marker_edge;
    endtask

    always @(row_marker_edge) begin : arithmetic_clocks
        int generation;
        generation = initialization_generation;
        arithmetic_clock(0, 0, odd_coefficient[3], even_coefficient[3], row_coefficient_sign, 0);
        #(3 * master_period / 2);
        if (generation == initialization_generation) ARUCK <= 0;
        #(3 * master_period / 2);
        if (generation == initialization_generation)
            arithmetic_clock(1, ~row_controls[0], row_odd_coefficient[3], row_even_coefficient[3], row_coefficient_sign, 0);
        #(3 * master_period / 2);
        if (generation == initialization_generation) ARUCK <= 0;
        #(3 * master_period / 2);
        if (generation == initialization_generation)
            arithmetic_clock(2, 0, row_odd_coefficient[2], row_even_coefficient[2], row_coefficient_sign, row_controls[6]);
        #(3 * master_period / 2);
        if (generation == initialization_generation) ARUCK <= 0;
    end

    always @(row_marker_edge) begin : register_write_window
        int generation;
        generation = initialization_generation;
        #(write_open);
        if (generation == initialization_generation) DAB_WSTB_N = 0;
        #(write_close - write_open);
        if (generation == initialization_generation) DAB_WSTB_N = 1;
    end

    // U20's DAB RSTB changes before the SRAM latch and source decoder.
    // DMEM receives this clock on the 224X backplane (M56).
    always @(row_marker_edge) begin : bus_read_strobe
        int generation;
        generation = initialization_generation;
        #(dab_read_fall);
        if (generation == initialization_generation) DAB_RSTB = 0;
        #(dab_read_rise - dab_read_fall);
        if (generation == initialization_generation) DAB_RSTB = 1;
    end

    always @(row_marker_edge) begin : result_capture
        int generation;
        generation = initialization_generation;
        #(6 * master_period - 13 * nanosecond / 2);
        if (generation == initialization_generation && row_controls[5]) XFER_CK = 1;
        #(master_period);
        if (generation == initialization_generation) XFER_CK = 0;
    end

    // SHEET 2 — INSTRUCTION FETCH AND CONTROL DECODE.
    // The address can be visited by CPU writes before this aperture.
    always @(row_marker_edge) begin : fetch_instruction
        int generation;
        generation = initialization_generation;
        #(instruction_fetch);
        if (generation == initialization_generation) begin
            MI = fetch_displaced(row_marker) ? 32'b0 : wcs[PC];
            row_fetched_instruction = MI;
            PC = !counter_clear_n ? 7'b0 : HALT_N ? PC + 7'd1 : PC;
            counter_clear_n = !(operation(MI) == operation_row && MI[3]);
            if (!row_instruction[22] && MI[22]) protected_pair = resetd_n ? !protected_pair : 0;
            if (resetd_n && operation(row_instruction) == operation_row && row_instruction[3]) protected_pair = 0;
            resetd_n = !(operation(row_instruction) == operation_row && row_instruction[3]);
            sign_latch = row_controls[4];
            instruction_controls = {MI[25:23], 1'b0, MI[21:20],
                (operation(MI) == operation_row && MI[4])};
            instruction_register = MI;
            even_coefficient = {row_even_coefficient[1:0], 2'b11};
            odd_coefficient = {row_odd_coefficient[1:0], 2'b11};
            OFST_N = MI[15:0];
            MEMAC = MI[17];
            MEMW_N = operation(MI) != memory_write_row;
        end

        // The fetched word reaches each output buffer through its
        // decoder path. Keeping this with the fetch avoids a second
        // process racing to observe the fetched instruction.
        case (operation(row_fetched_instruction))
            memory_write_row: begin
                #(89430 + decoder_skew - instruction_fetch);
                if (generation == initialization_generation && assume_result_during_memory_write)
                    RDRREG_N = 0;
            end
            operation_row: case (row_fetched_instruction[13:12])
                2'b00: ;  // selects nothing: U47 pin 12 (2Y0/) is unwired
                2'b01: begin
                    #(112182 + decoder_skew - instruction_fetch);
                    if (generation == initialization_generation) RDRREG_N = 0;
                end
                2'b10: begin
                    #(115350 + decoder_skew - instruction_fetch);
                    if (generation == initialization_generation) RD_XREG_N = 0;
                end
                2'b11: begin
                    #(126870 + decoder_skew - instruction_fetch);
                    if (generation == initialization_generation) RD_AD_N = 0;
                end
            endcase
            default: ;
        endcase
    end

    always @(row_marker_edge) begin : release_bus_driver
        int generation;
        generation = initialization_generation;
        if (operation(row_instruction) == memory_write_row) begin
            #(51930 + decoder_skew);
            if (generation == initialization_generation) RDRREG_N = 1;
        end else if (operation(row_instruction) == operation_row) begin
            case (row_instruction[13:12])
                2'b00: ;
                2'b01: begin
                    #(54522 + decoder_skew);
                    if (generation == initialization_generation) RDRREG_N = 1;
                end
                2'b10: begin
                    #(77562 + decoder_skew);
                    if (generation == initialization_generation) RD_XREG_N = 1;
                end
                2'b11: begin
                    #(89082 + decoder_skew);
                    if (generation == initialization_generation) RD_AD_N = 1;
                end
            endcase
        end
    end

    always @(row_marker_edge) begin : capture_xreg
        int generation;
        generation = initialization_generation;
        #(53340);
        if (generation == initialization_generation && operation(row_instruction) == operation_row && row_instruction[6])
            WR_XREG_N = 0;
        #(master_period);
        if (generation == initialization_generation) WR_XREG_N = 1;
    end

    always @(row_marker_edge) begin : reset_decode
        int generation;
        generation = initialization_generation;
        #(instruction_fetch + 9 * nanosecond);
        if (generation == initialization_generation)
            RESET_N = !(operation(row_instruction) == operation_row && row_instruction[3]);
    end

    always @(row_marker_edge) begin : diagnostic_captures
        int generation;
        generation = initialization_generation;
        #(25 * nanosecond);
        if (generation == initialization_generation) sample_arithmetic_monitor(0, generation);
        #(47580 - 25 * nanosecond);
        if (generation == initialization_generation)
            monitor5 = {!(operation(row_instruction) == memory_write_row ||   // U7 D7 = RD_RREG/ = NAND(MEMW/ . 2Y1/)
                          (operation(row_instruction) == operation_row && row_instruction[13:12] == 1)),
                !(operation(row_instruction) == operation_row && row_instruction[13:12] == 2),
                !(operation(row_instruction) == operation_row && row_instruction[6]),
                !(operation(row_instruction) == operation_row && row_instruction[7]), channel_select(row_instruction)};
        #(3 * master_period + 25 * nanosecond - 47580);
        if (generation == initialization_generation) sample_arithmetic_monitor(1, generation);
        #(87702 - 3 * master_period - 25 * nanosecond);
        if (generation == initialization_generation)
            monitor3_sample = pack_byte('{saturation,
                pin_level(!(arithmetic_held(row_marker + 3 * master_period) && arithmetic_held(row_marker + 6 * master_period))),
                word_bit(monitor_serial, 0), word_bit(monitor_serial, 2), word_bit(monitor_serial, 1),
                word_bit(monitor_serial, 3), pin_level(row_instruction[30] ^ row_instruction[31]), pin_level(!row_controls[6])});
        #(103830 - 87702);
        if (generation == initialization_generation) monitor3 = monitor3_sample;
        #(6 * master_period + 25 * nanosecond - 103830);
        if (generation == initialization_generation) sample_arithmetic_monitor(2, generation);
    end

    always @(row_marker_edge) begin : converter_clock_and_controls
        int generation;
        generation = initialization_generation;
        #(converter_clock);
        if (generation == initialization_generation) begin
            WR_DA_N = !(operation(row_fetched_instruction) == operation_row && row_fetched_instruction[7]);
            {SDAD, SDAC, SDAB, SDAA} = channel_select(row_fetched_instruction);
            FPC_CK <= 1;
        end
        #(master_period / 2);
        if (generation == initialization_generation) FPC_CK <= 0;
    end

    task automatic run_clock(input int generation);
        #(first_marker);
        while (generation == initialization_generation) begin
            begin_row(generation);
            #(row_period);
        end
    endtask

    initial begin : start_clock
        static int unsigned clock_generation = 0;
        forever begin
            wait (initialization_generation != clock_generation);
            clock_generation = initialization_generation;
            fork
                run_clock(clock_generation);
            join_none
        end
    end
    initial initialize();
endmodule
