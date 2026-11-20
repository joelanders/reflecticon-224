// M0, the pre-registered speed gate:
// the cost of running the machine inside the audio callback, per host block.
//
//   speed_gate ROM_DIR TIMELINE.events [options]
//     --block N       host block size (default 128)
//     --rate R        host sample rate: 48000 (no resampler), 44100, 96000
//     --seconds S     steady-state audio after the timeline (default 300)
//     --analog 0|1    the analog boards (default 1)
//     --breakdown     time the analog code, the machine and the resampler
//                     separately (adds a few clock reads per frame)
//     --csv FILE      one line per timed block
//     --realtime      run the timed part as CoreAudio runs a host's IO
//                     thread: a time-constraint thread that has joined an
//                     audio work interval (os_workgroup), each block an
//                     interval with its deadline
//     --paced         start each block on its real-time schedule (sleep
//                     between blocks, as a device does) instead of back to back
//
// TIMELINE (bench/record_timeline.mjs): operator inputs by 48 kHz frame. The
// machine boots untimed up to the "boot" mark (in the plugin that happens on
// a background thread); from there every host block is timed: the operator
// part (program load, slider sweep, second program load) and then S seconds
// of steady state on the second program. Input: white noise at 0.25 peak,
// both channels, deterministic.
//
// Reported per phase: the ratio (block compute time / block duration) as
// mean, p99, p99.9 and max, and the thread's own CPU time. Pass: p99.9 < 0.5
// and max < 0.9 at 128 frames.
//
// Portable: the engine is JUCE-free, so this builds anywhere. --realtime (a
// time-constraint thread that has joined an audio work interval) is macOS
// only; elsewhere the note says so and the load is measured the same way.
//
//   clang++ -std=c++20 -O3 -g -DNDEBUG -ffp-contract=off -o speed_gate bench/speed_gate.cpp
//   g++     -std=c++20 -O3 -g -DNDEBUG -ffp-contract=off -o speed_gate bench/speed_gate.cpp
//
// A/B two builds of the machine on the same ROMs: the steady-state mean is
// the stable number (it is what a PGO or a flag change moves), and the last
// line is a hash of every output sample's bits, so two builds can be shown to
// differ in speed only. scripts/speed_gate_ab.sh runs the set.
#include "../source/engine.hpp"
#include "../source/resampler.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <AudioToolbox/AudioWorkInterval.h>
#include <os/workgroup.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct Event {
    uint64_t frame;
    std::string kind;
    unsigned a = 0, b = 0;
};

int load_chips(lexplug::Engine &engine, const fs::path &dir) {
    std::regex sbc(R"(SBC\s*(\d))", std::regex::icase), nvs(R"(NVS\s*(\d))", std::regex::icase),
        rom(R"(ROM\s*([1-4])(?!\d))", std::regex::icase);
    int loaded = 0;
    for (const auto &entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        std::smatch m;
        unsigned base;
        if (std::regex_search(name, m, sbc)) {
            base = unsigned(std::stoi(m[1]) - 1) * 0x800;
        } else if (std::regex_search(name, m, nvs)) {
            base = 0x8000 + unsigned(std::stoi(m[1]) - 1) * 0x1000;
        } else if (std::regex_search(name, m, rom)) {
            base = unsigned(std::stoi(m[1]) - 1) * 0x800;
        } else {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
        engine.load(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), base);
        loaded++;
    }
    return loaded;
}

int model_of(const fs::path &dir) {
    std::regex rom(R"(ROM\s*[1-4](?!\d))", std::regex::icase), xl(R"(SBC|NVS)", std::regex::icase);
    for (const auto &entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        if (std::regex_search(name, rom) && !std::regex_search(name, xl)) {
            return 1;
        }
    }
    return 0;
}

struct Block {
    int phase;              // 0 operator, 1 steady
    double ratio;           // compute / duration
    double cpu_ratio;       // this thread's CPU time / duration (less than ratio when preempted)
    double analog, machine, resample;   // seconds, with --breakdown
};

// ---- The two clocks, in nanoseconds so both platforms share the arithmetic ----

// This thread's own CPU time (what the plugin costs the audio thread).
uint64_t thread_cpu_ns() {
#if defined(__APPLE__)
    return clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
#elif defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user) == 0) {
        return 0;
    }
    auto ns = [](const FILETIME &t) {
        return (uint64_t(t.dwHighDateTime) << 32 | uint64_t(t.dwLowDateTime)) * 100ull;   // 100 ns units
    };
    return ns(kernel) + ns(user);
#else
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}

#if defined(__APPLE__)
// mach_absolute_time's ticks per nanosecond (numer/denom nanoseconds each).
double mach_ticks_per_ns() {
    static const double v = [] {
        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        return double(tb.denom) / double(tb.numer);
    }();
    return v;
}

uint64_t to_mach_ticks(uint64_t ns) {
    return uint64_t(double(ns) * mach_ticks_per_ns());
}
#endif

// A monotonic clock, for --paced's block schedule.
uint64_t now_ns() {
#if defined(__APPLE__)
    return uint64_t(double(mach_absolute_time()) / mach_ticks_per_ns());
#else
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
#endif
}

void wait_until_ns(uint64_t t) {
#if defined(__APPLE__)
    mach_wait_until(to_mach_ticks(t));
#else
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(t)));
#endif
}

#if defined(__APPLE__)
// Make this thread a real-time thread, as CoreAudio does for its IO thread:
// a time-constraint policy with the block as its period.
bool make_realtime(double block_seconds) {
    double ticks_per_second = 1e9 * mach_ticks_per_ns();
    thread_time_constraint_policy_data_t policy;
    policy.period = uint32_t(block_seconds * ticks_per_second);
    policy.computation = uint32_t(0.5 * block_seconds * ticks_per_second);
    policy.constraint = uint32_t(block_seconds * ticks_per_second);
    policy.preemptible = 1;
    return thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, thread_policy_t(&policy),
                             THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
}
#endif

// --realtime's CoreAudio setup: a time-constraint thread that joins an audio
// work interval, each block an interval with its deadline. A no-op with a
// note where CoreAudio's scheduling does not exist.
class Realtime {
public:
    Realtime(bool on, double block_seconds) {
        block_ns_ = uint64_t(block_seconds * 1e9);
#if defined(__APPLE__)
        if (!on) {
            return;
        }
        if (!make_realtime(block_seconds)) {
            throw std::runtime_error("could not set the time-constraint policy");
        }
        workgroup_ = AudioWorkIntervalCreate("speed_gate", OS_CLOCK_MACH_ABSOLUTE_TIME, nullptr);
        if (workgroup_ == nullptr || os_workgroup_join(workgroup_, &token_) != 0) {
            throw std::runtime_error("could not join an audio work interval");
        }
        active_ = true;
#else
        if (on) {
            std::fprintf(stderr, "speed_gate: --realtime is macOS-only; the load is measured the same way\n");
        }
        (void)block_seconds;
#endif
    }

    ~Realtime() {
#if defined(__APPLE__)
        if (active_) {
            os_workgroup_leave(workgroup_, &token_);
        }
#endif
    }

    Realtime(const Realtime &) = delete;
    Realtime &operator=(const Realtime &) = delete;

    void block_start(uint64_t ns) {
#if defined(__APPLE__)
        if (active_) {
            (void)os_workgroup_interval_start(workgroup_, to_mach_ticks(ns), to_mach_ticks(ns + block_ns_), nullptr);
        }
#else
        (void)ns;
#endif
    }

    void block_finish() {
#if defined(__APPLE__)
        if (active_) {
            (void)os_workgroup_interval_finish(workgroup_, nullptr);
        }
#endif
    }

private:
#if defined(__APPLE__)
    os_workgroup_interval_t workgroup_ = nullptr;
    os_workgroup_join_token_s token_{};
#endif
    uint64_t block_ns_ = 0;
    bool active_ = false;
};

double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    size_t i = size_t(std::min<double>(double(v.size() - 1), q * double(v.size() - 1) + 0.5));
    return v[i];
}

}  // namespace

int main(int argc, char **argv) try {
    if (argc < 3) {
        throw std::runtime_error("usage: speed_gate ROM_DIR TIMELINE.events [--block N] [--rate R] [--seconds S] [--analog 0|1] [--breakdown] [--csv FILE]");
    }
    fs::path rom_dir = argv[1];
    std::string timeline = argv[2], csv;
    int block = 128, rate = 48000;
    double seconds = 300;
    bool analog = true, breakdown = false, realtime = false, paced = false;
    for (int i = 3; i < argc; i++) {
        std::string k = argv[i];
        if (k == "--breakdown") {
            breakdown = true;
            continue;
        }
        if (k == "--realtime") {
            realtime = true;
            continue;
        }
        if (k == "--paced") {
            paced = true;
            continue;
        }
        if (i + 1 >= argc) {
            throw std::runtime_error("missing value for " + k);
        }
        std::string v = argv[++i];
        if (k == "--block") {
            block = std::stoi(v);
        } else if (k == "--rate") {
            rate = std::stoi(v);
        } else if (k == "--seconds") {
            seconds = std::stod(v);
        } else if (k == "--analog") {
            analog = v != "0";
        } else if (k == "--csv") {
            csv = v;
        } else {
            throw std::runtime_error("unknown option " + k);
        }
    }

    std::vector<Event> events;
    uint64_t boot_frame = 0, end_frame = 0;
    {
        std::ifstream in(timeline);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream s(line);
            std::string first;
            s >> first;
            if (first == "end") {
                s >> end_frame;
                continue;
            }
            Event e;
            e.frame = std::stoull(first);
            s >> e.kind;
            if (e.kind == "mark") {
                std::string name;
                s >> name;
                if (name == "boot") {
                    boot_frame = e.frame;
                }
                continue;
            }
            s >> e.a >> e.b;
            events.push_back(e);
        }
    }
    if (end_frame == 0 || boot_frame == 0) {
        throw std::runtime_error(timeline + ": no boot mark or end frame");
    }

    int model = model_of(rom_dir);
    lexplug::Engine engine(model);
    if (load_chips(engine, rom_dir) == 0) {
        throw std::runtime_error("no ROM chips in " + rom_dir.string());
    }
    engine.set_analog(analog);
    auto &host = engine.host();

    // Buffers, all sized up front as the plugin will.
    const int max_internal = block * 48000 / rate + 64;
    std::vector<float> in_l(static_cast<size_t>(max_internal)), in_r(static_cast<size_t>(max_internal));
    std::vector<float> out_ch[4];
    for (auto &o : out_ch) {
        o.assign(size_t(max_internal), 0.0f);
    }
    float *out_ptrs[4] = {out_ch[0].data(), out_ch[1].data(), out_ch[2].data(), out_ch[3].data()};
    const size_t block_n = static_cast<size_t>(block);
    std::vector<float> host_in_l(block_n), host_in_r(block_n), host_out_l(block_n), host_out_r(block_n);

    lexplug::Resampler up_l, up_r, down_l, down_r;
    up_l.setup(rate, 48000, block);
    up_r.setup(rate, 48000, block);
    down_l.setup(48000, rate, max_internal);
    down_r.setup(48000, rate, max_internal);
    const bool resampling = !up_l.identity();
    // The internal-rate input waiting to be rendered, primed so that it never runs dry.
    const size_t fifo_size = static_cast<size_t>(4 * max_internal + 256);
    std::vector<float> fifo_l(fifo_size), fifo_r(fifo_size);
    size_t fifo_n = 0;
    if (resampling) {
        fifo_n = 64;
    }
    const size_t host_fifo_size = static_cast<size_t>(2 * block + 64);
    std::vector<float> host_fifo_l(host_fifo_size), host_fifo_r(host_fifo_size);
    size_t host_fifo_n = 0;

    uint64_t frame = 0;   // internal 48 kHz frames rendered
    size_t next_event = 0;
    auto dispatch = [&]() {
        while (next_event < events.size() && events[next_event].frame <= frame) {
            const Event &e = events[next_event++];
            if (e.kind == "key") {
                engine.key(e.a, e.b != 0);
            } else if (e.kind == "fader") {
                engine.fader(e.a, e.b);
            } else if (e.kind == "poke") {
                engine.poke(uint16_t(e.a), uint8_t(e.b));
            } else if (e.kind == "button") {
                engine.button(e.a, e.b);
            } else if (e.kind == "pot") {
                engine.pot(e.a, e.b);
            }
        }
    };
    // A hash of every output sample's bits, so two builds of the machine can
    // be shown to differ in speed only (the same check byte_exact makes, minus
    // the reference loop).
    uint64_t output_hash = 1469598103934665603ull;   // FNV-1a
    auto hash_samples = [&](const float *const out[4], int n) {
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i < n; i++) {
                uint32_t bits;
                std::memcpy(&bits, &out[c][i], sizeof bits);
                output_hash = (output_hash ^ bits) * 1099511628211ull;
            }
        }
    };

    // Render n internal frames from in/out offset 0, splitting at events.
    double t_analog = 0, t_machine = 0;
    auto render = [&](const float *l, const float *r, int n) {
        int done = 0;
        while (done < n) {
            dispatch();
            int count = n - done;
            if (next_event < events.size()) {
                uint64_t until = events[next_event].frame;
                if (until > frame) {
                    count = int(std::min<uint64_t>(uint64_t(count), until - frame));
                }
            }
            float *o[4] = {out_ptrs[0] + done, out_ptrs[1] + done, out_ptrs[2] + done, out_ptrs[3] + done};
            if (breakdown) {
                // Engine::render, unrolled with clocks around its parts.
                auto &io = engine.analog_io();
                float four[4];
                for (int f = 0; f < count; f++) {
                    auto a = Clock::now();
                    lexplug::Tick end = io.before_frame(l[done + f], r[done + f]);
                    auto b = Clock::now();
                    host.run_until(end / lexplug::cpu_period);
                    auto c = Clock::now();
                    io.after_frame(four);
                    auto d = Clock::now();
                    for (int ch = 0; ch < 4; ch++) {
                        o[ch][f] = four[ch];
                    }
                    t_analog += std::chrono::duration<double>((b - a) + (d - c)).count();
                    t_machine += std::chrono::duration<double>(c - b).count();
                }
            } else {
                engine.render(l + done, r + done, o, count);
            }
            hash_samples(o, count);
            done += count;
            frame += uint64_t(count);
        }
    };

    // Boot, untimed, with silence. With --realtime the last 3 s before the
    // boot mark run as untimed real-time paced blocks instead (below), so the
    // timed part starts on a thread the scheduler has already promoted, as a
    // host's IO thread is.
    uint64_t boot_until = boot_frame;
    if (realtime) {
        boot_until = boot_frame - 3 * 48000;
    }
    {
        std::vector<float> zero(4800, 0.0f);
        std::vector<float> scratch[4];
        for (auto &s : scratch) {
            s.assign(4800, 0.0f);
        }
        while (frame < boot_until) {
            int n = int(std::min<uint64_t>(4800, boot_until - frame));
            dispatch();
            float *o[4] = {scratch[0].data(), scratch[1].data(), scratch[2].data(), scratch[3].data()};
            engine.render(zero.data(), zero.data(), o, n);
            frame += uint64_t(n);
        }
    }

    // Timed: host blocks.
    uint32_t noise = 12345;
    auto white = [&]() {
        noise = noise * 1664525u + 1013904223u;
        return (float(noise >> 8) / float(1u << 24) * 2.0f - 1.0f) * 0.25f;
    };
    const double block_seconds = double(block) / double(rate);
    Realtime realtime_thread(realtime, block_seconds);
    const uint64_t block_ns = uint64_t(block_seconds * 1e9);
    uint64_t next_start = now_ns();
    const uint64_t steady_start = end_frame;
    const uint64_t stop = end_frame + uint64_t(seconds * 48000.0);
    std::vector<Block> blocks;
    blocks.reserve(size_t(double(stop - boot_frame) / 48000.0 / block_seconds) + 16);
    while (frame < stop) {
        for (int i = 0; i < block; i++) {
            host_in_l[size_t(i)] = white();
            host_in_r[size_t(i)] = white();
        }
        t_analog = 0;
        t_machine = 0;
        double t_resample = 0;
        int phase = 0;
        if (frame < boot_frame) {
            phase = -1;   // warm-up, untimed
        } else if (frame >= steady_start) {
            phase = 1;
        }
        if (paced) {
            wait_until_ns(next_start);
        }
        uint64_t block_start = now_ns();
        if (paced) {
            block_start = next_start;
        }
        next_start = block_start + block_ns;
        realtime_thread.block_start(block_start);
        auto started = Clock::now();
        uint64_t cpu_started = thread_cpu_ns();
        if (!resampling) {
            render(host_in_l.data(), host_in_r.data(), block);
            std::copy(out_ch[0].begin(), out_ch[0].begin() + block, host_out_l.begin());
            std::copy(out_ch[2].begin(), out_ch[2].begin() + block, host_out_r.begin());
        } else {
            auto r0 = Clock::now();
            size_t made = size_t(up_l.process(host_in_l.data(), block, fifo_l.data() + fifo_n));
            up_r.process(host_in_r.data(), block, fifo_r.data() + fifo_n);
            fifo_n += made;
            int need = down_l.inputs_needed(block - int(host_fifo_n));
            auto r1 = Clock::now();
            if (size_t(need) > fifo_n) {
                throw std::runtime_error("input fifo ran dry");
            }
            render(fifo_l.data(), fifo_r.data(), need);
            auto r2 = Clock::now();
            std::copy(fifo_l.begin() + need, fifo_l.begin() + long(fifo_n), fifo_l.begin());
            std::copy(fifo_r.begin() + need, fifo_r.begin() + long(fifo_n), fifo_r.begin());
            fifo_n -= size_t(need);
            size_t got = size_t(down_l.process(out_ch[0].data(), need, host_fifo_l.data() + host_fifo_n));
            down_r.process(out_ch[2].data(), need, host_fifo_r.data() + host_fifo_n);
            host_fifo_n += got;
            std::copy(host_fifo_l.begin(), host_fifo_l.begin() + block, host_out_l.begin());
            std::copy(host_fifo_r.begin(), host_fifo_r.begin() + block, host_out_r.begin());
            std::copy(host_fifo_l.begin() + block, host_fifo_l.begin() + long(host_fifo_n), host_fifo_l.begin());
            std::copy(host_fifo_r.begin() + block, host_fifo_r.begin() + long(host_fifo_n), host_fifo_r.begin());
            host_fifo_n -= size_t(block);
            auto r3 = Clock::now();
            t_resample = std::chrono::duration<double>((r1 - r0) + (r3 - r2)).count();
        }
        double compute = std::chrono::duration<double>(Clock::now() - started).count();
        double cpu = double(thread_cpu_ns() - cpu_started) * 1e-9;
        realtime_thread.block_finish();
        if (phase < 0) {
            continue;
        }
        blocks.push_back({phase, compute / block_seconds, cpu / block_seconds, t_analog, t_machine, t_resample});
    }

    if (!csv.empty()) {
        FILE *f = std::fopen(csv.c_str(), "w");
        std::fprintf(f, "phase,ratio,cpu_ratio,analog_s,machine_s,resample_s\n");
        for (const Block &b : blocks) {
            std::fprintf(f, "%d,%.5f,%.5f,%.7f,%.7f,%.7f\n", b.phase, b.ratio, b.cpu_ratio, b.analog, b.machine, b.resample);
        }
        std::fclose(f);
    }
    const char *names[2] = {"operator", "steady"};
    for (int p = 0; p < 2; p++) {
        std::vector<double> r, c;
        double sum = 0, an = 0, ma = 0, rs = 0;
        for (const Block &b : blocks) {
            if (b.phase == p) {
                r.push_back(b.ratio);
                c.push_back(b.cpu_ratio);
                sum += b.ratio;
                an += b.analog;
                ma += b.machine;
                rs += b.resample;
            }
        }
        if (r.empty()) {
            continue;
        }
        std::printf("%-8s blocks %7zu  mean %.3f  p99 %.3f  p99.9 %.3f  max %.3f", names[p], r.size(), sum / double(r.size()),
                    quantile(r, 0.99), quantile(r, 0.999), *std::max_element(r.begin(), r.end()));
        double csum = 0;
        for (double v : c) {
            csum += v;
        }
        std::printf("  | cpu mean %.3f p99.9 %.3f max %.3f", csum / double(c.size()), quantile(c, 0.999),
                    *std::max_element(c.begin(), c.end()));
        if (breakdown) {
            double total = an + ma + rs;
            std::printf("  [analog %.0f%% machine %.0f%% resample %.0f%%]", 100 * an / total, 100 * ma / total,
                        100 * rs / total);
        }
        std::printf("\n");
    }
    std::printf("output hash %016llx\n", static_cast<unsigned long long>(output_hash));
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "speed_gate: %s\n", e.what());
    return 1;
}
