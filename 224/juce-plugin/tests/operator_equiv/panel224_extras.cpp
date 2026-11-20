// The 224 operator calls the timeline does not exercise (toggles, readPages,
// SHIFT-page and pre-delay moves, sweeps, variations, the error path), in the
// order of panel224_record_extras.mjs, writing the same two files: the
// machine's inputs and one results line per call. Both must equal the JS's.
//
//   panel224_extras ROM_SET_DIR CATALOG_DIRS OUT.events OUT.results
#include "panel224_script.hpp"
#include "../../source/operator/rom_set.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;
using namespace lexplug::op::panel224_test;

namespace {

struct Out {
    std::vector<std::string> results;
    Machine *m = nullptr;
    void say(const std::string &text) {
        results.push_back(std::to_string(m->js_frame()) + " " + text);
    }
};

std::string quoted(const char *s) {
    return std::string("\"") + s + "\"";
}
std::string echo_json(const Echo &e) {
    if (!e.present) {
        return "null";
    }
    return quoted(e.value);
}
std::string toggles_text(const Toggles &t) {
    return std::to_string(t.value[0]) + " " + std::to_string(t.value[1]) + " " + std::to_string(t.value[2]);
}
std::string table_json(const Panel224Table &t) {
    std::string s = "[";
    for (int i = 0; i < t.count; i++) {
        if (i) {
            s += ",";
        }
        s += "[" + std::to_string(t.entries[i].raw) + "," + quoted(t.entries[i].text) + "]";
    }
    return s + "]";
}
const char *bool_text(bool b) {
    if (b) {
        return "true";
    }
    return "false";
}

Task<void> moveCall(Panel224Operator &op, Out &out, const char *label, int page, unsigned slot, unsigned value) {
    Result<Echo> r = co_await caught(op.moveSlider(page, slot, value));
    if (r.ok) {
        out.say(std::string(label) + " " + echo_json(r.value));
    } else {
        out.say(std::string(label) + " threw " + r.error.text);
    }
}
Task<void> sweepCall(Panel224Operator &op, Out &out, const char *label, int page, unsigned slot,
                     Panel224Table &table) {
    Result<void> r = co_await caught(op.sweep(page, slot, table));
    if (r.ok) {
        out.say(std::string(label) + " " + table_json(table));
    } else {
        out.say(std::string(label) + " threw " + r.error.text);
    }
}

Task<void> extras(Machine &m, Panel224Operator &op, timeline::Lines &lines, Out &out, Panel224Table &table,
                  Panel224Pages &pages) {
    co_await m.sleep(16);
    timeline::mark(m, lines, "boot");
    const char *kind = "layout";
    if (op.layout().has_diffusion) {
        kind = "panel224";
    }
    out.say(std::string("operator ") + kind + " layout " + op.layout().name);
    bool selected = co_await op.selectProgram(2);
    out.say(std::string("select ") + bool_text(selected));
    timeline::mark(m, lines, "select");
    if (op.layout().has_toggles) {
        Toggles t = co_await op.readToggles();
        out.say("toggles " + toggles_text(t));
        Toggles a = co_await op.setToggle(1, t.value[1] != 1);
        out.say("set MODE ENH " + toggles_text(a));
        Toggles b = co_await op.setToggle(2, t.value[2] != 1);
        out.say("set DECAY OPT " + toggles_text(b));
        Toggles c = co_await op.setToggle(2, t.value[2] == 1);
        out.say("set DECAY OPT " + toggles_text(c));
        timeline::mark(m, lines, "toggles");
    }
    co_await op.readPages(pages);
    for (int i = 0; i < pages.count; i++) {
        const Panel224Pages::Page &p = pages.pages[i];
        std::string line = "page " + std::to_string(p.page) + " [" + p.heading + "] column " + std::to_string(p.column) + ": ";
        for (int s = 0; s < 6; s++) {
            if (s) {
                line += ", ";
            }
            line += std::string(p.sliders[s].name) + "=" + p.sliders[s].value + "/" + std::to_string(p.sliders[s].raw);
        }
        out.say(line);
    }
    timeline::mark(m, lines, "pages");
    co_await moveCall(op, out, "move 1 6 40", 1, 5, 40);
    co_await moveCall(op, out, "move 1 5 253", 1, 4, 253);
    co_await moveCall(op, out, "move 1 5 253", 1, 4, 253);
    if (op.layout().has_diffusion) {
        co_await moveCall(op, out, "move 2 5 100", 2, 4, 100);
    }
    co_await moveCall(op, out, "move 2 1 10", 2, 0, 10);
    {
        Result<PanelText> r = co_await caught(op.readValue(1, 2));
        if (r.ok) {
            out.say(std::string("value 1 3 ") + quoted(r.value.text));
        } else {
            out.say(std::string("value 1 3 threw ") + r.error.text);
        }
    }
    timeline::mark(m, lines, "moves");
    co_await sweepCall(op, out, "sweep 1 5", 1, 4, table);
    if (op.layout().has_diffusion) {
        co_await sweepCall(op, out, "sweep 2 5", 2, 4, table);
    } else {
        co_await sweepCall(op, out, "sweep 1 2", 1, 1, table);
    }
    timeline::mark(m, lines, "sweeps");
    bool v1 = co_await op.loadVariation(1);
    out.say(std::string("variation 1 ") + bool_text(v1));
    bool v2 = co_await op.loadVariation(2);
    out.say(std::string("variation 2 ") + bool_text(v2));
    std::string stored = "[";
    for (unsigned s = 0; s < 6; s++) {
        stored += std::to_string(op.stored(0, s)) + ",";
    }
    stored += std::to_string(op.stored(1, 4)) + "]";
    out.say("stored " + stored);
    timeline::mark(m, lines, "variations");
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
    if (argc < 5) {
        std::fprintf(stderr, "usage: panel224_extras ROM_SET_DIR CATALOG_DIRS OUT.events OUT.results\n");
        return 2;
    }
    try {
        roms::RomSet set = roms::read_set(argv[1], false);
        Catalog catalog = find(split_dirs(argv[2]), set.hash);
        auto engine = std::make_unique<Engine>(set.model);
        roms::load_into(*engine, set);
        auto machine = std::make_unique<Machine>(*engine);
        Panel224Operator op(*machine, panel224_layout_named(catalog.layout.c_str()));
        timeline::Lines lines;
        machine->set_recorder(&lines);
        Out out;
        out.m = machine.get();
        auto table = std::make_unique<Panel224Table>();
        auto pages = std::make_unique<Panel224Pages>();
        RootState result = machine->run_task([&] { return extras(*machine, op, lines, out, *table, *pages); });
        if (result.failed) {
            std::fprintf(stderr, "FAILED: %s\n", result.error.text);
            return 1;
        }
        std::ofstream events(argv[3]);
        events << lines.text(machine->js_frame());
        std::ofstream results(argv[4]);
        for (const auto &line : out.results) {
            results << line << "\n";
        }
        std::printf("%s: %zu events, end %.1f s; pool high water %zu, largest frame %zu bytes\n", argv[3],
                    lines.lines.size() + 1, machine->time(), machine->pool().high_water(),
                    machine->pool().max_request());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
