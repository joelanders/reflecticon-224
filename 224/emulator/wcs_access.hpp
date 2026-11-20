#pragma once
// Part:    T&C sheet 1, "the access to the WCS from the SBC module" (224X
//          Service Manual 3.5): decoded by U50, synchronized by U52/U53,
//          acknowledged with XACK/ by U54.
// Mirrors: ../board-level-verilog/tc.sv (grant_wcs, commit_cpu_write).
// Inputs:  on_request (the SBC's MRDC/ or MWTC/ for a WCS address),
//          at_marker (each step's marker), at_fetch (each microinstruction fetch).
// Outputs: displaced() (a microinstruction displaced by an all-zero word),
//          held() (a multiplicand clock held), the Request record (when
//          XACK/ asserts, when a read's byte is on the bus), and the
//          committed WCS byte.
//
// The SBC can reach the WCS while the DSP runs, "allowing it to change
// program characteristics on the fly" (3.5). The T&C grants a request at an
// allowed access slot (a step marker), then for a few steps it owns the WCS:
// - the next microinstruction is displaced: the microinstruction register's
//   upper 16 bits are cleared (a no-operation);
// - the multiplicand register is held on some ARU clock edges;
// - a write commits mid-step; a read drives its byte lane for the SBC;
// - XACK/ tells the 8080 when to leave its wait states.
// The protect bit (bit 22 of the microinstruction) marks steps in which no
// access may be granted.
#include "../isa-level-cpp/lexicon224x.hpp"
#include "scheduler.hpp"
#include "trace.hpp"
#include <array>
#include <stdexcept>

namespace lexicon224x::cpu {

class WcsAccess {
public:
    Model model = Model::Lexicon224X;    // which T&C board: where its protect bit is
    struct Request {
        bool valid = false, scheduled = false, reading = false;
        unsigned row = 0, lane = 0;
        uint8_t data = 0;
        Tick requested_at = 0, cpu_t1 = 0, displaced_start = 0, displaced_end = 0;
        Tick first_held = 0, last_held = 0, ack_time = 0, sampled_at = 0, commit_at = 0;
        Tick read_enable = 0, read_release = 0;
    };

    // Commits write into `dsp`'s WCS.
    WcsAccess(Scheduler &scheduler, Machine &dsp) : scheduler(scheduler), dsp(dsp) {}

    // ---- Inputs ----

    // The SBC's request, made when its MRDC/ or MWTC/ starts. `value` is the
    // byte as the CPU wrote it (the bus inverts it).
    Request &on_request(bool reading, unsigned wcs_row, unsigned lane, uint8_t value, Tick t1) {
        Request *slot = nullptr;
        for (auto &request : requests) {
            if (!slot && (!request.valid || (request.scheduled && request.last_held < scheduler.now()))) {
                slot = &request;
            }
        }
        if (!slot) {
            throw std::runtime_error("T&C WCS request storage exhausted");
        }
        *slot = Request{};
        slot->valid = true;
        slot->reading = reading;
        slot->row = wcs_row;
        slot->lane = lane;
        slot->data = uint8_t(~value);
        slot->requested_at = scheduler.now();
        slot->cpu_t1 = t1;
        waiting++;
        if (trace) {
            trace->wcs_request(scheduler.now(), reading, wcs_row, lane);
        }
        return *slot;
    }

    // At a step's marker (an access slot): grant waiting requests, unless the
    // protect bit or the pair flip-flop refuses this slot. A halted DSP
    // grants at any marker ("the SBC module can read from and write to the
    // WCS anytime").
    // `marker_protect`: that microinstruction's protect bit.
    LEXICON_ALWAYS_INLINE void at_marker(bool marker_protect, bool dsp_running) {
        if (waiting == 0) {
            return;     // nothing to grant: the common row
        }
        for (auto &request : requests) {
            if (request.valid && !request.scheduled && request.requested_at < scheduler.now() &&
                (!dsp_running || (!protected_pair && !marker_protect))) {
                grant(request);
            }
        }
    }

    // At a fetch: the protect logic follows bit 22. protected_pair (the pair
    // flip-flop, half of U53: "an allowed access slot not utilized will
    // disable the next access slot", 3.5) toggles each
    // time bit 22 rises from one fetched microinstruction to the next (it is forced
    // off instead in the row after a RESET), and a RESET row clears it; while
    // it is set, requests wait too. resetd_n (RESETD/) is low for the row
    // after an OPER RESET. (tc.sv's names.)
    // The bits of the microinstruction register's old word and of the fetched one.
    LEXICON_ALWAYS_INLINE void at_fetch(bool previous_protect, bool previous_reset, bool fetched_protect) {
        if (!previous_protect && fetched_protect) {
            if (resetd_n) {
                protected_pair = !protected_pair;
            } else {
                protected_pair = false;
            }
        }
        if (resetd_n && previous_reset) {
            protected_pair = false;
        }
        resetd_n = !previous_reset;
    }

    // ---- Outputs ----

    // Is the fetch at this row marker displaced by a granted access?
    LEXICON_ALWAYS_INLINE bool displaced(Tick row_marker) const {
        if (row_marker >= windows_end) {
            return false;
        }
        for (auto &request : requests) {
            if (request.valid && request.scheduled && row_marker >= request.displaced_start &&
                row_marker < request.displaced_end) {
                return true;
            }
        }
        return false;
    }

    // Is the multiplicand register held on this ARU clock edge?
    LEXICON_ALWAYS_INLINE bool held(Tick edge) const {
        if (edge > windows_end) {
            return false;
        }
        for (auto &request : requests) {
            if (request.valid && request.scheduled && edge >= request.first_held && edge <= request.last_held) {
                return true;
            }
        }
        return false;
    }

    // No access holds a clock after this: a row whose marker is later can
    // skip the three held() checks.
    Tick holds_end() const {
        return windows_end;
    }

    uint64_t writes_committed() const {
        return writes;
    }

    // ---- Probe ----

    void set_trace(Trace *probe) {
        trace = probe;
    }

private:
    // The T&C grants a request at a row marker. Times from tc.sv grant_wcs
    // (typical delays; tc.sv does not attribute them to individual chips).
    void grant(Request &request) {
        const RowTiming &timing = scheduler.timing;   // the model's row and slot periods
        const Tick row = timing.row, master = timing.master;
        auto aruck_edge = [&](Tick marker, unsigned n) { return timing.aruck_edge(marker, n); };
        static constexpr Tick capture_delay = 9 * ns / 2;       // the marker captures the request
        static constexpr Tick latch_delay = 145 * ns / 2;       // ... and latches it
        static constexpr Tick xack_delay = 55 * ns;             // XACK/ after the acknowledging CPU edge
        Tick row_marker = scheduler.now();
        Tick capture = row_marker + capture_delay;
        Tick latched = capture + latch_delay;
        // XACK/ answers at the first phi2 rise of a CPU state after the latch.
        Tick ack_edge = periodic_edge(request.cpu_t1 + phi2_rise, cpu_period, latched);
        request.ack_time = ack_edge + xack_delay;
        // The CPU samples READY in T2 and in each TW until XACK/ is there. The
        // state after that is T3, and the SBC samples the data at its phi2 rise.
        Tick ready_edge = state_after(request.cpu_t1, 1) + ready_sample;
        while (ready_edge <= request.ack_time) {
            ready_edge += cpu_period;
        }
        request.sampled_at = ready_edge - ready_sample + cpu_period + phi2_rise;
        request.scheduled = true;
        waiting--;
        // The next row's fetch is displaced; the multiplicand holds start at
        // ARUCK edge 1 of the row after that.
        request.displaced_start = row_marker + row;
        request.first_held = aruck_edge(row_marker + 2 * row, 1);
        if (request.reading) {
            static constexpr Tick select_fall = 3 * ns;         // select line fall delay
            static constexpr Tick select_lead = 21 * ns / 2;    // select releases this before 3 master periods into a row,
            static constexpr Tick sample_margin = 40 * ns;      // ... at least this after the CPU's data sample
            static constexpr Tick lane_enable_delay = 13 * ns;  // the byte lane drives the bus 5 master periods + this after capture,
            static constexpr Tick lane_release_delay = 25 * ns; // ... until this after the grant releases
            static constexpr Tick hold_end_delay = 15 * ns / 2; // holds end 14 master periods + this after the grant releases
            Tick select_release = periodic_edge(row_marker + 3 * master - select_lead + select_fall, row,
                                                request.sampled_at + select_fall + sample_margin);
            Tick grant_release = select_release + master;
            request.read_enable = capture + 5 * master + lane_enable_delay;
            request.read_release = grant_release + lane_release_delay;
            request.displaced_end = periodic_edge(row_marker, row, select_release + 1);
            request.last_held = grant_release + 14 * master + hold_end_delay;
        } else {
            static constexpr Tick select_lead = 12 * ns;        // select releases 1 row + 3 master periods - this after capture
            static constexpr Tick commit_delay = 10 * ns;       // the WCS byte is written this after the select releases
            Tick select_release = capture + row + 3 * master - select_lead;
            request.displaced_end = row_marker + 2 * row;
            request.last_held = row_marker + 3 * row;
            request.commit_at = select_release + commit_delay;
            Request copy = request;
            scheduler.at(request.commit_at, [this, copy] {
                uint32_t &word = dsp.wcs[copy.row];
                word = (word & ~(0xffu << (8 * copy.lane))) | uint32_t(copy.data) << (8 * copy.lane);
                writes++;
                if (trace) {
                    trace->wcs_commit(scheduler.now(), copy.row, copy.lane, uint8_t(~copy.data));
                }
            });
        }
        windows_end = std::max({windows_end, request.displaced_end, request.last_held});
        if (trace) {
            trace->wcs_grant(row_marker, request.ack_time);
            if (request.reading) {
                trace->wcs_read_drive(request.read_enable, request.read_release);
            }
        }
    }

    Scheduler &scheduler;
    Machine &dsp;
    std::array<Request, 4> requests;
    Tick windows_end = 0;   // no access displaces a fetch or holds a clock after this
    bool protected_pair = false, resetd_n = false;
    unsigned waiting = 0;   // requests made and not yet granted
    uint64_t writes = 0;
    Trace *trace = nullptr;
};

}  // namespace lexicon224x::cpu
