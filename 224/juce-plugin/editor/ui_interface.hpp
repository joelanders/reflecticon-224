// The editor's two contracts with the rest of the plugin (no JUCE here).
//
// The editor never touches the machine. It
//   - READS a UiSnapshot: a plain, trivially copyable struct the processor
//     publishes (a seqlock or a mutex-guarded copy; the audio thread may
//     write it, the editor copies it on its timer, ~20 Hz), and
//   - SENDS commands through UiCommands, called on the message thread only.
//     The implementation turns them into parameter changes (APVTS, so hosts
//     record automation) and/or operator tasks; it must not block.
//
// Everything the editor shows about programs, pages, slider names, value
// tables and tooltips comes from the catalog model (source/catalog), looked
// up by the snapshot's romHash and program index. The snapshot carries only
// the live state: which set, which program, the stored bytes, the meters.
#pragma once
#include <cstdint>
#include <string>
#include <type_traits>

namespace lexui {

constexpr int kSliders = 36;       // slider_1 .. slider_36 (lexcat::kGenericSliders)
constexpr int kMaxRomSets = 16;    // firmware sets found (firmware_sets.json has 8)
constexpr int kTextLength = 24;    // a display value, e.g. "10.0 SEC", with room to spare

enum class UiStatus : uint8_t {
    MissingRoms,       // no usable ROM set found: running dry (statusText says what is missing)
    Booting,           // a replacement engine is booting / restoring (dry until ready)
    Ready,             // the machine runs; the operator is idle
    LoadingProgram,    // a program or variation load is in progress (sliders belong to the old one)
    Operating,         // another operator task (slider move, toggle) is in progress
    MachineStopped,    // the machine halted (statusText has the reason); running dry
};

// Toggle bits (UiSnapshot::toggles / togglesKnown). The labels are the
// firmware's and the share link's: see lexcat::Catalog::toggles().
enum UiToggleBit : uint8_t {
    kToggleDynDecay = 1,   // "DYN DECAY" (224XL)
    kToggleModeEnh = 2,    // "MODE ENH" (224XL; the 224's PROGRAM 7)
    kToggleDecayOpt = 4,   // "DECAY OPT" (224XL; the 224's PROGRAM 8)
    kToggleMute = 8,       // the LARC's MUTE key (224XL; not in share links)
};

struct UiRomSet {
    char name[64];         // firmware_sets.json name, e.g. "224XL v8.21"
    char hash[17];         // the catalog key: first 8 bytes of SHA-256 over the chips, hex ('' if not computed)
    uint8_t model;         // 0 = 224X/224XL, 1 = the original 224
    bool unsupported;      // listed but not selectable (e.g. 224 v2.2)
};

struct UiSnapshot {
    // Bumped by the publisher on every publish (0 = nothing published yet).
    uint32_t serial;

    // --- Machine and firmware ------------------------------------------
    UiStatus status;
    char statusText[160];              // detail for the status line ('' = none)
    int32_t romSetCount;               // entries used in romSets
    UiRomSet romSets[kMaxRomSets];
    int32_t selectedRomSet;            // index into romSets, -1 = none
    char romHash[17];                  // hash of the running set (catalog key), '' if none
    double machineSeconds;             // machine time since power-on (meters are timed in it)

    // --- Program ---------------------------------------------------------
    int32_t program;                   // index into lexcat::Catalog::programs, -1 = unknown
    int32_t variation;                 // 1..8 (the 224: 1)
    // slider_k's stored byte (k = 1..36 at [k-1]), for the current program's
    // named sliders in catalog page/slot order (Program::generic). Entries
    // past the program's named-slider count are unused.
    uint8_t stored[kSliders];
    // The firmware's display text for slider_k when the operator has read it
    // (a move's echo, or the preset after a load); '' = unknown, the editor
    // then shows the catalog table's text for `stored`.
    char sliderText[kSliders][kTextLength];
    uint8_t toggles;                   // UiToggleBit set = on
    uint8_t togglesKnown;              // UiToggleBit set = the state was read from the firmware

    // --- Audio (the parameter values, mirrored for display) ------------
    float levelDb;                     // input level, -24..+30 dB, default 0
    float dryWet;                      // 0 = dry .. 1 = wet, default 1
    uint8_t outLeft;                   // DAC output for the left channel, 0..3 = A..D (default A)
    uint8_t outRight;                  // for the right channel (default 2 = C)
    bool analog;                       // the AIN/AOUT boards modelled (default on)

    // --- Meters (machine time of the latest event; < 0 = never) ---------
    // Headroom: detector k of channel c (0 = L, 1 = R) exceeded, k = 0..4 is
    // -24, -18, -12, -6, 0 dB below ADC clipping. (The hardware register
    // bit k is CLEAR when exceeded; the publisher records the time instead.)
    double headroomHit[2][5];
    // Gain ranger: gain g of channel c used, g = 0..3 is 0, +6, +12, +18 dB
    // (lex_input_gains bits c*4+g). The editor lights a segment for 0.3 s
    // of machine time after its last event, as the web page does.
    double gainUsed[2][4];
};

static_assert(std::is_trivially_copyable_v<UiSnapshot>, "UiSnapshot must stay a plain struct");

// A snapshot with the defaults above (status Booting, nothing loaded).
inline UiSnapshot defaultSnapshot() {
    UiSnapshot s{};
    s.status = UiStatus::Booting;
    s.selectedRomSet = -1;
    s.program = -1;
    s.variation = 1;
    s.levelDb = 0.0f;
    s.dryWet = 1.0f;
    s.outLeft = 0;
    s.outRight = 2;
    s.analog = true;
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < 5; k++) {
            s.headroomHit[c][k] = -1.0;
        }
        for (int g = 0; g < 4; g++) {
            s.gainUsed[c][g] = -1.0;
        }
    }
    return s;
}

// Which control a gesture is for (hosts group automation by gestures).
enum class UiGesture : uint8_t { Slider, Level, DryWet };

class UiCommands {
public:
    virtual ~UiCommands() = default;

    // --- Machine and firmware ---
    // A folder, .zip or chip file the user chose or dropped (the implementation
    // scans it off the message thread and republishes romSets).
    virtual void addRomLocation(const std::string &path) = 0;
    virtual void selectRomSet(int index) = 0;                 // index into UiSnapshot::romSets

    // --- Program ---
    virtual void selectProgram(int programIndex) = 0;         // index into Catalog::programs
    virtual void selectVariation(int variation) = 0;          // one of Program::variations
    // slider_k (1..36) to a stored byte in [Catalog::rawMin(), rawMax()]. Called
    // on every drag step; the implementation coalesces (latest value wins).
    virtual void moveSlider(int k, int raw) = 0;
    // A toggle by its firmware label ("DYN DECAY", "MODE ENH", "DECAY OPT",
    // or "MUTE" for the LARC's mute key).
    virtual void setToggle(const std::string &label, bool on) = 0;

    // --- Audio ---
    virtual void setLevelDb(float db) = 0;
    virtual void setDryWet(float wet) = 0;                    // 0..1
    virtual void setOutputPair(int left, int right) = 0;      // 0..3 = A..D
    virtual void setAnalog(bool on) = 0;

    // Begin/end of a drag, for host automation gestures (k: slider_k, or 0).
    virtual void gesture(UiGesture which, int k, bool begin) = 0;
};

}  // namespace lexui
