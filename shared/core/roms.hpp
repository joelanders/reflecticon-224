// Bring-your-own ROMs, JUCE-free. A plugin reads the user's files at run time and never stores
// their bytes; a machine's table holds only sizes, SHA-256 hashes and file-name hints.
//
// Each RomSpec is one role (a chip, or a whole firmware image). A file fills a role when its size
// fits and its hash is a known dump; failing that, when its size fits and its name contains one of
// the role's hints (marked "by name": the machine's boot then decides whether it runs).
#pragma once
#include "sha256.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace kit {

struct KnownRom {
    std::string sha256;   // lower-case hex
    std::string label;    // e.g. "v2.20"
};

struct RomSpec {
    std::string role;                     // shown to the user, e.g. "firmware EPROM"
    std::vector<size_t> sizes;            // accepted file sizes
    std::vector<KnownRom> known;          // in order of preference when several are offered
    std::vector<std::string> name_hints;  // case-insensitive substrings of the file name
};

struct RomFile {
    std::string name;    // file name, no directories
    std::string path;    // where it came from (remembered, never the bytes)
    std::vector<uint8_t> bytes;
};

struct RomSet {
    struct Slot {
        bool found = false, known = false;
        std::string label;   // the known dump's label, or the file name
        RomFile file;
    };
    std::vector<Slot> slots;   // one per RomSpec, in table order

    bool complete() const {
        return !slots.empty() && std::all_of(slots.begin(), slots.end(), [](const Slot& s) { return s.found; });
    }
    const std::vector<uint8_t>& bytes(size_t i) const { return slots.at(i).file.bytes; }
    std::string describe(const std::vector<RomSpec>& specs) const {
        std::string s;
        for (size_t i = 0; i < specs.size(); i++) {
            const Slot* sl = i < slots.size() ? &slots[i] : nullptr;
            if (!s.empty()) s += ", ";
            s += specs[i].role + ": ";
            if (!sl || !sl->found) s += "missing";
            else if (sl->known) s += sl->label;
            else s += sl->label + " (not a known dump)";
        }
        return s;
    }
};

inline bool size_fits(const RomSpec& spec, size_t n) {
    return std::find(spec.sizes.begin(), spec.sizes.end(), n) != spec.sizes.end();
}

inline bool any_size_fits(const std::vector<RomSpec>& specs, size_t n) {
    return std::any_of(specs.begin(), specs.end(), [n](const RomSpec& s) { return size_fits(s, n); });
}

inline RomSet recognise(const std::vector<RomSpec>& specs, const std::vector<RomFile>& files) {
    RomSet set;
    set.slots.resize(specs.size());
    std::vector<std::string> hashes;
    for (auto& f : files) hashes.push_back(Sha256::of(f.bytes.data(), f.bytes.size()));
    auto lower = [](std::string s) {
        for (auto& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    };
    // by hash first, preferring dumps earlier in the table, then by name for what is left
    for (size_t r = 0; r < specs.size(); r++)
        for (auto& k : specs[r].known)
            for (size_t i = 0; i < files.size() && !set.slots[r].found; i++)
                if (size_fits(specs[r], files[i].bytes.size()) && k.sha256 == hashes[i])
                    set.slots[r] = {true, true, k.label, files[i]};
    for (size_t r = 0; r < specs.size(); r++)
        for (size_t i = 0; i < files.size() && !set.slots[r].found; i++) {
            if (!size_fits(specs[r], files[i].bytes.size())) continue;
            bool used = std::any_of(set.slots.begin(), set.slots.end(),
                                    [&](const RomSet::Slot& s) { return s.found && s.file.path == files[i].path && s.file.name == files[i].name; });
            if (used) continue;
            std::string n = lower(files[i].name);
            for (auto& h : specs[r].name_hints)
                if (n.find(lower(h)) != std::string::npos) {
                    set.slots[r] = {true, false, files[i].name, files[i]};
                    break;
                }
        }
    return set;
}

}  // namespace kit
