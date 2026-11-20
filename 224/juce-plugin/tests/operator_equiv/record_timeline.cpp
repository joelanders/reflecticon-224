// The C++ twin of ../../bench/record_timeline.mjs for the 224XL (LARC):
// power on, boot 16 s, load program FIRST, move slider 1 of page 1 six times,
// load SECOND, all through the C++ operator (../../source/operator/), and
// write every input the machine is given with the 48 kHz frame it precedes.
// The JS recorder's file for the same set and programs must be identical:
//
//   record_timeline ROM_SET_DIR CATALOG_DIR OUT.events FIRST SECOND
//   diff OUT.events <node bench/record_timeline.mjs ... output>
#include "script.hpp"
#include "../../source/operator/rom_set.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;

using namespace lexplug::op::timeline;


int main(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: record_timeline ROM_SET_DIR CATALOG_DIR OUT.events FIRST SECOND\n");
        return 2;
    }
    try {
        roms::RomSet set = roms::read_set(argv[1], false);
        mini::Catalog catalog = mini::load_catalog(std::string(argv[2]) + "/" + set.hash + ".json");
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
        LarcOperator op(*machine);
        Lines lines;
        machine->set_recorder(&lines);
        RootState result = machine->run_task([&] { return script(*machine, op, lines, s); });
        if (result.failed) {
            std::fprintf(stderr, "FAILED: %s\n", result.error.text);
            return 1;
        }
        std::ofstream out(argv[3]);
        out << lines.text(machine->frame());
        std::printf("%s: %s -> %s, sweep echoes [", argv[3], argv[4], argv[5]);
        for (int i = 0; i < 6; i++) {
            if (i) {
                std::printf(",");
            }
            if (s.moves[i].present) {
                std::printf("\"%s\"", s.moves[i].value);
            } else {
                std::printf("null");
            }
        }
        std::printf("], %zu events, end %.1f s; frames: pool high water %zu of %zu, largest %zu bytes; "
                    "resumed while busy %u\n",
                    lines.lines.size(), machine->time(), machine->pool().high_water(), FramePool::block_count,
                    machine->pool().max_request(), machine->resumed_while_busy());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
