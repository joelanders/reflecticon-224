// Replay an events file (bench/record_timeline.mjs format) into the machine,
// frame for frame, printing the LARC display whenever it changes inside a
// frame window: for looking at what the firmware showed around a soak
// failure (a seed reproduces it exactly).
//
//   replay_display ROM_SET_DIR EVENTS FROM_FRAME TO_FRAME
#include "../../source/engine.hpp"
#include "../../source/operator/rom_set.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lexplug;

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: replay_display ROM_SET_DIR EVENTS FROM_FRAME TO_FRAME\n");
        return 2;
    }
    auto set = op::roms::read_set(argv[1], false);
    Engine engine(set.model);
    op::roms::load_into(engine, set);
    long long from = std::atoll(argv[3]);
    long long to = std::atoll(argv[4]);
    std::ifstream file(argv[2]);
    std::string line;
    struct Event {
        long long frame;
        std::string kind;
        unsigned a, b;
    };
    std::vector<Event> events;
    while (std::getline(file, line)) {
        std::istringstream in(line);
        Event e{};
        std::string first;
        in >> first;
        if (first == "end") {
            continue;
        }
        e.frame = std::atoll(first.c_str());
        in >> e.kind;
        if (e.kind == "mark") {
            std::string name;
            in >> name;
            e.a = 0;
            e.b = 0;
            e.kind = "mark " + name;
        } else {
            in >> e.a >> e.b;
        }
        events.push_back(e);
    }
    static float zero[1] = {0};
    float o[4][1];
    float *out[4] = {o[0], o[1], o[2], o[3]};
    long long now = 0;
    size_t next = 0;
    char last[49] = {0};
    while (now < to) {
        while (next < events.size() && events[next].frame <= now) {
            const Event &e = events[next++];
            if (e.frame >= from) {
                std::printf("%lld  %s %u %u\n", e.frame, e.kind.c_str(), e.a, e.b);
            }
            if (e.kind == "key") {
                engine.key(e.a, e.b != 0);
            } else if (e.kind == "fader") {
                engine.fader(e.a, e.b);
            } else if (e.kind == "poke") {
                engine.poke(uint16_t(e.a), uint8_t(e.b));
            } else if (e.kind == "button") {
                engine.button(e.a, e.b);
            } else if (e.kind == "pot") {
                engine.pot(e.a, e.b);
            }
        }
        engine.render(zero, zero, out, 1);
        now++;
        if (now >= from) {
            char text[49];
            std::memcpy(text, engine.larc_text(), 48);
            text[48] = 0;
            static unsigned last_entry = 999;
            if (std::strcmp(text, last) != 0 || engine.peek(0x3c11) != last_entry) {
                last_entry = engine.peek(0x3c11);
                std::memcpy(last, text, 49);
                std::printf("%lld  [%.24s] [%.24s]  page %u entry %u\n", now, text, text + 24, engine.peek(0x3c32),
                            engine.peek(0x3c11));
            }
        }
    }
    return 0;
}
