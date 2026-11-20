#pragma once
// Part:    none (units): the host's time. The two crystals, the edges inside
//          a CPU state and inside a DSP row, and a register whose new value
//          becomes visible later.
// Mirrors: the timing parameters of ../board-level-verilog (sbc.sv, tc.sv).
//
// One tick is 1/576 ns. The SBC's crystal (18.432 MHz) and the DSP's master
// clock (30.72 MHz) are exactly 3 : 5, and this tick makes both periods whole
// numbers. That is by design: the T&C's phase-lock loop multiplies the SBC's
// clock 02/ (2.048 MHz = 18.432 / 9) by 15 (224X Service Manual 3.5). Three
// 8080 states take exactly as long as five DSP steps (1.465 us).
//
// Words: the manual's "100-step control program" runs one microinstruction
// per step, and a step lasts one "system clock time" (293 ns). This code
// calls a step a row (one row of the WCS). One pass through the program is
// the manual's "sample time" (100 steps; 34.13 kHz).
// Delays come from the board-level machine (../board-level-verilog: sbc.sv,
// tc.sv, dmem.sv, fpc.sv), whose row timings are the board reductions in
// the board-level machine's timing constants (../board-level-verilog/tc.sv); each constant names its source.
#include <cstdint>

// The per-row hooks the scheduler's whole-row path calls: inlined into it
// whatever the compiler's own estimate, so each row is one straight run.
#if defined(_MSC_VER) && !defined(__clang__)
#define LEXICON_ALWAYS_INLINE __forceinline
#else
#define LEXICON_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

namespace lexicon224x::cpu {

using Tick = uint64_t;
constexpr Tick ns = 576;
constexpr Tick osc = 31250;                 //  54.25 ns: SBC crystal, 18.432 MHz
constexpr Tick cpu_period = 9 * osc;        // 488.28 ns: one 8080 state, one period of 02/ (the 8224 divides by 9)
constexpr Tick master = 18750;              //  32.55 ns: the master clock MC, 30.72 MHz (the T&C's PLL: 02/ x 15)
constexpr Tick row = 9 * master;            // 292.97 ns: one step: the nine time slots MS0-MS8
static_assert(3 * cpu_period == 5 * row, "the two crystals are 3 : 5");

// Inside one 8080 state, from the 8224 (sbc.sv): phi1 at its start, then
constexpr Tick phi2_rise = 2 * osc;         // SYNC and DBIN rise; the SBC samples read data
constexpr Tick ready_sample = 3 * osc;      // READY (the board's XACK/) is sampled

// The start of CPU state number `state` (the host's `cycles` counts them).
inline Tick state_start(uint64_t state) {
    return state * cpu_period;
}

// The start of the state n states after a bus cycle's T1 (T2 = 1; T3 = 2 without wait states).
inline Tick state_after(Tick t1, unsigned n) {
    return t1 + n * cpu_period;
}

// Inside one DSP row, from its marker (tc.sv; the board-level machine's names in brackets):
constexpr Tick first_marker = 207531;                   // 360.30 ns after power-up: row 0's marker
constexpr Tick write_close = 2 * master + 22 * ns / 2 - 37 * ns / 2;   //  57.60 ns: register-file write window closes
constexpr Tick execute_offset = write_close + 1;        //  57.61 ns: execute() of the previous row, just after it
constexpr Tick fetch_offset = 58842;                    // 102.16 ns: microinstruction register loads [typicalInstructionFetch]
constexpr Tick reset_decode_offset = fetch_offset + 9 * ns;    // RESET_N follows the new microinstruction
constexpr Tick xreg_capture_offset = 53340;             //  92.60 ns: WR_XREG/ falls (tc.sv capture_xreg)
constexpr Tick converter_offset = 162384 - 32 * ns;     // 249.92 ns: FPC converter clock [converterClockOffset]
constexpr Tick dac_observed = converter_offset + 60 * ns;      // 309.92 ns: a capture reaches the outputs [audioOutputDelay]

// A row's three ARU clock edges (ARUCK 0, 1, 2) are three master periods
// apart. A microinstruction's multiplicand loads at edge 1 of the row after its
// fetch and shifts at edge 2 and at the following row's edge 0.
inline Tick aruck_edge(Tick row_marker, unsigned n) {
    return row_marker + n * 3 * master;
}

inline Tick marker(uint64_t row_number) {
    return first_marker + row_number * row;
}

// ---- Row timing, per model ------------------------------------------------------
// The constants above are the 224X's. The original 224's T&C (060-01317) has
// the same nine time slots, ARUCK at slots 0, 3 and 6 and the same strobe
// derivations, but its PLL locks MS4/ to 02/ one-to-one: the row rate IS the
// SBC's clock (2.048 MHz, one row per 8080 state), and the slot clock is
// 9 x 02/ = the SBC crystal itself (224 T&C drawing 060-01317).
// So every time within a row scales by 5/3. The gate delays inside those
// offsets are not the 224's own (no 224 board-level model exists); its
// diagnostics are the check.
struct RowTiming {
    Tick master, row, first_marker;
    Tick execute_offset, fetch_offset, reset_decode_offset, xreg_capture_offset, converter_offset, dac_observed;

    Tick marker(uint64_t row_number) const {
        return first_marker + row_number * row;
    }

    Tick aruck_edge(Tick row_marker, unsigned n) const {
        return row_marker + n * 3 * master;
    }
};

constexpr RowTiming timing_224x{master, row, first_marker, execute_offset, fetch_offset, reset_decode_offset,
                                xreg_capture_offset, converter_offset, dac_observed};

constexpr Tick slower_224(Tick t) {
    return t * 5 / 3;
}

constexpr RowTiming timing_224{slower_224(master), slower_224(row), slower_224(first_marker),
                               slower_224(execute_offset), slower_224(fetch_offset), slower_224(reset_decode_offset),
                               slower_224(xreg_capture_offset), slower_224(converter_offset), slower_224(dac_observed)};
static_assert(timing_224.master == osc && timing_224.row == cpu_period, "the 224's slot clock is the SBC crystal");

// The first edge of a clock (edges at first + n * period) at or after `minimum`.
inline Tick periodic_edge(Tick first, Tick period, Tick minimum) {
    if (minimum <= first) {
        return first;
    }
    return first + (minimum - first + period - 1) / period * period;
}

// A register the CPU can read, whose new value becomes visible at `at`.
struct DelayedRegister {
    uint16_t before = 0, after = 0;
    Tick at = 0;

    uint16_t read(Tick t) const {
        if (t >= at) {
            return after;
        }
        return before;
    }

    void set(uint16_t value, Tick now, Tick when) {
        before = read(now);
        after = value;
        at = when;
    }
};

}  // namespace lexicon224x::cpu
