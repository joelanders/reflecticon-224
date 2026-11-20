// check_machine ROM_DIR [RAW_OUT]: the RMX16 machine, JUCE-free.
//   1. the core through its C API: program 1, an impulse at 3.0 s, 4 s of DAC A/B written to RAW_OUT
//      as int16 pairs (compare with the RMX16 project's `rmx16 firmware 4 --impulse 3.0 --wav-out`);
//   2. typed settings on all 17 programs read back from the firmware's display;
//   3. the kit's host checks (block sizes, no allocation, a change reaches the machine, instances);
//   4. a live change while audio runs: how long until the display shows it;
//   5. the hardware panel: layout against state, raw keys (PROGRAM 5 #), adopting what they set,
//      a held nudge key, easy mode after the panel, SAVE/RECALL and the battery RAM across a reboot;
//   6. what it costs (real-time factor).
#define KIT_COUNT_ALLOCATIONS
#include "../plugin/rmx16_machine.hpp"
#include "../core/rmx16_core.h"
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s ROM_DIR [RAW_OUT]\n", argv[0]);
        return 2;
    }
    const auto& t = rmx16plug::machine_type();
    auto roms = load_roms(t, argv[1]);
    std::printf("ROMs: %s\n", roms.describe(t.info.roms).c_str());
    if (!roms.complete()) return 2;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) fails++;
    };

    std::printf("1. the core through the C API\n");
    {
        rmxc_t* m = rmxc_create();
        for (int i = 0; i < 5; i++) rmxc_load(m, i, roms.bytes(size_t(i)).data(), int(roms.bytes(size_t(i)).size()));
        rmxc_power(m);
        std::vector<int16_t> raw;
        long imp = long(3.0 * RMXC_RATE);
        bool ran = true;
        for (long n = 0; n < 4L * RMXC_RATE; n++) {
            int16_t a, b;
            if (!rmxc_run_sample(m, n == imp ? 0x4000 : 0, &a, &b)) { ran = false; break; }
            raw.push_back(a);
            raw.push_back(b);
        }
        check(ran, "4 s of machine time");
        if (argc > 2) {
            std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size() * 2));
            std::printf("  wrote %s\n", argv[2]);
        }
        rmxc_destroy(m);
    }

    std::printf("2. typed settings, read back from the display\n");
    {
        std::string err;
        auto m = t.boot(roms, "", &err);
        check(m != nullptr, "boots" + (m ? std::string() : ": " + err));
        if (!m) return 1;
        m->prepare(48000, 512);
        struct Case { int prog, pre; float dec; int lf, hf; const char* want; };
        const Case cases[] = {
            {0, 150, 2.5f, -4, -9, "1  150  2.5  -4  -9   AMBIENCE"},
            {0, 0, 9.9f, 9, 0, "1  0  9.9  +9  0   AMBIENCE"},
            {1, 200, 0.3f, 5, -2, "2  200  0.3    -2   HALL  A1"},
            {2, 37, 7.1f, -9, -9, "3  37  7.1  -9  -9   HALL  B3"},
            {3, 199, 1.0f, 3, -1, "4  199  1.0  +3  -1   HALL  C1"},
            {4, 300, 0.0f, 0, -8, "5  300  0.0    -8   ROOM  A0"},
            {5, 250, 4.6f, 0, -5, "6  250  4.6    -5   ROOM  A1"},
            {6, 10, 3.3f, 0, 0, "7  10  3.3    0   ROOM  B1"},
            {7, 120, 2.0f, 1, -5, "8  120  2.0  +1  -5   PLATE A1"},
            {8, 999, 8.8f, -1, -3, "9  200  8.8  -1  -3   PLATE B1"},     // predelay clamped to 200
            {9, 50, 4.0f, 0, -4, "10  50  4.0    -4   IMAGE P1"},
            {10, 75, 4.0f, 0, -6, "11  75  4.0    -6   NONLIN 2"},
            {11, 100, 2.2f, 0, -9, "12  100  2.2       REVERSE1"},      // no filters: both digits dark
            {12, 300, 5.5f, 0, -7, "13  300  5.5    -7   REVERSE2"},
            {13, 25, 6.0f, 0, -9, "14  25  6.0    -8   CHORUS 1"},       // HF clamped to -8
            {14, 999, 5.0f, 4, -2, "15  999  5.0  +4  -2   ECHO   A"},
            {15, 810, 9.9f, 0, 0, "16  810  9.9       DELAY"},
            {16, 0, 0.2f, 0, 0, "17  0  0.2       FREEZE"},
            {0, 0, 4.0f, 0, -4, "1  0  4.0  0  -4   AMBIENCE"},
        };
        for (auto& c : cases) {
            m->set_params({float(c.prog), float(c.pre), c.dec, float(c.lf), float(c.hf), 0.0f});
            auto r = m->readouts();
            check(r.panel == c.want, "want [" + std::string(c.want) + "] got [" + r.panel + "]" +
                                         (r.status.empty() ? "" : " (" + r.status + ")"));
        }
        auto r = m->readouts();
        std::printf("  readouts on the last: program [%s] predelay [%s] decay [%s] LF [%s] HF [%s]\n", r.param[0].c_str(),
                    r.param[1].c_str(), r.param[2].c_str(), r.param[3].c_str(), r.param[4].c_str());
        check(m->presets().size() == 17, "17 factory presets");
    }

    std::printf("3. host checks\n");
    fails += kit_test::run_host_checks(t, roms, 2, 2.0f, 2.0);

    std::printf("4. a live change while audio runs\n");
    {
        kit_test::Params p(t.info);
        kit::Host* h = nullptr;
        kit_test::render(t, roms, p, 48000, 0.2, [](int) { return 256; }, &h);
        std::unique_ptr<kit::Host> host(h);
        auto run_until = [&](const std::string& want, double max_s) {
            std::vector<float> z(256, 0.0f), ol(256), orr(256);
            const float* zi[2] = {z.data(), z.data()};
            for (int k = 0; k < int(max_s * 48000 / 256); k++) {
                host->process(zi, ol.data(), orr.data(), 256);
                if (k % 4 == 0) {
                    host->refresh_readouts();
                    auto r = host->readouts();
                    if (r.panel == want && r.status.empty()) return (k + 1) * 256 / 48000.0;
                }
            }
            return -1.0;
        };
        p.v[2].store(2.5f);
        host->sync();
        double s1 = run_until("1  0  2.5  0  -4   AMBIENCE", 5);
        check(s1 > 0, "decay 4.0 -> 2.5 shown after " + std::to_string(s1) + " s of audio");
        p.v[0].store(4.0f);   // ROOM A0, with the other settings kept
        host->sync();
        double s2 = run_until("5  0  2.5    -4   ROOM  A0", 8);
        check(s2 > 0, "program 1 -> 5 (decay and HF retyped) shown after " + std::to_string(s2) + " s of audio");
    }

    std::printf("5. the hardware panel\n");
    fails += kit_test::run_panel_layout_checks(t, roms);
    {
        using namespace kit_test;
        Params p(t.info);
        kit::Host* h = nullptr;
        render(t, roms, p, 48000, 0.2, [](int) { return 256; }, &h);
        std::unique_ptr<kit::Host> host(h);
        run_audio(*host, 0.5);
        press(*host, {2, 112, 124});   // PROGRAM 5 #
        run_audio(*host, 1.5);
        std::string got = host->readouts().panel;
        check(got == "5  0  1.0    -8   ROOM  A0", "PROGRAM 5 # on the keys: [" + got + "]");
        auto ps = host->panel();
        check(ps.leds.size() == 11 && ps.leds[8] && ps.leds[10], "the program key's LED and power lit");
        check(adopt(*host, p) && std::lround(p.v[0].load()) == 4 && std::lround(p.v[4].load()) == -8,
              "adopted as parameters: program ROOM A0, HF -8");
        host->sync();
        run_audio(*host, 1.0);
        check(host->readouts().panel == got && !host->readouts().status.size(), "and not typed back (the display stays)");
        press(*host, {4});   // DECAY, then hold UP 1.5 s: the firmware's auto-repeat
        press(*host, {10}, 1.5, 0.6);
        run_audio(*host, 0.5);
        got = host->readouts().panel;
        double dec = std::atof(got.substr(6, 3).c_str());
        check(dec > 1.05, "DECAY then UP held 1.5 s: [" + got + "]");
        p.v[2].store(2.5f);   // easy mode again: the driver types from what the display shows
        host->sync();
        double t_easy = -1;
        for (int k = 0; k < 40 && t_easy < 0; k++) {
            run_audio(*host, 0.2);
            if (host->readouts().panel == "5  0  2.5    -8   ROOM  A0") t_easy = 0.2 * (k + 1);
        }
        check(t_easy > 0, "easy mode after the panel: decay 2.5 typed, shown after " + std::to_string(t_easy) + " s");
        press(*host, {7, 121, 124});        // SAVE 3 #
        press(*host, {2, 101, 124});        // PROGRAM 1 #
        run_audio(*host, 1.0);
        bool moved = host->readouts().panel.rfind("1 ", 0) == 0;
        press(*host, {8, 121, 124});        // RECALL 3 #
        run_audio(*host, 1.5);
        check(moved && host->readouts().panel == "5  0  2.5    -8   ROOM  A0", "SAVE 3, PROGRAM 1, RECALL 3: [" + host->readouts().panel + "]");
        // typed faster than a key is held (all six events queued at once): still three presses
        press(*host, {2, 101, 124}, 0.0, 0.0);
        run_audio(*host, 1.5);
        check(host->readouts().panel.rfind("1 ", 0) == 0, "PROGRAM 1 # tapped instantly: [" + host->readouts().panel + "]");
        press(*host, {8, 121, 124});
        run_audio(*host, 1.5);
        run_audio(*host, 0.9);   // the battery copy is made every 0.8 s
        // the battery RAM as the session's machine state: a new machine recalls store 3
        std::string err;
        const std::string state = host->machine_state();
        check(state.size() == 2048, "machine state = the 2 KB battery RAM");
        Params p2(t.info);
        kit::Host h2(t, p2.ptrs());
        h2.prepare(48000, 256);
        h2.install(t.boot(roms, state, &err));
        run_audio(h2, 0.5);
        press(h2, {8, 121, 124});
        run_audio(h2, 1.5);
        check(h2.readouts().panel == "5  0  2.5    -8   ROOM  A0", "after a reboot with that state, RECALL 3: [" + h2.readouts().panel + "]");
        // the power switch: off darkens the panel, on boots again (the battery keeps the settings)
        h2.panel_input(3, 0.0f);
        run_audio(h2, 0.3);
        auto off = h2.panel();
        bool dark = true;
        for (auto d : off.digits) dark = dark && d == 0;
        check(dark && off.leds[10] == 0, "power off: digits and power LED dark");
        h2.panel_input(3, 1.0f);
        run_audio(h2, 3.5);
        check(h2.panel().leds[10] && !h2.readouts().panel.empty(), "power on: boots again [" + h2.readouts().panel + "]");
    }

    std::printf("6. cost: 20 s of audio at 48 kHz in 512-sample blocks\n");
    {
        kit_test::Params p(t.info);
        auto t0 = std::chrono::steady_clock::now();
        kit_test::render(t, roms, p, 48000, 20.0, [](int) { return 512; });
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %.2f s of CPU on one thread: %.0fx real time (includes boot and the first settings)\n", s, 20.0 / s);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
