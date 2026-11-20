// What a machine plugin provides to the kit, JUCE-free. One kit::Machine is one booted unit: the
// emulator behind a rate bridge, with its own control/audio hand-offs.
//
// Threads (kit::Host keeps to these):
//   control  boot (MachineType::boot), prepare(), set_params(), readouts(), presets(), state().
//            Never two at once (Host holds one mutex for all of them), never on the audio thread.
//            set_params() may run concurrently with process(): the machine hands its results to
//            the audio side itself (kit::Mailbox), and may take milliseconds (firmware runs).
//   audio    process(). Never blocks, allocates or prints. Before the first prepare() it is not
//            called.
// prepare() is never concurrent with process().
#pragma once
#include "panel.hpp"
#include "roms.hpp"
#include <memory>
#include <string>
#include <vector>

namespace kit {

struct Param {
    enum Kind { Int, Choice, Bool, Float };
    std::string id, name;
    Kind kind = Int;
    float min = 0, max = 1, def = 0;
    std::vector<std::string> choices;   // Choice: the names (min = 0, max = size - 1)
    std::string unit;                   // Float/Int: shown after the value when no readout
    std::string help;                   // the editor's tooltip: what the control does (describe())
};

// Tooltips by parameter id, after the table: describe(i.params, {{"decay", "..."}, ...}).
inline void describe(std::vector<Param>& params, std::initializer_list<std::pair<const char*, const char*>> help) {
    for (auto& [id, text] : help)
        for (auto& p : params)
            if (p.id == id) p.help = text;
}

struct Preset {
    std::string name;
    std::vector<float> values;   // plain values for the first values.size() params, in table order
};

// What the machine shows besides its parameters, refreshed after each set_params().
struct Readouts {
    // per parameter, in table order: the machine's own text for the value (e.g. the firmware's
    // display digits), "" = the parameter does nothing in this setting
    std::vector<std::string> param;
    std::string panel;    // the front panel's display as text, or ""
    std::string status;   // a line for the user (program, warnings), or ""
};

class Machine {
public:
    virtual ~Machine() = default;

    // ---- control thread ----
    // Allocates. host_rate in Hz, max_block the host's largest block.
    virtual void prepare(int host_rate, int max_block) = 0;
    // All parameter values (plain), in table order; called with the full set whenever any changes,
    // and once before the machine is first heard.
    virtual void set_params(const std::vector<float>& values) = 0;
    // Host samples from input to output (rounded); valid after prepare().
    virtual long latency() const = 0;
    virtual Readouts readouts() const { return {}; }
    // true if the readouts change on their own (a machine set by typing keys shows the result only
    // once the firmware has taken them): the host then refreshes them every ~50 ms, not only after
    // set_params().
    virtual bool live_readouts() const { return false; }
    // Factory presets known once booted (e.g. read from the firmware's own table).
    virtual std::vector<Preset> presets() const { return {}; }
    // Machine state for the session beyond the parameters (e.g. battery RAM), handed back to boot.
    virtual std::string state() const { return {}; }

    // ---- hardware panel (panel.hpp; only for a machine whose Info::panel has elements) ----
    // A panel key went down or up (PanelElement::id of a Key). Holds and chords are the caller's:
    // every down gets its up, in any order, any number held at once. The machine queues it for
    // whichever thread runs the emulation and keeps a press down long enough for the firmware's
    // scan to see it, however short the click. Control thread (Host's worker).
    virtual void panel_key(int /*id*/, bool /*down*/) {}
    // A panel pot or switch that is not a host parameter (PanelInput::id), in its min..max.
    // Called with every input's value once after boot, then on each change. Control thread.
    virtual void panel_input(int /*id*/, float /*value*/) {}
    // What the panel shows now. Control thread; the Host's worker polls it ~30 times a second.
    virtual PanelState panel() const { return {}; }

    // ---- audio thread ----
    // in: Info::inputs channel pointers (the host's channels mixed down when inputs = 1).
    // in and out may alias.
    virtual void process(const float* const* in, float* out_l, float* out_r, int n) = 0;
};

struct Info {
    std::string name;          // shown in the editor
    std::string state_tag;     // root of the saved state; never change it after release
    std::string settings_name; // folder/file for remembered ROM paths (Application Support)
    std::string rom_env;       // environment variable naming a ROM folder, e.g. "DN780_ROMPATH"
    int inputs = 2;            // 1 = the host's input mixed to mono
    double tail_seconds = 5;
    std::vector<Param> params;
    std::vector<RomSpec> roms;
    std::string about;         // one or two lines for the editor (what it is, non-affiliation)
    PanelLayout panel;         // the front panel for hardware mode; no elements = easy mode only
};

struct MachineType {
    Info info;
    // A booted machine, or nullptr and *err. `state` is from a saved session (Machine::state), or "".
    std::unique_ptr<Machine> (*boot)(const RomSet& roms, const std::string& state, std::string* err) = nullptr;
};

}  // namespace kit
