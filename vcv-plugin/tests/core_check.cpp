// The module's core without Rack: boot a set on the worker thread, then run
// it one host sample at a time as Module::process would.
//
//   core_check ROM_DIR [SECONDS [CATALOG_DIR]]
//
// Without a catalog (no operator): at 48 kHz the output must equal, bit for
// bit, a Session booted the same way and rendered in 4800-frame blocks (the
// bridge is a straight wire there). At 44.1 kHz it reports only the cost.
// Input: the speed gate's white noise for 2 s, then silence. The core runs
// with flush-to-zero set, as on Rack's engine thread; the reference without.
//
// With CATALOG_DIR: boot the set with its catalog and operator, then load the
// second program, its last variation, and move slider_1 to the top, each a
// request as the module makes them; check that the machine holds each.
#include "../src/core.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {

using Clock = std::chrono::steady_clock;

// Two seconds of noise (two calls a sample), then silence: the tail decays
// toward the denormal range, where flush-to-zero would change the output.
struct Noise {
    uint32_t state = 12345;
    uint32_t calls = 0;
    float next() {
        if (++calls > 2 * 2 * 48000) {
            return 0.0f;
        }
        state = state * 1664525u + 1013904223u;
        return (float(state >> 8) / float(1u << 24) * 2.0f - 1.0f) * 0.25f;
    }
};

bool wait_running(lexvcv::Core &core) {
    for (int i = 0; i < 6000; i++) {
        if (core.status() != lexvcv::Status::Booting) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("  status: %s\n", core.status_text().c_str());
    return core.status() == lexvcv::Status::Running;
}

// Run silence until `done` holds (checked every 1024 samples) or `seconds` pass.
template <class Done>
bool run_until(lexvcv::Core &core, double seconds, Done &&done, double &took) {
    int n = int(seconds * 48000);
    float l, r;
    for (int i = 0; i < n; i++) {
        core.process(0.0f, 0.0f, l, r);
        if (i % 1024 == 1023 && done(core.published())) {
            took = double(i + 1) / 48000.0;
            return true;
        }
    }
    took = seconds;
    return false;
}

void show(const lexvcv::Core &core, const char *what) {
    lexparams::Published p = core.published();
    auto catalog = core.catalog();
    const char *name = "?";
    if (catalog && p.machine.program >= 0 && size_t(p.machine.program) < catalog->programs.size()) {
        name = catalog->programs[size_t(p.machine.program)].label.c_str();
    }
    auto texts = core.slider_texts();
    std::printf("  %-26s program %d %s, variation %d, %d sliders, slider_1 at %d \"%s\"\n", what, p.machine.program,
                name, p.machine.variation, p.machine.sliderCount, p.machine.sliders[0], texts.text[0]);
}

int operate_check(const std::string &dir, const std::string &catalogs) {
    std::printf("== operating %s through its catalog\n", dir.c_str());
    lexvcv::Core core;
    core.set_catalog_dir(catalogs);
    core.boot(dir);
    if (!wait_running(core) || !core.catalog()) {
        std::printf("  FAIL: no catalog\n");
        return 1;
    }
    auto catalog = core.catalog();
    int failures = 0;
    double took;
    auto idle = [](const lexparams::Published &p) { return p.ready && p.pendingMask == 0 && p.busyParam < 0; };
    if (!run_until(core, 5, [&](const lexparams::Published &p) { return idle(p) && p.machine.program >= 0; }, took)) {
        std::printf("  FAIL: the machine's state was never read\n");
        return 1;
    }
    show(core, "booted:");

    core.request(lexparams::kProgram, 1);
    bool ok = run_until(core, 10, [&](const lexparams::Published &p) { return idle(p) && p.machine.program == 1; }, took);
    std::printf("  program 2 %s after %.2f s\n", ok ? "loaded" : "FAILED", took);
    failures += !ok;
    show(core, "after the program:");

    const lexcat::Program &program = catalog->programs[1];
    int last = program.variations.empty() ? 1 : program.variations.back();
    core.request(lexparams::kVariation, last);
    ok = run_until(core, 10, [&](const lexparams::Published &p) { return idle(p) && p.machine.variation == last; }, took);
    std::printf("  variation %d %s after %.2f s\n", last, ok ? "loaded" : "FAILED", took);
    failures += !ok;
    show(core, "after the variation:");

    lexparams::Published before = core.published();
    int top = before.machine.rangeMax;
    core.request(lexparams::sliderParam(1), top);
    ok = run_until(core, 10, [&](const lexparams::Published &p) { return idle(p) && p.machine.sliders[0] == top; }, took);
    std::printf("  slider_1 (%s) to %d %s after %.2f s\n", program.slider(program.generic[0]).name.c_str(), top,
                ok ? "held" : "FAILED", took);
    failures += !ok;
    show(core, "after the slider:");
    return failures;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: core_check ROM_DIR [SECONDS]\n");
        return 2;
    }
    std::string dir = argv[1];
    double seconds = argc > 2 ? std::atof(argv[2]) : 10.0;
    int failures = 0;

    for (int rate : {48000, 44100}) {
        std::printf("== %d Hz, one sample at a time, %.0f s\n", rate, seconds);
        lexvcv::Core core;
        core.set_sample_rate(rate);
        auto t0 = Clock::now();
        core.boot(dir);
        if (!wait_running(core)) {
            return 1;
        }
        std::printf("  boot: %.2f s\n", std::chrono::duration<double>(Clock::now() - t0).count());
        size_t n = size_t(seconds * rate);
        std::vector<float> out_l(n), out_r(n);
        Noise noise;
        // As Rack's engine thread: flush-to-zero set (system::resetFpuFlags).
        // The reference below renders without it.
        lexvcv::DenormalsOn::set_flushing(true);
        auto t1 = Clock::now();
        for (size_t i = 0; i < n; i++) {
            float l = noise.next(), r = noise.next();
            core.process(l, r, out_l[i], out_r[i]);
        }
        double took = std::chrono::duration<double>(Clock::now() - t1).count();
        bool still = lexvcv::DenormalsOn::flushing();
        lexvcv::DenormalsOn::set_flushing(false);
        if (!still) {
            std::printf("  FAIL: the thread's flush-to-zero mode was not put back\n");
            failures++;
        }
        double peak = 0;
        for (size_t i = 0; i < n; i++) {
            peak = std::max(peak, double(std::fabs(out_l[i])) + double(std::fabs(out_r[i])));
        }
        std::printf("  cost: %.3f of real time (%.0f ns a sample), latency %d samples, output peak %.3f\n",
                    took / seconds, took / double(n) * 1e9, core.latency(), peak);
        if (core.status() != lexvcv::Status::Running || peak == 0) {
            std::printf("  FAIL: stopped or silent\n");
            failures++;
        }
        if (rate != 48000) {
            continue;
        }

        // Reference: the same set, booted the same way, rendered in blocks.
        auto set = lexplug::load_rom_directory(dir);
        lexplug::Session ref(set->model);
        for (const auto &chip : set->chips) {
            ref.engine.load(chip.bytes.data(), chip.bytes.size(), chip.base);
        }
        ref.engine.set_analog(true);
        constexpr int chunk = 4800;
        std::vector<float> in_l(chunk, 0.0f), in_r(chunk, 0.0f), ch[4];
        for (auto &c : ch) {
            c.assign(chunk, 0.0f);
        }
        float *o[4] = {ch[0].data(), ch[1].data(), ch[2].data(), ch[3].data()};
        while (ref.frame() < lexvcv::Core::boot_frames) {
            int k = int(std::min<uint64_t>(chunk, lexvcv::Core::boot_frames - ref.frame()));
            ref.render(in_l.data(), in_r.data(), o, k);
        }
        Noise again;
        size_t mismatches = 0, first = 0;
        for (size_t done = 0; done < n; done += chunk) {
            int k = int(std::min<size_t>(chunk, n - done));
            for (int i = 0; i < k; i++) {
                in_l[size_t(i)] = again.next();
                in_r[size_t(i)] = again.next();
            }
            ref.render(in_l.data(), in_r.data(), o, k);
            for (int i = 0; i < k; i++) {
                if (std::memcmp(&ch[0][size_t(i)], &out_l[done + size_t(i)], sizeof(float)) != 0 ||
                    std::memcmp(&ch[2][size_t(i)], &out_r[done + size_t(i)], sizeof(float)) != 0) {
                    if (mismatches++ == 0) {
                        first = done + size_t(i);
                    }
                }
            }
        }
        if (mismatches == 0) {
            std::printf("  bit-identical to the block-rendered Session over %zu samples\n", n);
        } else {
            std::printf("  FAIL: %zu samples differ from the block-rendered Session, the first at %zu\n", mismatches, first);
            failures++;
        }
    }
    if (argc > 3) {
        failures += operate_check(dir, argv[3]);
    }
    std::printf(failures == 0 ? "core_check: pass\n" : "core_check: FAILED\n");
    return failures == 0 ? 0 : 1;
}
