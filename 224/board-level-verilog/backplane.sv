// Electrical representation shared by the physical board connections.
// Explicit X/Z masks survive Verilator's two-state native compilation.
// Original wire names are uppercase; representation fields are lowercase.
package backplane_signals;
    timeunit 1ps;
    timeprecision 1ps;
    typedef struct packed {
        logic [15:0] value;
        logic [15:0] known;
        logic [15:0] high_z;
    } bus_word;

    typedef enum logic [1:0] {low, high, unknown, disconnected} logic_level;

    localparam bus_word released_bus = '{value: 0, known: 0, high_z: '1};
    localparam bus_word zero_word = '{value: 0, known: '1, high_z: 0};
    localparam bus_word released_byte = '{value: 0, known: 0, high_z: 16'h00ff};

    function automatic bus_word known_word(input logic [15:0] value);
        return '{value: value, known: '1, high_z: 0};
    endfunction

    function automatic logic_level pin_level(input bit value);
        return value ? high : low;
    endfunction
    function automatic logic_level invert_level(input logic_level value);
        return value == low ? high : value == high ? low : unknown;
    endfunction
    function automatic logic_level and_levels(input logic_level a, b);
        return a == low || b == low ? low : a == high && b == high ? high : unknown;
    endfunction
    function automatic logic_level word_bit(input bus_word word_value, input int index);
        return word_value.known[index] ? pin_level(word_value.value[index])
            : word_value.high_z[index] ? disconnected : unknown;
    endfunction
    function automatic bus_word pack_byte(input logic_level pins[8]);
        bus_word byte_word;
        byte_word = '0;
        foreach (pins[index]) begin
            byte_word.value[index] = pins[index] == high;
            byte_word.known[index] = pins[index] inside {low, high};
            byte_word.high_z[index] = pins[index] == disconnected;
        end
        return byte_word;
    endfunction
endpackage

// DAT0/..DAT7/ and the open-collector XACK return on the CPU backplane.
// The SBC inverts CPU bytes onto DAT/ and inverts them again when reading.
interface cpu_bus;
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    bus_word sbc_drive = released_byte, tc_drive;
    bus_word dmem_drive, fpc_drive;
    bus_word sample;
    logic_level tc_ack = disconnected, dmem_ack = disconnected, XACK;
    logic [3:0] drivers;
    logic [7:0] overlap;
    bus_word sources[4];
    assign sources = '{sbc_drive, tc_drive, dmem_drive, fpc_drive};
    assign XACK = tc_ack == low || dmem_ack == low ? low
        : tc_ack == unknown || dmem_ack == unknown ? unknown : high;

    always_comb begin
        logic [7:0] driven, ones, zeros, unknown_bits, active;
        driven = 0; ones = 0; zeros = 0; unknown_bits = 0;
        drivers = 0; overlap = 0;
        foreach (sources[index]) begin
            active = ~sources[index].high_z[7:0];
            drivers[index] = |active;
            overlap |= driven & active;
            driven |= active;
            ones |= sources[index].value[7:0] & sources[index].known[7:0] & active;
            zeros |= ~sources[index].value[7:0] & sources[index].known[7:0] & active;
            unknown_bits |= ~sources[index].known[7:0] & active;
        end
        sample.known = {8'b0, (ones ^ zeros) & ~unknown_bits};
        sample.value = {8'b0, ones & sample.known[7:0]};
        sample.high_z = {8'b0, ~driven};
    end
    modport sbc(input sample, XACK, output sbc_drive);
    modport tc(input sample, output tc_drive, tc_ack);
    modport dmem(input sample, output dmem_drive, dmem_ack);
    modport fpc(input sample, output fpc_drive);
endinterface

// DAB0..DAB15: the shared digitized audio bus. Each field ending in "drive"
// describes one set of physical output buffers. There is one resolved sample.
// DMEM has separate RAM and XREG output drivers on the same connector wires.
interface dab_bus;
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;

    bus_word aru_drive = released_bus;
    bus_word xreg_drive = released_bus;
    bus_word external_drive = released_bus;
    bus_word ram0_drive = released_bus;
    bus_word ram1_drive = released_bus;
    bus_word fpc_drive = released_bus;
    bus_word sample;

    logic [5:0] drivers;
    logic [15:0] overlap;
    bus_word sources [6];
    assign sources = '{aru_drive, xreg_drive, external_drive,
                       ram0_drive, ram1_drive, fpc_drive};

    always_comb begin : resolve_output_buffers
        logic [15:0] driven, ones, zeros, unknown_bits, active;
        driven = 0;
        ones = 0;
        zeros = 0;
        unknown_bits = 0;
        drivers = 0;
        overlap = 0;

        foreach (sources[index]) begin
            active = ~sources[index].high_z;
            drivers[index] = |active;
            overlap |= driven & active;
            driven |= active;
            ones |= sources[index].value & sources[index].known & active;
            zeros |= ~sources[index].value & sources[index].known & active;
            unknown_bits |= ~sources[index].known & active;
        end

        sample.known = (ones ^ zeros) & ~unknown_bits;
        sample.value = ones & sample.known;
        sample.high_z = ~driven;
    end

    modport aru(input sample, output aru_drive);
    modport dmem(input sample, output ram0_drive, ram1_drive, xreg_drive);
    modport fpc(input sample, drivers, overlap, output fpc_drive);
endinterface

// The DSP boards and their connector signals. The host supplies ADC pins;
// sample values, instructions and timing move between boards on these wires.
// HALT/ is supplied by the CPU-facing part of DMEM in the complete machine.
module BACKPLANE #(parameter bit maximum_delays = 0, parameter bit cpu_connected = 0) (
    input logic HALT_N,
    input backplane_signals::bus_word AD, IGA, LA_N,
    output logic CH1, STBGN, CNVCLK, STCNV_N,
    output backplane_signals::bus_word DA, OGA,
    output logic [3:0] OUT
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    dab_bus DAB();
    cpu_bus DAT_N();
    logic ARUCK, DAB_WSTB_N, XFER_CK, RA0_N, RA1_N, WA0_N, WA1_N;
    logic S0, S1, M0_N, M1_N, CSIGN_N, ZERO_N, RDRREG_N, RD_XREG_N;
    logic WR_XREG_N, RD_AD_N, MEMAC, MEMW_N, DAB_RSTB, FPC_CK;
    logic WR_DA_N, RESET_N, SDAA, SDAB, SDAC, SDAD;
    logic [15:0] OFST_N;
    logic_level SAT;
    logic_level dmem_halt_n;
    logic HR1_N, HR2_N, DPORT3_N, DPORT4_N, DPORT5_N;
    logic [15:0] ADR_N;
    logic PHI2, MRDC_N, MWTC_N, IORC_N, IOWC_N;

    SBC sbc(.ADR_N, .PHI2, .MRDC_N, .MWTC_N, .IORC_N, .IOWC_N, .DAT_N);

    TC #(.maximum_delays(maximum_delays)) tc(
        .HALT_N(cpu_connected ? dmem_halt_n == high : HALT_N), .SAT, .ARUCK, .DAB_WSTB_N, .XFER_CK,
        .ADR_N, .PHI2, .MRDC_N, .MWTC_N,
        .DPORT3_N, .DPORT4_N, .DPORT5_N, .DAT_N,
        .RA0_N, .RA1_N, .WA0_N, .WA1_N, .S0, .S1, .M0_N, .M1_N,
        .CSIGN_N, .ZERO_N, .RDRREG_N, .RD_XREG_N, .WR_XREG_N, .RD_AD_N,
        .OFST_N, .MEMAC, .MEMW_N, .DAB_RSTB, .FPC_CK, .WR_DA_N,
        .RESET_N, .SDAA, .SDAB, .SDAC, .SDAD
    );
    ARU aru(.ARUCKE_N(~ARUCK), .DAB_WSTB_N, .XFER_CK,
        .RA0_N, .RA1_N, .WA0_N, .WA1_N, .S0, .S1, .M0_N, .M1_N,
        .CSIGN_N, .ZERO_N, .RDRREG_N, .SAT, .DAB);
    DMEM #(.maximum_delays(maximum_delays), .cpu_connected(cpu_connected)) dmem(
        .DAB_RSTB, .RESET_N, .MEMAC, .MEMW_N, .OFST_N,
        .RD_XREG_N, .WR_XREG_N, .DAB,
        .ADR_N(ADR_N[7:0]), .IORC_N, .IOWC_N, .HALT_N(dmem_halt_n),
        .HR1_N, .HR2_N, .DPORT3_N, .DPORT4_N, .DPORT5_N, .DAT_N);
    FPC fpc(.FPC_CK, .RESET_N, .RD_AD_N, .WR_DA_N,
        .SDAA, .SDAB, .SDAC, .SDAD, .AD, .IGA, .LA_N, .HR1_N, .HR2_N, .DAT_N,
        .CH1, .STBGN, .CNVCLK, .STCNV_N, .DA, .OGA, .OUT, .DAB);
    AOUT aout(.DA, .OGA, .OUT);

    task initialize();
        tc.initialize();
        aru.initialize();
        dmem.initialize();
        fpc.initialize();
        aout.initialize();
        sbc.initialize();
    endtask
endmodule
