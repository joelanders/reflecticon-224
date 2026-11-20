// M1's RT-allocation gate:
// every operator new and delete made inside processBlock, from the block
// that takes the booted machine onward, is counted and its call stack kept.
//
//   rt_alloc ROM_DIR TIMELINE.events [--rate R] [--tail S] [--stacks N]
//     --rate R    host rate (default 48000; 44100 exercises the resampler)
//     --tail S    seconds after the timeline's end (default 20)
//     --stacks N  distinct call stacks to print, most frequent first (default 12)
//
// Host blocks: 128 samples, with an irregular block (1..2000, and 20000:
// over the 16384 capacity) every 64 blocks. The timeline's controls (program
// load, slider sweep, second load) give the operator phase; the tail is the
// steady state. Allocations are also binned per second of machine time, to
// tell growth that stops after warm-up from allocation that never stops.
// Exit status 0 only if nothing allocated or freed.
#include "PluginProcessor.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <fstream>
#include <map>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

thread_local bool watching = false;

constexpr int max_depth = 24;
constexpr int key_depth = 10;           // frames that tell one call site from another
constexpr int max_sites = 4096;
constexpr int max_blocks = 1 << 20;

// One call site: its first stack (kept whole for printing) and its counts.
// A static open-addressing table: recording never allocates.
struct Site {
    bool used;
    uint64_t hash;
    int depth;
    void *frames[max_depth];
    uint64_t news, deletes, bytes, first_block, last_block;
};

Site sites[max_sites];
std::atomic<uint64_t> news{0}, deletes{0};
uint64_t news_in_block[max_blocks], deletes_in_block[max_blocks];
uint64_t current_block = 0;
uint64_t lost_sites = 0;

void note(bool is_new, size_t size) {
    if (!watching) {
        return;
    }
    watching = false;   // backtrace() must not recurse into us
    if (is_new) {
        news++;
    } else {
        deletes++;
    }
    if (current_block < max_blocks) {
        if (is_new) {
            news_in_block[current_block]++;
        } else {
            deletes_in_block[current_block]++;
        }
    }
    void *frames[max_depth];
    int depth = backtrace(frames, max_depth);
    uint64_t hash = 1469598103934665603ull;
    for (int i = 2; i < depth && i < key_depth; i++) {   // skip note() and its caller's allocator frame
        hash = (hash ^ uint64_t(uintptr_t(frames[i]))) * 1099511628211ull;
    }
    hash = (hash ^ uint64_t(is_new)) * 1099511628211ull;
    Site *site = nullptr;
    for (int probe = 0; probe < max_sites; probe++) {
        Site &candidate = sites[(hash + uint64_t(probe)) % max_sites];
        if (!candidate.used) {
            candidate.used = true;
            candidate.hash = hash;
            candidate.depth = depth;
            std::memcpy(candidate.frames, frames, sizeof frames);
            candidate.first_block = current_block;
            site = &candidate;
            break;
        }
        if (candidate.hash == hash) {
            site = &candidate;
            break;
        }
    }
    if (site == nullptr) {
        lost_sites++;
    } else {
        if (is_new) {
            site->news++;
            site->bytes += size;
        } else {
            site->deletes++;
        }
        site->last_block = current_block;
    }
    watching = true;
}

void *allocate(size_t size) {
    note(true, size);
    if (size == 0) {
        size = 1;
    }
    void *p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void *allocate_aligned(size_t size, size_t alignment) {
    note(true, size);
    void *p = nullptr;
    if (posix_memalign(&p, std::max(alignment, sizeof(void *)), std::max<size_t>(size, 1)) != 0) {
        throw std::bad_alloc();
    }
    return p;
}

void release(void *p) {
    if (p != nullptr) {
        note(false, 0);
    }
    std::free(p);
}

}  // namespace

void *operator new(size_t size) {
    return allocate(size);
}
void *operator new[](size_t size) {
    return allocate(size);
}
void *operator new(size_t size, const std::nothrow_t &) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void *operator new(size_t size, std::align_val_t a) {
    return allocate_aligned(size, size_t(a));
}
void *operator new[](size_t size, std::align_val_t a) {
    return allocate_aligned(size, size_t(a));
}
void operator delete(void *p) noexcept {
    release(p);
}
void operator delete[](void *p) noexcept {
    release(p);
}
void operator delete(void *p, size_t) noexcept {
    release(p);
}
void operator delete[](void *p, size_t) noexcept {
    release(p);
}
void operator delete(void *p, std::align_val_t) noexcept {
    release(p);
}
void operator delete[](void *p, std::align_val_t) noexcept {
    release(p);
}
void operator delete(void *p, size_t, std::align_val_t) noexcept {
    release(p);
}
void operator delete[](void *p, size_t, std::align_val_t) noexcept {
    release(p);
}

namespace {

std::vector<lexplug::ControlEvent> read_controls(const std::string &path, uint64_t &end) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    std::vector<lexplug::ControlEvent> events;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string first, kind;
        if (!(s >> first)) {
            continue;
        }
        if (first == "end") {
            s >> end;
            continue;
        }
        s >> kind;
        lexplug::ControlEvent e{std::stoull(first), lexplug::ControlKind::Key};
        if (kind == "mark") {
            continue;
        } else if (kind == "key") {
            e.kind = lexplug::ControlKind::Key;
        } else if (kind == "fader") {
            e.kind = lexplug::ControlKind::Fader;
        } else if (kind == "poke") {
            e.kind = lexplug::ControlKind::Poke;
        } else if (kind == "button") {
            e.kind = lexplug::ControlKind::Button;
        } else if (kind == "pot") {
            e.kind = lexplug::ControlKind::Pot;
        } else {
            throw std::runtime_error("unknown event kind " + kind);
        }
        s >> e.a >> e.b;
        events.push_back(e);
    }
    return events;
}

// Symbolize a stack with atos (file:line, inlined frames included), from
// the allocating call up to Session::render. Linux has no atos, so
// backtrace_symbols() gives the module and offsets instead.
std::string symbolize(void *const *frames, int depth) {
#if !defined(__APPLE__)
    std::string text;
    char **symbols = backtrace_symbols(frames, depth);
    if (symbols == nullptr) {
        return "      (backtrace_symbols failed)\n";
    }
    for (int i = 2; i < depth; i++) {
        text += "      ";
        text += symbols[i];
        text += "\n";
        if (std::strstr(symbols[i], "lexplug::Session::render") != nullptr) {
            break;
        }
    }
    std::free(symbols);
    return text;
#else
    char command[8192];
    const struct mach_header *header = _dyld_get_image_header(0);
    int used = std::snprintf(command, sizeof command, "atos -i -o '%s' -l %p", _dyld_get_image_name(0),
                             (const void *)header);
    for (int i = 2; i < depth && used < int(sizeof command) - 32; i++) {
        used += std::snprintf(command + used, sizeof command - size_t(used), " %p", frames[i]);
    }
    std::string text;
    FILE *p = popen(command, "r");
    if (p == nullptr) {
        return "(atos failed)\n";
    }
    char line[1024];
    // Up to the plugin's own render call; the rest is the same for every site.
    bool done = false;
    while (std::fgets(line, sizeof line, p) != nullptr) {
        if (done || line[0] == '\n') {
            continue;
        }
        text += "      ";
        text += line;
        if (std::strstr(line, "lexplug::Session::render") != nullptr) {
            done = true;
        }
    }
    pclose(p);
    return text;
#endif
}

}  // namespace

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::fprintf(stderr, "usage: rt_alloc ROM_DIR TIMELINE.events [--rate R] [--tail S] [--stacks N]\n");
        return 2;
    }
    std::string rom_dir = argv[1], timeline = argv[2];
    int rate = 48000, show = 12;
    double tail = 20;
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--rate") {
            rate = std::stoi(v);
        } else if (k == "--tail") {
            tail = std::stod(v);
        } else if (k == "--stacks") {
            show = std::stoi(v);
        } else {
            throw std::runtime_error("unknown option " + k);
        }
    }
    juce::ScopedJuceInitialiser_GUI juce_init;
    setenv("LEXICON224_ROMPATH", rom_dir.c_str(), 1);
    uint64_t end = 0;
    auto controls = read_controls(timeline, end);
    if (end == 0) {
        throw std::runtime_error(timeline + ": no end frame");
    }

    PluginProcessor processor;
    processor.schedule_controls(controls);
    processor.setPlayConfigDetails(2, 2, double(rate), 128);
    processor.prepareToPlay(double(rate), 128);
    while (!processor.machine_offered()) {
        if (processor.status() == PluginProcessor::Status::NoRoms ||
            processor.status() == PluginProcessor::Status::Stopped) {
            throw std::runtime_error("the plugin did not boot");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    uint64_t stop = end + uint64_t(tail * 48000.0);
    juce::AudioBuffer<float> buffer(2, 20000);
    juce::MidiBuffer midi;
    uint32_t seed = 1;
    std::vector<uint64_t> block_frame;   // the machine frame at each block's end
    block_frame.reserve(1 << 20);
    uint64_t operator_news = 0, steady_news = 0;
    while (processor.machine_frame() < stop) {
        int n = 128;
        if (current_block % 64 == 63) {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + int((seed >> 8) % 2000);
            if (current_block % 1024 == 1023) {
                n = 20000;
            }
        }
        buffer.setSize(2, n, false, false, true);
        for (int i = 0; i < n; i++) {
            seed = seed * 1664525u + 1013904223u;
            float x = (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f) * 0.25f;
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, -x);
        }
        uint64_t frame = processor.machine_frame();
        uint64_t before = news.load();
        watching = true;
        processor.processBlock(buffer, midi);
        watching = false;
        block_frame.push_back(processor.machine_frame());   // the machine frame at the block's end
        if (frame < end) {
            operator_news += news.load() - before;
        } else {
            steady_news += news.load() - before;
        }
        current_block++;
        if (processor.status() != PluginProcessor::Status::Running) {
            throw std::runtime_error("the machine stopped");
        }
    }

    std::printf("rt_alloc: %s at %d Hz, %llu blocks, machine frames %llu..%llu (timeline end %llu)\n",
                timeline.c_str(), rate, (unsigned long long)current_block, (unsigned long long)PluginProcessor::boot_frames,
                (unsigned long long)processor.machine_frame(), (unsigned long long)end);
    std::printf("  operator new: %llu (operator phase %llu, steady %llu); operator delete: %llu\n",
                (unsigned long long)news.load(), (unsigned long long)operator_news, (unsigned long long)steady_news,
                (unsigned long long)deletes.load());

    // Per second of machine time.
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> per_second;
    for (uint64_t b = 0; b < current_block && b < max_blocks; b++) {
        uint64_t second = block_frame[b] / 48000;
        per_second[second].first += news_in_block[b];
        per_second[second].second += deletes_in_block[b];
    }
    std::printf("  per machine second (new/delete):");
    for (const auto &[second, c] : per_second) {
        std::printf(" %llus:%llu/%llu", (unsigned long long)second, (unsigned long long)c.first,
                    (unsigned long long)c.second);
    }
    std::printf("\n");

    // Call sites, most frequent first.
    std::vector<const Site *> order;
    for (const Site &site : sites) {
        if (site.used) {
            order.push_back(&site);
        }
    }
    std::sort(order.begin(), order.end(),
              [](const Site *x, const Site *y) { return x->news + x->deletes > y->news + y->deletes; });
    std::printf("  %zu distinct call sites (%llu not recorded: table full)\n", order.size(),
                (unsigned long long)lost_sites);
    for (int k = 0; k < int(order.size()) && k < show; k++) {
        const Site &site = *order[size_t(k)];
        std::printf("  site %d: %llu new (%llu bytes), %llu delete; machine frames %llu..%llu\n", k + 1,
                    (unsigned long long)site.news, (unsigned long long)site.bytes, (unsigned long long)site.deletes,
                    (unsigned long long)block_frame[site.first_block], (unsigned long long)block_frame[site.last_block]);
        std::printf("%s", symbolize(site.frames, site.depth).c_str());
    }
    if (news.load() + deletes.load() == 0) {
        std::printf("rt_alloc: PASS (no allocation or free in processBlock)\n");
        return 0;
    }
    std::printf("rt_alloc: FAIL\n");
    return 1;
} catch (const std::exception &e) {
    std::fprintf(stderr, "rt_alloc: %s\n", e.what());
    return 1;
}
