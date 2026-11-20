// A direct probe for the 224 v4.4 "collapse" that earlier HLE
// work saw (RAM -> 0xFF and a garbage WCS after ~5-9 slider events, v4.3
// immune), on
// the row machine through the C++ panel224 operator.
//
// Boot; note the pots' readings; load program IDENTITY and keep its WCS
// (the 128 microinstruction words); then MOVES random pot moves on every
// active parameter (values 0..253, the soak's LCG), with no load in
// between, checking after each that the operator succeeded and that RAM
// 3F00-3FFF is not mostly FF; then put every pot back where it was at
// boot, reload the program, and compare its WCS and stored bytes with the
// first load's (rows the firmware rewrites by itself, seen changing over
// 3 s after the first load with nothing touched, are excluded). A collapse shows as an operator failure, an FF flood, or a
// reloaded WCS that differs.
//
//   panel224_collapse ROM_SET_DIR CATALOG_DIRS IDENTITY MOVES [SEED]
#include "../../source/operator/panel224_operator.hpp"
#include "../../source/operator/rom_set.hpp"
#include "../operator_equiv/panel224_catalog.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace lexplug;
using namespace lexplug::op;

namespace {

struct Probe {
    unsigned identity = 1;
    int moves = 60;
    double seed = 1;
    uint32_t wcs_first[128] = {0};
    uint32_t wcs_again[128] = {0};
    uint8_t stored_first[12] = {0};
    uint8_t stored_again[12] = {0};
    uint8_t pots_boot[6] = {0};
    bool live[128] = {false};   // rows the firmware rewrites by itself (modulation), seen over 3 s
    int done_moves = 0;
    int echoes = 0;
    int worst_ff = 0;
    int first_flood = -1;
    bool loaded_first = false;
    bool loaded_again = false;

    double random() {
        seed = std::fmod(seed * 1103515245.0 + 12345.0, 2147483648.0);
        return seed / 2147483648.0;
    }
};

int ff_count(Machine &m) {
    int ff = 0;
    for (unsigned a = 0x3f00; a < 0x4000; a++) {
        if (m.peek(uint16_t(a)) == 0xff) {
            ff++;
        }
    }
    return ff;
}

void keep(Machine &m, const Panel224Operator &op, uint32_t *wcs, uint8_t *stored) {
    const uint32_t *words = m.engine().host().dsp->wcs;
    for (int i = 0; i < 128; i++) {
        wcs[i] = words[i];
    }
    for (unsigned i = 0; i < 12; i++) {
        stored[i] = m.peek(uint16_t(op.layout().PROGRAM + i));
    }
}

Task<void> probe(Machine &m, Panel224Operator &op, Probe &p) {
    co_await m.sleep(16);
    for (unsigned slot = 0; slot < 6; slot++) {
        p.pots_boot[slot] = m.peek(uint16_t(op.layout().POTS + slot));
    }
    p.loaded_first = co_await op.loadProgram(p.identity);
    if (!p.loaded_first) {
        co_await fail("could not load program %u", p.identity);
    }
    co_await m.sleep(1);
    keep(m, op, p.wcs_first, p.stored_first);
    // The rows that change with nothing touched (the modulated taps): 30
    // snapshots over 3 s after the load.
    for (int k = 0; k < 30; k++) {
        co_await m.sleep(0.1);
        const uint32_t *words = m.engine().host().dsp->wcs;
        for (int i = 0; i < 128; i++) {
            if (words[i] != p.wcs_first[i]) {
                p.live[i] = true;
            }
        }
    }
    // Every active parameter: page 1's six pots, and DIFFUSION on page 2.
    int choices[7][2];
    int count = 0;
    for (int page = 1; page <= op.pageCount(); page++) {
        for (unsigned slot = 0; slot < 6; slot++) {
            if (op.parameter(page, slot) != nullptr) {
                choices[count][0] = page;
                choices[count][1] = int(slot);
                count++;
            }
        }
    }
    for (int n = 0; n < p.moves; n++) {
        int k = int(std::floor(p.random() * count));
        unsigned value = unsigned(std::floor(p.random() * 254));
        Result<Echo> r = co_await caught(op.moveSlider(choices[k][0], unsigned(choices[k][1]), value));
        if (!r.ok) {
            std::printf("move %d (page %d pot %d -> %u) failed: %s\n", n, choices[k][0], choices[k][1] + 1, value,
                        r.error.text);
            co_return;
        }
        p.done_moves++;
        if (r.value.present) {
            p.echoes++;
        }
        co_await m.sleep(0.2);
        int ff = ff_count(m);
        if (ff > p.worst_ff) {
            p.worst_ff = ff;
        }
        if (ff >= 128 && p.first_flood < 0) {
            p.first_flood = n;
            std::printf("FF flood after move %d: %d of 256 bytes at 3F00-3FFF\n", n, ff);
        }
    }
    // Pots back to their boot readings, then the same load again.
    for (unsigned slot = 0; slot < 6; slot++) {
        co_await op.setPot(slot, p.pots_boot[slot]);
    }
    co_await m.sleep(1);
    p.loaded_again = co_await op.loadProgram(p.identity);
    co_await m.sleep(1);
    keep(m, op, p.wcs_again, p.stored_again);
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
        std::fprintf(stderr, "usage: panel224_collapse ROM_SET_DIR CATALOG_DIRS IDENTITY MOVES [SEED]\n");
        return 2;
    }
    try {
        roms::RomSet set = roms::read_set(argv[1], true);
        panel224_test::Catalog catalog = panel224_test::find(split_dirs(argv[2]), set.hash);
        auto engine = std::make_unique<Engine>(set.model);
        roms::load_into(*engine, set);
        auto machine = std::make_unique<Machine>(*engine);
        Panel224Operator op(*machine, panel224_layout_named(catalog.layout.c_str()));
        Probe p;
        p.identity = unsigned(std::atoi(argv[3]));
        p.moves = std::atoi(argv[4]);
        if (argc > 5) {
            p.seed = std::atof(argv[5]);
        }
        RootState result = machine->run_task([&] { return probe(*machine, op, p); });
        if (result.failed) {
            std::printf("FAILED: %s\n", result.error.text);
            return 1;
        }
        int wcs_diff = 0;
        int live_rows = 0;
        std::string rows;
        for (int i = 0; i < 128; i++) {
            if (p.live[i]) {
                live_rows++;
                continue;
            }
            if (p.wcs_first[i] != p.wcs_again[i]) {
                wcs_diff++;
                rows += " " + std::to_string(i);
            }
        }
        int stored_diff = 0;
        std::string first;
        std::string again;
        for (int i = 0; i < 12; i++) {
            if (p.stored_first[i] != p.stored_again[i]) {
                stored_diff++;
            }
            char hex[4];
            std::snprintf(hex, sizeof hex, "%02x ", p.stored_first[i]);
            first += hex;
            std::snprintf(hex, sizeof hex, "%02x ", p.stored_again[i]);
            again += hex;
        }
        const char *reload = "FAILED";
        if (p.loaded_again) {
            reload = "ok";
        }
        const char *verdict = "NO COLLAPSE";
        if (p.done_moves != p.moves || p.first_flood >= 0 || wcs_diff != 0 || stored_diff != 0 || !p.loaded_again) {
            verdict = "SUSPECT";
        }
        std::printf("%s (layout %s) program %u: %d of %d moves done (%d echoed), worst FF %d of 256, reload %s, "
                    "WCS words differing after reload %d of %d static (%d modulated rows excluded)%s, program state differing %d of 12 [%s| %s]; %.0f s "
                    "machine time: %s\n",
                    argv[1], op.layout().name, p.identity, p.done_moves, p.moves, p.echoes, p.worst_ff,
                    reload, wcs_diff, 128 - live_rows, live_rows, rows.c_str(), stored_diff, first.c_str(), again.c_str(),
                    machine->time(), verdict);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
