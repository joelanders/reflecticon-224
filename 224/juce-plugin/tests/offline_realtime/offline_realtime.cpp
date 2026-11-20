// Offline == realtime (M5's automation gate): one host automation timeline
// rendered through the processor in several host block schemes, with
// isNonRealtime() true and false; every run's output must be bit-identical.
//
//   offline_realtime ROM_DIR [--seconds S] [--rate HZ]... [--state-missing DIR]
//                    [--round-trip SEED]... [--skip-schemes]
//
// --state-missing DIR  also a state round trip (save after the timeline,
//                      restore into a new instance: same program, variation
//                      and stored bytes), then the same restore with
//                      LEXICON224_ROMPATH=DIR, a location without the set:
//                      the instance runs dry and saves the state unchanged.
//
// --round-trip SEED   a state round trip with real slider moves (one per
//                      seed): load a random program (and variation, where
//                      the catalog has several), move sliders to random
//                      control values through the host parameters (on the
//                      224 every parameter: BASS, MID, CROSSOVER, TREBLE
//                      DECAY, DEPTH, PRE-DELAY, DIFFUSION; elsewhere up to
//                      eight named sliders; not the unmeasurable ones, whose
//                      value the share format cannot carry), save, restore
//                      into a fresh instance, and require the same program, variation
//                      and stored bytes, a restore that says nothing is
//                      inexact, and (measured sliders) positions that store
//                      the bytes they show.
// --skip-schemes       only the state checks (no offline/realtime runs).
//
// The timeline moves the direct parameters (input level, dry/wet, the output
// pair, the analog boards) and also asks for operator tasks (program,
// variation, a slider, a toggle): with the operator stub those are refused
// and must change nothing; once the operator lands they become real moves
// and this test covers them unchanged.
//
// How a host delivers automation here: as parameter values set (setValue,
// as the VST3/AU wrappers do) at the start of the block that begins at the
// point's sample position. Every scheme splits its blocks at the points, as
// a sample-accurate host does, so each point reaches the processor at the
// same sample in every run; what differs is everything else (the block
// sizes around the points, the realtime flag). The message thread's work
// (write-back, UiSnapshot) runs after every block, as a host's timer would.
//
// Alignment: each run waits for the booted machine to be offered before its
// first block, so host sample 0 is internal frame boot_frames in every run.
#include "PluginProcessor.h"
#include "embedded.hpp"
#include "../../source/params/layout.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *verdict(bool good, const char *yes, const char *no) {
    if (good) {
        return yes;
    }
    return no;
}

struct Point {
    double seconds;
    int param;
    float value;
};

// The automation timeline (seconds after the machine is live).
std::vector<Point> timeline() {
    using namespace lexparams;
    SliderRange larc = kLarcRange;
    return {
        {0.50, kLevelDb, levelNormOf(6.0f)},
        {0.75, kProgram, fromInt(kProgram, 1, larc)},
        {1.00, kDryWet, 0.35f},
        {1.20, sliderParam(1), 0.3f},
        {1.70, kOutputLeft, fromInt(kOutputLeft, 1, larc)},
        {2.00, kVariation, fromInt(kVariation, 2, larc)},
        {2.30, kAnalog, 0.0f},
        {2.60, toggleParam(kModeEnh), 1.0f},
        {2.90, kLevelDb, levelNormOf(-3.0f)},
        {3.10, kDryWet, 0.0f},
        {3.60, kDryWet, 1.0f},
        {3.60, kOutputRight, fromInt(kOutputRight, 3, larc)},
        {4.20, kAnalog, 1.0f},
        {4.50, kLevelDb, levelNormOf(0.0f)},
    };
}

// The test input at host sample i: white noise (0.25 peak, a different
// stream per channel) with a 0.6 impulse every second.
float input_at(uint64_t i, int channel, int rate) {
    uint64_t x = i * 2654435761u + uint64_t(channel) * 0x9e3779b97f4a7c15ull + 12345;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    float noise = float(double(x >> 11) / double(1ull << 53) * 2.0 - 1.0) * 0.25f;
    if (i % uint64_t(rate) == 0) {
        noise += 0.6f;
    }
    return noise;
}

struct Scheme {
    std::string name;
    int block;          // > 0: that size; 0: irregular sizes from `seed`
    unsigned seed;
    bool nonRealtime;
};

std::vector<float> run(const Scheme &scheme, int rate, uint64_t total, bool &ok, std::string *state = nullptr,
                       lexparams::MachineState *machine = nullptr) {
    PluginProcessor processor;
    processor.setNonRealtime(scheme.nonRealtime);
    processor.setPlayConfigDetails(2, 2, double(rate), 4096);
    processor.prepareToPlay(double(rate), 4096);
    auto start = std::chrono::steady_clock::now();
    while (!processor.machine_offered()) {
        if (processor.status() == PluginProcessor::Status::NoRoms ||
            processor.status() == PluginProcessor::Status::Stopped) {
            std::printf("  %s: no machine (status %d)\n", scheme.name.c_str(), int(processor.status()));
            ok = false;
            return {};
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(300)) {
            std::printf("  %s: boot timed out\n", scheme.name.c_str());
            ok = false;
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::vector<Point> points = timeline();
    std::vector<uint64_t> at;
    for (const Point &p : points) {
        at.push_back(uint64_t(std::llround(p.seconds * rate)));
    }
    std::mt19937 random(scheme.seed);
    std::uniform_int_distribution<int> sizes(1, 3000);
    std::vector<float> out;
    out.reserve(size_t(total) * 2);
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    uint64_t done = 0;
    uint64_t traced = 0;
    size_t next = 0;
    while (done < total) {
        while (next < points.size() && at[next] <= done) {
            processor.param(points[next].param)->setValue(points[next].value);
            next++;
        }
        int n = scheme.block;
        if (n == 0) {
            n = sizes(random);
        }
        n = std::min(n, 4096);
        if (next < points.size() && at[next] - done < uint64_t(n)) {
            n = int(at[next] - done);
        }
        if (uint64_t(n) > total - done) {
            n = int(total - done);
        }
        buffer.setSize(2, n, false, false, true);
        for (int i = 0; i < n; i++) {
            buffer.setSample(0, i, input_at(done + uint64_t(i), 0, rate));
            buffer.setSample(1, i, input_at(done + uint64_t(i), 1, rate));
        }
        {
            juce::ScopedNoDenormals flushing;
            processor.processBlock(buffer, midi);
        }
        for (int i = 0; i < n; i++) {
            out.push_back(buffer.getSample(0, i));
            out.push_back(buffer.getSample(1, i));
        }
        done += uint64_t(n);
        if (std::getenv("OFFLINE_REALTIME_TRACE") != nullptr) {
            uint64_t d = processor.scheduler().dispatchedCount();
            if (d != traced) {
                traced = d;
                std::printf("    block end %llu: task %llu on parameter %d\n", (unsigned long long)done,
                            (unsigned long long)d, processor.scheduler().busyParam());
            }
        }
        processor.service_message_thread();
    }
    const lexparams::Scheduler &sched = processor.scheduler();
    std::printf("  %-20s tasks dispatched %llu, dropped %llu, rejected %llu, host changes %llu\n", scheme.name.c_str(),
                (unsigned long long)sched.dispatchedCount(), (unsigned long long)sched.droppedCount(),
                (unsigned long long)sched.rejectedCount(), (unsigned long long)sched.hostChangeCount());
    const lexparams::MachineState &m = sched.machine();
    std::printf("  %-20s machine at the end: program %d, variation %d, slider_1 stored %d, toggles %02x known %02x\n",
                scheme.name.c_str(), m.program, m.variation, int(m.stored[0]), m.toggles, m.togglesKnown);
    if (state != nullptr) {
        juce::MemoryBlock block;
        processor.getStateInformation(block);
        *state = block.toString().toStdString();
        *machine = m;
    }
    if (processor.status() != PluginProcessor::Status::Running) {
        std::printf("  %s: machine not running at the end (status %d)\n", scheme.name.c_str(), int(processor.status()));
        ok = false;
    }
    return out;
}

// Restore `blob` into a fresh processor (before prepareToPlay, as a host
// loading a project does), let it boot and replay, take the machine with one
// block, and return what the machine holds and what the processor saves.
bool restore(const std::string &blob, lexparams::MachineState &machine, std::string &saved,
             PluginProcessor::Status &status, std::string &text) {
    PluginProcessor processor;
    processor.setStateInformation(blob.data(), int(blob.size()));
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    bool finished = processor.wait_for_boot(300.0);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4; i++) {
        buffer.clear();
        processor.processBlock(buffer, midi);
        processor.service_message_thread();
    }
    machine = processor.scheduler().machine();
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    saved = block.toString().toStdString();
    status = processor.status();
    lexui::UiSnapshot snapshot;
    processor.readUiSnapshot(snapshot);
    text = snapshot.statusText;
    return finished;
}

std::string first_line_with(const std::string &blob, const std::string &key) {
    size_t at = blob.find("\n" + key + " ");
    if (at == std::string::npos) {
        return "";
    }
    size_t end = blob.find('\n', at + 1);
    return blob.substr(at + 1, end - at - 1);
}

// Render zero input in 512-frame blocks until the scheduler has nothing
// pending or in flight (and the machine is ready), at least `min_blocks`
// blocks, at most `max_seconds` of host time. True if it went idle.
bool run_until_idle(PluginProcessor &processor, int min_blocks, double max_seconds) {
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    int blocks = 0;
    int max_blocks = int(max_seconds * 48000.0 / 512.0);
    while (blocks < max_blocks) {
        buffer.clear();
        processor.processBlock(buffer, midi);
        processor.service_message_thread();
        blocks++;
        lexparams::Published s = processor.scheduler().channel().read();
        if (blocks >= min_blocks && s.ready != 0 && s.pendingMask == 0 && s.busyParam < 0) {
            return true;
        }
    }
    return false;
}

std::string rom_of(PluginProcessor &processor) {
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    std::string blob = block.toString().toStdString();
    std::string line = first_line_with(blob, "rom");
    if (line.size() > 4) {
        return line.substr(4);
    }
    return "";
}

// One --round-trip seed (see the header). True if it passed.
bool round_trip(unsigned seed) {
    using namespace lexparams;
    std::printf("round trip, seed %u\n", seed);
    std::mt19937 random(seed);
    PluginProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    if (!processor.wait_for_boot(300.0) || !run_until_idle(processor, 4, 60.0)) {
        std::printf("  the machine did not boot (status %d)\n", int(processor.status()));
        return false;
    }
    const lexcat::Catalog *catalog = lexplug::embeddedCatalogs().find(rom_of(processor));
    if (catalog == nullptr || catalog->programs.empty()) {
        std::printf("  no catalog for the booted set\n");
        return false;
    }
    SliderRange range{catalog->rawMin(), catalog->rawMax()};
    int index = std::uniform_int_distribution<int>(0, int(catalog->programs.size()) - 1)(random);
    const lexcat::Program &program = catalog->programs[size_t(index)];
    processor.param(kProgram)->setValue(fromInt(kProgram, index, range));
    if (!run_until_idle(processor, 4, 120.0)) {
        std::printf("  the program load did not finish\n");
        return false;
    }
    int variation = 1;
    if (program.variations.size() > 1) {
        variation = program.variations[size_t(
            std::uniform_int_distribution<int>(0, int(program.variations.size()) - 1)(random))];
    }
    if (variation != processor.scheduler().machine().variation) {
        processor.param(kVariation)->setValue(fromInt(kVariation, variation, range));
        if (!run_until_idle(processor, 4, 120.0)) {
            std::printf("  the variation load did not finish\n");
            return false;
        }
    }
    // Sliders the web share format can carry: every one with a measured
    // stored-byte table. (An unmeasurable one, the LARC's SIZE, keeps its
    // value elsewhere; its record byte is scratch while the firmware
    // rebuilds, so a SIZE move cannot round-trip through a share payload.)
    std::vector<int> ks;
    int skipped = 0;
    for (int k = 1; k <= int(program.generic.size()) && k <= kSliders; k++) {
        if (program.measuredSlider(program.generic[size_t(k - 1)], variation).measured()) {
            ks.push_back(k);
        } else {
            skipped++;
        }
    }
    if (skipped > 0) {
        std::printf("  (%d unmeasurable slider(s) not moved)\n", skipped);
    }
    std::shuffle(ks.begin(), ks.end(), random);
    if (catalog->remote != lexcat::Remote::Panel224 && ks.size() > 8) {
        ks.resize(8);
    }
    std::string moved;
    for (int k : ks) {
        int value = std::uniform_int_distribution<int>(range.min, range.max)(random);
        processor.param(sliderParam(k))->setValue(fromInt(sliderParam(k), value, range));
        const lexcat::SliderRef &ref = program.generic[size_t(k - 1)];
        moved += " " + program.slider(ref).name + "=" + std::to_string(value);
    }
    if (!run_until_idle(processor, 4, 240.0)) {
        std::printf("  the slider moves did not finish\n");
        return false;
    }
    const MachineState original = processor.scheduler().machine();
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    std::string blob = block.toString().toStdString();
    std::printf("  %s: %s v%d, moved%s\n  saved %s\n", catalog->rom.c_str(), program.label.c_str(), variation,
                moved.c_str(), first_line_with(blob, "share").c_str());
    bool ok = original.program == index && original.variation == variation;
    if (!ok) {
        std::printf("  the machine did not load the program and variation (it holds %d v%d)\n", original.program,
                    original.variation);
    }

    MachineState restored;
    std::string saved;
    PluginProcessor::Status status;
    std::string text;
    bool finished = restore(blob, restored, saved, status, text);
    int differing = 0;
    std::string diffs;
    for (int k = 1; k <= original.sliderCount; k++) {
        if (restored.stored[k - 1] != original.stored[k - 1]) {
            differing++;
            const lexcat::SliderRef &ref = program.generic[size_t(k - 1)];
            diffs += " " + program.slider(ref).name + " " + std::to_string(original.stored[k - 1]) + "->" +
                     std::to_string(restored.stored[k - 1]);
        }
    }
    bool same = finished && restored.program == original.program && restored.variation == original.variation &&
                restored.sliderCount == original.sliderCount && differing == 0;
    std::printf("  restored: program %d variation %d (saved %d %d), stored bytes %s%s\n", restored.program,
                restored.variation, original.program, original.variation, verdict(same, "identical", "DIFFER:"),
                diffs.c_str());
    if (!same) {
        ok = false;
    }
    if (!text.empty()) {
        std::printf("  the restored instance says: %s\n", text.c_str());
    }
    if (text.find("not replayed") != std::string::npos || text.find("could not") != std::string::npos) {
        ok = false;
    }
    if (first_line_with(saved, "share") != first_line_with(blob, "share")) {
        std::printf("  the restored instance saves another share: %s\n", first_line_with(saved, "share").c_str());
        ok = false;
    }
    // Where the slider parameters sit after the restore: on a measured
    // slider, a pot reading that stores the byte it shows.
    int placed = 0;
    for (int k = 1; k <= restored.sliderCount && same; k++) {
        const lexcat::Slider &slider = program.measuredSlider(program.generic[size_t(k - 1)], restored.variation);
        if (!slider.measured()) {
            continue;
        }
        if (lexcat::readingFor(slider, restored.stored[k - 1]) < 0) {
            continue;   // a preset byte no control value stores (not a move; the slider shows its text)
        }
        int position = restored.sliders[k - 1];
        if (!slider.stores(position, restored.stored[k - 1])) {
            int stores = -1;
            if (position >= 0 && size_t(position) < slider.stored.size()) {
                stores = slider.stored[size_t(position)];
            }
            std::printf("  %s sits at %d, which stores %d, not %d\n", slider.name.c_str(), position, stores,
                        int(restored.stored[k - 1]));
            ok = false;
        }
        placed++;
    }
    std::printf("  round trip seed %u: %s (%d measured slider positions checked)\n", seed, verdict(ok, "PASS", "FAIL"),
                placed);
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: offline_realtime ROM_DIR [--seconds S] [--rate HZ]...\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juce;
    setenv("LEXICON224_ROMPATH", argv[1], 1);
    // Never the user's own settings or drop folder: an empty support folder.
    juce::File support = juce::File::createTempFile("lexplug-support");
    support.createDirectory();
    setenv("LEXICON224_SUPPORT_DIR", support.getFullPathName().toRawUTF8(), 1);
    double seconds = 5.0;
    std::vector<int> rates;
    std::string state_dir;
    std::vector<unsigned> round_trips;
    bool skip_schemes = false;
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        } else if (arg == "--state-missing" && i + 1 < argc) {
            state_dir = argv[++i];
        } else if (arg == "--rate" && i + 1 < argc) {
            rates.push_back(std::atoi(argv[++i]));
        } else if (arg == "--round-trip" && i + 1 < argc) {
            round_trips.push_back(unsigned(std::atoi(argv[++i])));
        } else if (arg == "--skip-schemes") {
            skip_schemes = true;
        }
    }
    if (rates.empty()) {
        rates = {48000, 44100};
    }
    std::vector<Scheme> schemes = {
        {"128 realtime", 128, 0, false},
        {"512 offline", 512, 0, true},
        {"irregular offline", 0, 7, true},
        {"64 realtime", 64, 0, false},
        {"irregular realtime", 0, 99, false},
    };
    bool all = true;
    if (skip_schemes) {
        rates.clear();
    }
    for (int rate : rates) {
        uint64_t total = uint64_t(seconds * rate);
        std::printf("rate %d Hz, %llu samples, %zu automation points\n", rate, (unsigned long long)total,
                    timeline().size());
        std::vector<float> reference;
        for (size_t s = 0; s < schemes.size(); s++) {
            if (rate != 48000 && s > 2) {
                continue;   // the resampled path: three schemes suffice
            }
            bool ok = true;
            std::vector<float> out = run(schemes[s], rate, total, ok);
            if (!ok) {
                all = false;
                continue;
            }
            if (reference.empty()) {
                reference = out;
                double energy = 0.0;
                for (float x : out) {
                    energy += double(x) * double(x);
                }
                std::printf("  %-20s reference (rms %.4f)\n", schemes[s].name.c_str(),
                            std::sqrt(energy / double(std::max<size_t>(1, out.size()))));
                continue;
            }
            size_t first = out.size();
            size_t differing = 0;
            for (size_t i = 0; i < out.size() && i < reference.size(); i++) {
                if (std::memcmp(&out[i], &reference[i], sizeof(float)) != 0) {
                    if (first == out.size()) {
                        first = i;
                    }
                    differing++;
                }
            }
            bool same = differing == 0 && out.size() == reference.size();
            if (same) {
                std::printf("  %-20s identical\n", schemes[s].name.c_str());
            } else {
                std::printf("  %-20s DIFFERS: %zu samples, first at host sample %zu channel %zu\n",
                            schemes[s].name.c_str(), differing, first / 2, first % 2);
                all = false;
            }
        }
    }
    if (!state_dir.empty()) {
        // State round trip: save after the timeline, restore into a new
        // instance (boot + replay on the boot thread, then the swap).
        std::string blob;
        lexparams::MachineState original;
        bool ok = true;
        run(schemes[0], 48000, uint64_t(seconds * 48000), ok, &blob, &original);
        std::printf("state: saved\n%s", blob.c_str());
        lexparams::MachineState restored;
        std::string saved;
        PluginProcessor::Status status;
        std::string text;
        bool finished = restore(blob, restored, saved, status, text);
        bool same = finished && restored.program == original.program && restored.variation == original.variation &&
                    restored.sliderCount == original.sliderCount &&
                    std::memcmp(restored.stored, original.stored, sizeof original.stored) == 0;
        std::printf("state: restored program %d variation %d (original %d %d), stored bytes %s\n", restored.program,
                    restored.variation, original.program, original.variation, verdict(same, "equal", "DIFFER"));
        std::printf("state: the restored instance saves the same share: %s\n",
                    verdict(first_line_with(saved, "share") == first_line_with(blob, "share"), "yes", "NO"));
        if (!text.empty()) {
            std::printf("state: the restored instance says: %s\n", text.c_str());
        }
        // An inexact replay (a saved stored byte that is not a control value)
        // must say so and keep the saved state.
        bool declared = !same && text.find("not replayed") != std::string::npos;
        if ((!same && !declared) || first_line_with(saved, "share") != first_line_with(blob, "share")) {
            all = false;
        }
        // The same blob where the set is missing: runs dry, keeps the state.
        setenv("LEXICON224_ROMPATH", state_dir.c_str(), 1);
        finished = restore(blob, restored, saved, status, text);
        bool kept = first_line_with(saved, "share") == first_line_with(blob, "share") &&
                    first_line_with(saved, "rom") == first_line_with(blob, "rom");
        std::printf("state: with the set missing: status %d, state kept on save: %s\n", int(status),
                    verdict(kept, "yes", "NO"));
        if (!kept) {
            all = false;
        }
        setenv("LEXICON224_ROMPATH", argv[1], 1);
    }
    for (unsigned seed : round_trips) {
        if (!round_trip(seed)) {
            all = false;
        }
    }
    support.deleteRecursively();
    if (all) {
        std::printf("PASS offline == realtime\n");
        return 0;
    }
    std::printf("FAIL offline == realtime\n");
    return 1;
}
