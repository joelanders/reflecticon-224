#pragma once
// Lens:    how values flow between rows. Each row is one microinstruction,
//          and the values it uses were made by earlier rows. The links name
//          them, in the programmer's model's own order (README.md, "One row"):
//
//   Register  R[n] written by `from` (its bus value) is multiplied by `to`.
//   Shift     `to` keeps shifting: it multiplies what `from` left in X, / 64.
//   Product   `from`'s product is part of the sum `to` saves with XFER.
//   Result    `to` puts on the bus the RR that `from` saved with XFER
//             (MEMW stores it; OPER sends it to a register, the DAC or XREG).
//   Memory    `to` (MEMR) reads what `from` (MEMW) stored, some passes ago.
//
// The first four come from walking the rows in order for three passes, so a
// value made late in one pass and used early in the next is found too. The
// memory links come from the addresses: a MEMR at offset r reads what a MEMW
// at offset w stored (r - w) mod 65536 passes earlier (mod 16384 on the
// original 224, whose offsets are 14 bits), and the latest such write wins.
// measure_memory() checks those against a copy of the machine.
//
// Every function takes the machine's model: the 224X and the 224 decode the
// same word differently, and the 224's pass ends at its RESET row.
#include "../isa-level-cpp/lexicon224x.hpp"
#include "scheduler.hpp"
#include "dsp_trace.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lexicon224x::lens {

enum class LinkKind { Register, Shift, Product, Result, Memory };

struct Link {
    LinkKind kind;
    unsigned from, to;      // WCS rows
    unsigned detail;        // Register: the register; Memory: the lag in passes
    bool earlier_pass;      // `from` ran in an earlier pass than `to`
};

struct Tap {
    unsigned row;           // the MEMR row
    unsigned passes;        // how many passes (samples) ago the value was stored
};

struct DelayLine {
    unsigned write_row;     // the MEMW row
    std::vector<Tap> taps;  // the MEMR rows that read it, shortest lag first
};

struct Flow {
    Model model = Model::Lexicon224X;
    unsigned rows = 128;                    // rows in a pass
    std::vector<Link> links;
    std::vector<DelayLine> lines;           // one per MEMW row, in row order
    std::vector<unsigned> unwritten_reads;  // MEMR rows no MEMW row feeds
    std::vector<bool> bus_used;             // per row: its bus value goes somewhere
};

// Rows in a pass: on the 224X the pass ends with the row after the first
// RESET (the flush row); on the 224 with the RESET row itself.
inline unsigned rows_per_pass(const uint32_t wcs[128], Model model = Model::Lexicon224X) {
    for (unsigned row = 0; row + 1 < 128; row++) {
        if (decode(wcs[row], model).reset) {
            if (model == Model::Lexicon224) {
                return row + 1;
            }
            return row + 2;
        }
    }
    return 128;
}

// The number of delay memory words: 65536 on the 224X, 16384 on the 224.
inline uint32_t memory_words(Model model) {
    if (model == Model::Lexicon224) {
        return 16384;
    }
    return 65536;
}

inline bool puts_rr_on_bus(const Microinstruction &mi) {
    if (mi.op == MEMW) {
        return true;
    }
    return mi.op == OPER && mi.source == FromRR;
}

// The delay offset of a MEMR or MEMW (the field holds its complement).
inline uint16_t offset(const Microinstruction &mi, Model model = Model::Lexicon224X) {
    return uint16_t(uint16_t(~mi.low) & (memory_words(model) - 1));
}

inline Flow flow(const uint32_t wcs[128], Model model = Model::Lexicon224X) {
    Flow f;
    f.model = model;
    f.rows = rows_per_pass(wcs, model);
    const unsigned n = f.rows;
    const uint32_t words = memory_words(model);
    std::vector<Microinstruction> mi(n);
    for (unsigned row = 0; row < n; row++) {
        mi[row] = decode(wcs[row], model);
    }

    // ---- Registers, X, ACC and RR: three passes in row order ----
    struct Maker {
        bool known = false;
        unsigned row = 0, pass = 0;
    };
    Maker reg[4], rr, x;
    std::vector<Maker> sum;                 // rows whose products are in ACC
    std::vector<Link> multiplicand(n);      // what each row multiplies (last pass)
    std::vector<bool> has_multiplicand(n, false);
    std::vector<Link> results;
    for (unsigned pass = 0; pass < 3; pass++) {
        bool record = pass == 2;
        for (unsigned row = 0; row < n; row++) {
            const Microinstruction &m = mi[row];
            if (record && puts_rr_on_bus(m) && rr.known) {
                results.push_back({LinkKind::Result, rr.row, row, 0, rr.pass < pass});
            }
            reg[m.wa] = {true, row, pass};                  // R[WA] := bus
            Maker source = reg[m.ra];
            LinkKind kind = LinkKind::Register;
            unsigned detail = m.ra;
            if (m.keep_shifting) {
                source = x;
                kind = LinkKind::Shift;
                detail = 0;
            }
            if (record && source.known) {
                multiplicand[row] = {kind, source.row, row, detail, source.pass < pass};
                has_multiplicand[row] = true;
            }
            x = {true, row, pass};
            if (m.xfer) {
                if (record) {
                    for (const Maker &p : sum) {
                        f.links.push_back({LinkKind::Product, p.row, row, 0, p.pass < pass});
                    }
                }
                rr = {true, row, pass};
            }
            if (m.zero) {
                sum.clear();
            }
            if (m.coefficient != 0) {
                std::erase_if(sum, [row](const Maker &p) {
                    return p.row == row;
                });
                sum.push_back({true, row, pass});
            }
        }
    }

    // A multiplicand matters if its product does, or a keep-shifting row
    // after it carries it on.
    std::vector<bool> matters(n, false);
    for (unsigned round = 0; round < n; round++) {
        for (unsigned row = 0; row < n; row++) {
            unsigned next = (row + 1) % n;
            matters[row] = mi[row].coefficient != 0 || (mi[next].keep_shifting && matters[next]);
        }
    }
    for (unsigned row = 0; row < n; row++) {
        if (has_multiplicand[row] && matters[row]) {
            f.links.push_back(multiplicand[row]);
        }
    }

    // A row's bus value goes somewhere if it is stored, sent out, or multiplied.
    f.bus_used.assign(n, false);
    for (unsigned row = 0; row < n; row++) {
        f.bus_used[row] = mi[row].op == MEMW || mi[row].wr_da || mi[row].wr_xreg;
    }
    for (const Link &link : f.links) {
        if (link.kind == LinkKind::Register) {
            f.bus_used[link.from] = true;
        }
    }
    for (const Link &link : results) {
        if (f.bus_used[link.to]) {
            f.links.push_back(link);
        }
    }

    // ---- Delay memory: the latest write to the address each MEMR reads ----
    for (unsigned row = 0; row < n; row++) {
        if (mi[row].op == MEMW) {
            f.lines.push_back({row, {}});
        }
    }
    for (unsigned r = 0; r < n; r++) {
        if (mi[r].op != MEMR) {
            continue;
        }
        bool found = false;
        uint32_t best_lag = 0;
        unsigned best_line = 0;
        for (unsigned l = 0; l < f.lines.size(); l++) {
            unsigned w = f.lines[l].write_row;
            uint32_t lag = uint32_t(offset(mi[r], model) - offset(mi[w], model)) & (words - 1);
            if (lag == 0 && w > r) {
                lag = words;                // not yet written this pass
            }
            // Equal lags mean the same offset: the later row wrote last.
            if (!found || lag < best_lag || (lag == best_lag && w > f.lines[best_line].write_row)) {
                found = true;
                best_lag = lag;
                best_line = l;
            }
        }
        if (!found) {
            f.unwritten_reads.push_back(r);
            continue;
        }
        f.lines[best_line].taps.push_back({r, unsigned(best_lag)});
        f.links.push_back({LinkKind::Memory, f.lines[best_line].write_row, r, unsigned(best_lag), best_lag > 0});
    }
    for (DelayLine &line : f.lines) {
        std::stable_sort(line.taps.begin(), line.taps.end(), [](const Tap &a, const Tap &b) {
            return a.passes < b.passes;
        });
    }
    return f;
}

// ---- The check: run a copy of the machine and see who wrote what each MEMR reads ----

struct MemoryCheck {
    unsigned passes = 0;
    unsigned reads = 0;         // MEMR executions whose address had been written
    unsigned agree = 0;         // ... by the row and at the lag the flow says
    std::vector<std::string> disagreements;
};

// `passes` passes of `wcs` on a fresh machine (the delay memory starts empty,
// so only reads of addresses written during the run are checked).
inline MemoryCheck measure_memory(const uint32_t wcs[128], const Flow &f, unsigned passes) {
    std::vector<int> expected_writer(128, -1);
    std::vector<unsigned> expected_lag(128, 0);
    for (const Link &link : f.links) {
        if (link.kind == LinkKind::Memory) {
            expected_writer[link.to] = int(link.from);
            expected_lag[link.to] = link.detail;
        }
    }
    struct Written {
        bool valid = false;
        uint8_t row = 0;
        uint16_t cpc = 0;
    };
    auto written = std::make_unique<std::array<Written, 65536>>();
    auto machine = std::make_unique<Machine>();
    machine->model = f.model;
    for (unsigned row = 0; row < 128; row++) {
        machine->wcs[row] = wcs[row];
    }
    MemoryCheck check;
    check.passes = passes;
    for_each_row(*machine, cpu::RowPhase::Fetch, passes * f.rows, [&](const Machine &m, unsigned row) {
        uint16_t address = memory_address(m.cpc, m.mi, m.model);
        if (m.mi.op == MEMW) {
            (*written)[address] = {true, uint8_t(row), m.cpc};
        }
        if (m.mi.op != MEMR || !(*written)[address].valid) {
            return;
        }
        check.reads++;
        const Written &w = (*written)[address];
        unsigned lag = uint16_t(m.cpc - w.cpc) & (memory_words(m.model) - 1);
        if (int(w.row) == expected_writer[row] && lag == expected_lag[row]) {
            check.agree++;
        } else if (check.disagreements.size() < 20) {
            check.disagreements.push_back("row " + std::to_string(row) + " read row " + std::to_string(w.row) +
                                          "'s write from " + std::to_string(lag) + " passes ago");
        }
    });
    return check;
}

}  // namespace lexicon224x::lens
