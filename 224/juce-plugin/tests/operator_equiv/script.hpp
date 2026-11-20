// The record_timeline script (../../bench/record_timeline.mjs) as a C++
// coroutine, shared by record_timeline.cpp and chunks.cpp: boot 16 s, load
// FIRST, move slider 1 of page 1 to 32, 96, 160, 224, 128, 64, load SECOND,
// with "mark" lines at the phase boundaries.
#pragma once
#include "../../source/operator/larc_operator.hpp"
#include "../../source/operator/mini_catalog.hpp"
#include <string>
#include <vector>

namespace lexplug::op::timeline {


inline const char *kind_name(Input kind) {
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

// The recorded inputs and marks, kept as plain records in storage reserved
// up front (no allocation while recording), written out as text at the end.
class Lines : public Recorder {
public:
    struct Line {
        int64_t frame = 0;
        const char *kind = nullptr;   // an input kind, or "mark"
        unsigned a = 0;
        unsigned b = 0;
        const char *mark = nullptr;
    };
    std::vector<Line> lines;
    Lines() {
        lines.reserve(1 << 16);
    }
    void input(int64_t frame, Input kind, unsigned a, unsigned b) override {
        if (lines.size() == lines.capacity()) {
            fatal("Lines: reserved capacity exceeded");
        }
        lines.push_back(Line{frame, kind_name(kind), a, b, nullptr});
    }
    void mark(int64_t frame, const char *name) {
        lines.push_back(Line{frame, "mark", 0, 0, name});
    }
    std::string text(int64_t end) const {
        std::string out;
        for (const Line &line : lines) {
            out += std::to_string(line.frame) + " " + line.kind + " ";
            if (line.mark != nullptr) {
                out += line.mark;
            } else {
                out += std::to_string(line.a) + " " + std::to_string(line.b);
            }
            out += "\n";
        }
        return out + "end " + std::to_string(end) + "\n";
    }
};

struct Script {
    const mini::Program *first = nullptr;
    const mini::Program *second = nullptr;
    Echo moves[6];
    bool larc = false;
};

inline Task<bool> select(Machine &m, LarcOperator &op, const mini::Program &e) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt) {
            co_await op.cancelShift();
            co_await m.sleep(0.5);
            op.current.bank = 0;
        }
        if (co_await op.selectProgram(e.bank, e.program)) {
            co_return true;
        }
    }
    co_return false;
}

inline void mark(Machine &m, Lines &lines, const char *name) {
    lines.mark(m.frame(), name);
}

inline Task<void> script(Machine &m, LarcOperator &op, Lines &lines, Script &s) {
    co_await m.sleep(16);
    mark(m, lines, "boot");
    s.larc = m.engine().larc_connected();
    if (!s.larc) {
        co_await fail("no LARC after boot: this harness drives the 224XL operator only");
    }
    if (!co_await select(m, op, *s.first)) {
        co_await fail("could not load %s", s.first->name.c_str());
    }
    co_await m.sleep(1);
    mark(m, lines, "load_a");
    // Sweep the first slider of page 1 across its range, as a user dragging it.
    static constexpr unsigned raws[6] = {32, 96, 160, 224, 128, 64};
    for (int i = 0; i < 6; i++) {
        s.moves[i] = co_await op.moveSlider(1, 0, raws[i]);
        co_await m.sleep(0.2);
    }
    co_await m.sleep(1);
    mark(m, lines, "sweep");
    if (!co_await select(m, op, *s.second)) {
        co_await fail("could not load %s", s.second->name.c_str());
    }
    co_await m.sleep(1);
    mark(m, lines, "load_b");
}


}  // namespace lexplug::op::timeline
