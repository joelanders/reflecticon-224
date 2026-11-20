#include "rmx16_machine.hpp"
#include "rmx16_panel.hpp"
#include "../core/engine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace rmx16plug {
namespace {

enum P { Program, Predelay, Decay, LF, HF, Swap, NumParams };

// v3.2's programs as its panel accepts them (RMX16 project notes 2026-11-11-2321 and
// 2026-11-12-0115: keypad entry searched for the predelay maximum; the key LEDs for which filters
// a program has; factory values off the display after a load). Facts about the firmware, no bytes.
enum Kind { Reverb, Shaped, Chorus, Echo, Freeze };
struct Prog {
    const char* name;
    int pre_max, fac_pre, fac_dec, fac_hf, hf_min;
    bool lf, hf;
    Kind kind;
};
const Prog kProgs[17] = {
    {"AMBIENCE", 200, 0, 40, -4, -9, true, true, Reverb},
    {"HALL A1", 200, 0, 40, -4, -9, false, true, Reverb},
    {"HALL B3", 200, 0, 40, -4, -9, true, true, Reverb},
    {"HALL C1", 200, 0, 40, -4, -9, true, true, Reverb},
    {"ROOM A0", 300, 0, 10, -8, -9, false, true, Reverb},
    {"ROOM A1", 300, 0, 10, -5, -9, false, true, Reverb},
    {"ROOM B1", 300, 0, 40, -4, -9, false, true, Reverb},
    {"PLATE A1", 300, 0, 20, -5, -9, true, true, Reverb},
    {"PLATE B1", 200, 0, 40, -4, -9, true, true, Reverb},
    {"IMAGE P1", 300, 0, 40, -4, -9, false, true, Shaped},
    {"NONLIN 2", 300, 0, 40, -4, -9, false, true, Shaped},
    {"REVERSE1", 100, 0, 40, 0, 0, false, false, Shaped},
    {"REVERSE2", 300, 0, 40, -4, -9, false, true, Shaped},
    {"CHORUS 1", 50, 7, 0, 0, -8, false, true, Chorus},
    {"ECHO", 999, 0, 0, 0, -9, true, true, Echo},
    {"DELAY", 810, 0, 0, 0, 0, false, false, Echo},
    {"FREEZE", 200, 0, 0, 0, 0, false, false, Freeze},
};

class Rmx16Machine final : public kit::Machine {
public:
    bool boot(const kit::RomSet& roms, const std::string& state, std::string* err) {
        const uint8_t* p[5];
        int n[5];
        for (int i = 0; i < 5; i++) {
            p[i] = roms.bytes(size_t(i)).data();
            n[i] = int(roms.bytes(size_t(i)).size());
        }
        const bool bat = state.size() == size_t(rmx16::Engine::kBattery);
        if (!engine_.boot(p, n, bat ? reinterpret_cast<const uint8_t*>(state.data()) : nullptr, bat ? int(state.size()) : 0)) {
            *err = "the firmware did not start (not the RMX16 v3.2 ROMs?)";
            return false;
        }
        return true;
    }

    void prepare(int host_rate, int max_block) override { engine_.prepare(host_rate, max_block); }
    long latency() const override { return engine_.latency(); }
    bool live_readouts() const override { return true; }

    void set_params(const std::vector<float>& v) override {
        rmx16::Target t;
        t.prog = std::clamp(int(std::lround(v[Program])), 0, 16) + 1;
        const Prog& g = kProgs[t.prog - 1];
        t.pre = std::clamp(int(std::lround(v[Predelay])), 0, g.pre_max);
        t.dec = std::clamp(int(std::lround(v[Decay] * 10.0f)), 0, 99);
        t.has_lf = g.lf;
        t.has_hf = g.hf;
        t.lf = g.lf ? std::clamp(int(std::lround(v[LF])), -9, 9) : 0;
        t.hf = g.hf ? std::clamp(int(std::lround(v[HF])), g.hf_min, 0) : 0;
        engine_.set_target(t);
        swap_on_ = v[Swap] > 0.5f;
        engine_.set_swap(swap_on_);
        // before audio starts, type it here so the first block hears it (a session load, an
        // offline render); once audio runs, the audio thread's driver types it
        if (!audio_started_.load(std::memory_order_acquire)) {
            int free = 0;
            if (owner_.compare_exchange_strong(free, 1, std::memory_order_acq_rel)) {
                if (!audio_started_.load(std::memory_order_acquire)) engine_.run_until_idle();
                owner_.store(0, std::memory_order_release);
            }
        }
    }

    kit::Readouts readouts() const override {
        kit::Readouts o;
        o.param.assign(NumParams, "");
        rmx16::Panel p = engine_.panel();
        if (!p.valid) return o;
        if (p.stopped) {
            o.status = "the firmware stopped";
            return o;
        }
        auto field = [&](int from, int to) {
            std::string s;
            for (int k = from; k <= to; k++) {
                s += p.dig[k];
                if (p.dp[k]) s += '.';
            }
            size_t a = s.find_first_not_of(' ');
            return a == std::string::npos ? std::string() : s.substr(a);
        };
        auto sign = [](int sg) { return sg < 0 ? std::string("-") : sg > 0 ? std::string("+") : std::string(); };
        std::string prog = field(0, 1), pre = field(2, 4), dec = field(5, 6), lf = field(8, 8), hf = field(10, 10);
        std::string name(p.name, 8);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        int pn = std::atoi(prog.c_str());
        const Prog* g = pn >= 1 && pn <= 17 ? &kProgs[pn - 1] : nullptr;
        o.param[Program] = prog + " " + name;
        o.param[Predelay] = pre + " ms";
        if (g && g->kind == Freeze) {
            int d = int(std::lround(std::atof(dec.c_str()) * 10));
            o.param[Decay] = d == 0 ? "clear" : d == 1 ? "hold" : "add";   // the firmware's table, not the manual's
        } else if (g && g->kind == Chorus) {
            o.param[Decay] = "depth " + dec;
        } else if (g && g->kind == Echo) {
            o.param[Decay] = "feedback " + dec;
        } else if (g && g->kind == Shaped) {
            o.param[Decay] = "length " + dec;
        } else {
            o.param[Decay] = dec + " s";
        }
        o.param[LF] = g && g->lf ? sign(p.lf_sign) + lf : "";
        o.param[HF] = g && g->hf ? sign(p.hf_sign) + hf : "";
        o.param[Swap] = swap_on_ ? "DAC B left" : "DAC A left";
        // the panel left to right: program, predelay, decay, LF, HF, then the name
        o.panel = prog + "  " + field(2, 4) + "  " + dec + "  " + sign(p.lf_sign) + lf + "  " + sign(p.hf_sign) + hf +
                  "   " + name;
        if (p.busy) o.status = "typing on the panel...";
        return o;
    }

    // The battery RAM: the stores and the unit's last settings (hardware mode can reach them).
    std::string state() const override { return engine_.battery(); }

    void panel_key(int id, bool down) override { engine_.panel_key(id, down); }
    void panel_input(int id, float v) override {
        if (id == InPower) engine_.set_power(v > 0.5f);
        else engine_.set_pot(id, std::clamp(int(std::lround(v)), 0, 255));
    }
    kit::PanelState panel() const override {
        kit::PanelState s;
        rmx16::Panel p = engine_.panel();
        if (!p.valid) return s;
        s.valid = !p.stopped;
        s.digits.assign(p.seg, p.seg + RMXC_NDIG);
        // the TIL327 sign digits from E23: g = minus bar, b = vertical bar
        s.digits[7] = uint8_t((p.signs & 0x40 ? 0x40 : 0) | (p.signs & 0x20 ? 0x02 : 0));
        s.digits[9] = uint8_t((p.signs & 0x08 ? 0x40 : 0) | (p.signs & 0x04 ? 0x02 : 0));
        s.leds.assign(11, 0);
        for (int i = 0; i < 10; i++) s.leds[size_t(i)] = (p.leds >> i & 1) ? 255 : 0;
        s.leds[10] = p.powered ? 255 : 0;
        s.alpha.assign(p.name, 8);
        if (p.driver_key) s.held.push_back(p.driver_key);
        if (p.adopt_serial) {
            const rmx16::Target& t = p.adopt;
            const float nan = std::nanf("");
            s.adopt.assign(Swap, nan);
            if (t.prog >= 1 && t.prog <= 17) {
                const Prog& g = kProgs[t.prog - 1];
                s.adopt[Program] = float(t.prog - 1);
                s.adopt[Predelay] = float(t.pre);
                s.adopt[Decay] = float(t.dec) / 10.0f;
                if (g.lf) s.adopt[LF] = float(t.lf);
                if (g.hf) s.adopt[HF] = float(t.hf);
            }
            s.adopt_serial = p.adopt_serial;
        }
        return s;
    }

    std::vector<kit::Preset> presets() const override {
        std::vector<kit::Preset> out;
        for (int i = 0; i < 17; i++) {
            const Prog& g = kProgs[i];
            char name[48];
            std::snprintf(name, sizeof name, "%02d %s (factory)", i + 1, g.name);
            out.push_back({name, {float(i), float(g.fac_pre), float(g.fac_dec) / 10.0f, 0.0f, float(g.fac_hf)}});
        }
        return out;
    }

    void process(const float* const* in, float* out_l, float* out_r, int n) override {
        int free = 0;
        if (!owner_.compare_exchange_strong(free, 2, std::memory_order_acq_rel)) {   // the control side is typing
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        audio_started_.store(true, std::memory_order_release);
        engine_.process(in[0], out_l, out_r, n);
        owner_.store(0, std::memory_order_release);
    }

private:
    rmx16::Engine engine_;
    std::atomic<int> owner_{0};   // 0 free, 1 control (typing before audio), 2 audio
    std::atomic<bool> audio_started_{false};
    bool swap_on_ = false;   // control thread
};

std::unique_ptr<kit::Machine> boot(const kit::RomSet& roms, const std::string& state, std::string* err) {
    if (!roms.complete()) {
        *err = "ROMs missing";
        return nullptr;
    }
    auto m = std::make_unique<Rmx16Machine>();
    if (!m->boot(roms, state, err)) return nullptr;
    return m;
}

kit::Info make_info() {
    kit::Info i;
    i.name = "Reflecticon RMX16";
    i.state_tag = "RMX16KIT";
    i.settings_name = "ReflecticonRMX16";
    i.rom_env = "RMX16_ROMPATH";
    i.inputs = 1;
    i.tail_seconds = 15;
    using K = kit::Param;
    std::vector<std::string> progs;
    for (auto& g : kProgs) progs.push_back(g.name);
    i.params = {
        {"program", "Program", K::Choice, 0, 16, 0, progs, ""},
        {"predelay", "Predelay", K::Int, 0, 999, 0, {}, "ms"},
        {"decay", "Decay", K::Float, 0.0f, 9.9f, 4.0f, {}, ""},
        {"lf", "Decay LF", K::Int, -9, 9, 0, {}, ""},
        {"hf", "Decay HF", K::Int, -9, 0, -4, {}, ""},
        {"swap", "Swap outputs", K::Bool, 0, 1, 0, {}, ""},
    };
    kit::tempo_sync(i.params, {"predelay"});
    kit::describe(i.params, {
        {"program",
         "The reverb program, run by the unit's own firmware."},
        {"predelay",
         "Delay before the reverb starts."},
        {"decay",
         "Reverb time in seconds."},
        {"lf",
         "Low-frequency decay against the main decay: + rings longer, - shorter. Not every program has it."},
        {"hf",
         "High-frequency decay against the main decay: lower values darken the tail. Not every program has it."},
        {"swap",
         "Swaps the two outputs (DAC A and DAC B)."},
    });
    i.roms = {
        {"boot ROM R316", {2048, 4096}, {{"eb88029bea7fd6757b6e56ad35d6eeee8f15e1893ae08c993a24cac07854a04c", "R316"}}, {"r316", "316"}},
        {"IC1", {4096}, {{"bb2ca8a62a4631f0b923b92cb0d01aa15bf878371799df67dc3c77ec6daeeaba", "v3.2"}}, {"ic1"}},
        {"IC2", {4096}, {{"fefefd75c15a7af44c3f0d64f28f5953d00ddc59d8301e772186635dc733f1e6", "v3.2"}}, {"ic2"}},
        {"IC3", {4096}, {{"59093d1d2843132a7745bf588c48face9d2a7a0fa1dbeeb78c44a99c23047303", "v3.2"}}, {"ic3"}},
        {"IC4", {4096}, {{"c19d1ea6f45c8d401376890bf69fe088a8bafbcaff1edf85adaddb896f6a6899", "v3.2"}}, {"ic4"}},
    };
    i.about = "An emulation of a 1980s digital reverb running its own firmware, from the schematics. "
              "Bring your own v3.2 ROM images. Not affiliated with or endorsed by the maker.";
    i.panel = rmx16_layout();
    return i;
}

}  // namespace

const kit::MachineType& machine_type() {
    static const kit::MachineType t{make_info(), &boot};
    return t;
}

}  // namespace rmx16plug
