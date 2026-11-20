// M1's byte-exact gate: the plugin's processor, driven as a host drives it,
// against the reference loop (web-demo/wasm/web.cpp lex_render, copied below as
// tests/real_ir_render.cpp copies it) on the same ROM, input and controls.
//
//   byte_exact ROM_DIR TIMELINE.events [--tail S] [--schemes LIST] [--ftz-reference]
//              [--dump FILE] [--shift-controls N]
//     TIMELINE   bench/record_timeline.mjs's format: "frame kind a b" inputs
//                (key, fader, poke, button, pot), marks, "end FRAME"
//     --tail S   seconds rendered after the timeline's end (default 5)
//     --schemes  comma list of host block schemes (default 128,511,irregular):
//                a number = every block that size; "irregular" = sizes
//                1..2000 from a fixed seed, with a 20000-sample block (over
//                the 16384 capacity: split inside) every 50 blocks
//     --ftz-reference   also render the reference with flush-to-zero set, and
//                report whether it differs (whether the plugin's clearing of
//                a host's flush-to-zero mode is load-bearing)
//     --dump FILE  write the reference (float32, A,B,C,D interleaved, frames
//                boot_frames..) for comparison with the web build
//                (compare_wasm.mjs)
//     --shift-controls N  negative control: give the plugin every control N
//                frames late; the gate must then FAIL
//
// Alignment. Both sides count internal 48 kHz frames from power-on:
// - The reference runs frames 0.. with silent input up to the plugin's
//   boot_frames, then the test input; each control is given before the
//   frame it is stamped with.
// - The plugin boots on its own thread with silence to exactly boot_frames
//   (PluginProcessor::boot), taking the controls stamped before then. The
//   test waits for the booted machine to be offered, so the first
//   processBlock takes it: host sample i of the run is internal frame
//   boot_frames + i. That is checked (machine_frame() after the first
//   block), and the input fed at host sample i is the reference's input at
//   that frame. At 48 kHz the plugin adds no latency, so its output sample i
//   is compared with the reference's frame boot_frames + i.
// - The host calls processBlock with flush-to-zero set (ScopedNoDenormals,
//   as many hosts do); the plugin must clear it for the machine.
// Channels: the default pair (L = A, R = C) in every scheme, and B and D
// (set_output_pair(1, 3)) in one more run of the first scheme.
#include "PluginProcessor.h"
#include "rom_loading.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using lexplug::ControlEvent;

const char *verdict(bool good, const char *yes, const char *no) {
    if (good) {
        return yes;
    }
    return no;
}
using lexplug::ControlKind;

struct Timeline {
    std::vector<ControlEvent> events;
    uint64_t end = 0;
};

Timeline read_timeline(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    Timeline t;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string first, kind;
        if (!(s >> first)) {
            continue;
        }
        if (first == "end") {
            s >> t.end;
            continue;
        }
        s >> kind;
        ControlEvent e{std::stoull(first), ControlKind::Key};
        if (kind == "mark") {
            continue;
        } else if (kind == "key") {
            e.kind = ControlKind::Key;
        } else if (kind == "fader") {
            e.kind = ControlKind::Fader;
        } else if (kind == "poke") {
            e.kind = ControlKind::Poke;
        } else if (kind == "button") {
            e.kind = ControlKind::Button;
        } else if (kind == "pot") {
            e.kind = ControlKind::Pot;
        } else {
            throw std::runtime_error("unknown event kind " + kind);
        }
        s >> e.a >> e.b;
        t.events.push_back(e);
    }
    if (t.end == 0) {
        throw std::runtime_error(path + ": no end frame");
    }
    return t;
}

// The test input at an internal frame: silence through the boot, then white
// noise (0.25 peak, a different stream per channel) with a 0.6 impulse every
// 2 s, as a pure function of the frame so both sides agree by construction.
float input_at(uint64_t frame, int channel) {
    if (frame < PluginProcessor::boot_frames) {
        return 0.0f;
    }
    uint64_t since = frame - PluginProcessor::boot_frames;
    if (since % 96000 == 1000) {
        return 0.6f;
    }
    uint64_t x = frame * 2 + uint64_t(channel) + 0x9E3779B97F4A7C15ull;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return (float(x >> 40) / float(1u << 24) * 2.0f - 1.0f) * 0.25f;
}

void apply(lexicon224x::cpu::Host &h, const ControlEvent &e) {
    switch (e.kind) {
        case ControlKind::Key:
            h.key(uint8_t(e.a), e.b != 0);
            break;
        case ControlKind::Fader:
            h.fader(e.a, uint8_t(e.b));
            break;
        case ControlKind::Poke:
            if (e.a >= 0x2000 && e.a < 0x4000) {
                h.memory[e.a] = uint8_t(e.b);
            }
            break;
        case ControlKind::Button:
            h.panel.switches[e.a] = uint8_t(~e.b);
            break;
        case ControlKind::Pot:
            h.panel.pots[e.a] = uint8_t(e.b);
            break;
    }
}

// The reference: web-demo/wasm/web.cpp's lex_create + lex_load + lex_render, frame by
// frame on Host + AnalogIO directly. Returns the four channels, interleaved,
// for frames boot_frames..total-1.
std::vector<float> render_reference(const lexplug::RomSet &roms, const Timeline &t, uint64_t total, bool ftz) {
    using namespace lexicon224x::cpu;
    lexicon224x::Model model = lexicon224x::Model::Lexicon224X;
    if (roms.model == 1) {
        model = lexicon224x::Model::Lexicon224;
    }
    auto host = std::make_unique<Host>(model);
    for (const auto &chip : roms.chips) {
        std::memcpy(host->memory.data() + chip.base, chip.bytes.data(), chip.bytes.size());
    }
    auto io = std::make_unique<lexicon224x::analog::AnalogIO>(*host);
    std::unique_ptr<juce::ScopedNoDenormals> flush;
    if (ftz) {
        flush = std::make_unique<juce::ScopedNoDenormals>();
    }
    std::vector<float> out;
    out.reserve(size_t(total - PluginProcessor::boot_frames) * 4);
    size_t next = 0;
    float four[4];
    for (uint64_t f = 0; f < total; f++) {
        while (next < t.events.size() && t.events[next].frame <= f) {
            apply(*host, t.events[next++]);
        }
        Tick end = io->before_frame(input_at(f, 0), input_at(f, 1));
        host->run_until(end / cpu_period);
        io->after_frame(four);
        if (f >= PluginProcessor::boot_frames) {
            out.insert(out.end(), four, four + 4);
        }
    }
    return out;
}

struct PluginRun {
    std::vector<float> left, right;
    size_t blocks = 0, largest = 0;
    bool aligned = false;
};

std::vector<int> block_sizes(const std::string &scheme, uint64_t frames) {
    std::vector<int> sizes;
    uint64_t total = 0;
    uint32_t seed = 20260927;
    while (total < frames) {
        int n;
        if (scheme == "irregular") {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + int((seed >> 8) % 2000);
            if (sizes.size() % 50 == 49) {
                n = 20000;
            }
        } else {
            n = std::stoi(scheme);
        }
        n = int(std::min<uint64_t>(uint64_t(n), frames - total));
        sizes.push_back(n);
        total += uint64_t(n);
    }
    return sizes;
}

PluginRun render_plugin(const Timeline &t, uint64_t total, const std::string &scheme, int out_left, int out_right,
                        uint64_t shift) {
    PluginRun run;
    PluginProcessor processor;
    std::vector<ControlEvent> controls = t.events;
    for (auto &e : controls) {
        e.frame += shift;
    }
    processor.schedule_controls(controls);
    processor.set_output_pair(out_left, out_right);
    processor.setPlayConfigDetails(2, 2, 48000.0, 128);
    processor.prepareToPlay(48000.0, 128);
    if (processor.getLatencySamples() != 0) {
        throw std::runtime_error("latency reported at 48 kHz");
    }
    // Wait for the booted machine to be offered (status stays Booting until a
    // block takes it; a machine is offered when the boot thread finishes).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while (!processor.machine_offered()) {
        if (processor.status() == PluginProcessor::Status::NoRoms ||
            processor.status() == PluginProcessor::Status::Stopped) {
            throw std::runtime_error("the plugin did not boot");
        }
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("boot timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    uint64_t frames = total - PluginProcessor::boot_frames;
    run.left.reserve(frames);
    run.right.reserve(frames);
    juce::AudioBuffer<float> buffer(2, 20000);
    juce::MidiBuffer midi;
    uint64_t done = 0;
    for (int n : block_sizes(scheme, frames)) {
        buffer.setSize(2, n, false, false, true);
        for (int i = 0; i < n; i++) {
            uint64_t frame = PluginProcessor::boot_frames + done + uint64_t(i);
            buffer.setSample(0, i, input_at(frame, 0));
            buffer.setSample(1, i, input_at(frame, 1));
        }
        {
            juce::ScopedNoDenormals host_mode;
            processor.processBlock(buffer, midi);
        }
        if (run.blocks == 0) {
            run.aligned = processor.machine_frame() == PluginProcessor::boot_frames + uint64_t(n);
        }
        run.left.insert(run.left.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + n);
        run.right.insert(run.right.end(), buffer.getReadPointer(1), buffer.getReadPointer(1) + n);
        run.blocks++;
        run.largest = std::max(run.largest, size_t(n));
        done += uint64_t(n);
    }
    if (processor.status() != PluginProcessor::Status::Running) {
        throw std::runtime_error("the plugin's machine stopped");
    }
    if (processor.machine_frame() != total) {
        throw std::runtime_error("machine frame count mismatch at the end");
    }
    return run;
}

// Compare one plugin channel with the reference's channel `c`, bit for bit.
bool compare(const std::vector<float> &plugin, const std::vector<float> &reference, int c, const char *label,
             const std::string &scheme) {
    size_t frames = reference.size() / 4, first = frames;
    size_t differing = 0;
    float peak = 0;
    for (size_t i = 0; i < frames; i++) {
        float want = reference[i * 4 + size_t(c)];
        peak = std::max(peak, std::fabs(want));
        if (std::memcmp(&plugin[i], &want, sizeof want) != 0) {
            differing++;
            if (first == frames) {
                first = i;
            }
        }
    }
    if (differing == 0) {
        std::printf("  %-10s channel %s: %zu frames bit-identical (peak %.4f)\n", scheme.c_str(), label, frames, peak);
        return true;
    }
    std::printf("  %-10s channel %s: %zu of %zu frames DIFFER, first at frame %llu (plugin %.9g, reference %.9g)\n",
                scheme.c_str(), label, differing, frames,
                (unsigned long long)(first + PluginProcessor::boot_frames), plugin[first],
                reference[first * 4 + size_t(c)]);
    return false;
}

}  // namespace

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::fprintf(stderr, "usage: byte_exact ROM_DIR TIMELINE.events [--tail S] [--schemes LIST] [--ftz-reference]\n");
        return 2;
    }
    std::string rom_dir = argv[1], timeline_path = argv[2], schemes_arg = "128,511,irregular", dump;
    uint64_t shift = 0;
    double tail = 5;
    bool ftz_reference = false;
    for (int i = 3; i < argc; i++) {
        std::string k = argv[i];
        if (k == "--ftz-reference") {
            ftz_reference = true;
        } else if (k == "--tail" && i + 1 < argc) {
            tail = std::stod(argv[++i]);
        } else if (k == "--dump" && i + 1 < argc) {
            dump = argv[++i];
        } else if (k == "--shift-controls" && i + 1 < argc) {
            shift = std::stoull(argv[++i]);
        } else if (k == "--schemes" && i + 1 < argc) {
            schemes_arg = argv[++i];
        } else {
            throw std::runtime_error("unknown option " + k);
        }
    }
    juce::ScopedJuceInitialiser_GUI juce_init;
    setenv("LEXICON224_ROMPATH", rom_dir.c_str(), 1);
    auto roms = lexplug::load_rom_directory(rom_dir);
    if (!roms) {
        throw std::runtime_error("no ROM chips in " + rom_dir);
    }
    Timeline t = read_timeline(timeline_path);
    std::stable_sort(t.events.begin(), t.events.end(),
                     [](const ControlEvent &x, const ControlEvent &y) { return x.frame < y.frame; });
    uint64_t total = t.end + uint64_t(tail * 48000.0);
    std::printf("byte_exact: %s, model %d, %zu controls, boot %llu frames, compared frames %llu..%llu\n",
                timeline_path.c_str(), roms->model, t.events.size(), (unsigned long long)PluginProcessor::boot_frames,
                (unsigned long long)PluginProcessor::boot_frames, (unsigned long long)(total - 1));

    std::vector<float> reference = render_reference(*roms, t, total, false);
    bool ok = true;
    if (!dump.empty()) {
        std::ofstream o(dump, std::ios::binary);
        o.write(reinterpret_cast<const char *>(reference.data()), std::streamsize(reference.size() * sizeof(float)));
    }
    if (ftz_reference) {
        std::vector<float> flushed = render_reference(*roms, t, total, true);
        size_t differing = 0;
        for (size_t i = 0; i < reference.size(); i++) {
            if (std::memcmp(&reference[i], &flushed[i], sizeof(float)) != 0) {
                differing++;
            }
        }
        std::printf("  reference with flush-to-zero: %zu of %zu samples differ from the reference\n", differing,
                    reference.size());
    }

    std::vector<std::string> schemes;
    {
        std::stringstream s(schemes_arg);
        std::string item;
        while (std::getline(s, item, ',')) {
            schemes.push_back(item);
        }
    }
    for (size_t k = 0; k < schemes.size(); k++) {
        PluginRun run = render_plugin(t, total, schemes[k], 0, 2, shift);
        std::printf("  %-10s %zu blocks, largest %zu, first block aligned to boot_frames: %s\n", schemes[k].c_str(),
                    run.blocks, run.largest, verdict(run.aligned, "yes", "NO"));
        ok = ok && run.aligned;
        ok = compare(run.left, reference, 0, "A", schemes[k]) && ok;
        ok = compare(run.right, reference, 2, "C", schemes[k]) && ok;
        if (k == 0) {
            PluginRun other = render_plugin(t, total, schemes[k], 1, 3, shift);
            ok = ok && other.aligned;
            ok = compare(other.left, reference, 1, "B", schemes[k]) && ok;
            ok = compare(other.right, reference, 3, "D", schemes[k]) && ok;
        }
    }
    std::printf("byte_exact: %s\n", verdict(ok, "PASS", "FAIL"));
    if (!ok) {
        return 1;
    }
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "byte_exact: %s\n", e.what());
    return 1;
}
