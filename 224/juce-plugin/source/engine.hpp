// The machine behind the plugin: the row machine and its 8080 host
// (../../emulator/host.hpp) with the analog boards
// (../../analog/analog_io.hpp), at 48 kHz frames. No JUCE here, so
// console harnesses can drive it.
//
// render() is the loop of lex_render in ../../web-demo/wasm/web.cpp,
// frame for frame; the control primitives are the ones the page uses.
#pragma once
#include "../../emulator/host.hpp"
#include "../../analog/analog_io.hpp"
#include <cstdint>
#include <cstring>
#include <memory>

namespace lexplug {

using lexicon224x::cpu::Host;
using lexicon224x::cpu::Tick;
using lexicon224x::cpu::cpu_period;

class Engine {
public:
    static constexpr double rate = 48000.0;

    // model: 0 the 224X/224XL, 1 the original 224 (as lex_create).
    explicit Engine(int model)
        : host_(std::make_unique<Host>(model == 1 ? lexicon224x::Model::Lexicon224 : lexicon224x::Model::Lexicon224X)),
          io_(std::make_unique<lexicon224x::analog::AnalogIO>(*host_)) {}

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // Copy a ROM chip into CPU memory at `base` (as lex_load).
    void load(const uint8_t *data, size_t length, unsigned base) {
        std::memcpy(host_->memory.data() + base, data, length);
    }

    // Render `frames` 48 kHz frames: outputs A-D (DAC bit order), one pointer
    // per channel. Throws if the machine stops.
    void render(const float *left, const float *right, float *const out[4], int frames) {
        float four[4];
        for (int f = 0; f < frames; f++) {
            Tick end = io_->before_frame(left[f], right[f]);
            host_->run_until(end / cpu_period);
            gains_used_ |= 1u << host_->dsp->gain_left | 16u << host_->dsp->gain_right;
            io_->after_frame(four);
            for (int c = 0; c < 4; c++) {
                out[c][f] = four[c];
            }
        }
    }

    void set_analog(bool on) {
        io_->set_bypass(!on);
    }

    void button(unsigned bank, unsigned mask) {
        host_->panel.switches[bank] = uint8_t(~mask);
    }

    void pot(unsigned pot, unsigned value) {
        host_->panel.pots[pot] = uint8_t(value);
    }

    bool key(unsigned code, bool down) {
        return host_->key(uint8_t(code), down);
    }

    bool fader(unsigned slot, unsigned value) {
        return host_->fader(slot, uint8_t(value));
    }

    uint8_t peek(uint16_t address) const {
        return host_->peek(address);
    }

    // Only the SBC's RAM (0x2000-0x3FFF), as lex_poke.
    void poke(uint16_t address, uint8_t value) {
        if (address >= 0x2000 && address < 0x4000) {
            host_->memory[address] = value;
        }
    }

    uint8_t headroom(unsigned channel) const {
        return host_->headroom_held(channel & 1);
    }

    // The input gain ranges used since the last call (bits 0-3 left, 4-7 right).
    unsigned take_input_gains() {
        unsigned used = gains_used_;
        gains_used_ = 0;
        return used;
    }

    const uint8_t *panel_digits() const {
        return host_->panel.digits.data();
    }

    const char *larc_text() const {
        return host_->remote.display.data();
    }

    bool larc_connected() const {
        return host_->remote.connected;
    }

    uint64_t cycles() const {
        return host_->cycles;
    }

    Host &host() {
        return *host_;
    }

    lexicon224x::analog::AnalogIO &analog_io() {
        return *io_;
    }

private:
    std::unique_ptr<Host> host_;
    std::unique_ptr<lexicon224x::analog::AnalogIO> io_;
    unsigned gains_used_ = 0;
};

}  // namespace lexplug
