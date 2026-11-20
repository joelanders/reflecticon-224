// The record_timeline script (../../bench/record_timeline.mjs) for the
// original 224's front panel, as a C++ coroutine over Panel224Operator:
// boot 16 s, load FIRST, move pot 1 of page 1 to 32, 96, 160, 224, 128, 64,
// load SECOND, with "mark" lines at the phase boundaries. Shared by
// panel224_record_timeline.cpp and panel224_chunks.cpp.
#pragma once
#include "../../source/operator/panel224_operator.hpp"
#include "panel224_catalog.hpp"
#include "script.hpp"

namespace lexplug::op::panel224_test {

struct Script {
    const Program *first = nullptr;
    const Program *second = nullptr;
    Echo moves[6];
};

inline Task<void> script(Machine &m, Panel224Operator &op, timeline::Lines &lines, Script &s) {
    co_await m.sleep(16);
    timeline::mark(m, lines, "boot");
    if (!co_await op.selectProgram(s.first->identity)) {
        co_await fail("could not load %s", s.first->name.c_str());
    }
    co_await m.sleep(1);
    timeline::mark(m, lines, "load_a");
    // Sweep the first slider of page 1 across its range, as a user dragging it.
    static constexpr unsigned raws[6] = {32, 96, 160, 224, 128, 64};
    for (int i = 0; i < 6; i++) {
        s.moves[i] = co_await op.moveSlider(1, 0, raws[i]);
        co_await m.sleep(0.2);
    }
    co_await m.sleep(1);
    timeline::mark(m, lines, "sweep");
    if (!co_await op.selectProgram(s.second->identity)) {
        co_await fail("could not load %s", s.second->name.c_str());
    }
    co_await m.sleep(1);
    timeline::mark(m, lines, "load_b");
}

inline void print_moves(const Script &s) {
    std::printf("[");
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
    std::printf("]");
}

}  // namespace lexplug::op::panel224_test
