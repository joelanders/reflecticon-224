#pragma once
// Part:    DMEM board, sheet 2: what the CPU reaches through its DSP output ports.
// Mirrors: ../board-level-verilog/dmem.sv.
// Inputs:  on_port_strobe, on_port_release (a CPU OUT to ports 0-7: the
//          decoded write strobe's two edges), on_reset_n (the T&C's
//          RESET_N), on_wr_xreg (the DSP's WR_XREG/), at_execute (before an
//          microinstruction's memory access).
// Outputs: run_level_high() (HALT/: the DSP's program counter runs), the XREG
//          words both ways, the bus test register, the count of RESET
//          edges, (through at_execute) the current position counter's
//          clear, and XACK/ for the CPU's I/O commands (command_to_xack).
//
// In the 224X Service Manual's words (3.6), DMEM holds "the XREG (DMEM
// transfer register), diagnostic ports, and the 8080 port-decoding circuitry":
// - the single cycle/halt/run control latches (NAND gates U53, U54);
// - RESET: DMEM counts the T&C's RESET_N falling edges;
// - port 5 clears the current position counter (U51, U65);
// - the X register (U38-U41), between the SBC and the DAB;
// - the bus test register (U42), which lets the SBC read back its own data
//   bus (the "8080 bus test register" of diagnostic E32);
// - the open-collector XACK/ (U52) after an I/O access.
#include "../isa-level-cpp/lexicon224x.hpp"
#include "scheduler.hpp"
#include "../sbc/memory_map.hpp"
#include <optional>
#include <stdexcept>
#include <string>

// The SBC's memory map and ports are shared with the board-level machine.
namespace lexicon224x::cpu { using namespace lexicon224x::sbc; }

namespace lexicon224x::cpu {

// A three-valued latch output: the run/halt NAND latches power up unknown.
enum class Level { Low, High, Unknown };

inline Level nand(Level a, Level b) {
    if (a == Level::Low || b == Level::Low) {
        return Level::High;
    }
    if (a == Level::High && b == Level::High) {
        return Level::Low;
    }
    return Level::Unknown;
}

class Dmem {
public:
    // DMEM's delays (dmem.sv):
    static constexpr Tick port_decode_delay = 20 * ns;      // CPU strobes for ports 0-9 (also the headroom reads)
    static constexpr Tick clear_inverter_delay = 3 * ns;    // clear_counter follows port 5's write strobe
    static constexpr Tick counter_clear_delay = 24 * ns;    // ... and clears the position counter
    static constexpr Tick xreg_latch_delay = 8 * ns;        // an XREG input byte latches after its strobe ends
    static constexpr Tick reset_inverter_delay = 15 * ns;   // RESET follows the T&C's RESET_N
    // XACK/ for an I/O command to ports 0-9: command_active follows IORC/ or
    // IOWC/ (15 ns), and the acknowledge follows command_active (15 ns).
    static constexpr Tick command_to_xack = 15 * ns + 15 * ns;

    explicit Dmem(Scheduler &scheduler) : scheduler(scheduler) {}

    // ---- Inputs ----

    // The T&C drives RESET_N (low on an OPER RESET row).
    LEXICON_ALWAYS_INLINE void on_reset_n(bool low) {
        if (low && reset_n) {
            reset_edges++;
        }
        if (low != !reset_n) {
            scheduler.at(scheduler.now() + reset_inverter_delay, [this, low] {
                reset = low;
                settle_latches();
            });
        }
        reset_n = !low;
    }

    // A CPU OUT to DSP port 0-7: the decoded write strobe falls at `fall`.
    void on_port_strobe(unsigned port, Tick fall) {
        switch (port) {
            case SelectSingleStep:
            case SelectContinuous:
            case HaltDsp:
            case RunDsp:
                scheduler.at(fall, [this, port] {
                    strobe[port] = true;
                    settle_latches();
                });
                break;
            case ClearDelayCounter:
                // Active from here until the strobe's release says otherwise.
                scheduler.at(fall + clear_inverter_delay, [this, fall] {
                    clear_active_from = fall + clear_inverter_delay;
                    clear_active_until = never;
                    cpc_cleared_at = fall + clear_inverter_delay + counter_clear_delay;
                });
                break;
            case XregLowByte:
            case XregHighByte:
                break;      // the byte latches at the strobe's end
            default:
                throw std::runtime_error("Unconnected output port " + std::to_string(port));
        }
    }

    // ... and rises at `rise`; `value` is the byte on DAT0/..DAT7/.
    void on_port_release(unsigned port, uint8_t value, Tick rise) {
        switch (port) {
            case SelectSingleStep:
            case SelectContinuous:
            case HaltDsp:
            case RunDsp:
                scheduler.at(rise, [this, port] {
                    strobe[port] = false;
                    settle_latches();
                });
                break;
            case ClearDelayCounter:
                scheduler.at(rise + clear_inverter_delay, [this, rise] {
                    clear_active_until = rise + clear_inverter_delay;
                });
                break;
            case XregLowByte:
                scheduler.at(rise + xreg_latch_delay, [this, value] {
                    xreg_input = uint16_t((xreg_input & 0xff00) | value);
                });
                break;
            case XregHighByte:
                scheduler.at(rise + xreg_latch_delay, [this, value] {
                    xreg_input = uint16_t((xreg_input & 0x00ff) | value << 8);
                    bus_test = value;
                });
                break;
            default:
                throw std::runtime_error("Unconnected output port " + std::to_string(port));
        }
    }

    // Before a microinstruction's memory access at `next_marker` (already past):
    // a clear that has happened zeroes the counter, and one still active
    // then stops it advancing.
    LEXICON_ALWAYS_INLINE void at_execute(Tick next_marker, Machine &dsp) {
        if (cpc_cleared_at && *cpc_cleared_at <= next_marker) {
            dsp.cpc = 0;
            cpc_cleared_at.reset();
        }
        bool clearing = clear_active_from && *clear_active_from <= next_marker && next_marker < clear_active_until;
        if (clearing) {
            dsp.reset_pulse = false;
        }
    }

    // WR_XREG/: the DSP's word for the CPU, visible from `visible_at`.
    void on_wr_xreg(uint16_t value, Tick visible_at) {
        xreg_output.set(value, scheduler.now(), visible_at);
    }

    // ---- Outputs ----

    // HALT/ high: the DSP program counter runs.
    bool run_level_high() const {
        return halt_n == Level::High;
    }

    uint64_t reset_edge_count() const {
        return reset_edges;
    }

    // XREG: the CPU's word for the DSP (ports 6, 7) and the DSP's word for
    // the CPU (ports 6, 7). The bus test register (port 2) holds the last
    // byte the CPU wrote to port 7, as the board-level machine models it.
    uint16_t xreg_to_dsp() const {
        return xreg_input;
    }

    uint8_t bus_test_register() const {
        return uint8_t(bus_test);
    }

    uint16_t xreg_to_cpu() const {
        return xreg_output.read(scheduler.now());
    }

private:
    // Cross-coupled NANDs, level sensitive, powering up unknown. strobe[p] is
    // true while the CPU's decoded write strobe for port p is low: 0 select
    // single step, 1 select continuous, 2 halt, 3 run (DspWritePort).
    //   SINGLE = !(!strobe0 & CONTINUOUS)   CONTINUOUS = !(SINGLE & !strobe1)
    //   reset_gate = !(SINGLE & RESET)      halt_gate = !(reset_gate & !strobe2 & HALT/)
    //   HALT/ = !(halt_gate & !strobe3)
    static Level level(bool high) {
        if (high) {
            return Level::High;
        }
        return Level::Low;
    }

    void settle_latches() {
        for (int pass = 0; pass < 8; pass++) {
            Level next_single = nand(level(!strobe[0]), continuous);
            Level next_continuous = nand(single, level(!strobe[1]));
            Level reset_gate = nand(single, level(reset));

            // The three-input AND inside halt_gate: reset_gate & !strobe2 & HALT/.
            Level halt_gate_inputs;
            if (reset_gate == Level::Low || strobe[2]) {
                halt_gate_inputs = Level::Low;
            } else if (reset_gate == Level::High) {
                halt_gate_inputs = halt_n;
            } else if (halt_n == Level::Low) {
                halt_gate_inputs = Level::Low;
            } else {
                halt_gate_inputs = Level::Unknown;
            }

            Level next_halt_gate = nand(halt_gate_inputs, Level::High);
            Level next_halt_n = nand(halt_gate, level(!strobe[3]));
            if (next_single == single && next_continuous == continuous && next_halt_gate == halt_gate &&
                next_halt_n == halt_n) {
                return;
            }
            single = next_single;
            continuous = next_continuous;
            halt_gate = next_halt_gate;
            halt_n = next_halt_n;
        }
    }

    Scheduler &scheduler;
    Level single = Level::Unknown, continuous = Level::Unknown, halt_gate = Level::Unknown,
          halt_n = Level::Unknown;
    bool strobe[4] = {}, reset = false;
    bool reset_n = true;                // RESET_N as DMEM sees it
    uint64_t reset_edges = 0;
    static constexpr Tick never = ~Tick(0);
    std::optional<Tick> cpc_cleared_at, clear_active_from;
    Tick clear_active_until = 0;
    uint16_t xreg_input = 0, bus_test = 0;
    DelayedRegister xreg_output;
};

}  // namespace lexicon224x::cpu
