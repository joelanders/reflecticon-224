// A booted machine as the processor holds it (LiveMachine), and the
// Scheduler's view of it (MachineSink, lexparams::OperatorSink).
//
// A LiveMachine is built and booted on the boot thread, handed to the audio
// thread through an atomic pointer, and destroyed off the audio thread
// (PluginProcessor). The MachineSink belongs to whichever thread drives the
// machine: the processor's (audio thread) points at the live machine; a
// restore replay on the boot thread uses its own over the new machine.
#pragma once
#include "operator_port.hpp"
#include "ram_reader.hpp"
#include "session.hpp"
#include "../source/params/scheduler.hpp"
#include "../source/params/seqlock.hpp"
#include <cmath>
#include <cstring>
#include <memory>

namespace lexplug {

struct LiveMachine {
    LiveMachine(int model, const lexcat::Catalog *catalog_) : session(model), catalog(catalog_) {
        port = makeOperatorPort(session.engine, catalog);
    }

    // Render through the operator (which renders the engine), with the
    // Session's test-hook controls on the way. False once stopped.
    bool render(const float *left, const float *right, float *const out[4], int frames) {
        OperatorPort *p = port.get();
        return session.render_with(left, right, out, frames,
                                   [p](const float *l, const float *r, float *const o[4], int n) {
                                       return p->render(l, r, o, n);
                                   });
    }

    Session session;
    const lexcat::Catalog *catalog;     // the set's catalog, or null (unrecognized set)
    std::unique_ptr<OperatorPort> port;
    RamReader reader;                   // prepared on the boot thread
    uint32_t job = 0;                   // the boot job that made it (PluginProcessor)
    bool restored = false;              // a saved state was replayed into it
    char hash[17] = {};                 // the set's catalog hash
};

// The direct parameters, as the render applies them (audio thread).
struct DirectMix {
    double gain = 1.0;       // input level as a linear gain (10^(dB/20), as app.js); 1 = 0 dB
    double wet = 1.0;        // 0 = dry .. 1 = wet
    int outLeft = 0;         // DAC 0..3 = A..D
    int outRight = 2;
    bool analog = true;
};

class MachineSink final : public lexparams::OperatorSink {
public:
    explicit MachineSink(DirectMix &mix) : mix_(mix) {}

    // The machine the sink stands for (null: none; ready() is then false).
    void attach(LiveMachine *machine) {
        machine_ = machine;
        forget_sliders();
        state_ = lexparams::MachineState{};
        seen_echoes_ = 0;
        publish_texts();
    }
    LiveMachine *machine() const {
        return machine_;
    }
    // The machine stopped: no more tasks until another one is attached.
    void set_stopped(bool stopped) {
        stopped_ = stopped;
    }

    bool ready() const override {
        return machine_ != nullptr && !stopped_;
    }
    bool busy() const override {
        return machine_ != nullptr && machine_->port->busy();
    }

    bool loadProgram(int index) override {
        const lexcat::Catalog *catalog = machine_->catalog;
        if (catalog == nullptr || index < 0 || size_t(index) >= catalog->programs.size()) {
            return false;
        }
        if (!machine_->port->loadProgram(index, catalog->programs[size_t(index)])) {
            return false;
        }
        load_started_ = true;
        return true;
    }
    bool loadVariation(int variation) override {
        if (!machine_->port->loadVariation(variation)) {
            return false;
        }
        load_started_ = true;
        return true;
    }
    bool moveSlider(int k, int raw) override {
        const lexcat::Catalog *catalog = machine_->catalog;
        if (catalog == nullptr || state_.program < 0 || size_t(state_.program) >= catalog->programs.size()) {
            return false;
        }
        const lexcat::Program &program = catalog->programs[size_t(state_.program)];
        if (k < 1 || size_t(k) > program.generic.size()) {
            return false;
        }
        const lexcat::SliderRef &ref = program.generic[size_t(k - 1)];
        if (!machine_->port->moveSlider(ref.page, ref.slot, raw)) {
            return false;
        }
        move_k_ = k;
        move_raw_ = raw;
        return true;
    }
    bool setToggle(int toggle, bool on) override {
        return machine_->port->setToggle(toggle, on);
    }

    void setLevelDb(float db) override {
        mix_.gain = std::pow(10.0, double(db) / 20.0);
    }
    void setDryWet(float wet) override {
        mix_.wet = wet;
    }
    void setOutput(int side, int dac) override {
        if (side == 0) {
            mix_.outLeft = dac;
        } else {
            mix_.outRight = dac;
        }
    }
    void setAnalog(bool on) override {
        mix_.analog = on;
        if (machine_ != nullptr) {
            machine_->session.engine.set_analog(on);
        }
    }

    // RAM first, then what the operator knows on top; then each slider's
    // position (lexparams::MachineState::sliders) and display text as the web
    // page places them (app.js describeProgram and drawPages):
    //   - after a move, the position the move asked for and the firmware's
    //     echo, for as long as the stored byte is the one the move left;
    //   - else, with a display text known (an echo, or the variation's preset
    //     text when the stored byte is the preset's), lexcat's rule: the
    //     stored byte where the table agrees with the text, else the table's
    //     raw for the text (the panels store some parameters in another
    //     domain than the pot reading: the 224's BASS keeps pot >> 3);
    //   - else the stored byte, clamped to the remote's range;
    //   - on a measured slider (catalogs-extra/stored/), a position that does
    //     not store the byte (or any, with no text known) is replaced by
    //     lexcat::readingFor (the canonical control value that stores it).
    // A load forgets moves and texts. No allocation.
    void readState(lexparams::MachineState &out) override {
        machine_->reader.read(machine_->session.engine, out);
        OperatorKnowledge knows;
        machine_->port->describe(knows);
        if (knows.program >= 0) {
            out.program = knows.program;
        }
        if (knows.variation > 0) {
            out.variation = knows.variation;
        }
        out.toggles = uint8_t((out.toggles & ~knows.togglesKnown) | (knows.toggles & knows.togglesKnown));
        out.togglesKnown |= knows.togglesKnown;

        if (load_started_ || out.program != state_.program || out.variation != state_.variation) {
            forget_sliders();
            load_started_ = false;
        }
        const lexcat::Catalog *catalog = machine_->catalog;
        const lexcat::Program *program = nullptr;
        if (catalog != nullptr && out.program >= 0 && size_t(out.program) < catalog->programs.size()) {
            program = &catalog->programs[size_t(out.program)];
        }
        if (move_k_ > 0 && program != nullptr && move_k_ <= out.sliderCount) {
            int k = move_k_;
            moved_raw_[k - 1] = move_raw_;
            moved_stored_[k - 1] = out.stored[k - 1];
            const lexcat::SliderRef &ref = program->generic[size_t(k - 1)];
            if (knows.echoCount != seen_echoes_ && knows.echoPage == ref.page && knows.echoSlot == ref.slot) {
                copy(text_[k - 1], knows.echo);
            } else {
                text_[k - 1][0] = 0;
            }
        }
        seen_echoes_ = knows.echoCount;
        move_k_ = 0;

        for (int k = 1; k <= out.sliderCount && program != nullptr; k++) {
            const lexcat::SliderRef &ref = program->generic[size_t(k - 1)];
            const lexcat::Slider &slider = program->measuredSlider(ref, out.variation);
            int stored = out.stored[k - 1];
            if (moved_raw_[k - 1] >= 0 && moved_stored_[k - 1] == stored) {
                out.sliders[k - 1] = uint8_t(clamp(moved_raw_[k - 1], out.rangeMin, out.rangeMax));
                continue;
            }
            moved_raw_[k - 1] = -1;
            if (text_[k - 1][0] == 0) {
                preset_text(*program, out.variation, ref, stored, text_[k - 1]);
            }
            out.sliders[k - 1] = uint8_t(position(slider, stored, text_[k - 1], out.rangeMin, out.rangeMax));
        }
        state_ = out;
        publish_texts();
    }

    // The display text per slider_k as of the last readState ('' = unknown),
    // for the editor (UiSnapshot::sliderText). Any thread.
    struct Texts {
        char text[lexparams::kSliders][24];
    };
    const lexparams::Seqlock<Texts> &texts() const {
        return texts_;
    }

    // Where a slider sits for a stored byte and a display text (lexcat::
    // sliderPosition, without its string copy), clamped to the range.
    static int position(const lexcat::Slider &slider, int stored, const char *shown, int low, int high) {
        int at = stored;
        if (!slider.table.empty() && shown[0] != 0) {
            const std::string *text = nullptr;
            for (const lexcat::TableRun &run : slider.table) {
                if (run.raw > stored) {
                    break;
                }
                text = &run.text;
            }
            if (text == nullptr || *text != shown) {
                at = lexcat::tableRaw(slider.table, shown, high);
            }
        }
        // A measured slider: there if that control value stores the byte
        // (with a text known), else lexcat::readingFor.
        if (slider.measured()) {
            if (shown[0] == 0 || !slider.stores(at, stored)) {
                int reading = lexcat::readingFor(slider, stored);
                if (reading >= 0) {
                    return clamp(reading, low, high);
                }
            }
        }
        return clamp(at, low, high);
    }

private:
    static int clamp(int value, int low, int high) {
        if (value < low) {
            return low;
        }
        if (value > high) {
            return high;
        }
        return value;
    }

    static void copy(char (&to)[24], const char *from) {
        size_t n = 0;
        while (n + 1 < sizeof to && from[n] != 0) {
            to[n] = from[n];
            n++;
        }
        while (n > 0 && to[n - 1] == ' ') {
            n--;
        }
        to[n] = 0;
    }

    // The variation's preset text for a slider, when the stored byte is the
    // preset's (app.js describeProgram); '' otherwise.
    static void preset_text(const lexcat::Program &program, int variation, const lexcat::SliderRef &ref, int stored,
                            char (&out)[24]) {
        out[0] = 0;
        int v = program.presetVariation(variation);
        auto raw = program.raw.find(v);
        auto text = program.presets.find(v);
        if (raw == program.raw.end() || text == program.presets.end()) {
            return;
        }
        size_t page = size_t(ref.pageIndex);
        size_t slot = size_t(ref.slot);
        if (page >= raw->second.size() || slot >= raw->second[page].size() || page >= text->second.size() ||
            slot >= text->second[page].size()) {
            return;
        }
        if (raw->second[page][slot] != stored) {
            return;
        }
        copy(out, text->second[page][slot].c_str());
    }

    void forget_sliders() {
        for (int k = 0; k < lexparams::kSliders; k++) {
            text_[k][0] = 0;
            moved_raw_[k] = -1;
            moved_stored_[k] = -1;
        }
    }

    void publish_texts() {
        Texts t;
        std::memcpy(t.text, text_, sizeof t.text);
        texts_.write(t);
    }

    DirectMix &mix_;
    LiveMachine *machine_ = nullptr;
    bool stopped_ = false;
    lexparams::MachineState state_;
    bool load_started_ = false;
    int move_k_ = 0;
    int move_raw_ = 0;
    uint32_t seen_echoes_ = 0;
    char text_[lexparams::kSliders][24] = {};
    int moved_raw_[lexparams::kSliders] = {};
    int moved_stored_[lexparams::kSliders] = {};
    lexparams::Seqlock<Texts> texts_;
};

}  // namespace lexplug
