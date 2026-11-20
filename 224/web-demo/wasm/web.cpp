// C API for the browser: the whole 224X/224XL (8080 + row machine) behind a
// handful of functions called from ../web/app.js through emscripten.
//
// Audio runs at 48 kHz frames. The AIN and AOUT boards are modeled, the
// 224X's or the original 224's by the model
// (../../analog/analog_io.hpp): the input is filtered, sampled at
// the converter's own instants and gain-ranged into the ADC pins, and the
// four DAC outputs are filtered and read at the end of each frame.
// lex_set_analog(w, 0) switches back to the nearest-sample path.
#include "../../emulator/host.hpp"
#include "../../analog/analog_io.hpp"
#include "../../emulator/cpu_disassembler.hpp"
#include "../../emulator/dsp_trace.hpp"
#include "../../isa-level-cpp/wcs_disassembler.hpp"
#include <cmath>
#include <cstring>
#include <emscripten/emscripten.h>

using namespace lexicon224x::cpu;

namespace {

struct Web {
    // The analog boards follow the host's model: the 224X's, or the Model
    // 224's own (their drawn values and its 488 ns rows; analog_io.hpp).
    explicit Web(lexicon224x::Model model) : host(std::make_unique<Host>(model)) {}

    std::unique_ptr<Host> host;
    // The AIN and AOUT boards between the 48 kHz audio and the machine
    // (../../analog/analog_io.hpp): filters, sample-and-holds and
    // gain ranger. lex_set_analog(w, 0) bypasses them (nearest-sample path).
    std::unique_ptr<lexicon224x::analog::AnalogIO> io = std::make_unique<lexicon224x::analog::AnalogIO>(*host);
    std::string listing;        // the text of the last lex_*_listing() or lex_dsp_trace()
    unsigned gains_used = 0;    // the input gain ranges used since lex_input_gains(): bit g (left), 4 + g (right)
};

}  // namespace

extern "C" {
// model: 0 the 224X/224XL, 1 the original 224 (../cpp/lexicon224x.hpp Model).
EMSCRIPTEN_KEEPALIVE Web *lex_create(int model) {
    if (model == 1) {
        return new Web(lexicon224x::Model::Lexicon224);
    }
    return new Web(lexicon224x::Model::Lexicon224X);
}

EMSCRIPTEN_KEEPALIVE void lex_destroy(Web *w) {
    delete w;
}

// Copy a ROM chip into CPU memory at `base` (SBCn at (n-1)*0x800, NVSn at
// 0x8000+(n-1)*0x1000; the 224's ROMn at (n-1)*0x800).
EMSCRIPTEN_KEEPALIVE void lex_load(Web *w, const uint8_t *data, int length, int base) {
    std::memcpy(w->host->memory.data() + base, data, size_t(length));
}

// Render `frames` of 48 kHz audio: input `in_left/in_right`, output four
// channels interleaved A,B,C,D into `out`. Returns 0, or 1 after an error.
EMSCRIPTEN_KEEPALIVE int lex_render(Web *w, const float *in_left, const float *in_right, float *out, int frames) {
    try {
        for (int f = 0; f < frames; f++) {
            Tick end = w->io->before_frame(in_left[f], in_right[f]);
            w->host->run_until(end / cpu_period);
            w->gains_used |= 1u << w->host->dsp->gain_left | 16u << w->host->dsp->gain_right;
            w->io->after_frame(out + 4 * f);
        }
        return 0;
    } catch (const std::exception &e) {
        emscripten_log(EM_LOG_ERROR, "machine stopped: %s", e.what());
        return 1;
    }
}

// 1: model the analog boards (default); 0: bypass them, the nearest-sample
// path this file used before, for A/B listening.
EMSCRIPTEN_KEEPALIVE void lex_set_analog(Web *w, int on) {
    w->io->set_bypass(!on);
}

// A headroom register (0 = input channel 1 / left, 1 = right): bit k clear =
// level detector k (24, 18, 12, 6, 0 dB below clipping) exceeded since the
// firmware last read it. The same data the firmware shows on its headroom LEDs.
EMSCRIPTEN_KEEPALIVE int lex_headroom(Web *w, int channel) {
    return w->host->headroom_held(unsigned(channel & 1));
}

// The input gain ranges the AIN's gain ranger used since the last call (IGA
// 0-3 = 0, +6, +12, +18 dB; bits 0-3 left, 4-7 right), then clears them.
EMSCRIPTEN_KEEPALIVE int lex_input_gains(Web *w) {
    unsigned used = w->gains_used;
    w->gains_used = 0;
    return int(used);
}

EMSCRIPTEN_KEEPALIVE void lex_button(Web *w, int bank, int mask) {
    w->host->panel.switches[bank] = uint8_t(~mask);
}

EMSCRIPTEN_KEEPALIVE void lex_pot(Web *w, int pot, int value) {
    w->host->panel.pots[pot] = uint8_t(value);
}

EMSCRIPTEN_KEEPALIVE int lex_key(Web *w, int code, int down) {
    return w->host->key(uint8_t(code), down);
}

EMSCRIPTEN_KEEPALIVE int lex_fader(Web *w, int slot, int value) {
    return w->host->fader(unsigned(slot), uint8_t(value));
}

// Displays: 9 panel digit bytes (segment patterns), the 48-character LARC text.
EMSCRIPTEN_KEEPALIVE const uint8_t *lex_panel_digits(Web *w) {
    return w->host->panel.digits.data();
}

EMSCRIPTEN_KEEPALIVE const char *lex_larc_text(Web *w) {
    return w->host->remote.display.data();
}

EMSCRIPTEN_KEEPALIVE int lex_larc_connected(Web *w) {
    return w->host->remote.connected;
}

EMSCRIPTEN_KEEPALIVE double lex_cycles(Web *w) {
    return double(w->host->cycles);
}

// The running program: 128 WCS words in T&C polarity (see ../cpp/lexicon224x.hpp).
EMSCRIPTEN_KEEPALIVE const uint32_t *lex_wcs(Web *w) {
    return w->host->dsp->wcs;
}

// The running program, disassembled (../lens/wcs_disassembler.hpp): one
// line per row up to the RESET row. Valid until the next call.
EMSCRIPTEN_KEEPALIVE const char *lex_wcs_listing(Web *w) {
    w->listing = lexicon224x::lens::listing(w->host->dsp->wcs, w->host->model);
    return w->listing.c_str();
}

// The next `rows` DSP rows, run on a copy of the row machine as if the CPU
// stood still (../lens/dsp_trace.hpp). Valid until the next call.
EMSCRIPTEN_KEEPALIVE const char *lex_dsp_trace(Web *w, int rows) {
    auto values = lexicon224x::lens::dsp_trace(*w->host->dsp, w->host->next_row_phase(), unsigned(rows));
    w->listing = lexicon224x::lens::dsp_trace_text(values, w->host->model);
    return w->listing.c_str();
}

EMSCRIPTEN_KEEPALIVE int lex_pc(Web *w) {
    return int(w->host->dsp->pc);
}

// `count` 8080 instructions from `address`, disassembled
// (../lens/cpu_disassembler.hpp). Valid until the next call.
EMSCRIPTEN_KEEPALIVE const char *lex_cpu_listing(Web *w, int address, int count) {
    w->listing = lexicon224x::lens::cpu_listing(w->host->memory.data(), uint16_t(address), unsigned(count));
    return w->listing.c_str();
}

// One byte of CPU memory (RAM state such as LARC pickup flags at 0x3c20+slot).
EMSCRIPTEN_KEEPALIVE int lex_peek(Web *w, int address) {
    return w->host->peek(uint16_t(address));
}

// Write one byte of the SBC's RAM (0x2000-0x3FFF). The easy UI uses it for
// exactly one thing: marking a slider "picked up" (the state the firmware
// itself sets when a fader crosses the stored value), so that a slider can be
// taken over without moving any other.
EMSCRIPTEN_KEEPALIVE void lex_poke(Web *w, int address, int value) {
    if (address >= 0x2000 && address < 0x4000) {
        w->host->memory[size_t(address)] = uint8_t(value);
    }
}

// Delay memory: 65,536 16-bit words (the 224 addresses the first 16,384).
EMSCRIPTEN_KEEPALIVE const uint16_t *lex_memory(Web *w) {
    return w->host->dsp->memory;
}

// The machine: 0 the 224X/224XL, 1 the original 224 (as lex_create).
EMSCRIPTEN_KEEPALIVE int lex_model(Web *w) {
    if (w->host->model == lexicon224x::Model::Lexicon224) {
        return 1;
    }
    return 0;
}
}
