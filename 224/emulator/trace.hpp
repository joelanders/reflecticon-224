#pragma once
// Part:    none (a probe): the events the parts report while a timeline is
//          being traced. The parts hold a null pointer to a Trace unless one
//          is attached, so tracing costs one pointer test.
// The parts only say what happened, and when; a lens (timeline.hpp)
// puts it into words. Each event carries the time it happens on the
// hardware, not when the simulation computed it, so sorting by that time
// gives the hardware's order.
#include "scheduler.hpp"
#include "timing.hpp"
#include <cstdint>

namespace lexicon224x::cpu {

// The system bus commands the SBC's 8238 starts for a T&C access.
enum class BusCommand {
    MemoryRead,     // MRDC/, with DBIN in T2
    MemoryWrite,    // MWTC/, with WR/ at the end of T2
};

// The multiplicand clocks an SBC access can hold (wcs_access.hpp).
enum class Hold {
    Load,           // ARUCK 1 of the next row
    FirstShift,     // ARUCK 2 of the next row
    SecondShift,    // ARUCK 0 of the row after
};

// When the T&C's diagnostic ports change (tc_diagnostics.hpp).
enum class DportEvent {
    MicroinstructionShown,  // DPORT4 shows the microinstruction at the marker
    TimingVisible,          // DPORT5 visible
    ArithmeticSample,       // DPORT3 samples
    ArithmeticVisible,      // DPORT3 visible
};

class Trace {
public:
    virtual ~Trace() = default;

    // ---- The SBC ----
    // One 8080 clock state: the pins it was given and the pins it drove.
    virtual void cpu_state(Tick start, uint64_t pins_in, uint64_t pins_out, bool halted) = 0;
    virtual void bus_command(Tick when, BusCommand command) = 0;

    // ---- DSP rows ----
    virtual void row_phase(Tick when, RowPhase phase, uint64_t row_number) = 0;
    virtual void fetch_displaced(Tick when, uint64_t row_number) = 0;
    virtual void multiplicand_held(Tick when, Hold clock) = 0;

    // ---- The T&C: the SBC's WCS access ----
    virtual void wcs_request(Tick when, bool reading, unsigned wcs_row, unsigned lane) = 0;
    virtual void wcs_grant(Tick row_marker, Tick xack) = 0;
    virtual void wcs_read_drive(Tick drives, Tick releases) = 0;
    virtual void wcs_commit(Tick when, unsigned wcs_row, unsigned lane, uint8_t value) = 0;

    // ---- The T&C: diagnostics ----
    virtual void aruck(Tick when, unsigned edge) = 0;
    virtual void history_sample(Tick when, unsigned edge) = 0;   // DPORT3's history, at an ARUCK
    virtual void dport(Tick when, DportEvent event) = 0;
};

}  // namespace lexicon224x::cpu
