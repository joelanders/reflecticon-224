// check_machine ROM_DIR [RAW_IN RAW_OUT]: the StarGate 323 machine, JUCE-free.
//   1. the core: RAW_IN (stereo float64 interleaved at 192 kHz) through sg323::Machine with the
//      plugin's default settings, written to RAW_OUT (compare with the StarGate project's
//      cpp/build/run on the same file);
//   2. readouts for the defaults and a few settings;
//   3. an impulse through the host: predelay heard, decay 0 vs 5, direct only;
//   4. the kit's host checks (block sizes, no allocation, a change reaches the machine, instances);
//   5. a live change while audio runs (room, HF EQ);
//   6. what it costs (real-time factor).
#define KIT_COUNT_ALLOCATIONS
#include "../plugin/sg323_machine.hpp"
#include "../core/sg323.hpp"
#include "../../shared/tests/host_checks.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
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

enum { Input, LfDecay, HfDecay, DirectMix, ReverbMix, Room, Predelay, Decay, InputMute, DryOnly, ReverbClear, Modulation, Hiss, Unit };

// An impulse (0.5 full scale, both inputs) through a fresh host at 48 kHz; returns the left output.
static std::vector<float> impulse(const kit::MachineType& t, const kit::RomSet& roms, kit_test::Params& p, double seconds,
                                  long* latency = nullptr, int at_sample = 100) {
    auto host = std::make_unique<kit::Host>(t, p.ptrs());
    std::string err;
    auto m = t.boot(roms, "", &err);
    if (!m) {
        std::fprintf(stderr, "boot failed: %s\n", err.c_str());
        std::exit(1);
    }
    host->prepare(48000, 512);
    host->install(std::move(m));
    if (latency) *latency = host->latency();
    int total = int(seconds * 48000);
    std::vector<float> in(size_t(total), 0.0f), ol(size_t(total), 0.0f), orr(size_t(total), 0.0f);
    if (at_sample >= 0) in[size_t(at_sample)] = 0.5f;   // -1: no impulse
    for (int at = 0; at < total; at += 512) {
        int n = std::min(512, total - at);
        const float* ins[2] = {in.data() + at, in.data() + at};
        host->process(ins, ol.data() + at, orr.data() + at, n);
    }
    return ol;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s ROM_DIR [RAW_IN RAW_OUT]\n", argv[0]);
        return 2;
    }
    const auto& t = sg323plug::machine_type();
    auto roms = load_roms(t, argv[1]);
    std::printf("PROMs: %s\n", roms.describe(t.info.roms).c_str());
    if (!roms.complete()) return 2;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) fails++;
    };

    if (argc >= 4) {
        std::printf("1. the core on %s\n", argv[2]);
        sg323::Proms pr;
        pr.p0803 = roms.bytes(0), pr.p0804 = roms.bytes(1), pr.p0805 = roms.bytes(2);
        pr.p0806 = roms.bytes(3), pr.p0807 = roms.bytes(4), pr.p0808 = roms.bytes(5);
        if (pr.p0806.size() == 4096) pr.p0806.resize(2048);
        sg323::Controls c;   // the project's defaults = the plugin's (room 3, decay 5, 0 dB, hiss on)
        std::ifstream f(argv[2], std::ios::binary | std::ios::ate);
        size_t n = size_t(f.tellg()) / 16;
        f.seekg(0);
        std::vector<double> x(2 * n), y(2 * n);
        f.read(reinterpret_cast<char*>(x.data()), std::streamsize(16 * n));
        auto mach = std::make_unique<sg323::Machine>();
        check(mach->init(pr, c), "PROMs load");
        mach->set(sg323::Coefs::make(c), sg323::Taps::make(pr, c), c);
        for (size_t i = 0; i < n; i++) mach->step(x[2 * i], x[2 * i + 1], y[2 * i], y[2 * i + 1]);
        std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char*>(y.data()), std::streamsize(16 * n));
        std::printf("  wrote %zu frames to %s\n", n, argv[3]);
    }

    std::printf("2. readouts\n");
    {
        kit_test::Params p(t.info);
        kit::Host host(t, p.ptrs());
        std::string err;
        host.prepare(48000, 512);
        host.install(t.boot(roms, "", &err));
        auto show = [&](const char* what) {
            auto r = host.readouts();
            std::printf("  %s: panel \"%s\"\n   ", what, r.panel.c_str());
            for (size_t i = 0; i < r.param.size(); i++) std::printf(" %s=%s;", t.info.params[i].id.c_str(), r.param[i].c_str());
            std::printf("\n");
            return r;
        };
        auto r = show("defaults");
        check(r.param[Room] == "Room 3" && r.param[Predelay] == "0 ms", "defaults: Room 3, predelay 0 ms");
        check(r.param[Decay] == "3.0 s", "defaults: decay time 3.0 s (Table III, room 3 position 5): " + r.param[Decay]);
        p.v[Predelay].store(8.0f);
        p.v[HfDecay].store(0.0f);
        p.v[LfDecay].store(0.0f);
        host.sync();
        r = show("predelay knob 8, HF decay min, LF decay min");
        check(r.param[Predelay] == t.info.params[Predelay].choices[8], "the PROMs' predelay matches the knob label (" +
                                                                            r.param[Predelay] + ")");
        bool all = true;
        for (int k = 0; k < 16; k++) {
            p.v[Predelay].store(float(k));
            host.sync();
            all = all && host.readouts().param[Predelay] == t.info.params[Predelay].choices[size_t(k)];
        }
        check(all, "all 16 predelay labels match the PROMs");
    }

    std::printf("3. an impulse through the host at 48 kHz\n");
    {
        auto onset = [](const std::vector<float>& v) {
            float pk = 0;
            for (float x : v) pk = std::max(pk, std::fabs(x));
            for (size_t i = 0; i < v.size(); i++)
                if (std::fabs(v[i]) > 0.05f * pk) return int(i);
            return -1;
        };
        auto energy = [](const std::vector<float>& v, double from_s) {
            double e = 0;
            for (size_t i = size_t(from_s * 48000); i < v.size(); i++) e += double(v[i]) * v[i];
            return e;
        };
        kit_test::Params p0(t.info), p8(t.info), pd0(t.info), pdir(t.info), pmute(t.info);
        for (auto* p : {&p0, &p8, &pd0, &pdir, &pmute}) p->v[Hiss].store(0.0f);
        pmute.v[InputMute].store(1.0f);
        pmute.v[DirectMix].store(1.0f);
        p8.v[Predelay].store(8.0f);
        pd0.v[Decay].store(0.0f);
        pdir.v[ReverbMix].store(0.0f);
        pdir.v[DirectMix].store(1.0f);
        long lat = 0;
        auto a = impulse(t, roms, p0, 3.0, &lat), b = impulse(t, roms, p8, 3.0), d0 = impulse(t, roms, pd0, 3.0),
             dir = impulse(t, roms, pdir, 1.0);
        double shift = (onset(b) - onset(a)) / 48.0;
        check(std::fabs(shift - 100.0) < 3.0, "predelay knob 8 (100 ms) moves the first reflection by " + std::to_string(shift) + " ms");
        double e5 = energy(a, 1.0), e0 = energy(d0, 1.0);
        check(e5 > 100 * e0, "decay 5 vs 0: tail energy after 1 s " + std::to_string(e5) + " vs " + std::to_string(e0));
        int od = int(std::max_element(dir.begin(), dir.end(), [](float x, float y) { return std::fabs(x) < std::fabs(y); }) - dir.begin());
        check(std::abs(od - (100 + int(lat))) <= 2, "direct only: the impulse peaks at " + std::to_string(od - 100) +
                                                        " samples (latency " + std::to_string(lat) + ")");
        std::printf("  first reflection at %.2f ms after the latency\n", (onset(a) - 100 - lat) / 48.0);
        // input mute: the impulse makes no difference to the output (the machine's idle floor stays)
        auto mu = impulse(t, roms, pmute, 1.0, nullptr, 24000), quiet = impulse(t, roms, pmute, 1.0, nullptr, -1);
        float dm = 0;
        for (size_t i = 0; i < mu.size(); i++) dm = std::max(dm, std::fabs(mu[i] - quiet[i]));
        check(dm == 0.0f, "input mute, direct 1: an impulse changes nothing at the output (max difference " +
                              std::to_string(dm) + ")");
    }

    std::printf("4. host checks\n");
    fails += kit_test::run_host_checks(t, roms, Room, 6.0f, 0.0);

    std::printf("5. live changes while audio runs\n");
    {
        kit_test::Params p(t.info);
        kit::Host* h = nullptr;
        auto a = kit_test::render(t, roms, p, 48000, 1.0, [](int) { return 256; }, &h);
        std::unique_ptr<kit::Host> host(h);
        p.v[HfDecay].store(0.0f);
        p.v[Room].store(0.0f);
        host->sync();
        auto r = host->readouts();
        check(r.param[Room] == "Room 1" && r.param[HfDecay].find("3.0 kHz") != std::string::npos,
              "room 3 -> 1, HF decay max -> min shown at once (" + r.param[HfDecay] + ")");
        std::vector<float> z(256, 0.0f), ol(256), orr(256);
        const float* zi[2] = {z.data(), z.data()};
        kit_test::allocations = 0;
        kit_test::counting = true;
        for (int k = 0; k < 40; k++) host->process(zi, ol.data(), orr.data(), 256);
        kit_test::counting = false;
        check(kit_test::allocations == 0, "taking the new coefficients and taps does not allocate");
    }

    std::printf("6. cost: 20 s of audio at 48 kHz in 512-sample blocks\n");
    {
        kit_test::Params p(t.info);
        auto t0 = std::chrono::steady_clock::now();
        kit_test::render(t, roms, p, 48000, 20.0, [](int) { return 512; });
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %.2f s of CPU on one thread: %.1fx real time\n", s, 20.0 / s);
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
