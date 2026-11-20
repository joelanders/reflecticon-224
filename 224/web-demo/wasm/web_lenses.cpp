// The lenses' C API for the browser, compiled into the same module as
// ../cpu/web.cpp. They read a WCS the page already has (lex_wcs()), so they
// need nothing from the host.
#include "../../emulator/flow.hpp"
#include "../../isa-level-cpp/wcs_disassembler.hpp"
#include <algorithm>
#include <cstdio>
#include <emscripten/emscripten.h>
#include <string>

using namespace lexicon224x;

namespace {

const char *kind_name(lens::LinkKind kind) {
    switch (kind) {
        case lens::LinkKind::Register:
            return "register";
        case lens::LinkKind::Shift:
            return "shift";
        case lens::LinkKind::Product:
            return "product";
        case lens::LinkKind::Result:
            return "result";
        case lens::LinkKind::Memory:
            return "memory";
    }
    return "?";
}

// What drives the bus on this row: memory, RR, the XREG input, the ADC, or nothing.
const char *source_name(const Microinstruction &mi) {
    if (mi.op == MEMR) {
        return "memory";
    }
    if (mi.op == MEMW) {
        return "RR";
    }
    if (mi.op == NOP) {
        return "none";
    }
    if (mi.source == FromXREG) {
        return "XREG";
    }
    if (mi.source == FromADC) {
        return "ADC";
    }
    if (mi.source == FromRR) {
        return "RR";
    }
    return "none";  // OPER source 0 selects nothing
}

const char *boolean(bool value) {
    if (value) {
        return "true";
    }
    return "false";
}

std::string json_string(const std::string &text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

std::string flow_json(const uint32_t wcs[128], bool measure, Model model) {
    static const char *const operations[] = {"NOP", "OPER", "MEMW", "MEMR"};
    lens::Flow f = lens::flow(wcs, model);
    // A row lasts nine master-clock periods: 292.97 ns on the 224X, 488.28 ns
    // (one SBC clock state) on the 224 (../cpu/timing.hpp).
    const char *row_ns = "292.96875";
    if (model == Model::Lexicon224) {
        row_ns = "488.28125";
    }
    std::string json = "{\"rows\":" + std::to_string(f.rows) + ",\"rowNs\":" + row_ns +
                       ",\"model\":" + std::to_string(int(model == Model::Lexicon224)) + ",\"listing\":[";
    for (unsigned row = 0; row < f.rows; row++) {
        Microinstruction mi = decode(wcs[row], model);
        char word[12];
        std::snprintf(word, sizeof word, "%08x", unsigned(wcs[row]));
        std::string dac;
        if (mi.wr_da) {
            for (unsigned channel = 0; channel < 4; channel++) {
                if (mi.channels >> channel & 1) {
                    dac += "ABCD"[channel];
                }
            }
        }
        if (row > 0) {
            json += ",";
        }
        json += "{\"row\":" + std::to_string(row) + ",\"word\":\"" + word + "\",\"op\":\"" + operations[mi.op] +
                "\",\"text\":" + json_string(lens::disassemble(wcs[row], model)) +
                ",\"c\":" + std::to_string(mi.coefficient) + ",\"negative\":" + boolean(mi.negative) +
                ",\"ra\":" + std::to_string(mi.ra) + ",\"wa\":" + std::to_string(mi.wa) +
                ",\"xfer\":" + boolean(mi.xfer) + ",\"zero\":" + boolean(mi.zero) +
                ",\"keepShifting\":" + boolean(mi.keep_shifting) + ",\"reset\":" + boolean(mi.reset) +
                ",\"source\":\"" + source_name(mi) + "\",\"offset\":" + std::to_string(lens::offset(mi, model)) +
                ",\"dac\":\"" + dac + "\",\"wrXreg\":" + boolean(mi.wr_xreg) +
                ",\"busUsed\":" + boolean(f.bus_used[row]) + "}";
    }
    json += "],\"links\":[";
    for (size_t k = 0; k < f.links.size(); k++) {
        const lens::Link &link = f.links[k];
        if (k > 0) {
            json += ",";
        }
        json += "{\"kind\":\"" + std::string(kind_name(link.kind)) + "\",\"from\":" + std::to_string(link.from) +
                ",\"to\":" + std::to_string(link.to) + ",\"detail\":" + std::to_string(link.detail) +
                ",\"earlierPass\":" + boolean(link.earlier_pass) + "}";
    }
    json += "],\"lines\":[";
    uint32_t longest = 0;
    for (size_t l = 0; l < f.lines.size(); l++) {
        if (l > 0) {
            json += ",";
        }
        json += "{\"write\":" + std::to_string(f.lines[l].write_row) + ",\"taps\":[";
        for (size_t t = 0; t < f.lines[l].taps.size(); t++) {
            const lens::Tap &tap = f.lines[l].taps[t];
            if (t > 0) {
                json += ",";
            }
            json += "{\"row\":" + std::to_string(tap.row) + ",\"passes\":" + std::to_string(tap.passes) + "}";
            if (tap.passes < lens::memory_words(model)) {
                longest = std::max(longest, uint32_t(tap.passes));
            }
        }
        json += "]}";
    }
    json += "],\"unwrittenReads\":[";
    for (size_t k = 0; k < f.unwritten_reads.size(); k++) {
        if (k > 0) {
            json += ",";
        }
        json += std::to_string(f.unwritten_reads[k]);
    }
    json += "]";
    if (measure) {
        lens::MemoryCheck check = lens::measure_memory(wcs, f, longest + 2);
        json += ",\"check\":{\"passes\":" + std::to_string(check.passes) + ",\"reads\":" +
                std::to_string(check.reads) + ",\"agree\":" + std::to_string(check.agree) + ",\"disagreements\":[";
        for (size_t k = 0; k < check.disagreements.size(); k++) {
            if (k > 0) {
                json += ",";
            }
            json += json_string(check.disagreements[k]);
        }
        json += "]}";
    }
    return json + "}";
}

std::string result;

}  // namespace

extern "C" {

// The flow of the program in `wcs` (128 words in T&C polarity, as lex_wcs()
// gives them) as JSON: its rows, the links between them (../lens/flow.hpp),
// its delay lines, and with `measure` the check against a running copy of
// the machine. `model`: 0 the 224X/224XL, 1 the original 224. Valid until
// the next call.
EMSCRIPTEN_KEEPALIVE const char *lex_flow(const uint32_t *wcs, int measure, int model) {
    Model m = Model::Lexicon224X;
    if (model == 1) {
        m = Model::Lexicon224;
    }
    result = flow_json(wcs, measure != 0, m);
    return result.c_str();
}

}
