// M1's ROM loading, now used only by the test harnesses (byte_exact,
// rt_alloc) to build their reference machines; the plugin itself finds ROMs
// with source/roms/ RomLibrary (env var, settings, drop folder, zips, SHA-256
// recognition). A directory of chip files named as the web page expects
// (page/larc.js chipBase and modelOf), taken from LEXICON224_ROMPATH or given
// directly. The bytes are read from the user's files at run time; none are
// ever stored by the plugin.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace lexplug {

struct RomChip {
    unsigned base;
    std::vector<uint8_t> bytes;
};

struct RomSet {
    int model = 0;      // 0 the 224X/224XL, 1 the original 224 (Engine's)
    std::vector<RomChip> chips;
};

// A set's chips from `dir`: SBCn at (n-1)*0x800, NVSn at 0x8000+(n-1)*0x1000,
// the 224's ROMn (n = 1-4) at (n-1)*0x800. Empty if nothing matched.
inline std::optional<RomSet> load_rom_directory(const std::filesystem::path &dir) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::is_directory(dir, error)) {
        return std::nullopt;
    }
    std::regex sbc(R"(SBC\s*(\d))", std::regex::icase), nvs(R"(NVS\s*(\d))", std::regex::icase),
        rom(R"(ROM\s*([1-4])(?!\d))", std::regex::icase), xl(R"(SBC|NVS)", std::regex::icase);
    RomSet set;
    for (const auto &entry : fs::directory_iterator(dir, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        std::smatch m;
        unsigned base;
        if (std::regex_search(name, m, sbc)) {
            base = unsigned(std::stoi(m[1]) - 1) * 0x800;
        } else if (std::regex_search(name, m, nvs)) {
            base = 0x8000 + unsigned(std::stoi(m[1]) - 1) * 0x1000;
        } else if (std::regex_search(name, m, rom)) {
            base = unsigned(std::stoi(m[1]) - 1) * 0x800;
            if (!std::regex_search(name, xl)) {
                set.model = 1;
            }
        } else {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        RomChip chip{base, std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {})};
        if (chip.bytes.empty() || base + chip.bytes.size() > 0x10000) {
            continue;
        }
        set.chips.push_back(std::move(chip));
    }
    if (set.chips.empty()) {
        return std::nullopt;
    }
    return set;
}

// The set named by the LEXICON224_ROMPATH environment variable, if any.
inline std::optional<RomSet> find_rom_set() {
    const char *path = std::getenv("LEXICON224_ROMPATH");
    if (path == nullptr || *path == 0) {
        return std::nullopt;
    }
    return load_rom_directory(path);
}

}  // namespace lexplug
