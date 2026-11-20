// SINGLE BOARD COMPUTER — external 8080 bus and READY sampling.
// The licensed instruction core runs in the host. Its adapter submits each
// external byte transfer at T1; this board generates the actual connector pins.
module SBC (
    output logic [15:0] ADR_N,
    output logic PHI2, MRDC_N, MWTC_N, IORC_N, IOWC_N,
    cpu_bus.sbc DAT_N
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    localparam time nanosecond = 576, oscillator_period = 31250, cpu_period = 281250;
    typedef enum logic [2:0] {idle_bus, address_state, command_state, wait_state, data_state} bus_phase;
    typedef enum logic [1:0] {memory_read, memory_write, port_read, port_write} transfer_kind;
    bus_phase phase;
    transfer_kind transfer;
    logic [15:0] address;
    logic [7:0] write_byte;
    bus_word read_byte /* verilator public_flat_rw */;
    logic_level ready_sample;
    bit accepted, release_write, holding_write_data, address_waiting;
    logic [15:0] waiting_address;
    // These also change in the public T1 submission task. Marking them as
    // externally writable keeps the host's same-time observations live.
    int unsigned waits /* verilator public_flat_rw */;
    bit completed /* verilator public_flat_rw */;
    time cycle_start, ready_time, sample_time, completion_time;

    int unsigned initialization_generation /* verilator public_flat_rw */ = 0;
    task initialize();
        initialization_generation++;
    endtask
    task begin_transfer(input int kind, input logic [15:0] bus_address, input logic [7:0] value);
        assert (phase == idle_bus && $time == cycle_start)
            else $fatal(1, "SBC transfer must begin at an idle T1 boundary");
        transfer = transfer_kind'(kind);
        address = transfer inside {port_read, port_write} ? {bus_address[7:0], bus_address[7:0]} : bus_address;
        write_byte = value;
        waits = 0;
        completed = 0;
        ready_time = 0;
        sample_time = 0;
        read_byte = '0;
        accepted = 0;
        phase = address_state;
    endtask

    task automatic release_data(input int generation);
        #(79 * nanosecond);
        if (generation == initialization_generation) begin
            DAT_N.sbc_drive = released_byte;
            holding_write_data = 0;
            if (address_waiting) ADR_N = waiting_address;
            address_waiting = 0;
        end
    endtask

    task automatic clock_bus(input int generation);
        phase = idle_bus;
        ADR_N = '1;
        DAT_N.sbc_drive = released_byte;
        MRDC_N = 1; MWTC_N = 1; IORC_N = 1; IOWC_N = 1; PHI2 = 0;
        ready_sample = unknown;
        release_write = 0; holding_write_data = 0; address_waiting = 0;
        waits = 0; completed = 0; read_byte = '0;
        cycle_start = $time;
        ready_time = 0; sample_time = 0; completion_time = 0;

        while (generation == initialization_generation) begin
            #(2 * oscillator_period);
            if (generation != initialization_generation) return;
            PHI2 = 1;
            if (release_write) begin
                MWTC_N = 1;
                IOWC_N = 1;
                holding_write_data = 1;
                release_write = 0;
                fork
                    release_data(generation);
                join_none
            end
            case (phase)
                address_state: begin
                    if (holding_write_data) begin
                        address_waiting = 1;
                        waiting_address = ~address;
                    end else begin
                        ADR_N = ~address;
                        DAT_N.sbc_drive = released_byte;
                    end
                end
                command_state: case (transfer)
                    memory_read: MRDC_N = 0;
                    port_read: IORC_N = 0;
                    default: DAT_N.sbc_drive = '{value: {8'b0, ~write_byte}, known: 16'h00ff, high_z: 0};
                endcase
                data_state: if (transfer inside {memory_read, port_read}) begin
                    read_byte = '{value: DAT_N.sample.value ^ DAT_N.sample.known,
                        known: DAT_N.sample.known, high_z: DAT_N.sample.high_z};
                    sample_time = $time;
                    if (transfer == memory_read) MRDC_N = 1;
                    else IORC_N = 1;
                end
                default: ;
            endcase

            #(oscillator_period);
            if (generation != initialization_generation) return;
            ready_sample = invert_level(DAT_N.XACK);
            #(4 * oscillator_period);
            if (generation != initialization_generation) return;
            PHI2 = 0;
            if (phase inside {command_state, wait_state}) begin
                accepted = ready_sample == high;
                if (accepted) ready_time = $time;
            end
            #(2 * oscillator_period);
            if (generation != initialization_generation) return;
            case (phase)
                address_state: phase = command_state;
                command_state, wait_state: begin
                    if (phase == command_state) begin
                        if (transfer == memory_write) MWTC_N = 0;
                        if (transfer == port_write) IOWC_N = 0;
                    end
                    if (accepted) phase = data_state;
                    else begin phase = wait_state; waits++; end
                end
                data_state: begin
                    completion_time = $time;
                    completed = 1;
                    release_write = transfer inside {memory_write, port_write};
                    phase = idle_bus;
                end
                default: ;
            endcase
            cycle_start = $time;
        end
    endtask

    initial begin
        static int unsigned clock_generation = 0;
        forever begin
            wait (initialization_generation != clock_generation);
            clock_generation = initialization_generation;
            fork
                clock_bus(clock_generation);
            join_none
        end
    end
    initial initialize();
endmodule
