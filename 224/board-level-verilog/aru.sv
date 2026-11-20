// 060-01318, sheet 1 of 1: ARITHMETIC UNIT.
//
// REGISTER FILE -> DUAL RANK SHIFT REGISTER -> PARTIAL PRODUCT REGISTER
//                                                 |             |
//                                                 +-> sum <-----AC
//                                                      |
//                                                 RESULT REGISTER -> DAB
//
// Each arithmetic edge reads the previous pipeline stages. The result register
// samples the settled sum on XFER CK and makes that saved value visible later.
module ARU (
    input logic ARUCKE_N,                // ARUCKE/: pulled-up XOR clock input.
    input logic DAB_WSTB_N,              // DAB WSTB/: register-file write enable.
    input logic RA0_N, RA1_N, WA0_N, WA1_N,
    input logic S0, S1, M0_N, M1_N, CSIGN_N, ZERO_N,
    input logic XFER_CK, RDRREG_N,        // XFER CK, RDRREG/.
    output backplane_signals::logic_level SAT,
    dab_bus.aru DAB
);
    timeunit 1ps;
    timeprecision 1ps;
    import backplane_signals::*;
    localparam time nanosecond = 576;

    typedef struct packed {logic [19:0] value, known;} arithmetic_word;
    localparam arithmetic_word zero_sum = '{value: 0, known: '1};

    // Simulator initialization is separate from the connector. The original
    // ARU has no RESET/ input. A generation rejects publications left pending
    // by a previous fixture/machine initialization.
    // The annotation lets the compiled evaluator detect a host task changing
    // this field between eval() calls; it does not add a modeled wire.
    int unsigned initialization_generation /* verilator public_flat_rw */ = 0;
    int unsigned storage_generation = '1;

    // REGISTER FILE — U29..U32, four words with independent read/write ports.
    // U54 inverts the connector selects; the LS670 A/B pins cross their order.
    bus_word register_file [4];
    wire [1:0] read_address = {~RA0_N, ~RA1_N};
    wire [1:0] write_address = {~WA0_N, ~WA1_N};
    bus_word F;
    assign F = register_file[read_address];

    typedef struct packed {
        bus_word sample;
        logic [1:0] address;
        time capture_time;
        int unsigned serial;
        bit occupied;
    } register_capture;
    register_capture register_captures [4];
    int unsigned capture_serial = 0;
    int unsigned capture_slot = 0;
    int unsigned pending_writes = 0;

    task automatic publish_register(input int slot, generation, serial);
        #(25 * nanosecond);
        if (generation == initialization_generation &&
            register_captures[slot].occupied &&
            register_captures[slot].serial == serial) begin
            register_file[register_captures[slot].address] =
                register_captures[slot].sample;
            register_captures[slot].occupied = 0;
            pending_writes--;
        end
    endtask

    // A change during the transparent write window captures another sample.
    // Replacing a capture at the same time updates its pending value. Closing
    // the window does not cancel samples already travelling to the registers.
    always @(DAB.sample or DAB_WSTB_N) begin : capture_dab
        if (!DAB_WSTB_N) begin
            if (pending_writes != 0 && register_captures[capture_slot].occupied &&
                register_captures[capture_slot].capture_time == $time) begin
                register_captures[capture_slot].sample = DAB.sample;
            end else begin
                assert (pending_writes < 4) else $fatal(1, "ARU register captures full");
                capture_slot = (capture_slot + 1) % 4;
                capture_serial++;
                register_captures[capture_slot] = '{sample: DAB.sample,
                    address: write_address, capture_time: $time,
                    serial: capture_serial, occupied: 1};
                pending_writes++;
                fork
                    publish_register(capture_slot, initialization_generation, capture_serial);
                join_none
            end
        end
    end

    // DUAL RANK SHIFT REGISTER — six LS194 packages, with interleaved ranks.
    // Bits 19:0 are SR0..SR19. Four other package outputs retain state too.
    logic [23:0] operand_bits, operand_unknown;
    wire [19:0] SR = operand_bits[19:0];

    function automatic logic [23:0] right_shift_wires(input logic [23:0] bits);
        return {bits[22], bits[1], bits[20], bits[0], bits[19:18], bits[19:2]};
    endfunction

    function automatic logic [23:0] left_shift_wires(
        input logic [23:0] bits, input logic serial_bit
    );
        // Each gap is a package's pulled-high serial input. The same routing
        // moves unknown bits, with zero inserted because a pull-up is known.
        return {serial_bit, bits[23], serial_bit, bits[21],
                bits[17:12], {2{serial_bit}},
                bits[9:4], {2{serial_bit}}, bits[1:0], bits[22], bits[20]};
    endfunction

    // PARTIAL PRODUCT REGISTER — U10, U11, U12.
    // M1/ selects SR; M0/ selects SR shifted by one. NAND inputs make the
    // stored sum inverted. Unknown carry propagates through four-bit adders.
    arithmetic_word partial_product, product_sum;
    always_comb begin : form_partial_product
        logic [19:0] direct_term, shifted_term, missing_bits;
        logic carry_known;
        direct_term = M1_N ? 20'b0 : SR;
        shifted_term = M0_N ? 20'b0 : {SR[19], SR[19:1]};
        missing_bits = (M1_N ? 20'b0 : operand_unknown[19:0]) |
            (M0_N ? 20'b0 : {operand_unknown[19], operand_unknown[19:1]});

        carry_known = 1;
        product_sum.known = 0;
        for (int nibble = 0; nibble < 5; nibble++) begin
            carry_known &= ~|missing_bits[nibble*4 +: 4];
            product_sum.known[nibble*4 +: 4] = {4{carry_known}};
        end
        product_sum.value = ~(direct_term + shifted_term) & product_sum.known;
    end

    // ACCUMULATOR — U19..U23 add; U33..U37 select saturation; U45..U49 store AC.
    // CSIGN/ chooses the stored inverted product plus carry, or its complement.
    // PP0..PP19 is the saturated sum available to AC and the result register.
    arithmetic_word AC, PP;
    always_comb begin : add_and_saturate
        logic [19:0] product_addend, sum_bits;
        product_addend = CSIGN_N ? partial_product.value : ~partial_product.value;
        sum_bits = AC.value + product_addend + (CSIGN_N ? 20'd1 : 20'd0);
        PP = '0;
        SAT = unknown;
        if (&AC.known && &partial_product.known) begin
            SAT = (sum_bits[19] == sum_bits[18]) ? low : high;
            PP.known = '1;
            PP.value = (sum_bits[19] == sum_bits[18]) ? sum_bits :
                product_addend[19] ? {2'b11, 18'b0} : {2'b00, {18{1'b1}}};
        end
    end

    // A delta-cycle handoff lets register publications at this instant settle
    // before the arithmetic reads F. It adds no elapsed machine time. Only
    // this local handoff imposes the inherited register-before-clock priority.
    logic arithmetic_edge = 0;
    bit arithmetic_edge_pending = 0;
    int unsigned pipeline_generation = '1;
    always @(negedge ARUCKE_N) begin
        arithmetic_edge_pending = 1;
        arithmetic_edge <= ~arithmetic_edge;
    end

    always @(arithmetic_edge or initialization_generation) begin : clock_pipeline
        logic [23:0] shifted_bits, shifted_unknown;
        logic [15:0] operand_word, operand_missing;
        if (pipeline_generation != initialization_generation) begin
            pipeline_generation = initialization_generation;
            operand_bits <= 0;
            operand_unknown <= 0;
            partial_product <= zero_sum;
            AC <= zero_sum;
        end else if (arithmetic_edge_pending) begin
            arithmetic_edge_pending = 0;
            operand_word = F.value & F.known;
            // Preserve the existing LS670 input reduction: disconnected data
            // reads low; unresolved driven bits remain unknown.
            operand_missing = ~(F.known | F.high_z);
            shifted_bits = operand_bits;
            shifted_unknown = operand_unknown;

            case ({S1, S0})
                2'b00: ; // Retain the operand.
                2'b01: begin
                    shifted_bits = right_shift_wires(operand_bits);
                    shifted_unknown = right_shift_wires(operand_unknown);
                end
                2'b10: begin
                    shifted_bits = left_shift_wires(operand_bits, 1'b1);
                    shifted_unknown = left_shift_wires(operand_unknown, 1'b0);
                end
                2'b11: begin
                    shifted_bits = {4'hf, operand_word[15], operand_word, 3'b000};
                    shifted_unknown = {4'h0, operand_missing[15], operand_missing, 3'b000};
                end
            endcase

            operand_bits <= shifted_bits & ~shifted_unknown;
            operand_unknown <= shifted_unknown;
            partial_product <= product_sum;
            AC <= ZERO_N ? PP : zero_sum;
        end
    end

    // RESULT REGISTER — U43/U44. Capture PP[18:3]; keep that sample while PP
    // changes. The native delay publishes it eight nanoseconds later.
    bus_word result_register;
    int unsigned pending_results = 0;
    task automatic publish_result(input arithmetic_word sample, input int generation);
        #(8 * nanosecond);
        if (generation == initialization_generation) begin
            result_register = '{value: sample.value[18:3] & sample.known[18:3],
                known: (&sample.known[18:3] ? 16'hffff : 16'h0000), high_z: 0};
            pending_results--;
        end
    endtask

    always @(posedge XFER_CK) begin
        pending_results++;
        fork
            publish_result(PP, initialization_generation);
        join_none
    end

    assign DAB.aru_drive = RDRREG_N ? released_bus : result_register;

    task initialize();
        initialization_generation++;
    endtask

    // Run initialization through a process so all connected observations see
    // the changed storage after a host initialization call and eval().
    always @(initialization_generation) begin : initialize_storage
        if (storage_generation != initialization_generation) begin
            storage_generation = initialization_generation;
            foreach (register_file[index]) begin
                register_file[index] = zero_word;
                register_captures[index] = '0;
            end
            capture_slot = 0;
            capture_serial = 0;
            pending_writes = 0;
            pending_results = 0;
            arithmetic_edge_pending = 0;
            result_register = '{value: 0, known: 0, high_z: 0};
        end
    end

    initial initialize();
endmodule
