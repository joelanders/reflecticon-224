// Run the user's 224X v8.1 firmware on the emulator (host.hpp,
// read only) into a diagnostic program, and report the DSP state the
// gate-level signature benches need: WCS image, register file, XREG input
// writes, WCS writes and the panel display.
//
//   diag_state ROM_DIRECTORY CYCLES [cycle switchbank value]...
//
// Each triple sets panel switch bank `switchbank` to `value` (bit set = button
// down) at `cycle`. Prints "D cycle digits..." whenever the display changes,
// "X cycle pc port value" for DSP port writes, "W ..." for WCS writes, and a
// final "R" line with the register file, CPC and XREG output.
#include "host.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

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
    if (argc < 3) {
        throw std::runtime_error("usage: diag_state ROM_DIRECTORY CYCLES [cycle bank value]...");
    }
    fs::path rom = argv[1];
    uint64_t deadline = std::stoull(argv[2]);
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
    struct Press {
        uint64_t cycle;
        unsigned bank, value;
    };
    std::vector<Press> presses;
    for (int i = 3; i + 2 < argc; i += 3) {
        presses.push_back({std::stoull(argv[i]), unsigned(std::stoul(argv[i + 1], nullptr, 0)),
                           unsigned(std::stoul(argv[i + 2], nullptr, 0))});
    }
    bool trace_ports = std::getenv("TRACE_PORTS") != nullptr;
    h.port_trace = [&](uint64_t c, uint16_t pc, bool w, unsigned port, uint8_t v) {
        if (trace_ports && port < 10) {
            std::printf("X %llu %04x %s %u %02x\n", (unsigned long long)c, pc, w ? "out" : "in", port, v);
        }
    };
    uint64_t wcs_writes = 0;
    h.wcs_observer = [&](const WcsWrite &w) {
        wcs_writes++;
        if (std::getenv("TRACE_WCS")) {
            std::printf("W %llu %04x %04x %02x\n", (unsigned long long)w.cpu_t1, w.writer_pc, w.address, w.value);
        }
    };
    std::array<uint8_t, 9> shown{};
    size_t next = 0;
    while (h.cycles < deadline) {
        while (next < presses.size() && presses[next].cycle <= h.cycles) {
            h.panel.switches[presses[next].bank] = uint8_t(~presses[next].value);
            std::printf("E %llu bank %u value %02x\n", (unsigned long long)h.cycles, presses[next].bank,
                        presses[next].value);
            next++;
        }
        uint64_t stop = std::min<uint64_t>(deadline, h.cycles + 20000);
        if (next < presses.size()) {
            stop = std::min<uint64_t>(stop, presses[next].cycle);
        }
        h.run_until(stop);
        if (h.panel.digits != shown) {
            shown = h.panel.digits;
            std::printf("D %llu", (unsigned long long)h.cycles);
            for (auto d : shown) {
                std::printf(" %02x", d);
            }
            std::printf(" wcs_writes=%llu\n", (unsigned long long)wcs_writes);
        }
    }
    auto &m = *h.dsp;
    std::printf("R %llu R0=%04x R1=%04x R2=%04x R3=%04x RR=%04x ACC=%d cpc=%04x pc=%u xreg_to_cpu=%04x wcs_writes=%llu\n",
                (unsigned long long)h.cycles, m.R[0], m.R[1], m.R[2], m.R[3], uint16_t(m.RR), m.ACC, m.cpc, m.pc,
                m.xreg_to_cpu, (unsigned long long)wcs_writes);
    if (const char *out = std::getenv("WCS_OUT")) {
        std::FILE *f = std::fopen(out, "w");
        for (unsigned a = 0x4000; a < 0x4200; a += 16) {
            std::fprintf(f, "%04X:", a);
            for (unsigned k = 0; k < 16; k++) {
                std::fprintf(f, " %02X", h.peek(uint16_t(a + k)));
            }
            std::fprintf(f, "\n");
        }
        std::fclose(f);
    }
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
