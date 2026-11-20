#pragma once
// Part:    FPC sheet 2, the two headroom registers (the CPU's DSP input ports 8, 9).
// Mirrors: ../board-level-verilog/fpc.sv.
// Inputs:  on_read_released (the CPU's read strobe HR1/ or HR2/ ends);
//          sample (a CH1 edge clocks a register from the level detectors).
// Outputs: byte() (what the CPU reads).
//
// Each register holds the peak level its channel's detectors have seen;
// reading it restarts it.
#include "scheduler.hpp"

namespace lexicon224x::cpu {

class FpcHeadroom {
public:
    explicit FpcHeadroom(Scheduler &scheduler) : scheduler(scheduler) {}

    // ---- Inputs ----

    // `released`: when the CPU's headroom read strobe (HR1/ or HR2/) ends.
    void on_read_released(unsigned channel, Tick released) {
        // With the level detectors idle (pins high) the register re-initializes
        // to 0x1f: the initialization starts 35 ns later and stores 20 ns after
        // that (fpc.sv initialize_headroom, store_headroom).
        static constexpr Tick initialize_delay = 35 * ns;
        static constexpr Tick store_delay = 20 * ns;
        scheduler.at(released + initialize_delay + store_delay, [this, channel] {
            peak[channel] = 0x1f;
        });
    }

    // At an edge of CH1 the AIN's level detectors (five comparators, 6 dB
    // apart; bit 0 = 0.28 V ... bit 4 = 5 V, the onset of ADC clipping) clock
    // one register. "Peak detection occurs by clearing a register bit any
    // time the corresponding headroom bit is asserted" (224X Service Manual
    // 3.8): the register keeps the complement of the loudest level since the
    // CPU last read it. `asserted`: bit k set = comparator k exceeded.
    void sample(unsigned channel, unsigned asserted) {
        peak[channel] = uint8_t(peak[channel] & ~asserted & 0x1f);
    }

    // ---- Outputs ----

    // The register itself: bit k clear = comparator k exceeded since the last read.
    uint8_t held(unsigned channel) const {
        return peak[channel];
    }

    // The byte the CPU reads (before the SBC's bus inversion).
    uint8_t byte(unsigned channel) const {
        uint8_t p = peak[channel];
        uint8_t reversed = uint8_t((p & 1) << 4 | (p & 2) << 2 | (p & 4) | (p & 8) >> 2 | (p & 16) >> 4);
        return uint8_t(0xe0 | reversed);
    }

private:
    Scheduler &scheduler;
    // The reference machines start at 0x1f: a strobe edge at power-up runs the
    // registers' initialization before the CPU first reads them. (Real power-up
    // contents are unknown.)
    uint8_t peak[2] = {0x1f, 0x1f};
};

}  // namespace lexicon224x::cpu
