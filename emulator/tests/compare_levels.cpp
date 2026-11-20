// Two hosts on the same firmware from power-up, one at lean level 0 and one
// at another level, compared as they run: every DAC capture, every CPU WCS
// write (with its commit tick), the CPU's registers at each checkpoint, and
// the DSP's registers and delay memory at the end.
//
//   clang++ -std=c++20 -O2 -o build/compare_levels tests/compare_levels.cpp     (from emulator/)
//   build/compare_levels ROM_DIRECTORY 224|224X CPU_CYCLES [LEVEL=3]
//
// Host::set_lean() chooses how the DSP rows run; every level must give the same machine.
#include "../host.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>

using namespace lexicon224x;
using namespace lexicon224x::cpu;
namespace fs = std::filesystem;

struct Side {
    std::unique_ptr<Host> host;
    uint64_t audio_hash = 1469598103934665603ull, wcs_hash = 1469598103934665603ull, audio_events = 0, wcs_writes = 0;

    static void mix(uint64_t &h, uint64_t v) {
        h = (h ^ v) * 1099511628211ull;
    }

    Side(Model model, int level) : host(std::make_unique<Host>(model)) {
        host->set_lean(level);
        host->audio_observer = [this](const AudioEvent &e) {
            mix(audio_hash, e.time);
            mix(audio_hash, e.channels << 20 | e.dac << 4 | e.gain);
            audio_events++;
        };
        host->wcs_observer = [this](const WcsWrite &w) {
            mix(wcs_hash, w.cpu_t1);
            mix(wcs_hash, uint64_t(w.writer_pc) << 32 | uint64_t(w.address) << 8 | w.value);
            mix(wcs_hash, w.committed_at);
            wcs_writes++;
        };
    }
};

// Chip files as the firmware sets name them: SBCn at (n-1)*0x800, NVSn at 0x8000+(n-1)*0x1000; the 224's ROMn at (n-1)*0x800.
static int load(Host &h, const fs::path &directory) {
    int loaded = 0;
    for (const auto &entry : fs::directory_iterator(directory)) {
        std::string name = entry.path().filename().string();
        std::smatch m;
        long base = -1;
        if (std::regex_search(name, m, std::regex("^SBC ?([0-9]+)", std::regex::icase))) {
            base = (std::stol(m[1]) - 1) * 0x800;
        } else if (std::regex_search(name, m, std::regex("^NVS ?([0-9]+)", std::regex::icase))) {
            base = 0x8000 + (std::stol(m[1]) - 1) * 0x1000;
        } else if (std::regex_search(name, m, std::regex("^ROM ?([0-9]+)", std::regex::icase))) {
            base = (std::stol(m[1]) - 1) * 0x800;
        }
        if (base < 0) {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::memcpy(h.memory.data() + base, bytes.data(), bytes.size());
        loaded++;
    }
    return loaded;
}

int main(int argc, char **argv) try {
    if (argc < 4) {
        std::fprintf(stderr, "usage: compare_levels ROM_DIRECTORY 224|224X CPU_CYCLES [LEVEL]\n");
        return 2;
    }
    Model model = std::string(argv[2]) == "224" ? Model::Lexicon224 : Model::Lexicon224X;
    uint64_t cycles = std::strtoull(argv[3], nullptr, 10);
    int level = argc > 4 ? std::atoi(argv[4]) : 3;
    Side a(model, 0), b(model, level);
    int chips = load(*a.host, argv[1]);
    load(*b.host, argv[1]);
    for (uint64_t at = 250000; at <= cycles; at += 250000) {
        a.host->run_until(at);
        b.host->run_until(at);
        // A little input, the same on both sides, so that the ADC path and the level detectors move.
        unsigned code = unsigned(at / 250000 * 977) & 0xfff;
        a.host->set_audio(code, 1, code ^ 0x155, 2);
        b.host->set_audio(code, 1, code ^ 0x155, 2);
        CpuSnapshot sa = a.host->snapshot(), sb = b.host->snapshot();
        if (std::memcmp(&sa, &sb, sizeof sa) || a.host->cycles != b.host->cycles || a.audio_hash != b.audio_hash ||
            a.wcs_hash != b.wcs_hash || a.audio_events != b.audio_events || a.wcs_writes != b.wcs_writes) {
            std::printf("DIFFERENT by cycle %llu: audio %llu vs %llu events, WCS %llu vs %llu writes\n", (unsigned long long)at,
                        (unsigned long long)a.audio_events, (unsigned long long)b.audio_events,
                        (unsigned long long)a.wcs_writes, (unsigned long long)b.wcs_writes);
            return 1;
        }
    }
    const Machine &ma = *a.host->dsp, &mb = *b.host->dsp;
    bool same = !std::memcmp(ma.memory, mb.memory, sizeof ma.memory) && !std::memcmp(ma.wcs, mb.wcs, sizeof ma.wcs) &&
                !std::memcmp(ma.R, mb.R, sizeof ma.R) && ma.ACC == mb.ACC && ma.RR == mb.RR && ma.pc == mb.pc &&
                ma.cpc == mb.cpc && ma.operand == mb.operand && ma.partial == mb.partial &&
                !std::memcmp(a.host->memory.data(), b.host->memory.data(), a.host->memory.size());
    std::printf("%s: %d chips, %llu cycles, level 0 vs %d: %llu DAC captures, %llu WCS writes, CPU state at %llu checkpoints, "
                "DSP registers, delay memory and CPU memory at the end; level %d ran %llu lean rows and %llu exact, "
                "checked %llu monitor reads\n",
                same ? "IDENTICAL" : "DIFFERENT at the end", chips, (unsigned long long)cycles, level,
                (unsigned long long)a.audio_events, (unsigned long long)a.wcs_writes, (unsigned long long)(cycles / 250000),
                level, (unsigned long long)b.host->lean_rows, (unsigned long long)b.host->exact_rows,
                (unsigned long long)b.host->monitor_reads_checked);
    return same ? 0 : 1;
} catch (const std::exception &e) {
    std::fprintf(stderr, "compare_levels: %s\n", e.what());
    return 1;
}
