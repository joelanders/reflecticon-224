// The C++ twin of ../../../web-demo/tests/soak.mjs, for the 224XL
// LARC operator (../../source/operator/larc_operator.hpp) over the
// synchronous pump: random program, variation and slider actions against the
// real firmware, as the page would issue them, checking after every action
// what the firmware actually holds.
//
//   soak ROM_SET_DIR CATALOG_DIR [--actions 300] [--seed 1] [--verbose] [--events FILE]
//
// Invariants, read from the firmware's RAM (not from its display):
//   - after a program or variation load, every parameter's stored byte is the
//     catalog's preset for it;
//   - a slider move changes that parameter only: every other parameter of
//     the program, on every page, keeps its byte;
//   - (counted, not failed: the moved parameter may hold another value than
//     the one sent; the firmware enforces its own limits.)
// Snapshots are taken 1 s after a move (a SIZE move rebuilds the program for
// ~0.4 s, using parts of RAM as scratch meanwhile).
//
// The random sequence is the JS's own (an LCG in doubles, with the JS's
// rounding), so the same seed gives the same actions as soak.mjs, and
// --events writes the machine's inputs in the format of
// ../../bench/record_timeline.mjs plus a "FRAME mark aN KIND" line per action,
// for a frame-for-frame comparison with soak_record.mjs.
#include "../../source/operator/larc_operator.hpp"
#include "../../source/operator/mini_catalog.hpp"
#include "../../source/operator/rom_set.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;

namespace {

const char *kind_name(Input kind) {
    switch (kind) {
    case Input::key:
        return "key";
    case Input::fader:
        return "fader";
    case Input::poke:
        return "poke";
    case Input::button:
        return "button";
    case Input::pot:
        return "pot";
    }
    return "?";
}

class Lines : public Recorder {
public:
    std::vector<std::string> lines;
    void input(int64_t frame, Input kind, unsigned a, unsigned b) override {
        lines.push_back(std::to_string(frame) + " " + kind_name(kind) + " " + std::to_string(a) + " " +
                        std::to_string(b));
    }
};

struct Soak {
    const mini::Catalog *catalog = nullptr;
    int actions = 300;
    double seed = 1;
    bool verbose = false;
    Lines *events = nullptr;
    // results
    int failures = 0;
    int program_count = 0;
    int variation_count = 0;
    int move_count = 0;
    int adjusted = 0;
    double machine_seconds = 0;

    double random() {
        seed = std::fmod(seed * 1103515245.0 + 12345.0, 2147483648.0);
        return seed / 2147483648.0;
    }
    size_t pick(size_t length) {
        return size_t(std::floor(random() * double(length)));
    }
};

using Bytes = std::vector<std::vector<int>>;

Bytes snapshot(LarcOperator &op, const mini::Program &p) {
    Bytes bytes;
    for (const auto &page : p.pages) {
        std::vector<int> row;
        for (size_t slot = 0; slot < page.sliders.size(); slot++) {
            row.push_back(op.stored(page.column, unsigned(slot)));
        }
        bytes.push_back(row);
    }
    return bytes;
}

std::string text_of(const Bytes &bytes) {
    std::string s = "[";
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i) {
            s += ",";
        }
        s += "[";
        for (size_t k = 0; k < bytes[i].size(); k++) {
            if (k) {
                s += ",";
            }
            s += std::to_string(bytes[i][k]);
        }
        s += "]";
    }
    return s + "]";
}

void fail_line(Soak &s, const std::string &text) {
    s.failures++;
    std::printf("FAIL #%d: %s\n", s.failures, text.c_str());
    std::fflush(stdout);
}

void say(Machine &m, LarcOperator &op, Soak &s, const std::string &text) {
    if (s.verbose) {
        char top[25];
        text::trimmed(op.display().top, 0, 24, top, sizeof top);
        std::printf("  %.1f s: %s | %s\n", m.time(), text.c_str(), top);
    }
}

void loaded(LarcOperator &op, Soak &s, int n, const mini::Program &p, int v) {
    Bytes bytes = snapshot(op, p);
    auto found = p.raw.find(v);
    if (found != p.raw.end() && found->second == bytes) {
        return;
    }
    fail_line(s, std::to_string(n) + ": " + p.name + " V" + std::to_string(v) +
                     " bytes differ from the catalog: " + text_of(bytes));
}

Task<void> soak(Machine &m, LarcOperator &op, Soak &s) {
    co_await m.sleep(16);
    if (!m.engine().larc_connected()) {
        co_await fail("no LARC after boot: this soak drives the 224XL operator only");
    }
    static constexpr const char *kinds[8] = {"program", "variation", "move", "move", "move", "move", "move", "move"};
    const unsigned range[2] = {2, 254};
    const mini::Program *program = nullptr;
    double machine_start = m.time();
    for (int n = 0; n < s.actions; n++) {
        const char *kind = "program";
        if (program != nullptr) {
            kind = kinds[s.pick(8)];
        }
        if (s.events != nullptr) {
            s.events->lines.push_back(std::to_string(m.frame()) + " mark a" + std::to_string(n) + " " + kind);
        }
        if (std::strcmp(kind, "program") == 0) {
            const mini::Program &p = s.catalog->programs[s.pick(s.catalog->programs.size())];
            if (!co_await op.selectProgram(p.bank, p.program)) {
                fail_line(s, std::to_string(n) + ": could not load " + p.name);
                continue;
            }
            say(m, op, s, std::to_string(n) + " program " + p.name);
            program = &p;
            s.program_count++;
            loaded(op, s, n, p, 1);
        } else if (std::strcmp(kind, "variation") == 0) {
            int v = program->variations[s.pick(program->variations.size())];
            if (!co_await op.loadVariation(v)) {
                fail_line(s, std::to_string(n) + ": " + program->name + " V" + std::to_string(v) + " did not load");
                continue;
            }
            say(m, op, s, std::to_string(n) + " variation " + std::to_string(v));
            s.variation_count++;
            loaded(op, s, n, *program, v);
        } else {
            std::vector<std::pair<size_t, size_t>> choices;
            for (size_t i = 0; i < program->pages.size(); i++) {
                for (size_t slot = 0; slot < program->pages[i].sliders.size(); slot++) {
                    const std::string &name = program->pages[i].sliders[slot];
                    if (name != "INACTIVE" && !name.empty()) {
                        choices.push_back({i, slot});
                    }
                }
            }
            auto [i, slot] = choices[s.pick(choices.size())];
            unsigned value = range[0] + unsigned(std::floor(s.random() * double(range[1] - range[0] + 1)));
            Bytes before = snapshot(op, *program);
            Result<Echo> moved = co_await caught(op.moveSlider(program->pages[i].page, unsigned(slot), value));
            if (!moved.ok) {
                fail_line(s, std::to_string(n) + ": " + program->name + " page " + std::to_string(i + 1) + " slot " +
                                 std::to_string(slot + 1) + " -> " + std::to_string(value) + ": " +
                                 moved.error.text);
                continue;
            }
            s.move_count++;
            std::string echo = "null";
            if (moved.value.present) {
                echo = moved.value.value;
            }
            say(m, op, s,
                std::to_string(n) + " move page " + std::to_string(i + 1) + " " + program->pages[i].sliders[slot] +
                    " -> " + std::to_string(value) + ": " + echo);
            co_await m.sleep(1);
            Bytes after = snapshot(op, *program);
            if (after[i][slot] != int(value)) {
                s.adjusted++;
            }
            after[i][slot] = before[i][slot];
            std::string changed;
            // INACTIVE slots are not controls; on SIZE pages they hold the
            // firmware's own storage for the neighbouring parameter.
            for (size_t pi = 0; pi < after.size(); pi++) {
                for (size_t k = 0; k < after[pi].size(); k++) {
                    if (after[pi][k] != before[pi][k] && program->pages[pi].sliders[k] != "INACTIVE") {
                        if (!changed.empty()) {
                            changed += ", ";
                        }
                        changed += "page " + std::to_string(pi + 1) + " " + program->pages[pi].sliders[k] + " " +
                                   std::to_string(before[pi][k]) + "->" + std::to_string(after[pi][k]);
                    }
                }
            }
            if (!changed.empty()) {
                fail_line(s, std::to_string(n) + ": moving " + program->name + " page " + std::to_string(i + 1) + " " +
                                 program->pages[i].sliders[slot] + " also changed " + changed);
            }
            if (!moved.value.present) {
                fail_line(s, std::to_string(n) + ": no echo for " + program->pages[i].sliders[slot]);
            }
        }
    }
    s.machine_seconds = m.time() - machine_start;
}

}  // namespace

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    auto option = [&](const std::string &name, const std::string &fallback) {
        for (size_t i = 0; i + 1 < args.size(); i++) {
            if (args[i] == name) {
                std::string value = args[i + 1];
                args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
                return value;
            }
        }
        return fallback;
    };
    Soak s;
    s.actions = std::stoi(option("--actions", "300"));
    s.seed = std::stod(option("--seed", "1"));
    std::string events_path = option("--events", "");
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--verbose") {
            s.verbose = true;
            args.erase(args.begin() + long(i));
            break;
        }
    }
    if (args.size() < 2) {
        std::fprintf(stderr,
                     "usage: soak ROM_SET_DIR CATALOG_DIR [--actions 300] [--seed 1] [--verbose] [--events FILE]\n");
        return 2;
    }
    try {
        roms::RomSet set = roms::read_set(args[0], true);
        mini::Catalog catalog = mini::load_catalog(args[1] + "/" + set.hash + ".json");
        s.catalog = &catalog;
        auto engine = std::make_unique<Engine>(set.model);
        roms::load_into(*engine, set);
        auto machine = std::make_unique<Machine>(*engine);
        LarcOperator op(*machine);
        Lines lines;
        if (!events_path.empty()) {
            machine->set_recorder(&lines);
            s.events = &lines;
        }
        auto started = std::chrono::steady_clock::now();
        RootState result = machine->run_task([&] { return soak(*machine, op, s); });
        double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (result.failed) {
            fail_line(s, std::string("the soak stopped: ") + result.error.text);
        }
        if (!events_path.empty()) {
            lines.lines.push_back("end " + std::to_string(machine->frame()));
            std::ofstream out(events_path);
            for (const auto &line : lines.lines) {
                out << line << "\n";
            }
        }
        std::printf("%d actions (%d programs, %d variations, %d moves, %d held another value than sent): %d failures; "
                    "%.0f s of machine time (%.2f s per action average), %.0f s wall; frame pool high water %zu of "
                    "%zu, largest frame %zu bytes; resumed while busy %u\n",
                    s.actions, s.program_count, s.variation_count, s.move_count, s.adjusted, s.failures,
                    s.machine_seconds, s.machine_seconds / s.actions, wall, machine->pool().high_water(),
                    FramePool::block_count, machine->pool().max_request(), machine->resumed_while_busy());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    if (s.failures) {
        return 1;
    }
    return 0;
}
