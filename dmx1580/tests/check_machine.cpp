// check_machine ROM_DIR [RAW_OUT]: the DMX 15-80 machine, JUCE-free.
//   1. the core: a unit booted with the full options, then 3 s of noise on A and B with nothing
//      typed, written to RAW_OUT as float32 A/B pairs (compare with the DMX project's own engine:
//      the same boot, the same noise);
//   2. typed settings read back from the display (delay and pitch per channel, clamps, options);
//   3. the delay heard: an impulse comes back after the typed delay;
//   4. the kit's host checks (block sizes, no allocation, a change reaches the machine, instances);
//   5. live changes while audio runs: delay, pitch, and a machine option (a new machine swapped in);
//   6. the hardware panel: layout against state, raw keys (A 300 #, B C 1.500 #), adopting what
//      they set, a held nudge key, easy mode after the panel, the h/room LEDs on a loud input;
//   7. what it costs (real-time factor).
#define KIT_COUNT_ALLOCATIONS
#include "../plugin/dmx1580_machine.hpp"
#include "../core/engine.hpp"
#include "dmx1580_patches.h"
#include "../../shared/tests/host_checks.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

static kit::RomSet load_roms(const kit::MachineType& t, const std::string& dir) {
    std::vector<kit::RomFile> files;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        if (!e.is_regular_file() || !kit::any_size_fits(t.info.roms, size_t(e.file_size()))) continue;
        std::ifstream f(e.path(), std::ios::binary);
        files.push_back({e.path().filename().string(), e.path().string(),
                         std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {})});
    }
    return kit::recognise(t.info.roms, files);
}

// the noise both sides of the comparison use: an LCG, 0.2 full scale
static float lcg_noise(uint32_t& s) {
    s = s * 1103515245u + 12345u;
    return (float((s >> 8) & 0xFFFF) / 32768.0f - 1.0f) * 0.2f;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s ROM_DIR [RAW_OUT]\n", argv[0]);
        return 2;
    }
    const auto& t = dmx1580plug::machine_type();
    auto roms = load_roms(t, argv[1]);
    std::printf("ROMs: %s\n", roms.describe(t.info.roms).c_str());
    if (!roms.complete()) return 2;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) fails++;
    };
    enum { DelayA, DelayB, PitchA, PitchB, InA, InB, FbGainA, FbGainB, FbFilterA, FbFilterB, Out, Vco, VcoSpeed, VcoDepth, PhaseA, MonoIn,
           MixOut, LockIn, Trigger, Ram, Boards, Analog, Smooth };
    std::vector<float> defaults;
    for (auto& p : t.info.params) defaults.push_back(p.def);

    std::printf("1. the core, nothing typed\n");
    {
        dmxkit::Unit u;
        const uint8_t* p[4] = {roms.bytes(0).data(), roms.bytes(1).data(), roms.bytes(2).data(), roms.bytes(3).data()};
        std::string err;
        dmxkit::Options o;
        check(u.boot(p, kPatches, o, &err), "boots with the repairs" + (err.empty() ? std::string() : ": " + err));
        u.set_knobs(dmxkit::Knobs{});
        std::vector<float> raw;
        uint32_t s = 1;
        std::vector<float> a(400), b(400), x(400), y(400);
        for (int blk = 0; blk < 3 * 40000 / 400; blk++) {
            for (int i = 0; i < 400; i++) a[size_t(i)] = lcg_noise(s), b[size_t(i)] = lcg_noise(s);
            u.render(a.data(), b.data(), x.data(), y.data(), 400);
            for (int i = 0; i < 400; i++) raw.push_back(x[size_t(i)]), raw.push_back(y[size_t(i)]);
        }
        double e = 0;
        for (float v : raw) e += double(v) * v;
        check(e > 1e-3, "noise comes out");
        if (argc > 2) {
            std::ofstream(argv[2], std::ios::binary)
                .write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size() * sizeof(float)));
            std::printf("  wrote %s\n", argv[2]);
        }
    }

    std::printf("2. typed settings, read back from the display\n");
    {
        std::string err;
        auto m = t.boot(roms, "", &err);
        check(m != nullptr, "boots" + (m ? std::string() : ": " + err));
        if (!m) return 1;
        m->prepare(48000, 512);
        struct Case {
            float da, db, pa, pb, ram, boards;
            const char *want_da, *want_db, *want_pa, *want_pb;
        };
        const Case cases[] = {
            {250, 375, 1.0f, 1.0f, 3, 1, "250 ms", "375 ms", "off (1.000)", "off (1.000)"},
            {1638, 0, 2.0f, 0.5f, 3, 1, "1638 ms", "0 ms", "x2.000 (+12.0 st)", "x0.500 (-12.0 st)"},
            {7, 1000, 1.059f, 1.5f, 3, 1, "7 ms", "1000 ms", "x1.059 (+1.0 st)", "x1.500 (+7.0 st)"},
            {2000, 1638, 1.0f, 1.999f, 3, 1, "1638 ms", "1638 ms", "off (1.000)", "x1.999 (+12.0 st)"},   // clamped
            {500, 90, 0.75f, 1.25f, 0, 1, "102 ms", "90 ms", "x0.750 (-5.0 st)", "x1.250 (+3.9 st)"},     // 4K words
            {500, 900, 1.2f, 1.3f, 2, 0, "500 ms", "819 ms", "x1.200 (+3.2 st)", ""},                     // 32K, one board
            {250, 375, 1.0f, 1.0f, 3, 1, "250 ms", "375 ms", "off (1.000)", "off (1.000)"},
        };
        for (auto& c : cases) {
            auto v = defaults;
            v[DelayA] = c.da, v[DelayB] = c.db, v[PitchA] = c.pa, v[PitchB] = c.pb, v[Ram] = c.ram, v[Boards] = c.boards;
            m->set_params(v);
            auto r = m->readouts();
            bool ok = r.param[DelayA] == c.want_da && r.param[DelayB] == c.want_db && r.param[PitchA] == c.want_pa &&
                      r.param[PitchB] == c.want_pb && r.status.empty();
            check(ok, std::string("want ") + c.want_da + " / " + c.want_db + " / " + c.want_pa + " / " + c.want_pb + "; got " +
                          r.param[DelayA] + " / " + r.param[DelayB] + " / " + r.param[PitchA] + " / " + r.param[PitchB] +
                          "  panel " + r.panel + (r.status.empty() ? "" : " (" + r.status + ")"));
        }
    }

    std::printf("3. the delay heard\n");
    {
        for (float ms : {250.0f, 1000.0f}) {
            kit_test::Params p(t.info);
            p.v[DelayA].store(ms);
            p.v[DelayB].store(ms / 2);
            auto host = std::make_unique<kit::Host>(t, p.ptrs());
            std::string err;
            host->prepare(48000, 256);
            host->install(t.boot(roms, "", &err));
            long lat = host->latency();
            int total = int(1.6 * 48000);
            std::vector<float> in(size_t(total), 0.0f), ol(size_t(total), 0.0f), orr(size_t(total), 0.0f);
            in[4800] = 0.8f;
            for (int at = 0; at < total; at += 256) {
                int n = std::min(256, total - at);
                const float* ins[2] = {in.data() + at, in.data() + at};
                host->process(ins, ol.data() + at, orr.data() + at, n);
            }
            auto peak = [&](const std::vector<float>& o) {
                size_t b = 0;
                for (size_t i = 0; i < o.size(); i++)
                    if (std::fabs(o[i]) > std::fabs(o[b])) b = i;
                return (double(b) - 4800 - lat) / 48.0;
            };
            double la = peak(ol), lb = peak(orr);
            check(std::fabs(la - ms) < 1.0 && std::fabs(lb - ms / 2) < 1.0,
                  "A " + std::to_string(int(ms)) + " ms, B " + std::to_string(int(ms / 2)) + " ms: heard after " +
                      std::to_string(la) + " / " + std::to_string(lb) + " ms (latency " + std::to_string(lat) + " samples)");
        }
    }

    std::printf("4. host checks\n");
    fails += kit_test::run_host_checks(t, roms, DelayA, 600.0f, 2.0);

    std::printf("5. live changes while audio runs\n");
    {
        kit_test::Params p(t.info);
        kit::Host* h = nullptr;
        kit_test::render(t, roms, p, 48000, 0.2, [](int) { return 256; }, &h);
        std::unique_ptr<kit::Host> host(h);
        auto run_until = [&](int param, const std::string& want, double max_s) {
            std::vector<float> z(256, 0.0f), ol(256), orr(256);
            const float* zi[2] = {z.data(), z.data()};
            for (int k = 0; k < int(max_s * 48000 / 256); k++) {
                host->process(zi, ol.data(), orr.data(), 256);
                if (k % 4 == 0) {
                    host->refresh_readouts();
                    auto r = host->readouts();
                    if (r.param[size_t(param)] == want && r.status.empty()) return (k + 1) * 256 / 48000.0;
                }
            }
            return -1.0;
        };
        p.v[DelayA].store(500.0f);
        host->sync();
        double s1 = run_until(DelayA, "500 ms", 5);
        check(s1 > 0, "delay A 250 -> 500 ms shown after " + std::to_string(s1) + " s of audio");
        p.v[PitchB].store(1.5f);
        host->sync();
        double s2 = run_until(PitchB, "x1.500 (+7.0 st)", 5);
        check(s2 > 0, "pitch B 1.000 -> 1.500 shown after " + std::to_string(s2) + " s of audio");
        p.v[Ram].store(1.0f);   // 16K words: a new machine, with delay A clamped to 409 ms
        host->sync();
        double s3 = run_until(DelayA, "409 ms", 5);
        auto r = host->readouts();
        check(s3 > 0 && r.param[PitchB] == "x1.500 (+7.0 st)",
              "RAM 64K -> 16K words: new machine, delay A 409 ms and pitch B kept, after " + std::to_string(s3) + " s");
    }

    std::printf("6. the hardware panel\n");
    fails += kit_test::run_panel_layout_checks(t, roms);
    {
        using namespace kit_test;
        Params p(t.info);
        kit::Host* h = nullptr;
        render(t, roms, p, 48000, 0.2, [](int) { return 256; }, &h);
        std::unique_ptr<kit::Host> host(h);
        run_audio(*host, 2.0);   // the defaults typed (250 / 375)
        press(*host, {0xA, 3, 0, 0, 0xD});
        run_audio(*host, 1.0);
        std::string got = host->readouts().panel;
        check(got == " 300  A", "A 3 0 0 # on the keypad: [" + got + "]");
        press(*host, {0xB, 0xC, 1, 0xF, 5, 0, 0, 0xD});
        run_audio(*host, 1.0);
        got = host->readouts().panel;
        check(got.rfind("1.500", 0) == 0 || got.rfind("H.500", 0) == 0, "B C 1 * 5 0 0 #: [" + got + "]");
        auto ps = host->panel();
        check(ps.leds.size() == 14 && ps.leds[1] && ps.leds[2], "LEDs b and c lit");
        check(adopt(*host, p) && std::lround(p.v[0].load()) == 300 && std::abs(p.v[3].load() - 1.5f) < 1e-4f,
              "adopted as parameters: delay A 300, pitch B 1.500");
        host->sync();
        run_audio(*host, 1.0);
        check(host->readouts().status.empty(), "and not typed back");
        press(*host, {0xA});
        press(*host, {0x10}, 1.2, 0.6);   // UP held: the firmware's nudge
        run_audio(*host, 0.6);
        got = host->readouts().panel;
        check(std::atoi(got.c_str()) > 300 && got.find('A') != std::string::npos, "A then UP held 1.2 s: [" + got + "]");
        p.v[1].store(500.0f);   // easy mode again
        host->sync();
        double t_easy = -1;
        for (int k = 0; k < 40 && t_easy < 0; k++) {
            run_audio(*host, 0.2);
            if (host->readouts().param[1] == "500 ms" && host->readouts().status.empty()) t_easy = 0.2 * (k + 1);
        }
        check(t_easy > 0, "easy mode after the panel: delay B 500 typed, confirmed after " + std::to_string(t_easy) + " s");
        press(*host, {0xA, 4, 0, 0, 0xD}, 0.0, 0.0);   // typed faster than a key is held, 0 twice
        run_audio(*host, 1.5);
        got = host->readouts().panel;
        check(got == " 400  A", "A 4 0 0 # tapped instantly: [" + got + "]");
        // a loud input lights the h/room LEDs (6 dB below the samplers' clipping and up)
        std::vector<float> loud(256), ol(256), orr(256);
        for (int i = 0; i < 256; i++) loud[size_t(i)] = 0.9f * std::sin(float(i) * 0.2f);
        const float* li[2] = {loud.data(), loud.data()};
        for (int k = 0; k < 40; k++) host->process(li, ol.data(), orr.data(), 256);
        host->refresh_panel();
        ps = host->panel();
        check(ps.leds[7] && ps.leds[8] && ps.leds[9], "a loud input: ch.a 6/12/18 dB LEDs lit");
    }

    std::printf("7. cost: 20 s of audio at 48 kHz in 512-sample blocks\n");
    {
        kit_test::Params p(t.info);
        auto t0 = std::chrono::steady_clock::now();
        kit_test::render(t, roms, p, 48000, 20.0, [](int) { return 512; });
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %.2f s of CPU on one thread: %.1fx real time (includes boot and the first settings)\n", s, 20.0 / s);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
