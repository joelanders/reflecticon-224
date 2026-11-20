// Chunk independence for the original 224's front-panel operator
// (panel224_operator.hpp): chunks.cpp (for the LARC) with the
// 224 timeline script (panel224_script.hpp). The script is driven by
// Machine::render() in blocks of 1, 7, 128, 511, 4096 frames and random
// sizes (1-4096), and by the synchronous pump; every run must give the
// machine the same inputs at the same frames and produce the same output
// samples (the live runs render a fixed 30 s), and, with a JS recording
// given, equal it. Heap allocations made by operator code must be zero.
//
//   panel224_chunks ROM_SET_DIR CATALOG_DIRS FIRST SECOND [JS.events]
#define LEXPLUG_OPERATOR_PROBE(on) lexplug_probe_operator(on)
#include <cstddef>
#include <cstdlib>
#include <new>
static thread_local bool probe_in_operator = false;
static thread_local bool probe_in_render = false;
static size_t probe_operator_allocations = 0;
static size_t probe_render_allocations = 0;
inline void lexplug_probe_operator(bool on) {
    probe_in_operator = on;
}
void *operator new(std::size_t size) {
    if (probe_in_operator) {
        probe_operator_allocations++;
    }
    if (probe_in_render) {
        probe_render_allocations++;
    }
    if (size == 0) {
        size = 1;
    }
    void *p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}

#include "panel224_script.hpp"
#include "../../source/operator/rom_set.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;
using timeline::Lines;
using namespace lexplug::op::panel224_test;

namespace {

const Panel224Layout *layout_in_use = &LAYOUT_V4;

struct Run {
    std::string name;
    std::string trace;   // the inputs and marks, without the "end" line
    std::string end;
    uint64_t audio_hash = 0;
    bool ok = false;
    size_t operator_allocations = 0;
    size_t render_allocations = 0;
};

// Deterministic input noise by absolute frame, so it does not depend on the split.
float noise(int64_t frame, int channel) {
    uint64_t x = uint64_t(frame) * 2 + uint64_t(channel) + 0x9e3779b97f4a7c15ull;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    return float(int64_t(x >> 40) - (1 << 23)) / float(1 << 23) * 0.25f;
}

// block: frames per render() call; 0 = the synchronous pump; -1 = random sizes.
Run run(const roms::RomSet &set, const Script &base, int block, bool with_noise, const char *name) {
    Run result;
    result.name = name;
    auto engine = std::make_unique<Engine>(set.model);
    roms::load_into(*engine, set);
    auto machine = std::make_unique<Machine>(*engine);
    Panel224Operator op(*machine, *layout_in_use);
    Lines lines;
    machine->set_recorder(&lines);
    Script s = base;
    size_t operator_before = probe_operator_allocations;
    if (block == 0) {
        RootState state = machine->run_task([&] { return script(*machine, op, lines, s); });
        result.ok = !state.failed;
        if (state.failed) {
            std::printf("  %s failed: %s\n", name, state.error.text);
        }
    } else {
        std::vector<float> left(4096), right(4096), a(4096), b(4096), c(4096), d(4096);
        float *out[4] = {a.data(), b.data(), c.data(), d.data()};
        uint64_t hash = 1469598103934665603ull;
        uint64_t random = 12345;
        int root = machine->spawn([&] { return script(*machine, op, lines, s); });
        bool booted = false;
        size_t render_before = 0;
        // Render a fixed length, well past the script's end (~25 s), so that
        // every split renders the same frames.
        const int64_t total = 30 * 48000;
        while (machine->frame() < total) {
            int n = block;
            if (block < 0) {
                random = random * 6364136223846793005ull + 1442695040888963407ull;
                n = 1 + int((random >> 33) % 4096);
            }
            if (machine->frame() + n > total) {
                n = int(total - machine->frame());
            }
            for (int i = 0; i < n; i++) {
                float l = 0;
                float r = 0;
                if (with_noise) {
                    l = noise(machine->frame() + i, 0);
                    r = noise(machine->frame() + i, 1);
                }
                left[size_t(i)] = l;
                right[size_t(i)] = r;
            }
            if (!booted && machine->time() >= 16.0) {
                booted = true;
                render_before = probe_render_allocations;
            }
            probe_in_render = booted;
            bool ok = machine->render(left.data(), right.data(), out, n);
            probe_in_render = false;
            if (!ok) {
                std::printf("  %s: the machine stopped: %s\n", name, machine->failure());
                return result;
            }
            for (int i = 0; i < n; i++) {
                for (int ch = 0; ch < 4; ch++) {
                    uint32_t bits;
                    std::memcpy(&bits, &out[ch][i], 4);
                    hash = (hash ^ bits) * 1099511628211ull;
                }
            }
        }
        const RootState &state = machine->state(root);
        result.ok = machine->done(root) && !state.failed;
        if (state.failed) {
            std::printf("  %s failed: %s\n", name, state.error.text);
        }
        machine->release(root);
        result.audio_hash = hash;
        result.render_allocations = probe_render_allocations - render_before;
    }
    result.operator_allocations = probe_operator_allocations - operator_before;
    result.trace = lines.text(machine->frame());
    result.end = result.trace.substr(result.trace.rfind("end "));
    result.trace = result.trace.substr(0, result.trace.rfind("end "));
    if (const char *dump = std::getenv("CHUNKS_DUMP")) {
        std::string path = std::string(dump) + "/" + name;
        if (with_noise) {
            path += " noise";
        }
        std::ofstream out(path + ".events");
        out << result.trace << result.end;
    }
    return result;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: panel224_chunks ROM_SET_DIR CATALOG_DIRS FIRST SECOND [JS.events]\n");
        return 2;
    }
    roms::RomSet set = roms::read_set(argv[1], false);
    std::vector<std::string> dirs;
    {
        std::stringstream stream(argv[2]);
        std::string item;
        while (std::getline(stream, item, ':')) {
            dirs.push_back(item);
        }
    }
    Catalog catalog = find(dirs, set.hash);
    layout_in_use = &panel224_layout_named(catalog.layout.c_str());
    Script s;
    for (const auto &p : catalog.programs) {
        if (p.name == argv[3]) {
            s.first = &p;
        }
        if (p.name == argv[4]) {
            s.second = &p;
        }
    }
    if (s.first == nullptr || s.second == nullptr) {
        std::fprintf(stderr, "program not in the catalog\n");
        return 2;
    }
    std::string js;
    if (argc > 5) {
        std::ifstream file(argv[5]);
        std::stringstream buffer;
        buffer << file.rdbuf();
        js = buffer.str();
    }
    int failures = 0;
    // Silent input: every split against the pump (and the JS).
    std::vector<Run> silent;
    silent.push_back(run(set, s, 0, false, "pump"));
    const int blocks[] = {1, 7, 128, 511, 4096, -1};
    const char *names[] = {"block 1", "block 7", "block 128", "block 511", "block 4096", "random 1-4096"};
    for (int i = 0; i < 6; i++) {
        silent.push_back(run(set, s, blocks[i], false, names[i]));
    }
    std::printf("silent input:\n");
    for (const Run &r : silent) {
        bool same = r.trace == silent[0].trace;
        // The JS file ends with the pump's own end frame; the live runs render a fixed length.
        bool same_js = js.empty() || r.trace == js.substr(0, js.rfind("end "));
        if (!js.empty() && r.name == "pump") {
            same_js = same_js && r.end == js.substr(js.rfind("end "));
        }
        bool same_audio = r.name == "pump" || r.audio_hash == silent[1].audio_hash;
        std::string verdict = "trace != pump";
        if (same) {
            verdict = "trace == pump";
        }
        if (!js.empty()) {
            if (same_js) {
                verdict += ", == JS";
            } else {
                verdict += ", != JS";
            }
        }
        if (r.name != "pump") {
            if (same_audio) {
                verdict += ", audio == block 1";
            } else {
                verdict += ", audio DIFFERS from block 1";
            }
        }
        std::printf("  %-14s ok %d, %s; operator allocations %zu, render allocations after boot %zu\n", r.name.c_str(),
                    int(r.ok), verdict.c_str(), r.operator_allocations, r.render_allocations);
        if (!r.ok || !same || !same_js || !same_audio || r.operator_allocations != 0) {
            failures++;
        }
    }
    // Noise input: every split against block 1.
    std::vector<Run> noisy;
    for (int i = 0; i < 6; i++) {
        noisy.push_back(run(set, s, blocks[i], true, names[i]));
    }
    std::printf("noise input:\n");
    for (const Run &r : noisy) {
        bool same = r.trace == noisy[0].trace;
        bool same_audio = r.audio_hash == noisy[0].audio_hash;
        std::string verdict = "trace != block 1";
        if (same) {
            verdict = "trace == block 1";
        }
        if (same_audio) {
            verdict += ", audio == block 1";
        } else {
            verdict += ", audio DIFFERS from block 1";
        }
        std::printf("  %-14s ok %d, %s (%016llx); operator allocations %zu, render allocations after boot %zu\n",
                    r.name.c_str(), int(r.ok), verdict.c_str(), (unsigned long long)r.audio_hash,
                    r.operator_allocations, r.render_allocations);
        if (!r.ok || !same || !same_audio || r.operator_allocations != 0) {
            failures++;
        }
    }
    if (noisy[0].trace == silent[1].trace) {
        std::printf("noise vs silence: the same inputs at the same frames\n");
    } else {
        std::printf("noise vs silence: the inputs differ (the firmware reacted to the input)\n");
    }
    if (failures) {
        std::printf("FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
