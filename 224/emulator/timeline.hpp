#pragma once
// Lens:    a timeline. It listens to the parts' events (trace.hpp),
//          puts each into words with its part's name, and prints them sorted
//          by the time they happen on the hardware.
// A lens reads the machine and never takes part: the machine runs the same
// with or without it. All the wording lives here, not in the parts.
#include "host.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace lexicon224x::lens {

using cpu::BusCommand;
using cpu::DportEvent;
using cpu::Hold;
using cpu::RowPhase;
using cpu::Tick;

class Timeline : public cpu::Trace {
public:
    // row_detail: also every row's phases, ARUCK edges and diagnostic samples.
    explicit Timeline(bool row_detail) : row_detail(row_detail) {}

    struct Line {
        Tick when;
        std::string part, what;
    };
    std::vector<Line> lines;    // in the order the simulation reported them

    // The lines from `from` up to `to`, in time order, in ns from `origin`.
    void print(Tick origin, Tick from, Tick to) const {
        std::vector<Line> sorted = lines;
        std::stable_sort(sorted.begin(), sorted.end(), [](const Line &a, const Line &b) {
            return a.when < b.when;
        });
        for (const Line &line : sorted) {
            if (line.when < from || line.when >= to) {
                continue;
            }
            double ns_from_origin = (double(line.when) - double(origin)) / double(cpu::ns);
            std::printf("%9.1f ns  %-5s %s\n", ns_from_origin, line.part.c_str(), line.what.c_str());
        }
    }

    // ---- The SBC ----

    // A state is named from the pins (the 8080's T-states). T1 carries the
    // status word and the address; T2 is the state after it; T3 is the first
    // state after T2 that is not a wait.
    void cpu_state(Tick start, uint64_t pins_in, uint64_t pins_out, bool halted) override {
        std::string what;
        if (pins_out & I8080_SYNC) {
            t1_start = start;
            status = uint8_t(I8080_GET_DATA(pins_out));
            what = text("T1  %s at %04X (status %02X)", cycle_name(status), unsigned(I8080_GET_ADDR(pins_out)),
                        unsigned(status));
            after_t2 = false;
        } else if (start == t1_start + cpu::cpu_period) {
            if (pins_out & I8080_DBIN) {
                what = "T2  DBIN";
            } else if (status & I8080_STATUS_HLTA) {
                what = "T2";
            } else {
                what = text("T2  D = %02X", unsigned(I8080_GET_DATA(pins_out)));
            }
            after_t2 = true;
        } else if (halted) {
            what = "TWH halted";
        } else if (pins_out & I8080_WAIT) {
            what = "TW  READY was low";
        } else if (after_t2) {
            if (pins_out & I8080_WR) {
                what = "T3  WR/";
            } else {
                what = text("T3  data in: %02X", unsigned(I8080_GET_DATA(pins_in)));
            }
            after_t2 = false;
        } else {
            what = "T4/T5 internal";
        }
        add(start, "8080", what);
    }

    void bus_command(Tick when, BusCommand command) override {
        switch (command) {
            case BusCommand::MemoryRead:
                add(when, "SBC", "MRDC/ falls (with DBIN)");
                break;
            case BusCommand::MemoryWrite:
                add(when, "SBC", "MWTC/ falls (with WR/)");
                break;
        }
    }

    // ---- DSP rows ----

    void row_phase(Tick when, RowPhase phase, uint64_t row_number) override {
        if (!row_detail) {
            return;
        }
        switch (phase) {
            case RowPhase::Begin:
                add(when, "row", text("row %llu marker", (unsigned long long)row_number));
                break;
            case RowPhase::ExecutePrevious:
                add(when, "row", "execute: the previous row's microinstruction (register window closed)");
                break;
            case RowPhase::Fetch:
                add(when, "row", "fetch: the microinstruction register loads the next word");
                break;
            case RowPhase::ResetDecode:
                add(when, "row", "RESET_N follows the new microinstruction");
                break;
            case RowPhase::Converter:
                add(when, "row", "FPC converter clock");
                break;
        }
    }

    void fetch_displaced(Tick when, uint64_t row_number) override {
        add(when, "row", text("row %llu: fetch displaced, an all-zero word", (unsigned long long)row_number));
    }

    void multiplicand_held(Tick when, Hold clock) override {
        switch (clock) {
            case Hold::Load:
                add(when, "ARU", "multiplicand held (its load)");
                break;
            case Hold::FirstShift:
                add(when, "ARU", "multiplicand held (its first shift)");
                break;
            case Hold::SecondShift:
                add(when, "ARU", "multiplicand held (its second shift)");
                break;
        }
    }

    // ---- The T&C: the SBC's WCS access ----

    void wcs_request(Tick when, bool reading, unsigned wcs_row, unsigned lane) override {
        const char *kind = "write";
        if (reading) {
            kind = "read";
        }
        add(when, "T&C", text("request: %s WCS row %u, lane %u", kind, wcs_row, lane));
    }

    void wcs_grant(Tick row_marker, Tick xack) override {
        add(row_marker, "T&C", "grant, at this row marker");
        add(xack, "T&C", "XACK/ asserts");
    }

    void wcs_read_drive(Tick drives, Tick releases) override {
        add(drives, "T&C", "the byte lane drives the bus");
        add(releases, "T&C", "the byte lane releases the bus");
    }

    void wcs_commit(Tick when, unsigned wcs_row, unsigned lane, uint8_t value) override {
        add(when, "T&C", text("commit: WCS row %u, lane %u = %02X (the CPU's byte)", wcs_row, lane, unsigned(value)));
    }

    // ---- The T&C: diagnostics ----

    void aruck(Tick when, unsigned edge) override {
        if (row_detail) {
            add(when, "ARU", text("ARUCK %u", edge));
        }
    }

    void history_sample(Tick when, unsigned edge) override {
        if (row_detail) {
            add(when, "T&C", text("DPORT3's history samples ARUCK %u", edge));
        }
    }

    void dport(Tick when, DportEvent event) override {
        if (!row_detail) {
            return;
        }
        switch (event) {
            case DportEvent::MicroinstructionShown:
                add(when, "T&C", "DPORT4 (microinstruction monitor) shows the microinstruction at the marker");
                break;
            case DportEvent::TimingVisible:
                add(when, "T&C", "DPORT5 (timing monitor) visible");
                break;
            case DportEvent::ArithmeticSample:
                add(when, "T&C", "DPORT3 (arithmetic monitor) samples");
                break;
            case DportEvent::ArithmeticVisible:
                add(when, "T&C", "DPORT3 visible");
                break;
        }
    }

private:
    bool row_detail;
    Tick t1_start = ~Tick(0);   // the current bus cycle's T1
    uint8_t status = 0;         // ... and its status word
    bool after_t2 = false;      // the next state that is not a wait is T3

    void add(Tick when, const char *part, const std::string &what) {
        lines.push_back({when, part, what});
    }

    // printf into a std::string.
    template <class... Args>
    static std::string text(const char *pattern, Args... args) {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, pattern, args...);
        return buffer;
    }

    // The 8080's machine-cycle types, by their status word (Table 2-1).
    static const char *cycle_name(uint8_t status) {
        switch (status) {
            case I8080_CYCLE_FETCH:
                return "fetch";
            case I8080_CYCLE_MREAD:
                return "memory read";
            case I8080_CYCLE_MWRITE:
                return "memory write";
            case I8080_CYCLE_SREAD:
                return "stack read";
            case I8080_CYCLE_SWRITE:
                return "stack write";
            case I8080_CYCLE_INPUT:
                return "input";
            case I8080_CYCLE_OUTPUT:
                return "output";
            case I8080_CYCLE_INTA:
                return "interrupt acknowledge";
            case I8080_CYCLE_HALT:
                return "halt acknowledge";
            case I8080_CYCLE_INTA_HALT:
                return "interrupt acknowledge (halted)";
            default:
                return "?";
        }
    }
};

}  // namespace lexicon224x::lens
