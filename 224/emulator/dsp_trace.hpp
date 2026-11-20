#pragma once
// Lens:    the DSP trace. It copies the row machine, steps the copy, and
//          records what each row did: the bus word, the delay memory
//          address, the register file, ACC, RR, SAT and the DAC holds.
// The machine itself is not touched. The copy runs as if the CPU stood
// still: no WCS writes, no multiplicand holds, and the ADC, gain and XREG
// pins as they were.
//
//   row  op    c      bus  address     R0     R1     R2     R3       ACC      RR  SAT  DAC
//    39  MEMW   5+       0     3337      0  30583      0      0         0       0    .  .
//    40  MEMR  16+       0    62066      0  30583      0      0         0       0    .  B
#include "../isa-level-cpp/lexicon224x.hpp"
#include "scheduler.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace lexicon224x::lens {

// What one row did, read from the machine after it executed.
struct RowValues {
    unsigned row;           // the WCS row of the microinstruction
    uint32_t word;
    uint16_t bus;           // the data bus word: what register WA took
    uint16_t address;       // the delay memory address (used by MEMR and MEMW)
    uint16_t R[4];          // the register file after the row
    int32_t ACC;            // the accumulator after the row's three ARU clocks (sample * 8)
    int16_t RR;             // after the row's XFER, if any
    unsigned saturated;     // SAT at each of the row's three ARU clocks, bit 0 first
    unsigned dac_channels;  // DAC holds that took a sample at this row's converter clock
};

// Step a copy of `machine` through `rows` whole rows, calling
// visit(copy, row) after each one executes (`row` is its WCS row). `next` is
// the row phase due next (Host::next_row_phase()); a row already under way is
// finished first and not visited. A machine outside a host stands at Fetch.
template <class Visit>
void for_each_row(const Machine &machine, cpu::RowPhase next, unsigned rows, Visit &&visit) {
    auto copy = std::make_unique<Machine>(machine);
    Machine &m = *copy;
    switch (next) {
        case cpu::RowPhase::ResetDecode:
        case cpu::RowPhase::Converter:
            converter_clock(m);
            execute(m);
            break;
        case cpu::RowPhase::Begin:
        case cpu::RowPhase::ExecutePrevious:
            execute(m);
            break;
        case cpu::RowPhase::Fetch:
            break;
    }
    for (unsigned n = 0; n < rows; n++) {
        unsigned row = m.pc;
        fetch(m);
        converter_clock(m);
        execute(m);
        visit(static_cast<const Machine &>(m), row);
    }
}

// `rows` whole rows from where `machine` stands (see for_each_row).
inline std::vector<RowValues> dsp_trace(const Machine &machine, cpu::RowPhase next, unsigned rows) {
    std::vector<RowValues> values;
    for_each_row(machine, next, rows, [&values](const Machine &m, unsigned row) {
        RowValues v{};
        v.row = row;
        v.word = m.microinstruction;
        v.bus = m.R[m.mi.wa];
        v.address = memory_address(m.cpc, m.mi, m.model);
        for (unsigned r = 0; r < 4; r++) {
            v.R[r] = m.R[r];
        }
        v.ACC = m.ACC;
        v.RR = m.RR;
        v.saturated = m.saturated;
        v.dac_channels = m.dac_channels;
        values.push_back(v);
    });
    return values;
}

// One line per row. Words are shown as signed samples; the address only for
// MEMR and MEMW; SAT as the adds that saturated (1-3), DAC as the channels.
inline std::string dsp_trace_text(const std::vector<RowValues> &values, Model model = Model::Lexicon224X) {
    static const char *const operations[] = {"NOP ", "OPER", "MEMW", "MEMR"};
    std::string text = "row  op    c      bus  address     R0     R1     R2     R3       ACC      RR  SAT  DAC\n";
    for (const RowValues &v : values) {
        Microinstruction mi = decode(v.word, model);
        char sign = '+';
        if (mi.negative) {
            sign = '-';
        }
        char address[8] = "";
        if (mi.op == MEMR || mi.op == MEMW) {
            std::snprintf(address, sizeof address, "%u", unsigned(v.address));
        }
        std::string sat;
        for (unsigned add = 0; add < 3; add++) {
            if (v.saturated >> add & 1) {
                sat += char('1' + add);
            }
        }
        if (sat.empty()) {
            sat = ".";
        }
        std::string dac;
        for (unsigned channel = 0; channel < 4; channel++) {
            if (v.dac_channels >> channel & 1) {
                dac += "ABCD"[channel];
            }
        }
        if (dac.empty()) {
            dac = ".";
        }
        char line[160];
        std::snprintf(line, sizeof line, "%3u  %s  %2u%c  %6d  %7s  %5d  %5d  %5d  %5d  %8d  %6d  %3s  %s\n", v.row,
                      operations[mi.op], mi.coefficient, sign, int(int16_t(v.bus)), address, int(int16_t(v.R[0])),
                      int(int16_t(v.R[1])), int(int16_t(v.R[2])), int(int16_t(v.R[3])), int(v.ACC), int(v.RR),
                      sat.c_str(), dac.c_str());
        text += line;
    }
    return text;
}

}  // namespace lexicon224x::lens
