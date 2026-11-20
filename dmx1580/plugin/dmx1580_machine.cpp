#define DMX_CHIPS_IMPL
#include "dmx1580_machine.hpp"
#include "dmx1580_panel.hpp"
#include "../core/engine.hpp"
#include "dmx1580_patches.h"   // generated from core/patches.txt: bit masks and hashes, no ROM bytes
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace dmx1580plug {
namespace {

enum P {
    DelayA, DelayB, PitchA, PitchB,
    InA, InB, FbGainA, FbGainB, FbFilterA, FbFilterB, Out,
    Vco, VcoSpeed, VcoDepth,
    PhaseA, MonoIn, MixOut, LockIn, Trigger,
    Ram, Boards, Analog, Smooth,
    NumParams
};

const unsigned kWords[4] = {4096, 16384, 32768, 65536};

std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
float db_gain(float db) { return db <= -29.95f ? 0.0f : std::pow(10.0f, db / 20.0f); }
std::string db_text(float db) { return db <= -29.95f ? std::string("off") : fmt("%+.1f dB", db); }

class DmxMachine final : public kit::Machine {
public:
    bool boot(const kit::RomSet& roms, std::string* err) {
        for (int i = 0; i < 4; i++) roms_[i] = roms.bytes(size_t(i));
        opts_ = options_for(defaults());
        auto u = build(opts_, err);
        if (!u) return false;
        engine_.install(std::move(u));
        return true;
    }

    void prepare(int host_rate, int max_block) override { engine_.prepare(host_rate, max_block); }
    long latency() const override { return engine_.latency(); }
    bool live_readouts() const override { return true; }

    void set_params(const std::vector<float>& v) override {
        v_ = v;
        engine_.collect();
        dmxkit::Target t = target_for(v);
        dmxkit::Knobs k = knobs_for(v);
        dmxkit::Options o = options_for(v);
        if (!(o == opts_)) {   // a different machine: build it, type the settings in, then swap
            std::string err;
            auto u = build(o, &err);
            if (u) {
                dmxkit::Engine::type_into(*u, t, k);
                engine_.install(std::move(u));
                opts_ = o;
                error_.clear();
            } else {
                error_ = "cannot run with these options: " + err;
            }
        }
        engine_.set_target(t);
        engine_.set_knobs(k);
        // before audio starts, type it here so the first block hears it (a session load, an
        // offline render); once audio runs, the audio thread's driver types it
        if (!engine_.audio_started()) engine_.type_now();
    }

    kit::Readouts readouts() const override {
        kit::Readouts o;
        o.param.assign(NumParams, "");
        if (v_.size() < NumParams) return o;
        dmxkit::Shown s = engine_.shown();
        const bool two = v_[Boards] > 0.5f;
        if (s.have_cur) {
            for (int c = 0; c < 2; c++) {
                o.param[DelayA + c] = std::to_string(s.cur.delay[c]) + " ms";
                if (c == 1 && !two) continue;   // one pitch board: B has no pitch
                int r = s.cur.pitch[c];
                o.param[PitchA + c] = r == 1000 ? std::string("off (1.000)")
                                                : fmt("x%.3f", r / 1000.0) + fmt(" (%+.1f st)", 12.0 * std::log2(r / 1000.0));
            }
        }
        o.param[InA] = db_text(v_[InA]);
        o.param[InB] = v_[MonoIn] > 0.5f ? std::string() : db_text(v_[InB]);   // the single input feeds both
        for (int c = 0; c < 2; c++) {   // the pot's rotation, and the DC loop gain it gives
            o.param[FbGainA + c] = fmt("%.0f %%", v_[FbGainA + c]) + fmt(" (loop x%.2f)", dmx::Machine::fb_gain_law(v_[FbGainA + c] * 0.01));
            o.param[FbFilterA + c] = v_[FbFilterA + c] <= 0.f ? std::string("flat") : fmt("%.0f %%", v_[FbFilterA + c]);
        }
        o.param[Out] = db_text(v_[Out]);
        const bool vco = v_[Vco] > 0.5f;
        o.param[Vco] = vco ? "VCO" : "XTAL";
        if (vco) {
            o.param[VcoSpeed] = fmt("%.2f Hz", v_[VcoSpeed]);
            o.param[VcoDepth] = fmt("%.1f %%", v_[VcoDepth]);
        }
        o.param[PhaseA] = v_[PhaseA] > 0.5f ? "A out of phase" : "in phase";
        o.param[MonoIn] = v_[MonoIn] > 0.5f ? "A feeds both" : "stereo";
        o.param[MixOut] = v_[MixOut] > 0.5f ? "(A + B) / 2" : "separate";
        o.param[LockIn] = v_[LockIn] > 0.5f ? "locked" : "off";
        o.param[Trigger] = v_[Trigger] > 0.5f ? "held" : "off";
        int ri = std::clamp(int(std::lround(v_[Ram])), 0, 3);
        o.param[Ram] = fmt("up to %.0f ms", double(kWords[ri] / 40));
        o.param[Boards] = two ? "two" : "one (B has no pitch)";
        o.param[Analog] = v_[Analog] > 0.5f ? "on" : "off";
        o.param[Smooth] = v_[Smooth] > 0.5f ? "on" : "off";
        if (!s.valid) return o;
        // the display and the LEDs under it (A, B: the channel; C: pitch entry). The VCO's rate LED
        // blinks once per cycle and is left out (a moment's light, not a setting).
        o.panel = s.display;
        const char* led[3] = {"A", "B", "C"};
        for (int i = 0; i < 3; i++)
            if (s.leds & (1u << i)) o.panel += std::string("  ") + led[i];
        if (!error_.empty()) o.status = error_;
        else if (s.busy) o.status = "typing on the keypad...";
        else if (s.gave_up) o.status = "the display did not confirm a setting";
        return o;
    }

    void panel_key(int id, bool down) override { engine_.panel_key(id, down); }
    kit::PanelState panel() const override {
        kit::PanelState o;
        dmxkit::Shown s = engine_.shown();
        if (!s.valid || v_.size() < NumParams) return o;
        o.valid = !s.stopped;
        o.digits.assign(s.seg, s.seg + 4);
        o.leds.assign(NumDmxLeds, 0);
        auto on = [&](int i, bool b) { o.leds[size_t(i)] = b ? 255 : 0; };
        on(LedA, s.leds & 1);
        on(LedB, s.leds & 2);
        on(LedC, s.leds & 4);
        on(LedRate, s.vco_led);
        on(LedXtal, v_[Vco] <= 0.5f);
        on(LedVco, v_[Vco] > 0.5f);
        on(LedStereo, v_[MonoIn] <= 0.5f && v_[MixOut] <= 0.5f);
        for (int c = 0; c < 2; c++) {   // h/room: 18, 12, 6 dB below the samplers' clipping
            on(c ? LedB18 : LedA18, s.peak[c] > 0.126f);
            on(c ? LedB12 : LedA12, s.peak[c] > 0.251f);
            on(c ? LedB6 : LedA6, s.peak[c] > 0.501f);
        }
        on(LedPower, true);
        if (s.driver_key >= 0) o.held.push_back(s.driver_key);
        if (s.adopt_serial) {
            o.adopt.assign(s.adopt, s.adopt + 4);   // delay A, delay B, pitch A, pitch B: the first four parameters
            o.adopt_serial = s.adopt_serial;
        }
        return o;
    }

    void process(const float* const* in, float* out_l, float* out_r, int n) override {
        engine_.process(in[0], in[1], out_l, out_r, n);
    }

private:
    static std::vector<float> defaults();

    std::unique_ptr<dmxkit::Unit> build(const dmxkit::Options& o, std::string* err) const {
        auto u = std::make_unique<dmxkit::Unit>();
        const uint8_t* p[4] = {roms_[0].data(), roms_[1].data(), roms_[2].data(), roms_[3].data()};
        if (!u->boot(p, kPatches, o, err)) return nullptr;
        return u;
    }
    static dmxkit::Options options_for(const std::vector<float>& v) {
        dmxkit::Options o;
        int ri = std::clamp(int(std::lround(v[Ram])), 0, 3);
        o.opts = dmxkit::opts_for(kWords[ri], v[Boards] > 0.5f);
        o.analog = v[Analog] > 0.5f;
        o.interp = v[Smooth] > 0.5f;
        return o;
    }
    static dmxkit::Target target_for(const std::vector<float>& v) {
        dmxkit::Target t;
        int ri = std::clamp(int(std::lround(v[Ram])), 0, 3);
        int max_ms = int(kWords[ri] / 40);   // past the RAM's length the firmware's delay wraps
        for (int c = 0; c < 2; c++) {
            t.delay[c] = std::clamp(int(std::lround(v[DelayA + c])), 0, max_ms);
            t.pitch[c] = std::clamp(int(std::lround(v[PitchA + c] * 1000.0f)), 500, 2000);
        }
        t.pitch_on[1] = v[Boards] > 0.5f;
        if (!t.pitch_on[1]) t.pitch[1] = 1000;
        return t;
    }
    static dmxkit::Knobs knobs_for(const std::vector<float>& v) {
        dmxkit::Knobs k;
        k.lockin = v[LockIn] > 0.5f;
        k.trigger = v[Trigger] > 0.5f;
        k.vco = v[Vco] > 0.5f;
        k.vco_speed = v[VcoSpeed];
        k.vco_depth = v[VcoDepth] * 0.01f;
        k.in_gain[0] = db_gain(v[InA]);
        k.in_gain[1] = db_gain(v[InB]);
        for (int c = 0; c < 2; c++) {
            k.fb_gain[c] = v[FbGainA + c] * 0.01f;
            k.fb_filter[c] = v[FbFilterA + c] * 0.01f;
        }
        k.out_gain = db_gain(v[Out]);
        k.phase_a = v[PhaseA] > 0.5f;
        k.mono_in = v[MonoIn] > 0.5f;
        k.mix_out = v[MixOut] > 0.5f;
        return k;
    }

    std::vector<uint8_t> roms_[4];
    dmxkit::Engine engine_;
    dmxkit::Options opts_;
    std::vector<float> v_;   // control thread: the last parameters
    std::string error_;
};

std::unique_ptr<kit::Machine> boot(const kit::RomSet& roms, const std::string&, std::string* err) {
    if (!roms.complete()) {
        *err = "ROMs missing";
        return nullptr;
    }
    auto m = std::make_unique<DmxMachine>();
    if (!m->boot(roms, err)) return nullptr;
    return m;
}

kit::Info make_info() {
    kit::Info i;
    i.name = "Reflecticon DMX 15-80";
    i.state_tag = "DX1580KIT";
    i.settings_name = "ReflecticonDMX1580";
    i.rom_env = "DMX1580_ROMPATH";
    i.inputs = 2;
    i.tail_seconds = 5;
    using K = kit::Param;
    i.params = {
        {"delayA", "Delay A", K::Int, 0, 1638, 250, {}, "ms"},
        {"delayB", "Delay B", K::Int, 0, 1638, 375, {}, "ms"},
        {"pitchA", "Pitch A", K::Float, 0.5f, 2.0f, 1.0f, {}, "x"},
        {"pitchB", "Pitch B", K::Float, 0.5f, 2.0f, 1.0f, {}, "x"},
        {"inA", "Input A", K::Float, -30, 12, 0, {}, "dB"},
        {"inB", "Input B", K::Float, -30, 12, 0, {}, "dB"},
        {"fbGainA", "F/B Gain A", K::Float, 0, 100, 0, {}, "%"},     // pot rotation (10K log, loaded)
        {"fbGainB", "F/B Gain B", K::Float, 0, 100, 0, {}, "%"},
        {"fbFilterA", "F/B Filter A", K::Float, 0, 100, 0, {}, "%"},
        {"fbFilterB", "F/B Filter B", K::Float, 0, 100, 0, {}, "%"},
        {"out", "Output", K::Float, -30, 6, 0, {}, "dB"},
        {"vco", "XTAL/VCO", K::Bool, 0, 1, 0, {}, ""},
        {"vcoSpeed", "VCO Speed", K::Float, 0.05f, 20.0f, 1.0f, {}, "Hz"},
        {"vcoDepth", "VCO Depth", K::Float, 0, 20, 0, {}, "%"},
        {"phaseA", "Phase A Out", K::Bool, 0, 1, 0, {}, ""},
        {"monoIn", "Mono Input", K::Bool, 0, 1, 0, {}, ""},
        {"mixOut", "Mix Outputs", K::Bool, 0, 1, 0, {}, ""},
        {"lockin", "Lock In", K::Bool, 0, 1, 0, {}, ""},
        {"trigger", "Trigger", K::Bool, 0, 1, 0, {}, ""},
        {"ram", "RAM per channel", K::Choice, 0, 3, 3, {"4K words", "16K words", "32K words", "64K words"}, ""},
        {"boards", "Pitch boards", K::Choice, 0, 1, 1, {"one", "two"}, ""},
        {"analog", "Analog stages", K::Bool, 0, 1, 0, {}, ""},
        {"smooth", "Smooth pitch (later units)", K::Bool, 0, 1, 0, {}, ""},
    };
    kit::describe(i.params, {
        {"delayA",
         "Channel A's delay time, typed into the unit's keypad. The longest delay depends on RAM per channel."},
        {"delayB",
         "Channel B's delay time, typed into the unit's keypad. The longest delay depends on RAM per channel."},
        {"pitchA",
         "Channel A's pitch change as a ratio: 0.5 is an octave down, 2.0 an octave up, 1.000 off."},
        {"pitchB",
         "Channel B's pitch change as a ratio: 0.5 is an octave down, 2.0 an octave up, 1.000 off. Needs two pitch boards."},
        {"inA",
         "Channel A's input gain. The hardware panel's 6/12/18 dB LEDs show the headroom."},
        {"inB",
         "Channel B's input gain. The hardware panel's 6/12/18 dB LEDs show the headroom."},
        {"fbGainA",
         "Feedback (regeneration) on channel A: how much of the delayed signal goes back into the input. More gives more repeats; near the top they build up."},
        {"fbGainB",
         "Feedback (regeneration) on channel B: how much of the delayed signal goes back into the input. More gives more repeats; near the top they build up."},
        {"fbFilterA",
         "A treble cut inside channel A's feedback loop: each repeat comes back darker than the last. 0 % is flat."},
        {"fbFilterB",
         "A treble cut inside channel B's feedback loop: each repeat comes back darker than the last. 0 % is flat."},
        {"out",
         "Output level."},
        {"vco",
         "The clock: the steady crystal (XTAL), or the VCO, which VCO Speed and Depth sweep for chorus, flanging and vibrato."},
        {"vcoSpeed",
         "How fast the VCO sweeps the clock (with XTAL/VCO on VCO)."},
        {"vcoDepth",
         "How far the VCO sweeps the clock (with XTAL/VCO on VCO)."},
        {"phaseA",
         "Inverts channel A's output."},
        {"monoIn",
         "Feeds input A to both channels."},
        {"mixOut",
         "Sends (A + B) / 2 to both outputs."},
        {"lockin",
         "The panel's NORM / LOCK IN switch, which the firmware reads: holds what is in the delay memory so it repeats."},
        {"trigger",
         "Holds the firmware's trigger input, which a loud input on channel A normally pulses."},
        {"ram",
         "How much delay memory each channel has (units were fitted with different amounts): more memory, longer delays."},
        {"boards",
         "One or two pitch-change boards fitted. With one, channel B has no pitch change."},
        {"analog",
         "Models the input filters, the DAC's track-and-hold and the output filters from the schematics. Off: the digital path alone."},
        {"smooth",
         "Interpolates the pitch changer's reads: smoother pitch shifts, a guess at what later units did."},
    });
    i.roms = {
        {"DMX23-3 (processor board)", {2048}, {{"ae7dcbdcf6503990fa8e241f31dfef289a0f37507befb105da9ef409f9697766", "DMX23-3"}}, {"dmx23", "dmx-23"}},
        {"DMX-10-SHL-406-1 (extender)", {2048}, {{"64ddbabcdd4172a2e8634d9e83bc6dc2f02c324e19ee0eb48e0bcae6c7ed69a3", "SHL-406-1"}}, {"406-1"}},
        {"DMX-10-SHL-406-2 (extender)", {2048}, {{"2609642225359c49969e2cd752d596270a8820637407e59efa3002cc6bcf617c", "SHL-406-2"}}, {"406-2"}},
        {"DMX-10-SHL-406-3 (extender)", {2048},
         {{"29f17c12f1382d065e0b20d63c1f57fdf6dc9ae202c9104c3dca82dad879ee4d", "SHL-406-3 (repaired at load)"},
          {"8e11e160f042a290d6fceaf622848716a5ea3d848cf5dff476722141da74aedb", "SHL-406-3 (already repaired)"}},
         {"406-3"}},
    };
    i.about = "An emulation of a 1970s/80s digital delay and pitch changer running its own firmware, from the "
              "schematics. Bring your own ROM images. Not affiliated with or endorsed by the maker.";
    i.panel = dmx1580_layout();
    return i;
}

std::vector<float> DmxMachine::defaults() {
    std::vector<float> v;
    for (auto& p : make_info().params) v.push_back(p.def);
    return v;
}

}  // namespace

const kit::MachineType& machine_type() {
    static const kit::MachineType t{make_info(), &boot};
    return t;
}

}  // namespace dmx1580plug
