// Run user-supplied 224X firmware on the row machine and write the same
// traces as the board-level machine's `factory` command, for comparison:
//
//   factory ROM_DIRECTORY CPU_CYCLES OUTPUT_PREFIX [CONTROL_SCRIPT]
//
// stdout: S (state every 250,000 cycles), P (diagnostic PCs), E (controls).
// PREFIX.events: DAC captures. PREFIX.wcs.csv: every CPU WCS write.
#include "host.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>

using namespace lexicon224x::cpu;
namespace fs = std::filesystem;

static void load_rom(Host &h, unsigned base, const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.empty()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::copy(bytes.begin(), bytes.end(), h.memory.begin() + base);
}

static void load_factory(Host &h, const fs::path &directory) {
    for (unsigned chip = 1; chip <= 4; ++chip) {
        auto path = directory / ("SBC" + std::to_string(chip) + " 2716.BIN");
        if (chip <= 2 || fs::exists(path)) {
            load_rom(h, (chip - 1) * 0x800, path);
        }
    }
    for (unsigned chip = 1; chip <= 8; ++chip) {
        auto path = directory / ("NVS" + std::to_string(chip) + " 2732.BIN");
        if (fs::exists(path)) {
            load_rom(h, 0x8000 + (chip - 1) * 0x1000, path);
        }
    }
}

static void print_state(Host &h) {
    auto s = h.snapshot();
    std::cout << "S " << h.cycles << ' ' << s.pc << ' ' << s.sp << ' ' << unsigned(s.a) << ' '
              << unsigned(s.flags) << ' ' << s.bc << ' ' << s.de << ' ' << s.hl << ' ' << h.halted << ' '
              << h.interrupt_enabled() << ' ' << h.wcs_writes() << ' ' << h.audio_count << ' '
              << h.reset_edges() << ' ' << unsigned(h.remote.status()) << ' ' << h.remote.connected;
    for (unsigned a : {0x2000, 0x2001, 0x2002, 0x206b, 0x3c00, 0x3c01, 0x3ff0, 0x41fc, 0x41fd, 0x41fe, 0x41ff}) {
        std::cout << ' ' << unsigned(h.peek(uint16_t(a)));
    }
    for (auto digit : h.panel.digits) {
        std::cout << ' ' << unsigned(digit);
    }
    std::cout << '\n';
}

static void save_wcs(Host &h, const std::string &path) {
    std::ofstream out(path, std::ios::binary);
    for (unsigned a = 0x4000; a < 0x4200; ++a) {
        out.put(char(h.peek(uint16_t(a))));
    }
}

struct Control {
    uint64_t cycle;
    std::string action;
    std::vector<std::string> arguments;
};

static std::vector<Control> read_controls(const fs::path &path) {
    std::vector<Control> controls;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        std::istringstream words(line);
        Control c;
        if (!(words >> c.cycle >> c.action)) {
            continue;
        }
        for (std::string w; words >> w;) {
            c.arguments.push_back(w);
        }
        controls.push_back(c);
    }
    return controls;
}

static void apply(Host &h, const Control &c, const std::string &prefix) {
    auto n = [&](unsigned i) {
        return unsigned(std::stoul(c.arguments.at(i), nullptr, 0));
    };
    if (c.action == "button") {
        h.panel.switches[n(0)] = uint8_t(~n(1));
    } else if (c.action == "pot") {
        h.panel.pots[n(0)] = uint8_t(n(1));
    } else if (c.action == "key") {
        if (!h.key(uint8_t(n(0)), n(1))) {
            throw std::runtime_error("LARC rejected key");
        }
    } else if (c.action == "fader") {
        if (!h.fader(n(0), uint8_t(n(1)))) {
            throw std::runtime_error("LARC rejected fader");
        }
    } else if (c.action == "audio") {
        // The controls give the CH1 pin first (the board-level reference calls
        // it left); it is the right input (channel 2).
        h.set_audio(n(2), n(3), n(0), n(1));
    } else if (c.action == "snapshot") {
        save_wcs(h, prefix + "." + c.arguments.at(0) + ".wcs.bin");
        print_state(h);
    } else {
        throw std::runtime_error("unknown control " + c.action);
    }
    std::cout << "E " << h.cycles << ' ' << c.action;
    for (auto &a : c.arguments) {
        std::cout << ' ' << a;
    }
    std::cout << '\n';
}

int main(int argc, char **argv) try {
    if (argc < 4) {
        throw std::runtime_error("usage: factory ROM_DIRECTORY CPU_CYCLES PREFIX [CONTROLS]");
    }
    uint64_t deadline = std::stoull(argv[2]);
    std::string prefix = argv[3];
    auto host = std::make_unique<Host>();
    Host &h = *host;
    load_factory(h, argv[1]);
    for (unsigned pc : {0x028c, 0x0294, 0x059c, 0x05c8, 0x05cc, 0x05cd, 0x05dd, 0x0b61}) {
        h.pc_watches[pc] = true;
    }
    h.pc_observer = [](uint64_t cycles, CpuSnapshot s) {
        std::cout << "P " << cycles << std::hex << ' ' << s.pc << ' ' << unsigned(s.a) << ' ' << unsigned(s.flags)
                  << ' ' << s.bc << ' ' << s.de << ' ' << s.hl << ' ' << s.sp << std::dec << '\n';
    };
    std::ofstream events(prefix + ".events"), writes(prefix + ".wcs.csv");
    writes << "cpu_t1,writer_pc,address,value,commit_tick\n";
    h.audio_observer = [&](const AudioEvent &e) {
        events << "A " << e.time << ' ' << e.channels << " 15 0 " << e.dac << " 4095 0 " << (e.gain | 12) << " 15 0\n";
    };
    h.wcs_observer = [&](const WcsWrite &w) {
        writes << w.cpu_t1 << ',' << w.writer_pc << ',' << w.address << ',' << unsigned(w.value) << ','
               << w.committed_at << '\n';
    };
    if (std::getenv("PORT_TRACE")) {
        h.port_trace = [](uint64_t c, uint16_t pc, bool w, unsigned port, uint8_t v) {
            const char *direction = " in ";
            if (w) {
                direction = " out ";
            }
            std::cerr << "port " << c << ' ' << std::hex << pc << direction << port << ' '
                      << unsigned(v) << std::dec << '\n';
        };
    }
    if (std::getenv("NO_TRACE")) {
        // Measure the machine alone.
        h.audio_observer = {};
        h.wcs_observer = {};
    }
    std::vector<Control> controls;
    if (argc > 4) {
        controls = read_controls(argv[4]);
    }
    size_t next = 0;
    uint64_t checkpoint = 250000;
    while (h.cycles < deadline || (next < controls.size() && controls[next].cycle <= h.cycles)) {
        while (next < controls.size() && controls[next].cycle <= h.cycles) {
            apply(h, controls[next], prefix);
            next++;
        }
        if (h.cycles >= checkpoint) {
            print_state(h);
            checkpoint += 250000;
            std::cout << std::flush;
        }
        if (h.cycles >= deadline) {
            break;
        }
        uint64_t stop = std::min(checkpoint, deadline);
        if (next < controls.size()) {
            stop = std::min(stop, controls[next].cycle);
        }
        h.run_until(stop);
    }
    print_state(h);
    save_wcs(h, prefix + ".wcs.bin");
    if (h.remote.connected) {
        std::cout << "L " << std::string(h.remote.display.begin(), h.remote.display.end()) << '\n';
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
