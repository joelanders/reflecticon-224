// The plugin's host parameters, JUCE-free: a fixed list that never depends
// on the program or the firmware set, and the conversions between a
// parameter's normalized host value (0..1) and what the machine uses.
//
// RULES (hosts key automation and saved sessions by ID):
//   - The IDs below are APPEND ONLY. Never rename, reorder or remove one;
//     never reuse an index. A new parameter goes at the end with a new index
//     and kLayoutVersion+1 as its version hint (juce::ParameterID{id, hint}).
//   - The index is the parameter's position in the processor's APVTS layout
//     and in every per-parameter array here (Scheduler, WriteBack).
//
// Every parameter's host value is a plain normalized float 0..1 (a JUCE
// AudioParameterFloat 0..1 with no interval, or a choice/bool whose
// normalization matches the formulas below). The sliders cannot be
// AudioParameterInt: their raw range depends on the firmware set's remote
// (LARC 2..254, panels 0..253; lexcat::Catalog::rawMin/rawMax), which is
// only known once a set is loaded, while a JUCE parameter's range is fixed
// at construction. Their host display text comes from the catalog's value
// table (lexcat::tableText) for the current program's slider_k.
//
// Normalization of discrete parameters (round to nearest, then clamp):
//   program     index i in 0..kProgramSlots-1   <-> i / (kProgramSlots-1)
//   variation   v in 1..8                        <-> (v-1) / 7
//   slider_k    raw in [min, max] of the remote  <-> (raw-min) / (max-min)
//   toggles, analog: off/on                      <-> 0 / 1 (on if >= 0.5)
//   output_left/right: DAC 0..3 = A..D           <-> i / 3
//   level_db    -24..+30 dB (linear in dB)       <-> (dB+24) / 54
//   dry_wet     0..1                             <-> itself
// raw -> normalized -> raw is exact for every raw of both slider ranges
// (tests/params checks it).
//
// When the catalog (the firmware set) changes, a slider's host value keeps
// its normalized value, which now means a different raw byte (e.g. LARC 128
// = 0.5 -> panel 127). That is never sent to the machine as a move: a
// catalog change comes with a new machine, which boots into its own program;
// the write-back (writeback.hpp) then pushes the new machine's values over
// the host's. See Scheduler for why nothing moves by itself.
#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>

namespace lexparams {

inline constexpr int kLayoutVersion = 1;

inline constexpr int kSliders = 36;        // slider_1 .. slider_36 (lexcat::kGenericSliders)
inline constexpr int kProgramSlots = 32;   // program choices "1".."32"; the largest catalog has 22 programs
inline constexpr int kVariations = 8;

// Toggle indices (toggle_* parameters, MachineState::toggles bits). The bit
// values equal lexui::UiToggleBit (1 << index).
enum Toggle : int { kDynDecay = 0, kModeEnh = 1, kDecayOpt = 2, kMute = 3 };
inline constexpr int kToggles = 4;
// The firmware / share-link labels (lexcat::Catalog::toggles() and
// muteToggle()): the 224XL offers all four; the 224 offers MODE ENH and
// DECAY OPT (its PROGRAM 7 and 8); the 224X panel offers none.
inline constexpr const char *kToggleLabels[kToggles] = {"DYN DECAY", "MODE ENH", "DECAY OPT", "MUTE"};

// Parameter indices.
inline constexpr int kProgram = 0;
inline constexpr int kVariation = 1;
inline constexpr int kSlider1 = 2;                      // slider_k is kSlider1 + k - 1
inline constexpr int kToggle0 = kSlider1 + kSliders;    // 38: toggle t is kToggle0 + t
inline constexpr int kLevelDb = kToggle0 + kToggles;    // 42
inline constexpr int kDryWet = 43;
inline constexpr int kOutputLeft = 44;
inline constexpr int kOutputRight = 45;
inline constexpr int kAnalog = 46;
inline constexpr int kParamCount = 47;

static_assert(kLevelDb == 42, "parameter indices are append-only");

enum class Kind : uint8_t {
    Program,     // operator task: load a program (index into the catalog's programs)
    Variation,   // operator task: load a variation
    Slider,      // operator task: move slider_k
    Toggle,      // operator task: set a toggle
    Level,       // direct: input level in dB
    DryWet,      // direct: dry/wet mix
    Output,      // direct: which DAC output feeds a host channel
    Analog,      // direct: analog boards modelled
};

struct ParamInfo {
    const char *id;          // stable string ID (never changes)
    const char *name;        // host-visible name
    Kind kind;
    int sub;                 // slider k (1..36), toggle index, output side (0 = L, 1 = R); else 0
    float defaultNorm;       // default normalized value
    int versionHint;         // juce::ParameterID version hint: the layout version that added it
};

// Operator parameters run as operator tasks (one at a time); the others are
// applied directly (not through the operator).
inline bool isOperatorKind(Kind kind) {
    return kind == Kind::Program || kind == Kind::Variation || kind == Kind::Slider || kind == Kind::Toggle;
}

namespace detail {
#define LEXPARAMS_SLIDER(k) {"slider_" #k, "Slider " #k, Kind::Slider, k, 0.5f, 1}
inline constexpr ParamInfo kTable[kParamCount] = {
    {"program", "Program", Kind::Program, 0, 0.0f, 1},
    {"variation", "Variation", Kind::Variation, 0, 0.0f, 1},
    LEXPARAMS_SLIDER(1),  LEXPARAMS_SLIDER(2),  LEXPARAMS_SLIDER(3),  LEXPARAMS_SLIDER(4),
    LEXPARAMS_SLIDER(5),  LEXPARAMS_SLIDER(6),  LEXPARAMS_SLIDER(7),  LEXPARAMS_SLIDER(8),
    LEXPARAMS_SLIDER(9),  LEXPARAMS_SLIDER(10), LEXPARAMS_SLIDER(11), LEXPARAMS_SLIDER(12),
    LEXPARAMS_SLIDER(13), LEXPARAMS_SLIDER(14), LEXPARAMS_SLIDER(15), LEXPARAMS_SLIDER(16),
    LEXPARAMS_SLIDER(17), LEXPARAMS_SLIDER(18), LEXPARAMS_SLIDER(19), LEXPARAMS_SLIDER(20),
    LEXPARAMS_SLIDER(21), LEXPARAMS_SLIDER(22), LEXPARAMS_SLIDER(23), LEXPARAMS_SLIDER(24),
    LEXPARAMS_SLIDER(25), LEXPARAMS_SLIDER(26), LEXPARAMS_SLIDER(27), LEXPARAMS_SLIDER(28),
    LEXPARAMS_SLIDER(29), LEXPARAMS_SLIDER(30), LEXPARAMS_SLIDER(31), LEXPARAMS_SLIDER(32),
    LEXPARAMS_SLIDER(33), LEXPARAMS_SLIDER(34), LEXPARAMS_SLIDER(35), LEXPARAMS_SLIDER(36),
    {"toggle_dyn_decay", "Dynamic Decay", Kind::Toggle, kDynDecay, 0.0f, 1},
    {"toggle_mode_enh", "Mode Enhancement", Kind::Toggle, kModeEnh, 0.0f, 1},
    {"toggle_decay_opt", "Decay Optimization", Kind::Toggle, kDecayOpt, 0.0f, 1},
    {"toggle_mute", "Output Mute", Kind::Toggle, kMute, 0.0f, 1},
    {"level_db", "Input Level", Kind::Level, 0, 24.0f / 54.0f, 1},
    {"dry_wet", "Dry/Wet", Kind::DryWet, 0, 1.0f, 1},
    {"output_left", "Output L", Kind::Output, 0, 0.0f, 1},
    {"output_right", "Output R", Kind::Output, 1, 2.0f / 3.0f, 1},
    {"analog", "Analog Boards", Kind::Analog, 0, 1.0f, 1},
};
#undef LEXPARAMS_SLIDER
}  // namespace detail

inline const ParamInfo &param(int index) {
    return detail::kTable[index];
}

// The index of a parameter ID, or -1.
inline int indexOf(std::string_view id) {
    for (int i = 0; i < kParamCount; i++) {
        if (id == detail::kTable[i].id) {
            return i;
        }
    }
    return -1;
}

inline int sliderParam(int k) {
    return kSlider1 + k - 1;
}
inline int toggleParam(int toggle) {
    return kToggle0 + toggle;
}

// The current remote's slider range (lexcat::Catalog::rawMin/rawMax).
struct SliderRange {
    int min = 2;
    int max = 254;
    bool operator==(const SliderRange &) const = default;
};
inline constexpr SliderRange kLarcRange{2, 254};
inline constexpr SliderRange kPanelRange{0, 253};

inline float clamp01(float x) {
    if (!(x > 0.0f)) {
        return 0.0f;   // also NaN
    }
    if (x > 1.0f) {
        return 1.0f;
    }
    return x;
}

// Nearest step of `steps` equal steps over 0..1: 0..steps.
inline int stepOf(float norm, int steps) {
    return int(std::floor(double(clamp01(norm)) * double(steps) + 0.5));
}

// --- discrete parameters: normalized <-> integer ---------------------
// The integer is: program index, variation (1..8), slider raw byte, toggle
// or analog 0/1, output DAC 0..3. Level and dry/wet are continuous (below).
inline int toInt(int index, float norm, SliderRange range) {
    switch (param(index).kind) {
        case Kind::Program:
            return stepOf(norm, kProgramSlots - 1);
        case Kind::Variation:
            return 1 + stepOf(norm, kVariations - 1);
        case Kind::Slider:
            return range.min + stepOf(norm, range.max - range.min);
        case Kind::Toggle:
        case Kind::Analog:
            if (norm >= 0.5f) {
                return 1;
            }
            return 0;
        case Kind::Output:
            return stepOf(norm, 3);
        default:
            return 0;
    }
}

inline float fromInt(int index, int value, SliderRange range) {
    switch (param(index).kind) {
        case Kind::Program:
            return clamp01(float(value) / float(kProgramSlots - 1));
        case Kind::Variation:
            return clamp01(float(value - 1) / float(kVariations - 1));
        case Kind::Slider:
            return clamp01(float(value - range.min) / float(range.max - range.min));
        case Kind::Toggle:
        case Kind::Analog:
            if (value != 0) {
                return 1.0f;
            }
            return 0.0f;
        case Kind::Output:
            return clamp01(float(value) / 3.0f);
        default:
            return 0.0f;
    }
}

// --- continuous parameters ---------------------------------------------
inline constexpr float kLevelMinDb = -24.0f;
inline constexpr float kLevelMaxDb = 30.0f;

inline float levelDbOf(float norm) {
    return kLevelMinDb + clamp01(norm) * (kLevelMaxDb - kLevelMinDb);
}
inline float levelNormOf(float db) {
    return clamp01((db - kLevelMinDb) / (kLevelMaxDb - kLevelMinDb));
}

}  // namespace lexparams
