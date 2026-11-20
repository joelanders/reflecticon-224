// Print what the hardware does, state by state, from the host itself:
//
//   timeline wcs-write   the CPU writes one WCS byte while the DSP runs
//   timeline row         one DSP row: its phases, ARU clock edges and
//                        diagnostic samples
//
// No firmware is needed: the CPU runs a four-instruction program of our own
// from address 0 (below). Every part reports the time an event happens, and
// the lines are sorted by that time, so the output is the hardware's order
// whatever order the simulation computed things in. Times are in ns, from
// the start of the traced instruction (wcs-write) or the row marker (row).
// The wording is the timeline lens's (timeline.hpp).
// tests/check_timeline.py compares the output with tests/timeline.expected.
#include "timeline.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

using namespace lexicon224x::cpu;
using lexicon224x::lens::Timeline;

namespace {

// OUT 3 is DMEM's RUN strobe: HALT/ goes high and the DSP program counter
// runs. (The WCS holds all-zero words: every row is a NOP.)
const uint8_t program[] = {
    0xD3, 0x03,         // 0000  OUT 03h
    0x3E, 0x5A,         // 0002  MVI A,5Ah
    0x32, 0x23, 0x41,   // 0004  STA 4123h    one WCS byte: row 55, lane 3
    0x76,               // 0007  HLT
};
constexpr uint16_t sta_address = 0x0004;

}  // namespace

int main(int argc, char **argv) {
    std::string mode;
    if (argc > 1) {
        mode = argv[1];
    }
    if (mode != "wcs-write" && mode != "row") {
        std::fprintf(stderr, "usage: %s wcs-write|row\n", argv[0]);
        return 2;
    }

    Host host;
    std::copy(std::begin(program), std::end(program), host.memory.begin());
    if (mode == "wcs-write") {
        // Run instruction by instruction to the STA, then trace it and a few rows after.
        while (host.snapshot().pc != sta_address) {
            host.run_until(host.cycles + 1);
        }
        Tick origin = host.cycles * cpu_period;
        Timeline timeline(false);
        host.set_trace(&timeline);
        host.run_until(host.cycles + 1);             // the STA
        uint64_t end = host.cycles + 4;
        host.run_until(end);                          // and the HLT, while the commit lands
        host.set_trace(nullptr);
        std::printf("STA 4123h: the CPU writes 5A into WCS row 55, lane 3, while the DSP runs.\n");
        timeline.print(origin, origin, end * cpu_period);
    } else {
        // Trace one whole row once the DSP is running.
        while (host.snapshot().pc != sta_address) {
            host.run_until(host.cycles + 1);
        }
        Timeline timeline(true);
        host.set_trace(&timeline);
        host.run_until(host.cycles + 20);
        host.set_trace(nullptr);
        // The first row that starts after the first traced state.
        uint64_t row_number = 0;
        const auto &lines = timeline.lines;
        Tick first = std::min_element(lines.begin(), lines.end(), [](const Timeline::Line &a, const Timeline::Line &b) {
            return a.when < b.when;
        })->when;
        while (marker(row_number) < first) {
            row_number++;
        }
        std::printf("One DSP row (292.97 ns), from its marker.\n");
        timeline.print(marker(row_number), marker(row_number), marker(row_number + 1));
    }
    return 0;
}
