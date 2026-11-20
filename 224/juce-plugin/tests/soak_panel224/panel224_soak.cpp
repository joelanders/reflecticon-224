// The C++ twin of ../../../web-demo/tests/soak.mjs for the original
// 224's front-panel operator (../../source/operator/panel224_operator.hpp),
// over the synchronous pump: random program, variation and pot actions
// against the real firmware, as the page would issue them, checking after
// every action what the firmware actually holds. The same as
// ../soak/soak.cpp in shape and output.
//
//   panel224_soak ROM_SET_DIR CATALOG_DIRS [--actions 300] [--seed 1] [--verbose] [--events FILE]
// CATALOG_DIRS: colon-separated directories searched for <hash>.json; the
// catalog's "layout" field picks the RAM layout (v4 or v3.2).
//
// Invariants, read from the firmware's RAM (not from its display):
//   - after a program load (or "variation 1", a reload), every parameter's
//     stored byte is the catalog's preset, or differs only as the firmware's
//     own load pickup explains (a resting DEPTH or PRE-DELAY pot takes over:
//     panel224.js unexplainedLoadDifferences, ported here);
//   - a pot move changes that parameter only;
//   - (counted, not failed: the moved parameter may hold another value than
//     the one sent.)
// The random sequence is the JS's (an LCG in doubles), so a seed gives the
// same actions as soak.mjs; --events writes the machine's inputs with a
// "FRAME mark aN KIND" line per action, for a comparison with
// ../soak/soak_record.mjs (v4.3, v4.4) or panel224_soak_record.mjs (any set).
//
// Also (not part of the JS soak): a RAM health line, to look for the v4.4
// "collapse" earlier HLE work saw (RAM -> 0xFF, garbage WCS after ~5-9
// slider events): after every action the count of 0xFF bytes in 0x3F00-0x3FFF
// and whether PROGRAM holds one program button; the first action where
// either looks wrong is reported ("HEALTH"), and the summary gives the
// worst seen. It never changes what the soak does.
#include "../../source/operator/panel224_operator.hpp"
#include "../../source/operator/rom_set.hpp"
#include "../operator_equiv/panel224_catalog.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;
using namespace lexplug::op::panel224_test;

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
    const Catalog *catalog = nullptr;
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
    int by_pot = 0;
    double machine_seconds = 0;
    // health
    int worst_ff = 0;
    int first_unhealthy = -1;
    int moves_before_unhealthy = 0;

    double random() {
        seed = std::fmod(seed * 1103515245.0 + 12345.0, 2147483648.0);
        return seed / 2147483648.0;
    }
    size_t pick(size_t length) {
        return size_t(std::floor(random() * double(length)));
    }
};

using Bytes = std::vector<std::vector<int>>;

Bytes snapshot(Panel224Operator &op, const Program &p) {
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

void fail_line(Soak &s, const std::string &text) {
    s.failures++;
    std::printf("FAIL #%d: %s\n", s.failures, text.c_str());
    std::fflush(stdout);
}

void say(Machine &m, Panel224Operator &op, Soak &s, const std::string &text) {
    if (s.verbose) {
        std::printf("  %.1f s: %s | %s\n", m.time(), text.c_str(), op.display().top);
    }
}

// JS parseInt(text, 10) === value, with parseInt(null) = NaN.
bool parses_to(const std::string *text, int value) {
    if (text == nullptr) {
        return false;
    }
    size_t i = 0;
    while (i < text->size() && text::is_space((*text)[i])) {
        i++;
    }
    int sign = 1;
    if (i < text->size() && ((*text)[i] == '-' || (*text)[i] == '+')) {
        if ((*text)[i] == '-') {
            sign = -1;
        }
        i++;
    }
    if (i >= text->size() || !text::is_digit((*text)[i])) {
        return false;
    }
    long n = 0;
    while (i < text->size() && text::is_digit((*text)[i])) {
        n = n * 10 + ((*text)[i] - '0');
        i++;
    }
    return sign * n == value;
}

// A run-length value table's text at a pot reading (panel224.js tableText).
const std::string *table_text(const Slider &slider, int raw) {
    const std::string *text = nullptr;
    for (const auto &entry : slider.table) {
        if (entry.first > raw) {
            break;
        }
        text = &entry.second;
    }
    return text;
}

// panel224.js unexplainedLoadDifferences.
std::vector<std::string> unexplained(Machine &m, Panel224Operator &op, const Program &program, const Bytes &bytes) {
    std::vector<std::string> differences;
    for (size_t i = 0; i < program.pages.size(); i++) {
        const Page &page = program.pages[i];
        for (size_t slot = 0; slot < page.sliders.size(); slot++) {
            if (bytes[i][slot] == program.raw[i][slot]) {
                continue;
            }
            bool by_pot = page.page == 1 && (slot == panel224::DEPTH_SLOT || slot == panel224::PREDELAY_SLOT) &&
                          parses_to(table_text(page.sliders[slot], m.peek(uint16_t(op.layout().POTS + slot))),
                                    bytes[i][slot]);
            if (!by_pot) {
                differences.push_back("page " + std::to_string(page.page) + " " + page.sliders[slot].name + " " +
                                      std::to_string(program.raw[i][slot]) + "->" + std::to_string(bytes[i][slot]));
            }
        }
    }
    return differences;
}

void loaded(Machine &m, Panel224Operator &op, Soak &s, int n, const Program &p, int v) {
    Bytes bytes = snapshot(op, p);
    if (bytes == p.raw) {
        return;
    }
    std::vector<std::string> differences = unexplained(m, op, p, bytes);
    if (differences.empty()) {
        s.by_pot++;
        return;
    }
    std::string joined;
    for (size_t i = 0; i < differences.size(); i++) {
        if (i) {
            joined += ", ";
        }
        joined += differences[i];
    }
    fail_line(s, std::to_string(n) + ": " + p.name + " V" + std::to_string(v) +
                     " bytes differ from the catalog: " + joined);
}

void health(Machine &m, Panel224Operator &op, Soak &s, int n) {
    int ff = 0;
    for (unsigned a = 0x3f00; a < 0x4000; a++) {
        if (m.peek(uint16_t(a)) == 0xff) {
            ff++;
        }
    }
    if (ff > s.worst_ff) {
        s.worst_ff = ff;
    }
    unsigned program = m.peek(op.layout().PROGRAM) & 0x3f;
    bool one_button = program != 0 && (program & (program - 1)) == 0;
    if (s.first_unhealthy < 0 && (ff >= 128 || !one_button)) {
        s.first_unhealthy = n;
        s.moves_before_unhealthy = s.move_count;
        std::printf("HEALTH at action %d (after %d moves): %d of 256 bytes at 3F00-3FFF are FF, PROGRAM %02x\n", n,
                    s.move_count, ff, m.peek(op.layout().PROGRAM));
        std::fflush(stdout);
    }
}

Task<void> soak(Machine &m, Panel224Operator &op, Soak &s) {
    co_await m.sleep(16);
    static constexpr const char *kinds[8] = {"program", "variation", "move", "move", "move", "move", "move", "move"};
    const unsigned range[2] = {0, 253};
    const Program *program = nullptr;
    double machine_start = m.time();
    for (int n = 0; n < s.actions; n++) {
        if (n > 0) {
            health(m, op, s, n - 1);
        }
        const char *kind = "program";
        if (program != nullptr) {
            kind = kinds[s.pick(8)];
        }
        if (s.events != nullptr) {
            s.events->lines.push_back(std::to_string(m.frame()) + " mark a" + std::to_string(n) + " " + kind);
        }
        if (std::strcmp(kind, "program") == 0) {
            const Program &p = s.catalog->programs[s.pick(s.catalog->programs.size())];
            if (!co_await op.loadProgram(p.identity)) {
                fail_line(s, std::to_string(n) + ": could not load " + p.name);
                continue;
            }
            say(m, op, s, std::to_string(n) + " program " + p.name);
            program = &p;
            s.program_count++;
            loaded(m, op, s, n, p, 1);
        } else if (std::strcmp(kind, "variation") == 0) {
            int v = program->variations[s.pick(program->variations.size())];
            if (!co_await op.loadVariation(v)) {
                fail_line(s, std::to_string(n) + ": " + program->name + " V" + std::to_string(v) + " did not load");
                continue;
            }
            say(m, op, s, std::to_string(n) + " variation " + std::to_string(v));
            s.variation_count++;
            loaded(m, op, s, n, *program, v);
        } else {
            std::vector<std::pair<size_t, size_t>> choices;
            for (size_t i = 0; i < program->pages.size(); i++) {
                for (size_t slot = 0; slot < program->pages[i].sliders.size(); slot++) {
                    const std::string &name = program->pages[i].sliders[slot].name;
                    if (name != "INACTIVE" && !name.empty()) {
                        choices.push_back({i, slot});
                    }
                }
            }
            auto [i, slot] = choices[s.pick(choices.size())];
            unsigned value = range[0] + unsigned(std::floor(s.random() * double(range[1] - range[0] + 1)));
            Bytes before = snapshot(op, *program);
            const std::string &name = program->pages[i].sliders[slot].name;
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
                std::to_string(n) + " move page " + std::to_string(i + 1) + " " + name + " -> " +
                    std::to_string(value) + ": " + echo);
            co_await m.sleep(1);
            Bytes after = snapshot(op, *program);
            if (after[i][slot] != int(value)) {
                s.adjusted++;
            }
            after[i][slot] = before[i][slot];
            std::string changed;
            for (size_t pi = 0; pi < after.size(); pi++) {
                for (size_t k = 0; k < after[pi].size(); k++) {
                    if (after[pi][k] != before[pi][k] && program->pages[pi].sliders[k].name != "INACTIVE") {
                        if (!changed.empty()) {
                            changed += ", ";
                        }
                        changed += "page " + std::to_string(pi + 1) + " " + program->pages[pi].sliders[k].name + " " +
                                   std::to_string(before[pi][k]) + "->" + std::to_string(after[pi][k]);
                    }
                }
            }
            if (!changed.empty()) {
                fail_line(s, std::to_string(n) + ": moving " + program->name + " page " + std::to_string(i + 1) + " " +
                                 name + " also changed " + changed);
            }
            if (!moved.value.present) {
                fail_line(s, std::to_string(n) + ": no echo for " + name);
            }
        }
    }
    health(m, op, s, s.actions);
    s.machine_seconds = m.time() - machine_start;
}

std::vector<std::string> split_dirs(const std::string &dirs) {
    std::vector<std::string> out;
    std::stringstream stream(dirs);
    std::string item;
    while (std::getline(stream, item, ':')) {
        out.push_back(item);
    }
    return out;
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
        std::fprintf(stderr, "usage: panel224_soak ROM_SET_DIR CATALOG_DIRS [--actions 300] [--seed 1] [--verbose] "
                             "[--events FILE]\n");
        return 2;
    }
    try {
        roms::RomSet set = roms::read_set(args[0], true);
        Catalog catalog = find(split_dirs(args[1]), set.hash);
        s.catalog = &catalog;
        auto engine = std::make_unique<Engine>(set.model);
        roms::load_into(*engine, set);
        auto machine = std::make_unique<Machine>(*engine);
        Panel224Operator op(*machine, panel224_layout_named(catalog.layout.c_str()));
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
        std::string health = "healthy throughout";
        if (s.first_unhealthy >= 0) {
            health = "UNHEALTHY from action " + std::to_string(s.first_unhealthy) + " (after " +
                     std::to_string(s.moves_before_unhealthy) + " moves)";
        }
        std::printf("RAM health (layout %s): most FF bytes at 3F00-3FFF %d of 256; %s\n", op.layout().name,
                    s.worst_ff, health.c_str());
        std::string by_pot;
        if (s.by_pot) {
            by_pot = ", " + std::to_string(s.by_pot) + " loads where a resting pot took over";
        }
        std::printf("%d actions (%d programs, %d variations, %d moves, %d held another value than sent%s): %d failures; "
                    "%.0f s of machine time (%.2f s per action average), %.0f s wall; frame pool high water %zu of "
                    "%zu, largest frame %zu bytes; resumed while busy %u\n",
                    s.actions, s.program_count, s.variation_count, s.move_count, s.adjusted, by_pot.c_str(),
                    s.failures, s.machine_seconds, s.machine_seconds / s.actions, wall, machine->pool().high_water(),
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
