// Script the LARC and watch the display: for working out how the firmware
// responds before building UI on it.
//
//   probe ROM_DIRECTORY 'boot 16; key 0x23; wait 60; show; fader 0 200; wait 60; show; peek 0x202c'
//
// Commands (separated by ';'):
//   boot S         run S seconds of machine time (silent input)
//   key CODE       press and release a LARC key (release = CODE & ~0x20), 20 ms apart
//   fader N V      move LARC fader N (0-5) to V (2-254)
//   wait MS        run MS milliseconds
//   show           print the 48-character LARC display (two 24-character lines)
//   peek ADDR [N]  print N bytes of CPU memory from ADDR (hex)
//   button B MASK MS   hold 224X panel buttons MASK (hex) in bank B (0-2) for MS ms
//   hold B MASK    set bank B's held buttons to MASK (hex; 0 releases them all)
//   pot N V        set 224X panel pot N (0-5) to V (0-255)
//   wcs            print the running program's 128 WCS words (T&C polarity)
//   save FILE      write the WCS as the CPU sees it (0x4000-0x41FF) as an
//                  address-prefixed hex dump
//   dmem FILE      write the delay memory: 65,536 16-bit words, little-endian
//   watch ADDR...  report (once each) when the CPU reaches these PCs (hex)
//   panel          print the 224X panel's nine digit/LED bytes (lit bits = 1)
//   audio L R      hold the converter pins: left and right ADC codes (12-bit, gain 0)
//   listen MS      run MS ms and print, per DAC output A-D: captures, the
//                  largest |value| (DAC code unscaled by its gain), the ms to
//                  the first one above 64, and the sum of |value|
#include "host.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace lexicon224x;
using namespace lexicon224x::cpu;
namespace fs = std::filesystem;

// Where a chip file goes, by its name: SBCn and NVSn (224X/224XL), or ROMn
// (the original 224's ROM1-ROM4 at (n-1) * 0x800). -1: not a chip we place.
static int chip_base(const std::string &name) {
    auto sbc = name.find("SBC");
    if (sbc != std::string::npos) {
        return (name[sbc + 3] - '1') * 0x800;
    }
    auto nvs = name.find("NVS");
    if (nvs != std::string::npos) {
        return 0x8000 + (name[nvs + 3] - '1') * 0x1000;
    }
    auto rom = name.find("ROM");
    if (rom != std::string::npos && name[rom + 3] >= '1' && name[rom + 3] <= '4') {
        return (name[rom + 3] - '1') * 0x800;
    }
    return -1;
}

// A set with ROM1-ROM4 is an original 224's.
static Model model_of(const fs::path &dir) {
    for (auto &entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        if (name.find("ROM1") != std::string::npos) {
            return Model::Lexicon224;
        }
    }
    return Model::Lexicon224X;
}

static void load(Host &h, const fs::path &dir) {
    for (auto &entry : fs::directory_iterator(dir)) {
        int base = chip_base(entry.path().filename().string());
        if (base < 0 || !entry.is_regular_file()) {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
        std::copy(bytes.begin(), bytes.end(), h.memory.begin() + base);
    }
}

int main(int argc, char **argv) try {
    if (argc < 3) {
        throw std::runtime_error("usage: probe ROM_DIRECTORY 'commands'");
    }
    auto host = std::make_unique<Host>(model_of(argv[1]));
    Host &h = *host;
    load(h, argv[1]);
    auto run_ms = [&](double ms) {
        h.run_until(h.cycles + uint64_t(ms * 1e-3 / 488.28125e-9));
    };
    h.pc_observer = [](uint64_t cycle, CpuSnapshot cpu) {
        std::cout << "pc " << std::hex << cpu.pc << std::dec << " at cycle " << cycle << '\n';
    };
    std::istringstream script(argv[2]);
    for (std::string command; std::getline(script, command, ';');) {
        std::istringstream words(command);
        std::string op;
        if (!(words >> op)) {
            continue;
        }
        if (op == "boot") {
            double s;
            words >> s;
            run_ms(s * 1000);
        } else if (op == "wait") {
            double ms;
            words >> ms;
            run_ms(ms);
        } else if (op == "key") {
            std::string code;
            words >> code;
            unsigned c = unsigned(std::stoul(code, nullptr, 0));
            h.key(uint8_t(c), true);
            run_ms(20);
            h.key(uint8_t(c), false);
            run_ms(20);
        } else if (op == "fader") {
            unsigned n, v;
            words >> n >> v;
            h.fader(n, uint8_t(v));
            run_ms(20);
        } else if (op == "button") {
            unsigned bank;
            std::string mask;
            double ms;
            words >> bank >> mask >> ms;
            h.panel.switches[bank] = uint8_t(~std::stoul(mask, nullptr, 16));
            run_ms(ms);
            h.panel.switches[bank] = 0xff;
            run_ms(20);
        } else if (op == "hold") {
            unsigned bank;
            std::string mask;
            words >> bank >> mask;
            h.panel.switches[bank] = uint8_t(~std::stoul(mask, nullptr, 16));
        } else if (op == "pot") {
            unsigned n, v;
            words >> n >> v;
            h.panel.pots[n] = uint8_t(v);
        } else if (op == "audio") {
            int left, right;
            words >> left >> right;
            h.set_audio(unsigned(left) & 0xfff, 0, unsigned(right) & 0xfff, 0);
        } else if (op == "listen") {
            double ms;
            words >> ms;
            long count[4] = {}, largest[4] = {};
            double sum[4] = {}, first[4] = {-1, -1, -1, -1};
            Tick start = h.now();
            h.audio_observer = [&](const AudioEvent &e) {
                long v = long(e.dac) - 2048;
                if (v < 0) {
                    v = -v;
                }
                v = (v * 8) >> (e.gain - 12);
                for (int c = 0; c < 4; c++) {
                    if (!(e.channels >> c & 1)) {
                        continue;
                    }
                    count[c]++;
                    sum[c] += double(v);
                    largest[c] = std::max(largest[c], v);
                    if (v > 64 && first[c] < 0) {
                        first[c] = double(e.time - start) / (576.0 * 1e6);   // the event's own time, in ms
                    }
                }
            };
            run_ms(ms);
            h.audio_observer = {};
            std::cout << "listen";
            for (int c = 0; c < 4; c++) {
                std::cout << ' ' << "ABCD"[c] << ':' << count[c] << '/' << largest[c] << '@' << first[c]
                          << " L1=" << sum[c];
            }
            std::cout << '\n';
        } else if (op == "wcs") {
            std::cout << "wcs" << std::hex;
            for (auto word : h.dsp->wcs) {
                std::cout << ' ' << word;
            }
            std::cout << std::dec << '\n';
        } else if (op == "save") {
            std::string path;
            words >> path;
            std::ofstream out(path);
            char line[80];
            for (unsigned a = 0x4000; a < 0x4200; a += 16) {
                int n = std::snprintf(line, sizeof line, "%04x:", a);
                for (unsigned i = 0; i < 16; i++) {
                    n += std::snprintf(line + n, sizeof line - n, " %02x", unsigned(h.peek(uint16_t(a + i))));
                }
                out << line << '\n';
            }
        } else if (op == "dmem") {
            std::string path;
            words >> path;
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char *>(h.dsp->memory), sizeof h.dsp->memory);
        } else if (op == "watch") {
            for (std::string a; words >> a;) {
                h.pc_watches[std::stoul(a, nullptr, 16)] = true;
            }
        } else if (op == "panel") {
            std::cout << "panel";
            for (auto d : h.panel.digits) {
                std::cout << ' ' << std::hex << (unsigned(d) | 0x100) << std::dec;
            }
            std::cout << '\n';
        } else if (op == "show") {
            std::string d(h.remote.display.begin(), h.remote.display.end());
            std::cout << '[' << d.substr(0, 24) << "] [" << d.substr(24) << "]\n";
        } else if (op == "peek") {
            std::string a;
            unsigned n = 1;
            words >> a;
            words >> n;
            unsigned address = unsigned(std::stoul(a, nullptr, 16));
            std::cout << "peek " << std::hex << address << ':';
            for (unsigned i = 0; i < n; i++) {
                std::cout << ' ' << unsigned(h.peek(uint16_t(address + i)));
            }
            std::cout << std::dec << '\n';
        } else {
            throw std::runtime_error("unknown command " + op);
        }
    }
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
