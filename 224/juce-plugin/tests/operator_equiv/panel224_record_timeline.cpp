// The C++ twin of ../../bench/record_timeline.mjs for the original 224
// (front panel, panel224_operator.hpp): power on, boot 16 s, load FIRST,
// move pot 1 of page 1 six times, load SECOND, writing every input the
// machine is given with the 48 kHz frame it precedes. The JS recorder's file
// for the same set and programs must be identical (for v3.2 and TEST, the JS
// recorder is panel224_record_timeline.mjs).
//
//   panel224_record_timeline ROM_SET_DIR CATALOG_DIRS OUT.events FIRST SECOND
// CATALOG_DIRS: colon-separated directories searched for <hash>.json; the
// catalog's "layout" field picks the RAM layout.
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

std::vector<std::string> split_dirs(const std::string &dirs) {
    std::vector<std::string> out;
    std::stringstream stream(dirs);
    std::string item;
    while (std::getline(stream, item, ':')) {
        out.push_back(item);
    }
    return out;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: panel224_record_timeline ROM_SET_DIR CATALOG_DIRS OUT.events FIRST SECOND\n");
        return 2;
    }
    try {
        roms::RomSet set = roms::read_set(argv[1], false);
        Catalog catalog = find(split_dirs(argv[2]), set.hash);
        Script s;
        for (const auto &p : catalog.programs) {
            if (p.name == argv[4]) {
                s.first = &p;
            }
            if (p.name == argv[5]) {
                s.second = &p;
            }
        }
        if (s.first == nullptr || s.second == nullptr) {
            std::fprintf(stderr, "program not in the catalog\n");
            return 2;
        }
        auto engine = std::make_unique<Engine>(set.model);
        roms::load_into(*engine, set);
        auto machine = std::make_unique<Machine>(*engine);
        Panel224Operator op(*machine, panel224_layout_named(catalog.layout.c_str()));
        timeline::Lines lines;
        machine->set_recorder(&lines);
        RootState result = machine->run_task([&] { return script(*machine, op, lines, s); });
        if (result.failed) {
            std::fprintf(stderr, "FAILED: %s\n", result.error.text);
            return 1;
        }
        std::ofstream out(argv[3]);
        out << lines.text(machine->frame());
        std::printf("%s: %s -> %s (layout %s), sweep echoes ", argv[3], argv[4], argv[5], op.layout().name);
        print_moves(s);
        std::printf(", %zu events, end %.1f s; frames: pool high water %zu of %zu, largest %zu bytes; "
                    "resumed while busy %u\n",
                    lines.lines.size(), machine->time(), machine->pool().high_water(), FramePool::block_count,
                    machine->pool().max_request(), machine->resumed_while_busy());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
