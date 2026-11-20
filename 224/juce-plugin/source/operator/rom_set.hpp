// TEMPORARY test support: read a ROM set directory the way the web demo's
// Node tools do (tests/soak.mjs, bench/record_timeline.mjs): chip files by
// name (SBCn at (n-1)*0x800, NVSn at 0x8000+(n-1)*0x1000, the 224's ROMn at
// (n-1)*0x800), sorted by base; the set's hash is the SHA-256 of the chips
// in that order, first 16 hex digits, which names its catalog. ROM bytes
// stay in memory; nothing is written. To be replaced by the plugin's ROM
// locator/recognizer once it lands. macOS: CommonCrypto.
#pragma once
#include "../engine.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace lexplug::op::roms {

struct Chip {
    std::string name;
    unsigned base = 0;
    std::vector<uint8_t> bytes;
};
struct RomSet {
    std::vector<Chip> chips;
    int model = 0;   // 0 the 224X/224XL, 1 the original 224
    std::string hash;
};

// chipBase(name): the chip's address, or -1.
inline long chip_base(const std::string &name) {
    std::smatch m;
    std::regex sbc("SBC\\s*(\\d)", std::regex::icase);
    std::regex nvs("NVS\\s*(\\d)", std::regex::icase);
    std::regex rom("ROM\\s*([1-4])(?!\\d)", std::regex::icase);
    if (std::regex_search(name, m, sbc)) {
        return (std::stol(m[1]) - 1) * 0x800;
    }
    if (std::regex_search(name, m, nvs)) {
        return 0x8000 + (std::stol(m[1]) - 1) * 0x1000;
    }
    if (std::regex_search(name, m, rom)) {
        return (std::stol(m[1]) - 1) * 0x800;
    }
    return -1;
}

inline RomSet read_set(const std::string &directory, bool bin_only) {
    RomSet set;
    std::regex bin("\\.BIN$", std::regex::icase);
    std::regex rom("ROM\\s*[1-4](?!\\d)", std::regex::icase);
    std::regex sbcnvs("SBC|NVS", std::regex::icase);
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        std::string name = entry.path().filename().string();
        long base = chip_base(name);
        if (base < 0) {
            continue;
        }
        if (bin_only && !std::regex_search(name, bin)) {
            continue;
        }
        if (std::regex_search(name, rom) && !std::regex_search(name, sbcnvs)) {
            set.model = 1;
        }
        std::ifstream file(entry.path(), std::ios::binary);
        Chip chip;
        chip.name = name;
        chip.base = unsigned(base);
        chip.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        set.chips.push_back(std::move(chip));
    }
    std::stable_sort(set.chips.begin(), set.chips.end(), [](const Chip &a, const Chip &b) { return a.base < b.base; });
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    for (const Chip &chip : set.chips) {
        CC_SHA256_Update(&context, chip.bytes.data(), CC_LONG(chip.bytes.size()));
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    char hex[17];
    for (int i = 0; i < 8; i++) {
        std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    }
    set.hash = hex;
    if (set.chips.empty()) {
        throw std::runtime_error("no ROM chips in " + directory);
    }
    return set;
}

inline void load_into(Engine &engine, const RomSet &set) {
    for (const Chip &chip : set.chips) {
        engine.load(chip.bytes.data(), chip.bytes.size(), chip.base);
    }
}

}  // namespace lexplug::op::roms
