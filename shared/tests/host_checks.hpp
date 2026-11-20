// Checks every machine on the kit should pass, JUCE-free, through kit::Host as a plugin drives it:
//   1. block-size invariance: random block sizes 1..1500 give exactly the output of 512-blocks;
//   2. no allocation on the audio thread (the test's operator new hook counts during process());
//   3. a parameter change reaches the machine (the readouts change, the output changes);
//   4. two instances at once give identical output (no shared state).
// And helpers for a machine's own hardware-panel checks (panel keys through the Host, as the
// editor sends them; adopting what the panel set, as the plugin does).
// The including test must define KIT_COUNT_ALLOCATIONS once (it installs the operator new hook).
#pragma once
#include "../core/host.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <algorithm>
#include <random>
#include <string>

namespace kit_test {
inline thread_local bool counting = false;   // per thread: the host's worker may allocate
inline std::atomic<long> allocations{0};
}  // namespace kit_test

#ifdef KIT_COUNT_ALLOCATIONS
void* operator new(size_t n) {
    if (kit_test::counting) kit_test::allocations++;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#endif

namespace kit_test {

struct Params {
    std::vector<std::atomic<float>> v;
    explicit Params(const kit::Info& info) : v(info.params.size()) {
        for (size_t i = 0; i < v.size(); i++) v[i].store(info.params[i].def);
    }
    std::vector<const std::atomic<float>*> ptrs() const {
        std::vector<const std::atomic<float>*> p;
        for (auto& a : v) p.push_back(&a);
        return p;
    }
};

// Render `seconds` of noise through a fresh host at `rate`, in blocks from `block_of(k)`.
template <class BlockOf>
std::vector<float> render(const kit::MachineType& t, const kit::RomSet& roms, Params& p, int rate, double seconds,
                          BlockOf block_of, kit::Host** keep = nullptr, int max_block = 1500) {
    auto host = std::make_unique<kit::Host>(t, p.ptrs());
    std::string err;
    auto m = t.boot(roms, "", &err);
    if (!m) {
        std::fprintf(stderr, "boot failed: %s\n", err.c_str());
        std::exit(1);
    }
    host->prepare(rate, max_block);
    host->install(std::move(m));
    int total = int(seconds * rate), nin = t.info.inputs;
    std::vector<float> in[2], out(2 * size_t(total));
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0, 0.05f);
    for (int c = 0; c < nin; c++) {
        in[c].resize(size_t(total));
        for (auto& x : in[c]) x = nd(rng);
    }
    int at = 0, k = 0;
    while (at < total) {
        int n = std::min(total - at, block_of(k++));
        const float* ins[2] = {in[0].data() + at, nin > 1 ? in[1].data() + at : nullptr};
        counting = true;
        host->process(ins, out.data() + at, out.data() + total + at, n);
        counting = false;
        at += n;
    }
    if (keep) *keep = host.release();
    return out;
}

// 0 = all passed. settle_seconds: audio to run after the change before reading the readouts (a
// machine set by typing keys takes them in audio time).
inline int run_host_checks(const kit::MachineType& t, const kit::RomSet& roms, int change_param, float change_to,
                           double settle_seconds = 0) {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) fails++;
    };
    for (int rate : {44100, 48000, 96000}) {
        std::printf("%d Hz\n", rate);
        Params pa(t.info), pb(t.info);
        allocations = 0;
        auto a = render(t, roms, pa, rate, 2.0, [](int) { return 512; });
        std::mt19937 bs(7);
        auto b = render(t, roms, pb, rate, 2.0, [&](int) { return int(bs() % 1500) + 1; });
        check(a == b, "random block sizes = 512-sample blocks, bit for bit");
        check(allocations == 0, "no allocation in process()");
        double e = 0;
        for (float x : a) e += double(x) * x;
        check(e > 1e-6, "output is not silent");
    }
    {
        Params p(t.info);
        kit::Host* h = nullptr;
        auto a = render(t, roms, p, 48000, 0.5, [](int) { return 256; }, &h);
        std::unique_ptr<kit::Host> host(h);
        auto r0 = host->readouts();
        p.v[size_t(change_param)].store(change_to);
        host->sync();
        if (settle_seconds > 0) {
            std::vector<float> z(256, 0.0f), ol(256), orr(256);
            const float* zi[2] = {z.data(), z.data()};
            for (int k = 0; k < int(settle_seconds * 48000 / 256); k++) host->process(zi, ol.data(), orr.data(), 256);
            host->refresh_readouts();
        }
        auto r1 = host->readouts();
        check(r0.param != r1.param || r0.panel != r1.panel, "a parameter change changes the readouts");
        std::printf("    before: %s\n    after:  %s\n", r0.panel.c_str(), r1.panel.c_str());
        Params q(t.info);
        q.v[size_t(change_param)].store(change_to);
        auto b = render(t, roms, q, 48000, 0.5, [](int) { return 256; });
        check(a != b, "and the output");
    }
    {
        Params p1(t.info), p2(t.info);
        kit::Host *h1 = nullptr, *h2 = nullptr;
        auto a = render(t, roms, p1, 48000, 0.5, [](int) { return 300; }, &h1);
        auto b = render(t, roms, p2, 48000, 0.5, [](int) { return 300; }, &h2);
        delete h1;
        delete h2;
        check(a == b, "two instances alive at once: identical output");
    }
    return fails;
}

// ---- hardware panel ----
// `seconds` of silence through the host (256-sample blocks), then fresh readouts and panel.
inline void run_audio(kit::Host& h, double seconds) {
    std::vector<float> z(256, 0.0f), ol(256), orr(256);
    const float* zi[2] = {z.data(), z.data()};
    for (int k = 0; k < int(seconds * 48000 / 256) + 1; k++) h.process(zi, ol.data(), orr.data(), 256);
    h.refresh_readouts();
    h.refresh_panel();
}
// Press panel keys one after another: each down `hold` seconds, then `gap` seconds of audio.
inline void press(kit::Host& h, std::initializer_list<int> ids, double hold = 0.15, double gap = 0.25) {
    for (int id : ids) {
        h.panel_key(id, true);
        h.refresh_panel();
        run_audio(h, hold);
        h.panel_key(id, false);
        h.refresh_panel();
        run_audio(h, gap);
    }
}
// What the plugin does with PanelState::adopt: write the values without having them typed back.
inline bool adopt(kit::Host& h, Params& p) {
    auto ps = h.panel();
    if (ps.adopt.empty()) return false;
    h.adopt([&] {
        for (size_t i = 0; i < ps.adopt.size() && i < p.v.size(); i++)
            if (!std::isnan(ps.adopt[i])) p.v[i].store(ps.adopt[i]);
    });
    return true;
}
// The layout against the state the machine reports: sizes, ids in range, unique key ids.
inline int run_panel_layout_checks(const kit::MachineType& t, const kit::RomSet& roms) {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) fails++;
    };
    const auto& L = t.info.panel;
    Params p(t.info);
    kit::Host* hp = nullptr;
    render(t, roms, p, 48000, 0.3, [](int) { return 256; }, &hp);
    std::unique_ptr<kit::Host> h(hp);
    h->refresh_panel();
    auto s = h->panel();
    check(s.valid, "the panel state is valid once booted");
    check(int(s.digits.size()) == L.digits && int(s.leds.size()) == L.leds && int(s.alpha.size()) == L.alpha,
          "digits/LEDs/alpha sizes match the layout (" + std::to_string(s.digits.size()) + "/" + std::to_string(s.leds.size()) + "/" +
              std::to_string(s.alpha.size()) + ")");
    std::vector<int> keys;
    bool in_range = true, params_ok = true;
    for (auto& e : L.elements) {
        if (e.kind == kit::PanelElement::Key) keys.push_back(e.id);
        if (e.kind == kit::PanelElement::Led && e.id >= L.leds) in_range = false;
        if (e.kind == kit::PanelElement::Digit && (e.id < 0 || e.id >= L.digits)) in_range = false;
        if ((e.kind == kit::PanelElement::Pot || e.kind == kit::PanelElement::Switch) && !e.param.empty()) {
            bool found = false;
            for (auto& prm : t.info.params) found = found || prm.id == e.param;
            params_ok = params_ok && found;
        }
    }
    std::sort(keys.begin(), keys.end());
    check(std::adjacent_find(keys.begin(), keys.end()) == keys.end(), std::to_string(keys.size()) + " keys, ids unique");
    check(in_range, "LED and digit ids in range");
    check(params_ok, "every bound knob and switch names a parameter");
    h->panel_key(keys.empty() ? 0 : keys[0], false);   // an up with no down is harmless
    run_audio(*h, 0.1);
    check(h->panel().valid, "a stray key-up is harmless");
    return fails;
}

}  // namespace kit_test
