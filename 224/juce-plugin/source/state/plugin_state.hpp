// The plugin's saved state (M6), JUCE-free: getStateInformation stores
// serialise(state) as the blob; setStateInformation parses it. No ROM bytes,
// ever: the firmware set is named by its catalog hash (share.js "fw", the
// RomSet::catalog_hash of source/roms/recognize.hpp), and the machine's
// settings by the share-link payload (source/catalog/share.hpp: the same
// string opens the same settings in the web demo).
//
// Format: UTF-8 text, one "key value" per line, '\n' line ends:
//
//   lexplug-state 1
//   rom eb3a7a765a703ece
//   share fw=eb3a7a765a703ece&p=1.1&v=2&s=1.0.200&t=DYN_DECAY-1,MODE_ENH-0,DECAY_OPT-0
//   level_db 0
//   dry_wet 1
//   output_left A
//   output_right C
//   analog 1
//
//   - The first line is the magic and the format version (kStateVersion).
//     A blob from a newer version is read as far as its keys are known.
//   - Unknown keys are kept, in order, and written again on save, so an
//     older plugin re-saving a newer project does not lose them.
//   - Missing keys take the defaults below; an empty share means "no
//     machine settings" (a fresh instance that never booted).
//   - Floats are written with 9 significant digits (float round trip).
//
// Missing ROMs: restore keeps the parsed state (StateKeeper) until a machine
// has applied it, and saving meanwhile writes the kept rom/share back (with
// the live level/dry-wet/output/analog), so a project saved on a machine
// without the ROM set does not lose its settings.
#pragma once
#include "../catalog/catalog.hpp"
#include "../catalog/share.hpp"
#include "../params/command_queue.hpp"
#include "../params/scheduler.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lexstate {

inline constexpr int kStateVersion = 1;
inline constexpr const char *kMagic = "lexplug-state";

struct PluginState {
    int version = kStateVersion;   // the version the blob was written with
    std::string rom;               // the ROM set's catalog hash ('' = none)
    std::string share;             // the share payload ('' = none)
    float levelDb = 0.0f;          // -24..+30
    float dryWet = 1.0f;           // 0..1
    int outLeft = 0;               // 0..3 = A..D
    int outRight = 2;
    bool analog = true;
    std::vector<std::pair<std::string, std::string>> extra;   // unknown keys, kept

    bool operator==(const PluginState &) const = default;
};

std::string serialise(const PluginState &state);
// False (and `error` says why) if the blob is not a plugin state at all.
// Malformed values of known keys keep their defaults and are described in
// `problems`.
bool parse(std::string_view blob, PluginState &out, std::string *error = nullptr,
           std::vector<std::string> *problems = nullptr);

// --- share payload <-> machine ----------------------------------------

// The payload for what a machine holds (Published::machine; the stored bytes
// by generic index), as share.js shareLink writes it. `toggles` in the page's
// order, only those the remote has (Catalog::toggles(); MUTE never). Empty
// if the program is unknown or has pages without record columns.
std::string shareFromMachine(const lexcat::Catalog &catalog, const lexparams::MachineState &machine);

// The stored bytes a machine holds after restoring `share` (the variation's
// preset with the moves applied), by page index then slot, the shape of
// Program::raw[v]: the round-trip gate's expectation. False if the program
// is not in the catalog or has no preset.
bool expectedStored(const lexcat::Catalog &catalog, const lexcat::ShareState &share,
                    std::vector<std::vector<int>> &out);

// The commands that replay `share` through the scheduler (frame 0: as soon
// as seen), in share.js applyShareLink's order: program, variation, moves,
// toggles (the Scheduler dispatches them in that order anyway).
//
// A move's value is a STORED byte, a slider parameter's value is a control
// value (a LARC fader position, a panel pot reading). On a measured slider
// (the 224's and 224X's panels: catalogs-extra/stored/) the move becomes
// lexcat::readingFor(stored): a pot reading the firmware turns into exactly
// that byte. A stored byte no reading gives is left out and counted in
// `unreachable`. An unmeasured slider takes the byte as the control value.
// (web/share.js applyShareLink sends every stored byte as a pot value.)
//
// A direction-dependent slider (lexcat::approachFor) also needs two moves
// first: to the far end of the range, then to its approach end (the first
// move after a load does not always leave what the tables say; after those
// two it does, as when the tables were measured). `approaches` gets two
// phases, [0] the far ends and [1] the approach ends, each to be replayed
// and finished in turn after the program and variation and before `out`'s
// moves; with `approaches` null such a move is replayed without them.
//
// Moves on sliders that are not named, unreachable bytes, and values
// outside the remote's range are described in `problems` (the latter
// clamped). False if the program is not in the catalog.
bool restoreCommands(const lexcat::Catalog &catalog, const lexcat::ShareState &share,
                     std::vector<lexparams::Command> &out, std::vector<std::string> *problems = nullptr,
                     int *unreachable = nullptr, std::vector<std::vector<lexparams::Command>> *approaches = nullptr);

// The direct parameters' normalized values for a state (level, dry/wet,
// outputs, analog), to set on the processor's parameters after a restore.
void directValues(const PluginState &state, float values[lexparams::kParamCount]);

// Keeps a restored state until a machine has applied it (see the header).
class StateKeeper {
public:
    // setStateInformation: the parsed state is now what the plugin is.
    void restored(PluginState state) {
        held_ = std::move(state);
        holding_ = true;
    }
    // The machine now plays the held state (restore replay finished).
    void applied() {
        holding_ = false;
    }
    bool holding() const {
        return holding_;
    }
    const PluginState &held() const {
        return held_;
    }

    // getStateInformation. `live` has the live direct parameters, and the
    // running machine's rom/share when there is one (else empty). While a
    // restored state is held (no ROMs, or still booting), its rom, share and
    // unknown keys are saved instead of the live ones.
    // The last restored blob's unknown keys are always carried along.
    PluginState forSave(const PluginState &live) const {
        PluginState out = live;
        out.extra = held_.extra;
        if (holding_) {
            out.rom = held_.rom;
            out.share = held_.share;
        }
        return out;
    }

private:
    PluginState held_;
    bool holding_ = false;
};

}  // namespace lexstate
