// The web demo's audio path with the analog boards (analog_io.hpp), offline.
// Same boot and program as render_current.cpp (Diagnostic Program 8, MAX
// DELAY; the left input appears on DAC bits 0 and 3, outputs A and D).
//
//   render_io ROM_DIR IN.f32 OUT.f32 [--no-gain-ranging]
#include "../analog_io.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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

int main(int argc, char **argv) try {
    if (argc < 4) {
        throw std::runtime_error("usage: render_io ROM_DIR IN.f32 OUT.f32 [--no-gain-ranging]");
    }
    bool gain_ranging = !(argc > 4 && !std::strcmp(argv[4], "--no-gain-ranging"));
    fs::path rom = argv[1];
    auto host = std::make_unique<Host>();
    Host &h = *host;
    for (unsigned chip = 1; chip <= 4; ++chip) {
        auto path = rom / ("SBC" + std::to_string(chip) + " 2716.BIN");
        if (chip <= 2 || fs::exists(path)) {
            load_rom(h, (chip - 1) * 0x800, path);
        }
    }
    for (unsigned chip = 1; chip <= 8; ++chip) {
        auto path = rom / ("NVS" + std::to_string(chip) + " 2732.BIN");
        if (fs::exists(path)) {
            load_rom(h, 0x8000 + (chip - 1) * 0x1000, path);
        }
    }
    struct Press { uint64_t at; uint8_t mask; };
    for (Press p : {Press{2000000, 0x40}, Press{2120000, 0}, Press{2320000, 0x80}, Press{2570000, 0}}) {
        h.run_until(p.at);
        h.panel.switches[0] = uint8_t(~p.mask);
    }
    h.run_until(4000000);

    std::vector<float> x;
    {
        std::ifstream f(argv[2], std::ios::binary | std::ios::ate);
        size_t n = size_t(f.tellg()) / 4;
        f.seekg(0);
        x.resize(n);
        f.read(reinterpret_cast<char *>(x.data()), std::streamsize(n * 4));
    }
    lexicon224x::analog::AnalogIO io(h, gain_ranging);
    std::vector<float> out(x.size());
    for (size_t f = 0; f < x.size(); f++) {
        Tick end = io.before_frame(x[f], 0.0);
        h.run_until(end / cpu_period);
        float four[4];
        io.after_frame(four);
        out[f] = four[0];   // output A
    }
    std::ofstream o(argv[3], std::ios::binary);
    o.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size() * 4));
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
