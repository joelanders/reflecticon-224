// What a running machine holds, read from its RAM (and, for the 224XL, its
// LARC display text), with no key presses: the program, the variation, every
// named slider's stored byte and the toggles the RAM shows. This is how
// OperatorSink::readState works without the operator (source/operator).
//
// The addresses are the web demo's (../../web-demo/page):
//   224XL (LARC)   larc.js recordLayout(): the parameter record's base and
//                  column byte, found by the fader handler's byte pattern in
//                  the NVS ROMs (v8.21 0x3CA3, v8.1A 0x3C9C: never
//                  hard-coded); stored(column, slot) = base + 6*column + slot.
//                  The program and variation are the display's own
//                  "Bn Pm Vv" (larc.js loadProgram reads the same text); when
//                  the display shows something else (a slider, a prompt) the
//                  last program seen is kept. The toggles (DYN DECAY, MODE
//                  ENH, DECAY OPT) are not in RAM at a known address: unknown
//                  here (the operator reads them with PARAM key taps).
//   224X (panel)   panel.js PANEL: RECORD 0x3C9A (identity, then 6 bytes per
//                  column), VARIATION 0x3C58 (the PROGRAM button mask).
//   224 (panel224) panel224.js PANEL224 (v4.x) and tools/panel224_layouts.mjs
//                  LAYOUT_V32 (v3.2): the program byte (bits 0-5 the button,
//                  bit 6 MODE ENH, bit 7 DECAY OPT) and one cell per pot
//                  (column 0), DIFFUSION on the SHIFT page (column 1, slot 4;
//                  v4.x only). No variations: always 1. Both layouts keep
//                  MODE ENH / DECAY OPT in the program byte's bits 6 and 7.
//
// prepare() scans ROM (up to 32 K peeks): call it once per machine, off the
// audio thread (the boot thread does). read() does no allocation and no
// scanning: it may run on the audio thread.
//
// Known limits (the operator lifts them): a slider's position is its stored
// byte, clamped to the remote's range (on SIZE-type pages the firmware keeps the value elsewhere and the
// operator uses the displayed text instead, lexcat::sliderPosition); the
// LARC's toggles are unknown; a LARC program is only recognized while the
// display shows its "Bn Pm Vv" line.
#pragma once
#include "../source/catalog/catalog.hpp"
#include "../source/engine.hpp"
#include "../source/params/layout.hpp"
#include "../source/params/scheduler.hpp"
#include <cstdint>

namespace lexplug {

class RamReader {
public:
    // Find the firmware's addresses. `catalog` may be null (an unrecognized
    // set): read() then reports nothing known.
    void prepare(Engine &engine, const lexcat::Catalog *catalog) {
        catalog_ = catalog;
        found_ = false;
        v32_ = false;
        if (catalog_ == nullptr) {
            return;
        }
        if (catalog_->remote == lexcat::Remote::Larc) {
            // LDA column; ADD A; MOV H,A; ADD A; ADD H; ADD B; MOV E,A; MVI D,0; LXI H,base
            static constexpr int pattern[14] = {0x3a, -1, -1, 0x87, 0x67, 0x87, 0x84, 0x80, 0x5f, 0x16, 0x00, 0x21, -1, -1};
            for (unsigned a = 0x8000; a < 0x10000 - 14; a++) {
                bool match = true;
                for (int i = 0; i < 14; i++) {
                    if (pattern[i] >= 0 && engine.peek(uint16_t(a + unsigned(i))) != pattern[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    column_ = uint16_t(engine.peek(uint16_t(a + 1)) | engine.peek(uint16_t(a + 2)) << 8);
                    base_ = uint16_t(engine.peek(uint16_t(a + 12)) | engine.peek(uint16_t(a + 13)) << 8);
                    found_ = true;
                    break;
                }
            }
        } else {
            found_ = true;
            v32_ = catalog_->layout == "v3.2";
        }
    }

    const lexcat::Catalog *catalog() const {
        return catalog_;
    }

    // Whether the parameter record was found (false: nothing is read).
    bool found() const {
        return found_;
    }

    // Fill `out` from the machine. `lastProgram`/`lastVariation` carry the
    // LARC's last recognized program across calls (see the header).
    void read(Engine &engine, lexparams::MachineState &out) {
        out = lexparams::MachineState{};
        if (catalog_ == nullptr || !found_) {
            return;
        }
        out.rangeMin = catalog_->rawMin();
        out.rangeMax = catalog_->rawMax();
        int program = -1;
        int variation = 0;
        switch (catalog_->remote) {
            case lexcat::Remote::Larc:
                readLarcProgram(engine, program, variation);
                break;
            case lexcat::Remote::Panel: {
                program = byIdentity(engine.peek(kPanelRecord));
                uint8_t mask = engine.peek(kPanelVariation);
                for (int v = 0; v < 8; v++) {
                    if (mask == (1u << v)) {
                        variation = v + 1;
                    }
                }
                break;
            }
            case lexcat::Remote::Panel224: {
                uint8_t byte = engine.peek(programByte());
                program = byIdentity(byte & 0x3f);
                variation = 1;
                {
                    out.togglesKnown = uint8_t(1u << lexparams::kModeEnh | 1u << lexparams::kDecayOpt);
                    if ((byte & 0x40) != 0) {
                        out.toggles |= uint8_t(1u << lexparams::kModeEnh);
                    }
                    if ((byte & 0x80) != 0) {
                        out.toggles |= uint8_t(1u << lexparams::kDecayOpt);
                    }
                }
                break;
            }
        }
        if (program < 0) {
            return;
        }
        out.program = program;
        out.variation = variation;
        const lexcat::Program &p = catalog_->programs[size_t(program)];
        int count = int(p.generic.size());
        if (count > lexparams::kSliders) {
            count = lexparams::kSliders;
        }
        out.sliderCount = count;
        for (int k = 0; k < count; k++) {
            const lexcat::SliderRef &ref = p.generic[size_t(k)];
            int column = p.pages[size_t(ref.pageIndex)].column;
            uint8_t byte = 0;
            if (column >= 0) {
                byte = stored(engine, unsigned(column), unsigned(ref.slot));
            }
            out.stored[k] = byte;
            // A position is within the remote's range: some parameters are
            // stored as 0, 1 or 255 (larc.js), which no fader position means.
            int position = byte;
            if (position < out.rangeMin) {
                position = out.rangeMin;
            }
            if (position > out.rangeMax) {
                position = out.rangeMax;
            }
            out.sliders[k] = uint8_t(position);
        }
    }

    // A parameter's stored byte by record column and slot (the web
    // operators' op.stored(column, slot)).
    uint8_t stored(Engine &engine, unsigned column, unsigned slot) const {
        if (catalog_ == nullptr || !found_) {
            return 0;
        }
        switch (catalog_->remote) {
            case lexcat::Remote::Larc:
                return engine.peek(uint16_t(base_ + 6 * column + slot));
            case lexcat::Remote::Panel:
                return engine.peek(uint16_t(kPanelRecord + 1 + 6 * column + slot));
            case lexcat::Remote::Panel224:
                if (column == 0 && slot < 6) {
                    if (v32_) {
                        return engine.peek(kPots32[slot]);
                    }
                    return engine.peek(kPots4[slot]);
                }
                if (column == 1 && slot == 4 && !v32_) {
                    return engine.peek(kDiffusion4);
                }
                return 0;
        }
        return 0;
    }

private:
    // panel.js PANEL.RECORD / VARIATION
    static constexpr uint16_t kPanelRecord = 0x3c9a;
    static constexpr uint16_t kPanelVariation = 0x3c58;
    // panel224.js PANEL224.PROGRAM (v4.x) / LAYOUT_V32.PROGRAM
    static constexpr uint16_t kProgram4 = 0x3f65;
    static constexpr uint16_t kProgram32 = 0x3f44;
    // The pots' stored cells, slot order BASS, MID, CROSSOVER, TREBLE DECAY, DEPTH, PRE-DELAY.
    static constexpr uint16_t kPots4[6] = {0x3f67, 0x3f66, 0x3f68, 0x3f69, 0x3f6a, 0x3f6d};
    static constexpr uint16_t kPots32[6] = {0x3f46, 0x3f45, 0x3f47, 0x3f48, 0x3f49, 0x3f4c};
    static constexpr uint16_t kDiffusion4 = 0x3f6e;

    uint16_t programByte() const {
        if (v32_) {
            return kProgram32;
        }
        return kProgram4;
    }

    int byIdentity(int identity) const {
        if (identity == 0) {
            return -1;
        }
        for (size_t i = 0; i < catalog_->programs.size(); i++) {
            if (catalog_->programs[i].identity == identity) {
                return int(i);
            }
        }
        return -1;
    }

    // "Bn Pm Vv" in the top line after its 12-character name (larc.js
    // loadProgram: display().top.slice(12)).
    void readLarcProgram(Engine &engine, int &program, int &variation) {
        const char *text = engine.larc_text();
        char top[25] = {};
        for (int i = 0; i < 24; i++) {
            char c = text[i];
            if (c == 0) {
                break;
            }
            top[i] = c;
        }
        for (int at = 12; at + 8 <= 24; at++) {
            const char *s = top + at;
            if (s[0] == 'B' && digit(s[1]) && s[2] == ' ' && s[3] == 'P' && digit(s[4]) && s[5] == ' ' &&
                s[6] == 'V' && digit(s[7])) {
                int bank = s[1] - '0';
                int number = s[4] - '0';
                for (size_t i = 0; i < catalog_->programs.size(); i++) {
                    if (catalog_->programs[i].bank == bank && catalog_->programs[i].program == number) {
                        lastProgram_ = int(i);
                        lastVariation_ = s[7] - '0';
                        break;
                    }
                }
                break;
            }
        }
        program = lastProgram_;
        variation = lastVariation_;
    }

    static bool digit(char c) {
        return c >= '0' && c <= '9';
    }

    const lexcat::Catalog *catalog_ = nullptr;
    bool found_ = false;
    bool v32_ = false;
    uint16_t base_ = 0;
    uint16_t column_ = 0;
    int lastProgram_ = -1;
    int lastVariation_ = 0;
};

}  // namespace lexplug
