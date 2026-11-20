// The web demo's audio path with the Model 224's analog boards
// (analog_io.hpp on a Model::Lexicon224 host), offline: render_io.cpp for
// the original 224. Program: the firmware's stereo delay line (about 0.4 s),
// reached by pressing PROGRAM 8 during the power-on panel test (224 service
// manual, "Diagnostics"; the display then reads "d"). The left input comes
// out on outputs A and D (DAC bits 0 and 3), as the 224 owner's manual says;
// output A is written out (CHANNEL=n picks another).
//
//   render_io_224 ROM_DIR IN.f32 OUT.f32 [--no-gain-ranging | --bypass]
#include "../analog_io.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace lexicon224x::cpu;
namespace fs = std::filesystem;

int main(int argc, char **argv) try {
    if (argc < 4) {
        throw std::runtime_error("usage: render_io_224 ROM_DIR IN.f32 OUT.f32 [--no-gain-ranging | --bypass]");
    }
    bool gain_ranging = !(argc > 4 && !std::strcmp(argv[4], "--no-gain-ranging"));
    bool bypass = argc > 4 && !std::strcmp(argv[4], "--bypass");
    auto host = std::make_unique<Host>(lexicon224x::Model::Lexicon224);
    Host &h = *host;
    // ROM1-ROM4 at (n-1) * 0x800.
    for (auto &entry : fs::directory_iterator(argv[1])) {
        std::string name = entry.path().filename().string();
        auto r = name.find("ROM");
        if (r == std::string::npos || name[r + 3] < '1' || name[r + 3] > '4') {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
        std::copy(bytes.begin(), bytes.end(), h.memory.begin() + (name[r + 3] - '1') * 0x800);
    }
    auto seconds = [](double s) { return uint64_t(s / 488.28125e-9); };
    h.run_until(seconds(3.0));           // the panel test (all LEDs) runs from about 0.1 s to 6.6 s
    h.panel.switches[0] = uint8_t(~0x80);
    h.run_until(seconds(3.3));
    h.panel.switches[0] = 0xff;
    h.run_until(seconds(5.0));

    std::vector<float> x;
    {
        std::ifstream f(argv[2], std::ios::binary | std::ios::ate);
        size_t n = size_t(f.tellg()) / 4;
        f.seekg(0);
        x.resize(n);
        f.read(reinterpret_cast<char *>(x.data()), std::streamsize(n * 4));
    }
    lexicon224x::analog::AnalogIO io(h, gain_ranging);
    io.set_bypass(bypass);
    std::vector<float> out(x.size());
    float peak[4] = {0, 0, 0, 0};
    int channel = 0;
    if (const char *c = std::getenv("CHANNEL")) {
        channel = std::atoi(c);
    }
    for (size_t f = 0; f < x.size(); f++) {
        Tick end = io.before_frame(x[f], 0.0);
        h.run_until(end / cpu_period);
        float four[4];
        io.after_frame(four);
        for (int c = 0; c < 4; c++) {
            peak[c] = std::max(peak[c], std::fabs(four[c]));
        }
        out[f] = four[channel];
    }
    std::fprintf(stderr, "peaks A-D: %g %g %g %g\n", peak[0], peak[1], peak[2], peak[3]);
    std::ofstream o(argv[3], std::ios::binary);
    o.write(reinterpret_cast<const char *>(out.data()), std::streamsize(out.size() * 4));
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
