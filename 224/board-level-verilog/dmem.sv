// 060-02512, DATA MEMORY, MODEL 224X.
// Sheet 1: sample storage, address adder, XREG and diagnostic data paths.
// Sheet 2: CPU port decoding and the memory-cycle timing generator.
module DMEM #(
    parameter bit maximum_delays = 0,
    parameter bit cpu_connected = 0
) (
    input logic DAB_RSTB, RESET_N, MEMAC, MEMW_N,
    input logic [15:0] OFST_N,
    input logic RD_XREG_N, WR_XREG_N,
    input logic [7:0] ADR_N,
    input logic IORC_N, IOWC_N,
    output backplane_signals::logic_level HALT_N,
    output logic HR1_N, HR2_N, DPORT3_N, DPORT4_N, DPORT5_N,
    cpu_bus.dmem DAT_N,
    dab_bus.dmem DAB
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    localparam time nanosecond = 576;
    localparam time row_period = 168750;
    localparam time decoder_skew = maximum_delays ? 2 * nanosecond : 0;
    localparam time dab_read_fall = 51930 + decoder_skew;
    localparam time write_sample = maximum_delays ? 152214 : 151062;

    // SHEET 1 — CURRENT POSITION COUNTER AND ADDRESS ADDER.
    // OFST/ already contains the complemented offset. Addition wraps on the
    // sixteen address wires; the two DRAM address captures exchange the bytes.
    logic [15:0] position_counter, marker_address;
    logic clear_counter, reset_pulse;
    bus_word row_address [2], column_address [2];
    bus_word memory [2][65536];
    bus_word DOUT [2];
    logic [1:0] memory_output_enable;
    bus_word write_sample_word;

    assign DAB.ram0_drive = memory_output_enable[0] ? DOUT[0] : released_bus;
    assign DAB.ram1_drive = memory_output_enable[1] ? DOUT[1] : released_bus;

    // XREG: separate CPU-to-DSP and DSP-to-CPU latches, not a mailbox.
    // CPU to DAB: the XREG-IN latches U39/U41 (D = SBC data, Q = DAB, clocked
    // by WRL/WRH XREG/); DAB to CPU: the XREG-OUT latches U38/U40.
    bus_word xreg_input, xreg_output, xreg_monitor;
    assign DAB.xreg_drive = RD_XREG_N ? released_bus : xreg_input;
    always @(negedge WR_XREG_N) xreg_output = DAB.sample;

    int unsigned initialization_generation /* verilator public_flat_rw */ = 0;
    int unsigned storage_generation = '1;
    task initialize();
        initialization_generation++;
    endtask
    task load_xreg(input bus_word sample_word);
        xreg_input = sample_word;
    endtask

    always @(initialization_generation) begin : initialize_storage
        if (storage_generation != initialization_generation) begin
            storage_generation = initialization_generation;
            // Prepared-zero RAM is a fixture convention, not a power-on fact.
            foreach (memory[bank, address]) memory[bank][address] = zero_word;
            position_counter = 1;
            marker_address = 2;
            reset_pulse = 0;
            foreach (row_address[bank]) begin
                row_address[bank] = '0;
                column_address[bank] = '{value: 0, known: 16'h00ff, high_z: 0};
                DOUT[bank] = zero_word;
            end
            memory_output_enable = 0;
            write_sample_word = released_bus;
            xreg_input = zero_word;
            xreg_output = '0; // Startup U61 sees an unresolved DAB sample.
            xreg_monitor = '{value: 0, known: 16'h00ff, high_z: 0};
        end
    end

    // SHEET 2 — CPU I/O DECODERS AND SINGLE/CONTINUOUS, RUN/HALT LATCHES.
    // Keep the cross-coupled NANDs visible: OUT 03 releases HALT/ through
    // the RUN latch; it does not issue a software instruction to the DSP.
    logic [7:0] cpu_write_n, cpu_read_n;
    logic [1:0] headroom_read_n;
    logic page_n, page_selected, high_page, command_active, RESET;
    logic_level single_step, continuous_run, reset_gate, halt_gate;
    int unsigned io_initialized_generation = '1, clear_version;
    longint unsigned reset_edges;
    time last_reset_rise;
    wire io_initializing = io_initialized_generation != initialization_generation;

    if (cpu_connected) begin : cpu_circuits
        assign #(8 * nanosecond) page_n = ~(&ADR_N[7:4]);
        assign #(3 * nanosecond) page_selected = ~page_n;
        assign #(3 * nanosecond) high_page = ~ADR_N[3];
        assign #(15 * nanosecond) command_active = ~(IORC_N & IOWC_N);
        assign #(15 * nanosecond) DAT_N.dmem_ack = page_selected && command_active ? low : disconnected;
        assign #(15 * nanosecond) RESET = ~RESET_N;

        for (genvar port = 0; port < 8; port++) begin
            assign #(20 * nanosecond) cpu_write_n[port] =
                !(ADR_N[3] && !page_n && !IOWC_N && ADR_N[2:0] == 3'(7 - port));
            assign #(20 * nanosecond) cpu_read_n[port] =
                !(ADR_N[3] && !page_n && !IORC_N && ADR_N[2:0] == 3'(7 - port));
        end
        for (genvar port = 0; port < 2; port++) begin
            assign #(20 * nanosecond) headroom_read_n[port] =
                !(high_page && !page_n && !IORC_N && ADR_N[2:0] == 3'(7 - port));
        end

        assign #(3 * nanosecond) clear_counter = ~cpu_write_n[5];
        assign #(3 * nanosecond) single_step = io_initializing ? unknown
            : invert_level(and_levels(pin_level(cpu_write_n[0]), continuous_run));
        assign #(3 * nanosecond) continuous_run = io_initializing ? unknown
            : invert_level(and_levels(single_step, pin_level(cpu_write_n[1])));
        assign #(3 * nanosecond) reset_gate = io_initializing ? unknown
            : invert_level(and_levels(single_step, pin_level(RESET)));
        assign #(8 * nanosecond) halt_gate = io_initializing ? unknown
            : invert_level(and_levels(and_levels(reset_gate, pin_level(cpu_write_n[2])), HALT_N));
        assign #(3 * nanosecond) HALT_N = io_initializing ? unknown
            : invert_level(and_levels(halt_gate, pin_level(cpu_write_n[3])));

        always @(posedge cpu_write_n[6]) capture_cpu_byte(0, DAT_N.sample, initialization_generation);
        always @(posedge cpu_write_n[7]) capture_cpu_byte(1, DAT_N.sample, initialization_generation);
        always @(clear_counter) begin
            clear_version++;
            if (clear_counter) begin
                fork
                    clear_position_counter(clear_version, initialization_generation);
                join_none
            end
        end
        always @(posedge RESET) begin
            reset_edges++;
            last_reset_rise = $time;
        end
    end else begin : disconnected_cpu
        assign cpu_write_n = '1;
        assign cpu_read_n = '1;
        assign headroom_read_n = '1;
        assign clear_counter = 0;
        assign HALT_N = high;
        assign DAT_N.dmem_ack = disconnected;
    end
    assign {HR2_N, HR1_N} = headroom_read_n;
    assign {DPORT5_N, DPORT4_N, DPORT3_N} = cpu_read_n[5:3];

    always_comb begin : cpu_data_buffers
        DAT_N.dmem_drive = released_byte;
        if (!cpu_read_n[0]) DAT_N.dmem_drive = '{value: {8'b0, OFST_N[7:0]}, known: 16'h00ff, high_z: 0};
        else if (!cpu_read_n[1]) DAT_N.dmem_drive = '{value: {8'b0, OFST_N[15:8]}, known: 16'h00ff, high_z: 0};
        else if (!cpu_read_n[2]) DAT_N.dmem_drive = xreg_monitor;
        else if (!cpu_read_n[6]) DAT_N.dmem_drive = '{value: {8'b0, xreg_output.value[7:0]},
            known: {8'b0, xreg_output.known[7:0]}, high_z: {8'b0, xreg_output.high_z[7:0]}};
        else if (!cpu_read_n[7]) DAT_N.dmem_drive = '{value: {8'b0, xreg_output.value[15:8]},
            known: {8'b0, xreg_output.known[15:8]}, high_z: {8'b0, xreg_output.high_z[15:8]}};
    end

    task automatic store_cpu_byte(input bit high_byte, input bus_word sample_word, input int generation);
        #(8 * nanosecond);
        if (generation == initialization_generation) begin
            xreg_input.value[8 * int'(high_byte) +: 8] = sample_word.value[7:0] & sample_word.known[7:0];
            xreg_input.known[8 * int'(high_byte) +: 8] = sample_word.known[7:0];
            xreg_input.high_z[8 * int'(high_byte) +: 8] = 0;
            if (high_byte) xreg_monitor = '{value: {8'b0, sample_word.value[7:0] & sample_word.known[7:0]},
                known: {8'b0, sample_word.known[7:0]}, high_z: 0};
        end
    endtask
    task automatic capture_cpu_byte(input bit high_byte, input bus_word sample_word, input int generation);
        fork
            store_cpu_byte(high_byte, sample_word, generation);
        join_none
    endtask
    task automatic clear_position_counter(input int version, generation);
        #(24 * nanosecond);
        if (generation == initialization_generation && version == clear_version) position_counter = 0;
    endtask
    initial begin : initialize_cpu_circuits
        static int unsigned observed_generation = 0;
        forever begin
            wait (initialization_generation != observed_generation);
            observed_generation = initialization_generation;
            clear_version = 0; reset_edges = 0; last_reset_rise = 0;
            #(40 * nanosecond);
            io_initialized_generation = observed_generation;
        end
    end

    // SHEET 2 — MEMORY-CYCLE TIMING.
    // The reduction below keeps the established visibility boundary:
    // sample DIN late in the cycle; commit and capture the address before the
    // next read; expose DOUT only during its output-enable interval. DAB RSTB
    // supplies the phase. This does not claim chip-level RAS/CAS equivalence.
    // The selected fitted DMEM-I/O configuration straps CAS1 off: normal WCS
    // uses bank 0. See the 2026-09-17 clean-direct-real-wcs-zoom-parity note.
    task automatic memory_cycle(input int generation);
        logic [15:0] address_sum, cell_address;
        bus_word captured_word;
        #(write_sample - dab_read_fall);
        captured_word = DAB.sample;
        if (generation == initialization_generation) write_sample_word = captured_word;

        #(row_period - write_sample);
        if (generation == initialization_generation) begin
            if (reset_pulse && !clear_counter) position_counter++;
            reset_pulse = !RESET_N;
            address_sum = position_counter + OFST_N + 16'd1;
            cell_address = {address_sum[7:0], address_sum[15:8]};
            marker_address = address_sum;
            foreach (row_address[bank])
                row_address[bank] = '{value: {8'b0, address_sum[7:0]}, known: 16'h00ff, high_z: 0};

            if (MEMAC) begin
                column_address[0] = '{value: {8'b0, cell_address[7:0]}, known: 16'h00ff, high_z: 0};
                if (!MEMW_N) memory[0][cell_address] = captured_word;
                else DOUT[0] = memory[0][cell_address];
            end

            if (MEMAC && MEMW_N) begin
                #(13992 + decoder_skew);
                if (generation == initialization_generation) memory_output_enable[0] = 1;
                #(68136 - 13992);
                if (generation == initialization_generation) memory_output_enable[0] = 0;
            end
        end
    endtask

    always @(negedge DAB_RSTB) begin
        fork
            memory_cycle(initialization_generation);
        join_none
    end
    initial initialize();
endmodule
