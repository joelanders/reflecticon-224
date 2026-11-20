#pragma once
// Part:    T&C diagnostic hardware, the CPU's DSP input ports 3-5: "three
//          groups of eight timing control signals ... read by the SBC module
//          via tristate bus drivers, U6, or registers U7 and U8" (224X
//          Service Manual 3.5).
// Mirrors: ../board-level-verilog/tc.sv (diagnostic_captures,
//          sample_arithmetic_monitor).
// Inputs:  at_marker (each row's marker: the microinstruction latched there,
//          and the sign the ARU adds with this row); at_arithmetic (the
//          controls the T&C drove at the row's three ARU clocks, and SAT).
// Outputs: arithmetic_monitor() (DPORT3), microinstruction_monitor() (DPORT4),
//          timing_monitor() (DPORT5).
//
// - DPORT3, the arithmetic monitor (register U8): the serial-to-parallel
//   shift register U9 over the ARU's bit streams S0, S1, M0/, M1/ (through
//   the exclusive-or gates U12), and SAT, OR'ed with a one-shot because SAT
//   can be transient (~186 ms here);
// - DPORT4, the microinstruction monitor (tristate drivers U6): fields of the
//   microinstruction at the marker;
// - DPORT5, the timing monitor (register U7): its bus and DAC controls.
// U7 and U8 hold dynamic signals, so they are sampled at set times (below).
// The firmware's power-up diagnostics read them.
#include "../isa-level-cpp/lexicon224x.hpp"
#include "scheduler.hpp"
#include "trace.hpp"
#include <array>

namespace lexicon224x::cpu {

// A byte from its eight bits, bit 0 first (as backplane.sv's pack_byte).
inline uint8_t pack_byte(const std::array<bool, 8> &bits_from_0) {
    uint8_t byte = 0;
    for (unsigned n = 0; n < 8; n++) {
        if (bits_from_0[n]) {
            byte |= uint8_t(1u << n);
        }
    }
    return byte;
}

class TcDiagnostics {
public:
    explicit TcDiagnostics(const Scheduler &scheduler) : scheduler(scheduler) {}

    Model model = Model::Lexicon224X;    // which T&C board: how its microinstruction is laid out

    // ---- Inputs ----

    // At a row marker: the microinstruction latched there (fetched in the
    // previous row), and the sign latch (U20): the sign of the multiply whose
    // partial products the ARU adds this row, one microinstruction older.
    LEXICON_ALWAYS_INLINE void at_marker(uint32_t marker_microinstruction, bool sign_latch) {
        this->marker_microinstruction = marker_microinstruction;
        dport4 = {marker_microinstruction, sign_latch, false, false, 0, true};
        dport5.set({marker_microinstruction, false, false, false, 0, true}, scheduler.now(),
                   scheduler.now() + dport5_visible);
        if (trace) {
            Tick row_marker = scheduler.now();
            trace->dport(row_marker, DportEvent::MicroinstructionShown);
            for (unsigned edge = 0; edge < 3; edge++) {
                trace->aruck(scheduler.timing.aruck_edge(row_marker, edge), edge);
                trace->history_sample(scheduler.timing.aruck_edge(row_marker, edge) + aruck_sample_delay, edge);
            }
            trace->dport(row_marker + dport5_visible, DportEvent::TimingVisible);
            trace->dport(row_marker + dport3_sample, DportEvent::ArithmeticSample);
            trace->dport(row_marker + dport3_visible, DportEvent::ArithmeticVisible);
        }
    }

    // After the row's ARU clocks: what the T&C drove at each (`clocked`) and
    // the ARU's SAT at each. U9 samples them 25 ns after each clock.
    LEXICON_ALWAYS_INLINE void at_arithmetic(const Aruck (&clocked)[3], unsigned saturated, Tick row_marker) {
        if (saturated == 0) {
            // No SAT at any edge (nearly every row): the one-shot cannot
            // trigger, and U9's contents after each edge follow from this
            // row's edges alone, exactly what the three sample_arithmetic
            // calls below would leave.
            // What each edge shifted into U9 (sample_arithmetic's bits).
            bool s0_xor_s1[3];
            bool m0_xor_m1[3];
            for (unsigned edge = 0; edge < 3; edge++) {
                s0_xor_s1[edge] = clocked[edge].operand == OperandClock::Shift;
                m0_xor_m1[edge] = bit(clocked[edge].pair, 1) != bit(clocked[edge].pair, 0);
            }
            uint8_t after_edge1 = uint8_t(s0_xor_s1[1] | m0_xor_m1[0] << 1 | s0_xor_s1[0] << 2 | m0_xor_m1[1] << 3);
            bool saturation = pulse_active(row_marker + dport3_sample);
            bool held = clocked[1].operand == OperandClock::Hold && clocked[2].operand == OperandClock::Hold;
            dport3.set({marker_microinstruction, false, saturation, held, after_edge1, true}, scheduler.now(),
                       row_marker + dport3_visible);
            aru_history = uint8_t(s0_xor_s1[2] | m0_xor_m1[1] << 1 | s0_xor_s1[1] << 2 | m0_xor_m1[2] << 3);
            last_sat_sample = false;
            return;
        }
        sample_arithmetic(clocked[0], bit(saturated, 0), row_marker, 0);
        sample_arithmetic(clocked[1], bit(saturated, 1), row_marker, 1);
        sample_dport3(clocked, row_marker);
        sample_arithmetic(clocked[2], bit(saturated, 2), row_marker, 2);
    }

    // ---- Outputs ----

    uint8_t arithmetic_monitor() const {
        return pack_dport3(dport3.read(scheduler.now()));
    }

    uint8_t microinstruction_monitor() const {
        return pack_dport4(dport4);
    }

    uint8_t timing_monitor() const {
        return pack_dport5(dport5.read(scheduler.now()));
    }

    // ---- Probe ----

    void set_trace(Trace *probe) {
        trace = probe;
    }

private:
    // When the registers sample and publish, from the row marker (tc.sv
    // diagnostic_captures). The order within a row: ARUCK edge 0's sample,
    // DPORT5, edge 1's sample, DPORT3's sample, DPORT3 visible, edge 2's sample.
    static constexpr Tick aruck_sample_delay = 25 * ns;     // each ARUCK edge's history sample
    static constexpr Tick dport5_visible = 47580;           //  82.60 ns
    static constexpr Tick dport3_sample = 87702;            // 152.26 ns
    static constexpr Tick dport3_visible = 103830;          // 180.26 ns
    // The SAT one-shot (tc.sv raise_/end_saturation_pulse): a new SAT sample
    // raises a pulse 35 ns later that ends 186.12 ms after the sample; it can
    // retrigger 1.034 ms after the last trigger. Continuous SAT does not
    // retrigger it; the plain SAT sample is OR'ed in (224X Service Manual 3.5).
    static constexpr Tick sat_pulse_rise = 35 * ns;
    static constexpr Tick sat_pulse_end = 186120035 * ns;
    static constexpr Tick sat_retrigger = 1034000 * ns;

    bool pulse_active(Tick t) const {
        return pulse_end && t >= pulse_start && t < pulse_end;
    }

    // What a register holds: the inputs its byte is made from, not the byte.
    // The firmware reads the ports ~10 times a second (DPORT4 and DPORT5 only
    // in its power-up diagnostics) against 3.4 M rows, so each row keeps the
    // inputs and a read decodes and packs them. `set` false: never loaded,
    // reads 0 (as the registers before the first row).
    struct Inputs {
        uint32_t word = 0;          // the microinstruction at the marker
        bool sign_latch = false;    // DPORT4
        bool saturation = false;    // DPORT3: SAT, or the one-shot's pulse
        bool held = false;          // DPORT3: held at both ARU clocks 1 and 2
        uint8_t aru_history = 0;    // DPORT3: U9 after ARU clocks 0 and 1
        bool set = false;
    };

    // DelayedRegister (timing.hpp) for Inputs: `before` until `at`, then `after`.
    struct DelayedInputs {
        Inputs before, after;
        Tick at = 0;

        const Inputs &read(Tick t) const {
            return t >= at ? after : before;
        }

        void set(const Inputs &value, Tick now, Tick when) {
            before = read(now);
            after = value;
            at = when;
        }
    };

    uint8_t pack_dport4(const Inputs &in) const {
        if (!in.set) {
            return 0;
        }
        Microinstruction decoded = decode(in.word, model);
        // Bit 7: the 224X shows the protect bit; the 224 (drawing 060-01317,
        // U29; 224 Service Manual port table) shows RESET there.
        bool bit7 = decoded.protect;
        if (model == Model::Lexicon224) {
            bit7 = decoded.reset;
        }
        return pack_byte({
            bit(decoded.wa, 0), bit(decoded.wa, 1),             // 0-1  WA
            bit(decoded.ra, 0), bit(decoded.ra, 1),             // 2-3  RA
            in.sign_latch,                                      // 4    the sign the ARU adds with
            !(decoded.op == OPER && decoded.source == FromADC), // 5    RD_AD/
            decoded.op != MEMW,                                 // 6    MEMW/
            bit7,                                               // 7    protect bit (224X), RESET (224)
        });
    }

    uint8_t pack_dport5(const Inputs &in) const {
        if (!in.set) {
            return 0;
        }
        uint32_t word = in.word;
        bool oper = decode(word, model).op == OPER;
        return pack_byte({
            bit(word, 11),                                   // 0  DAC select A
            bit(word, 10),                                   // 1  DAC select B
            bit(word, 9),                                    // 2  DAC select C
            bit(word, 8),                                    // 3  DAC select D
            !(oper && bit(word, 7)),                         // 4  WR_DA/
            !(oper && bit(word, 6)),                         // 5  WR_XREG/
            !(oper && bits(word, 13, 12) == 2),              // 6  RD_XREG/
            !(decode(word, model).op == MEMW || (oper && bits(word, 13, 12) == 1)),  // 7  RD_RREG/ = NAND(MEMW/ . 2Y1/)
        });
    }

    // aru_history as sampled (after ARU clocks 0 and 1): bit 0 = S0^S1 at
    // clock 1, bit 1 = M0^M1 at clock 0, bit 2 = S0^S1 at clock 0, bit 3 =
    // M0^M1 at clock 1.
    uint8_t pack_dport3(const Inputs &in) const {
        if (!in.set) {
            return 0;
        }
        return pack_byte({
            in.saturation,                                   // 0  SAT, or the one-shot's pulse
            !in.held,                                        // 1  not held at both ARU clocks 1 and 2
            bit(in.aru_history, 0),                          // 2  S0^S1 at clock 1
            bit(in.aru_history, 2),                          // 3  S0^S1 at clock 0
            bit(in.aru_history, 1),                          // 4  M0^M1 at clock 0
            bit(in.aru_history, 3),                          // 5  M0^M1 at clock 1
            bit(in.word, 30) != bit(in.word, 31),            // 6  c4^c5 of the microinstruction at the marker
            !decode(in.word, model).zero,                    // 7  ZERO/
        });
    }

    // Each ARU clock shifts {S0^S1, M0^M1, SAT} into U9 (through the XORs U12):
    // S0^S1 is 1 when the operand register shifts (0 when it loads or holds),
    // M0^M1 when exactly one of the two coefficient bits is set.
    void sample_arithmetic(const Aruck &clock, bool sat, Tick row_marker, unsigned edge) {
        bool s0_xor_s1 = clock.operand == OperandClock::Shift;
        bool m0_xor_m1 = bit(clock.pair, 1) != bit(clock.pair, 0);
        bool previous_sat = bit(aru_history, 4);
        aru_history = uint8_t(s0_xor_s1 | bit(aru_history, 3) << 1 | bit(aru_history, 0) << 2 | m0_xor_m1 << 3 | sat << 4);
        Tick sample_time = scheduler.timing.aruck_edge(row_marker, edge) + aruck_sample_delay;
        if (!previous_sat && sat && sample_time >= retrigger_time) {
            retrigger_time = sample_time + sat_retrigger;
            if (!pulse_active(sample_time + sat_pulse_rise)) {
                pulse_start = sample_time + sat_pulse_rise;
            }
            pulse_end = sample_time + sat_pulse_end;
        }
        last_sat_sample = sat;
    }

    void sample_dport3(const Aruck (&clocked)[3], Tick row_marker) {
        bool saturation = last_sat_sample || pulse_active(row_marker + dport3_sample);
        bool held = clocked[1].operand == OperandClock::Hold && clocked[2].operand == OperandClock::Hold;
        dport3.set({marker_microinstruction, false, saturation, held, aru_history, true}, scheduler.now(),
                   row_marker + dport3_visible);
    }

    const Scheduler &scheduler;
    uint32_t marker_microinstruction = 0;
    DelayedInputs dport3;       // the arithmetic monitor [arithmeticMonitor]
    Inputs dport4;              // the microinstruction monitor [instructionMonitor]
    DelayedInputs dport5;       // the timing monitor [timingMonitor]
    uint8_t aru_history = 0;    // shift register U9: the ARU bit-stream history DPORT3 shows
    bool last_sat_sample = false;
    Tick retrigger_time = first_marker, pulse_start = 0, pulse_end = 0;
    Trace *trace = nullptr;
};

}  // namespace lexicon224x::cpu
