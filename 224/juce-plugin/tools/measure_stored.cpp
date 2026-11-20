// Measure, on a booted machine, what the firmware STORES for each control
// value: stored(value) for every named slider of a program, with the control
// driven through the C++ operator (the plugin's), the slider taken over first
// (its pickup set, as moveSlider does), and the stored byte (the one a share
// payload records: the parameter record, or the 224's parameter cell) read
// from RAM once it has been still for 0.06 s.
//
//   measure_stored ROM_SET_DIR CATALOG_JSON [--program KEY]... [--variation V|all]
//                  [--down | --down-only] [--slots page.slot,...] [--step N] [--first SLOT=READING]...
//                  [--settle S] [--still S]
//
// Remotes and control values:
//   panel224  the 224 (v4 and v3.2 layouts): pot readings 0..253
//   panel     the 224X: pot readings 0..253
//   larc      the 224XL: LARC fader values 2..254
// KEY is app.js keyOf: "x<identity hex>" (panels) or "B.P" (LARC).
// --variation  the variation loaded before the sweeps (default 1; "all": each
//              of the program's variations in turn).
// --down       also sweep back down (a stored value that depends on the
//              direction would show there); --down-only: only that.
// --first      (224) move a page-1 pot (0-based SLOT) to READING after each
//              load, before the sweeps (does one parameter's stored value
//              depend on another's?).
// --settle S   (LARC) machine seconds after each fader message (default 0.07)
// --still S    the stored byte must hold still S seconds (default 0.06). A
//              sweep read too early lags behind the control, which shows as
//              up and down sweeps that disagree (gen_stored_tables refuses).
//
// Prints one line per (program, variation, page, slot, direction):
//   <key> <variation> <page> <slot> <up|down> <name> <stored for the lowest value> ... <highest>
// (names with spaces as '_'; with --step, only the values visited, as
// value:stored). tools/gen_stored_tables.py turns these lines into
// catalogs-extra/stored/<hash>.stored.json. No ROM bytes are written
// anywhere; only stored bytes (measured behaviour).
//
// Build (from juce-plugin):
//   clang++ -std=c++20 -O2 -ffp-contract=off -I ../analog \
//       tools/measure_stored.cpp -o build-i/measure_stored
#include "../source/operator/larc_operator.hpp"
#include "../source/operator/mini_catalog.hpp"
#include "../source/operator/panel224_operator.hpp"
#include "../source/operator/panel_operator.hpp"
#include "../source/operator/rom_set.hpp"
#include "../tests/operator_equiv/panel224_catalog.hpp"
#include "../tests/soak_panel/panel_catalog.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;

namespace {

struct Target {
    int page = 0;
    unsigned slot = 0;
    std::string name;
};

struct Options {
    std::vector<std::string> programs;                  // keys; empty: all
    std::vector<std::pair<int, unsigned>> only;         // page.slot; empty: every named slider
    int variation = 1;                                  // 0: all
    bool down = false;
    bool downOnly = false;   // --down-only: only the downward sweeps
    unsigned step = 1;
    std::vector<std::pair<unsigned, unsigned>> first;   // --first SLOT=READING (224)
    double settle = 0.07;   // LARC: machine time after a fader message before reading
    double still = 0.06;    // the stored byte must hold this long
};

Options g_options;

bool wanted(const Options &o, int page, unsigned slot) {
    if (o.only.empty()) {
        return true;
    }
    for (const auto &p : o.only) {
        if (p.first == page && p.second == slot) {
            return true;
        }
    }
    return false;
}

bool take_program(const Options &o, const std::string &key) {
    if (o.programs.empty()) {
        return true;
    }
    for (const std::string &k : o.programs) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

std::string panel_key(unsigned identity) {
    char text[16];
    std::snprintf(text, sizeof text, "x%x", identity);
    return text;
}

std::string underscored(std::string name) {
    for (char &c : name) {
        if (c == ' ') {
            c = '_';
        }
    }
    return name;
}

// Wait until `cell` has held still for --still seconds (at most 0.8 s).
Task<uint8_t> stillValue(Machine &m, uint16_t cell) {
    double quiet = g_options.still;
    uint8_t last = m.peek(cell);
    double since = m.time();
    co_await m.wait_for(
        [&]() {
            uint8_t now = m.peek(cell);
            if (now != last) {
                last = now;
                since = m.time();
                return false;
            }
            return m.time() - since >= quiet;
        },
        0.8);
    co_return m.peek(cell);
}

void print_line(const std::string &key, int variation, const Target &t, bool down,
                const std::vector<std::pair<unsigned, int>> &values, unsigned step) {
    const char *direction = "up";
    if (down) {
        direction = "down";
    }
    std::printf("%s %d %d %u %s %s", key.c_str(), variation, t.page, t.slot, direction, t.name.c_str());
    for (const auto &v : values) {
        if (step == 1) {
            std::printf(" %d", v.second);
        } else {
            std::printf(" %u:%d", v.first, v.second);
        }
    }
    std::printf("\n");
    std::fflush(stdout);
}

std::vector<unsigned> values_between(unsigned low, unsigned high, unsigned step, bool down) {
    std::vector<unsigned> out;
    for (unsigned r = low; r <= high; r += step) {
        out.push_back(r);
    }
    if (out.back() != high) {
        out.push_back(high);
    }
    if (down) {
        std::vector<unsigned> reversed(out.rbegin(), out.rend());
        return reversed;
    }
    return out;
}

// ---------------- the 224 ----------------

Task<void> sweep224(Machine &m, Panel224Operator &op, const std::string &key, const Target &t, bool down,
                    unsigned step) {
    const Panel224Parameter *p = op.parameter(t.page, t.slot);
    co_await op.takeOver(t.page, t.slot);
    if (t.page == 2 && !co_await op.hold(1, panel224::SHIFT)) {
        co_await fail("SHIFT was not seen");
    }
    // Start the up sweep from above 0, so the pot's first reading is a real
    // move (a pot already at 0 would not report it).
    if (!down && !co_await op.setPot(t.slot, 8)) {
        co_await fail("pot %u did not take 8", t.slot + 1);
    }
    std::vector<std::pair<unsigned, int>> values;
    for (unsigned r : values_between(0, 253, step, down)) {
        if (!co_await op.setPot(t.slot, r)) {
            co_await fail("pot %u did not take %u", t.slot + 1, r);
        }
        uint8_t b = co_await stillValue(m, p->cell);
        values.push_back({r, int(b)});
    }
    if (t.page == 2) {
        co_await op.hold(1, 0);
    }
    co_await op.cancelShift();
    print_line(key, 1, t, down, values, step);
}

Task<void> run224(Machine &m, Panel224Operator &op, const panel224_test::Catalog &catalog, const Options &o) {
    co_await m.sleep(16);
    for (const panel224_test::Program &program : catalog.programs) {
        std::string key = panel_key(program.identity);
        if (!take_program(o, key)) {
            continue;
        }
        if (!co_await op.loadProgram(program.identity)) {
            co_await fail("could not load program %u", program.identity);
        }
        for (const auto &f : o.first) {
            co_await op.moveSlider(1, f.first, f.second);
        }
        for (int page = 1; page <= op.pageCount(); page++) {
            for (unsigned slot = 0; slot < 6; slot++) {
                if (op.parameter(page, slot) == nullptr || !wanted(o, page, slot)) {
                    continue;
                }
                Target t{page, slot, underscored(op.sliderName(page, slot))};
                if (!o.downOnly) {
                    co_await sweep224(m, op, key, t, false, o.step);
                }
                if (o.down) {
                    co_await sweep224(m, op, key, t, true, o.step);
                }
            }
        }
    }
}

// ---------------- the 224X ----------------

Task<void> sweepX(Machine &m, PanelOperator &op, const std::string &key, int variation, const Target &t, bool down,
                  unsigned step) {
    // moveSlider takes the page and the pickup; then the pot alone.
    co_await op.moveSlider(t.page, t.slot, 0);
    uint16_t cell = uint16_t(panel::RECORD + 1 + 6 * m.peek(panel::COLUMN) + t.slot);
    if (!down && !co_await op.setPot(t.slot, 8)) {
        co_await fail("pot %u did not take 8", t.slot + 1);
    }
    std::vector<std::pair<unsigned, int>> values;
    for (unsigned r : values_between(0, 253, step, down)) {
        if (!co_await op.setPot(t.slot, r)) {
            co_await fail("pot %u did not take %u", t.slot + 1, r);
        }
        uint8_t b = co_await stillValue(m, cell);
        values.push_back({r, int(b)});
    }
    print_line(key, variation, t, down, values, step);
}

Task<void> runX(Machine &m, PanelOperator &op, const panel_catalog::Catalog &catalog, const Options &o) {
    co_await m.sleep(16);
    for (const panel_catalog::Program &program : catalog.programs) {
        std::string key = panel_key(program.identity);
        if (!take_program(o, key)) {
            continue;
        }
        std::vector<int> variations = {o.variation};
        if (o.variation == 0) {
            variations = program.variations;
        }
        for (int v : variations) {
            if (!co_await op.loadProgram(program.identity)) {
                co_await fail("could not load program %u", program.identity);
            }
            if (v != 1 && !co_await op.loadVariation(v)) {
                co_await fail("could not load variation %d of program %u", v, program.identity);
            }
            for (const mini::Page &page : program.pages) {
                for (unsigned slot = 0; slot < page.sliders.size(); slot++) {
                    const std::string &name = page.sliders[slot];
                    if (name.empty() || name == "INACTIVE" || !wanted(o, page.page, slot)) {
                        continue;
                    }
                    Target t{page.page, slot, underscored(name)};
                    if (!o.downOnly) {
                        co_await sweepX(m, op, key, v, t, false, o.step);
                    }
                    if (o.down) {
                        co_await sweepX(m, op, key, v, t, true, o.step);
                    }
                }
            }
        }
    }
}

// ---------------- the 224XL (LARC) ----------------

Task<void> sweepL(Machine &m, LarcOperator &op, const std::string &key, int variation, const Target &t, bool down,
                  unsigned step) {
    // moveSlider goes to the page and takes the slot over; then the fader alone.
    unsigned start = 254;
    if (down) {
        start = 2;
    }
    co_await op.moveSlider(t.page, t.slot, start);
    uint16_t cell = uint16_t(op.recordBase() + 6 * op.column() + t.slot);
    std::vector<std::pair<unsigned, int>> values;
    for (unsigned v : values_between(2, 254, step, down)) {
        // (a fader message equal to the last one is ignored; the sweep never repeats one)
        m.fader(t.slot, v);
        co_await m.sleep(g_options.settle);
        uint8_t b = co_await stillValue(m, cell);
        values.push_back({v, int(b)});
    }
    print_line(key, variation, t, down, values, step);
}

Task<bool> selectL(Machine &m, LarcOperator &op, int bank, int program) {
    // As the plugin's LarcPort: up to 3 attempts, cancelling SHIFT between.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            co_await caught(op.cancelShift());
            co_await m.sleep(0.5);
            op.current.bank = 0;
        }
        Result<bool> r = co_await caught(op.selectProgram(bank, program));
        if (r.ok && r.value) {
            co_return true;
        }
    }
    co_return false;
}

Task<void> runL(Machine &m, LarcOperator &op, const mini::Catalog &catalog, const Options &o) {
    co_await m.sleep(16);
    for (const mini::Program &program : catalog.programs) {
        std::string key = std::to_string(program.bank) + "." + std::to_string(program.program);
        if (!take_program(o, key)) {
            continue;
        }
        std::vector<int> variations = {o.variation};
        if (o.variation == 0) {
            variations = program.variations;
        }
        for (int v : variations) {
            if (!co_await selectL(m, op, program.bank, program.program)) {
                co_await fail("could not load program %s", key.c_str());
            }
            if (op.current.variation != v && !co_await op.loadVariation(v)) {
                co_await fail("could not load variation %d of program %s", v, key.c_str());
            }
            for (const mini::Page &page : program.pages) {
                for (unsigned slot = 0; slot < page.sliders.size(); slot++) {
                    const std::string &name = page.sliders[slot];
                    if (name.empty() || name == "INACTIVE" || !wanted(o, page.page, slot)) {
                        continue;
                    }
                    Target t{page.page, slot, underscored(name)};
                    if (!o.downOnly) {
                        co_await sweepL(m, op, key, v, t, false, o.step);
                    }
                    if (o.down) {
                        co_await sweepL(m, op, key, v, t, true, o.step);
                    }
                }
            }
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: measure_stored ROM_SET_DIR CATALOG_JSON [--program KEY]... [--variation V|all] [--down] "
                     "[--slots p.s,...] [--step N] [--first SLOT=READING]...\n");
        return 2;
    }
    Options o;
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--program" && i + 1 < argc) {
            o.programs.push_back(argv[++i]);
        } else if (arg == "--variation" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "all") {
                o.variation = 0;
            } else {
                o.variation = std::atoi(v.c_str());
            }
        } else if (arg == "--first" && i + 1 < argc) {
            std::string item = argv[++i];
            size_t eq = item.find('=');
            o.first.push_back(
                {unsigned(std::atoi(item.substr(0, eq).c_str())), unsigned(std::atoi(item.substr(eq + 1).c_str()))});
        } else if (arg == "--settle" && i + 1 < argc) {
            o.settle = std::atof(argv[++i]);
        } else if (arg == "--still" && i + 1 < argc) {
            o.still = std::atof(argv[++i]);
        } else if (arg == "--down") {
            o.down = true;
        } else if (arg == "--down-only") {
            o.down = true;
            o.downOnly = true;
        } else if (arg == "--step" && i + 1 < argc) {
            o.step = unsigned(std::atoi(argv[++i]));
        } else if (arg == "--slots" && i + 1 < argc) {
            std::string list = argv[++i];
            size_t at = 0;
            while (at < list.size()) {
                size_t comma = list.find(',', at);
                if (comma == std::string::npos) {
                    comma = list.size();
                }
                std::string item = list.substr(at, comma - at);
                size_t dot = item.find('.');
                o.only.push_back(
                    {std::atoi(item.substr(0, dot).c_str()), unsigned(std::atoi(item.substr(dot + 1).c_str()))});
                at = comma + 1;
            }
        } else {
            std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
            return 2;
        }
    }
    if (o.step < 1) {
        o.step = 1;
    }
    g_options = o;
    try {
        roms::RomSet set = roms::read_set(argv[1], true);
        auto engine = std::make_unique<Engine>(set.model);
        roms::load_into(*engine, set);
        auto machine = std::make_unique<Machine>(*engine);
        mini::Catalog probe;   // rom and remote only (the LARC loader needs bank/program fields)
        {
            std::ifstream file(argv[2]);
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string text = buffer.str();
            mini::Json root = mini::Parser(text).parse();
            probe.rom = root["rom"].s;
            probe.remote = root["remote"].s;
        }
        if (probe.rom != set.hash) {
            std::fprintf(stderr, "catalog %s is for %s, the set is %s\n", argv[2], probe.rom.c_str(), set.hash.c_str());
            return 2;
        }
        std::printf("# rom %s remote %s\n", set.hash.c_str(), probe.remote.c_str());
        RootState result;
        if (probe.remote == "panel224") {
            panel224_test::Catalog catalog = panel224_test::load(argv[2]);
            std::printf("# layout %s\n", panel224_layout_named(catalog.layout.c_str()).name);
            Panel224Operator op(*machine, panel224_layout_named(catalog.layout.c_str()));
            result = machine->run_task([&] { return run224(*machine, op, catalog, o); });
        } else if (probe.remote == "panel") {
            panel_catalog::Catalog catalog = panel_catalog::load(argv[2]);
            PanelOperator op(*machine);
            result = machine->run_task([&] { return runX(*machine, op, catalog, o); });
        } else if (probe.remote == "larc") {
            mini::Catalog catalog = mini::load_catalog(argv[2]);
            LarcOperator op(*machine);
            result = machine->run_task([&] { return runL(*machine, op, catalog, o); });
        } else {
            std::fprintf(stderr, "remote %s: unknown\n", probe.remote.c_str());
            return 2;
        }
        if (result.failed) {
            std::printf("# FAILED: %s\n", result.error.text);
            return 1;
        }
        std::printf("# done\n");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
