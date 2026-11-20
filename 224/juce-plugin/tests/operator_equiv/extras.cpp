// The C++ twin of record_extras.mjs: the LARC operator calls that
// record_timeline does not exercise (toggles, mute, readPages, a sweep, a
// variation, ALL SLIDERS), writing the machine's inputs and the operator's
// results in the same formats, for a byte comparison with the JS files.
//
//   extras ROM_SET_DIR OUT.events OUT.results
#include "script.hpp"
#include "../../source/operator/rom_set.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;
using namespace lexplug::op::timeline;

namespace {

std::string toggles_text(const Toggles &t) {
    return std::to_string(t.value[0]) + " " + std::to_string(t.value[1]) + " " + std::to_string(t.value[2]);
}
std::string yes(bool b) {
    if (b) {
        return "true";
    }
    return "false";
}

struct Out {
    std::vector<std::string> results;
    PagesReading pages;
    SweepTable table;
};

void say(Machine &m, Out &out, const std::string &text) {
    out.results.push_back(std::to_string(m.frame()) + " " + text);
}

Task<void> extras(Machine &m, LarcOperator &op, Lines &lines, Out &out) {
    co_await m.sleep(16);
    mark(m, lines, "boot");
    say(m, out, "select " + yes(co_await op.selectProgram(1, 1)));
    mark(m, lines, "select");
    Toggles t = co_await op.readToggles();
    say(m, out, "toggles " + toggles_text(t));
    bool on = true;
    if (t.value[0] == 1) {
        on = false;
    }
    say(m, out, "set DYN DECAY " + toggles_text(co_await op.setToggle(0, on)));
    say(m, out, "mute " + yes(co_await op.toggleMute()));
    say(m, out, "mute " + yes(co_await op.toggleMute()));
    mark(m, lines, "toggles");
    co_await op.readPages(out.pages);
    for (int i = 0; i < out.pages.count; i++) {
        const PageReading &page = out.pages.pages[i];
        std::string text = "page " + std::to_string(page.page) + " [" + page.heading + "] column " +
                           std::to_string(page.column) + ": ";
        for (int slot = 0; slot < 6; slot++) {
            if (slot) {
                text += ", ";
            }
            text += std::string(page.sliders[slot].shown.name) + "=" + page.sliders[slot].shown.value + "/" +
                    std::to_string(page.sliders[slot].raw);
        }
        say(m, out, text);
    }
    mark(m, lines, "pages");
    co_await op.sweep(1, 1, "MID DECAY", out.table);
    std::string text = "sweep ";
    for (int i = 0; i < out.table.count; i++) {
        if (i) {
            text += ", ";
        }
        text += std::to_string(out.table.entries[i].raw) + ":" + out.table.entries[i].text;
    }
    say(m, out, text);
    mark(m, lines, "sweep");
    say(m, out, "variation " + yes(co_await op.loadVariation(2)));
    say(m, out, "activate " + yes(co_await op.activateSliders()));
    mark(m, lines, "activate");
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: extras ROM_SET_DIR OUT.events OUT.results\n");
        return 2;
    }
    roms::RomSet set = roms::read_set(argv[1], false);
    auto engine = std::make_unique<Engine>(set.model);
    roms::load_into(*engine, set);
    auto machine = std::make_unique<Machine>(*engine);
    LarcOperator op(*machine);
    Lines lines;
    machine->set_recorder(&lines);
    auto out = std::make_unique<Out>();
    RootState result = machine->run_task([&] { return extras(*machine, op, lines, *out); });
    if (result.failed) {
        std::fprintf(stderr, "FAILED: %s\n", result.error.text);
        return 1;
    }
    std::ofstream events(argv[2]);
    events << lines.text(machine->frame());
    std::ofstream results(argv[3]);
    for (const auto &line : out->results) {
        results << line << "\n";
    }
    std::printf("%s: %zu events, end %.1f s; frame pool high water %zu, largest frame %zu bytes\n", argv[2],
                lines.lines.size() + 1, machine->time(), machine->pool().high_water(), machine->pool().max_request());
    return 0;
}
