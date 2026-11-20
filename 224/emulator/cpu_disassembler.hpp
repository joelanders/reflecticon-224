#pragma once
// Lens:    the 8080 disassembler, over the SBC's memory. The mnemonics and
//          lengths come from ../../i8080-cycle/i8080dasm.h, generated from
//          the same descriptions as the core, so the listing names each
//          opcode the way the SBC runs it.
//
//   0004  32 23 41  STA 4123
#include "../8080/i8080dasm.h"
#include <cstdint>
#include <cstdio>
#include <string>

namespace lexicon224x::lens {

// `count` instructions from `address`: address, bytes, text, one per line.
inline std::string cpu_listing(const uint8_t memory[65536], uint16_t address, unsigned count) {
    std::string text;
    for (unsigned n = 0; n < count; n++) {
        uint8_t bytes[3];
        for (unsigned i = 0; i < 3; i++) {
            bytes[i] = memory[uint16_t(address + i)];
        }
        char mnemonic[24];
        int length = i8080dasm(bytes, mnemonic, sizeof mnemonic);
        char hex[12] = "";
        for (int i = 0; i < length; i++) {
            std::snprintf(hex + 3 * i, sizeof hex - 3 * i, "%02X ", unsigned(bytes[i]));
        }
        char line[64];
        std::snprintf(line, sizeof line, "%04X  %-9s %s\n", unsigned(address), hex, mnemonic);
        text += line;
        address = uint16_t(address + length);
    }
    return text;
}

}  // namespace lexicon224x::lens
