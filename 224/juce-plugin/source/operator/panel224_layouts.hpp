// The original 224's RAM layouts for the front-panel operator
// (panel224_operator.hpp): LAYOUT_V4 and LAYOUT_V32 of
// ../../tools/panel224_layouts.mjs, which read them from the ROMs.
//
//   v4    224 v4.3, v4.4 and the "P4.A" 224 TEST set; the addresses of
//         ../../../web-demo/page/panel224.js (PANEL224, POTS, DIFFUSION).
//   v3.2  224 v3.2: the same scan and dispatch 0x20 lower, one page (no
//         DIFFUSION parameter).
// A catalog says which one it was made with: "layout": "v3.2", else v4.
#pragma once
#include <cstdint>
#include <cstring>

namespace lexplug::op {

struct Panel224Parameter {
    const char *name = nullptr;
    uint16_t cell = 0;     // the stored byte
    uint16_t pickup = 0;   // its pickup byte (bit 7: the pot is live)
};

struct Panel224Layout {
    const char *name = nullptr;
    uint16_t POTS = 0;      // the scan's last accepted reading of each pot
    uint16_t SCAN = 0;      // banks 6-8 (PROGRAM, IMED..REG D, display select), active low
    uint16_t LEDS = 0;      // LED byte 4: IMED 1, SET 2, CALL 4, SHIFT 8, REG A-D 10-80
    uint16_t SHOWN = 0;     // what the digits show (the held select)
    uint16_t MODE = 0;      // 0 IMED, 1 SET, 2 CALL
    uint16_t PROGRAM = 0;   // bits 0-5 the program's button, 6 Mode Enhancement, 7 Decay Optimization
    Panel224Parameter pots[6];
    bool has_diffusion = false;       // page 2 ("SHIFT"): DIFFUSION in the DEPTH pot's place
    Panel224Parameter diffusion;
    // PROGRAM 7 and 8 toggle bits 6 and 7 of PROGRAM (v4.3 030B; a load
    // sets both). v3.2 does the same at its PROGRAM (3F44): measured on the
    // row machine 2026-09-27 (a load leaves C4 for PROG 3; PROG 7 -> 84,
    // PROG 8 -> 04, PROG 7 -> 44, PROG 8 -> C4, as v4.3), not read from the
    // v3.2 code. The layouts.mjs operator has no toggles; the JS
    // reference for them is tests/operator_equiv/panel224_ops.mjs.
    bool has_toggles = false;
};

inline constexpr Panel224Layout LAYOUT_V4 = {
    "v4",
    0x3f20,
    0x3f26,
    0x3f3f,
    0x3f44,
    0x3f45,
    0x3f65,
    {
        {"BASS", 0x3f67, 0x3f2b},
        {"MID", 0x3f66, 0x3f2a},
        {"CROSSOVER", 0x3f68, 0x3f2c},
        {"TREBLE DECAY", 0x3f69, 0x3f2d},
        {"DEPTH", 0x3f6a, 0x3f2e},
        {"PRE-DELAY", 0x3f6d, 0x3f2f},
    },
    true,
    {"DIFFUSION", 0x3f6e, 0x3f30},
    true,
};

inline constexpr Panel224Layout LAYOUT_V32 = {
    "v3.2",
    0x3f00,
    0x3f06,
    0x3f1f,
    0x3f24,
    0x3f25,
    0x3f44,
    {
        {"BASS", 0x3f46, 0x3f0b},
        {"MID", 0x3f45, 0x3f0a},
        {"CROSSOVER", 0x3f47, 0x3f0c},
        {"TREBLE DECAY", 0x3f48, 0x3f0d},
        {"DEPTH", 0x3f49, 0x3f0e},
        {"PRE-DELAY", 0x3f4c, 0x3f0f},
    },
    false,
    {},
    true,
};

// The layout for a catalog's "layout" field (absent or empty: v4).
inline const Panel224Layout &panel224_layout_named(const char *name) {
    if (name != nullptr && std::strcmp(name, "v3.2") == 0) {
        return LAYOUT_V32;
    }
    return LAYOUT_V4;
}

}  // namespace lexplug::op
