// The easy UI's catalog, in C++ and without JUCE: one firmware set's programs,
// their pages of named sliders, each slider's value table (stored byte ->
// display text), the variation presets, and the toggles its remote offers.
//
// The input is a catalog JSON exactly as the web demo ships it
// (../../../web-demo/page/catalogs/<rom-hash>.json, written by
// page/precompute.mjs). Everything here mirrors page/app.js: program keys
// (keyOf), labels and groups (whereOf and the optgroups), the slider range
// (range), tableText and tableRaw, and the preset lookup of describeProgram.
//
// The plugin's generic parameters slider_1..slider_36 map onto the current
// program's NAMED sliders (not INACTIVE, not empty) in catalog page order,
// then slot order: Program::generic[k-1] is slider_k's (page, slot).
#pragma once
#include "json.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace lexcat {

// Which operator the firmware is driven through (the catalog's "remote").
enum class Remote {
    Larc,      // 224XL: the LARC remote (page/larc.js)
    Panel,     // 224X: the front panel (page/panel.js)
    Panel224,  // the original 224's front panel (page/panel224.js)
};

// The number of generic slider parameters (the most named sliders in any
// program is 34; two spare).
constexpr int kGenericSliders = 36;

// One run of a run-length value table: `text` holds from `raw` on, up to
// the next run's raw.
struct TableRun {
    int raw = 0;
    std::string text;
};

struct Slider {
    std::string name;                 // as the display shows it, spaces kept
    std::vector<TableRun> table;      // empty for INACTIVE sliders
    // Measured, where a stored-byte sidecar exists (catalogs-extra/stored/
    // <hash>.stored.json, tools/measure_stored.cpp; CatalogLibrary::addStored):
    // the byte the firmware stores for each control value 0..255 (a panel's
    // pot reading, a LARC fader value; -1 outside the remote's range; empty:
    // not measured), and its inverse, readingFor's table: the canonical
    // control value for each stored byte 0..255 (-1: no value stores it).
    // Some LARC sliders (the 224XL's delays, NOTE PITCH) store another byte
    // for a fader value reached moving down than moving up: `stored` is then
    // the moving-up table and `storedDown` the moving-down one (empty: the
    // same). approachOf[byte]: the control value to move to first so that
    // the move to readingOf[byte] goes the right way (-1: none needed).
    std::vector<int16_t> stored;
    std::vector<int16_t> storedDown;
    std::vector<int16_t> readingOf;
    std::vector<int16_t> approachOf;
    // A slider the easy UI shows: named and not INACTIVE.
    bool named() const { return !name.empty() && name != "INACTIVE"; }
    bool measured() const { return !stored.empty(); }
    // Whether control value `value` can leave byte `b` (either direction).
    bool stores(int value, int b) const {
        if (value < 0 || size_t(value) >= stored.size()) {
            return false;
        }
        if (stored[size_t(value)] == b) {
            return true;
        }
        return !storedDown.empty() && storedDown[size_t(value)] == b;
    }
};

struct Page {
    int page = 0;                     // the firmware's page number (1-based)
    std::string heading;              // the display's heading line ('' if none)
    std::string label;                // 224X panel only: the page's display label, e.g. "1--"
    int column = -1;                  // the parameter record's column; -1 if the catalog has none
    std::vector<Slider> sliders;      // by slot (0-based)
};

// Where a generic slider parameter points in the current program.
struct SliderRef {
    int pageIndex = 0;                // index into Program::pages
    int page = 0;                     // Page::page (the firmware's page number)
    int slot = 0;                     // slot on that page (0-based)
};

struct Program {
    // LARC programs: bank and program. Panel programs: identity (the button
    // mask); bank comes from the LARC match (224X) or is 0.
    int bank = 0;
    int program = 0;
    int identity = -1;
    std::string name;
    std::string bankName;
    std::string buttons;              // panel programs: e.g. "PROG 1"

    std::string key;                  // app.js keyOf: "B.P" (LARC) or "x<identity hex>" (panel)
    std::string where;                // app.js whereOf: "B1 P1" or the buttons
    std::string group;                // the program menu's group: "1 HALLS" (LARC) or the bank name (panel)
    std::string label;                // the program menu's entry: "CONCERT HALL (B1 P1)"

    std::vector<int> variations;      // e.g. 1..7; the 224 has [1]
    std::vector<Page> pages;
    // Per variation: the display text of every slider (by page index, then
    // slot) and the stored byte of every slider.
    std::map<int, std::vector<std::vector<std::string>>> presets;
    std::map<int, std::vector<std::vector<int>>> raw;

    // The named sliders in page, then slot order: generic[k-1] = slider_k.
    std::vector<SliderRef> generic;

    // share.js: program.raw[variation] || program.raw[1] (null if neither).
    const std::vector<std::vector<int>> *rawFor(int variation) const;
    // app.js describeProgram: the variation whose presets apply
    // (variation if the catalog has presets for it, otherwise 1).
    int presetVariation(int variation) const;
    // Every page has a record column (app.js describeProgram's condition).
    bool allPagesHaveColumns() const;

    const Slider &slider(const SliderRef &ref) const { return pages[size_t(ref.pageIndex)].sliders[size_t(ref.slot)]; }
    // The stored-byte sidecar's per-variation tables: for a variation whose
    // measured sliders differ from variation 1's (the LARC's PREDELAY clamps
    // per variation), every slider of every page with that variation's
    // measurement. measuredSlider: that variation's slider if it has one,
    // else slider(ref).
    std::map<int, std::vector<std::vector<Slider>>> measuredByVariation;
    const Slider &measuredSlider(const SliderRef &ref, int variation) const;
    // The generic index k (1-based) of (page number, slot), or 0 if that slider is not named.
    int genericIndexOf(int page, int slot) const;
};

// A toggle the remote offers. `label` is the firmware/share-link label
// (web/share.js writes it with spaces as underscores); `title` and `tooltip`
// are the web page's checkbox text and title (web/index.html).
struct Toggle {
    std::string label;
    std::string title;
    std::string tooltip;
};

struct Catalog {
    int version = 0;
    std::string rom;                  // the ROM hash (first 8 bytes of SHA-256, hex)
    std::string made;
    Remote remote = Remote::Larc;
    // The firmware's RAM layout when it is not the remote's usual one: "v3.2"
    // for the 224 v3.2 (tools/panel224_layouts.mjs LAYOUT_V32); '' otherwise.
    std::string layout;
    std::vector<Program> programs;

    // Throws JsonError on malformed input.
    static Catalog parse(std::string_view text);

    // The program with this key (app.js keyOf), or nullptr / -1.
    const Program *find(std::string_view key) const;
    int indexOf(std::string_view key) const;

    // app.js range(): LARC fader values 2-254; panel pot values 0-253.
    int rawMin() const;
    int rawMax() const;

    // The toggles this remote offers, in the page's order. The 224X panel
    // offers none; the web page also shows "Output mute" for the LARC, which
    // is not a stored toggle (see muteToggle()).
    std::vector<Toggle> toggles() const;
    bool hasMute() const { return remote == Remote::Larc; }
    static Toggle muteToggle();
};

// Every catalog the plugin knows (the web demo's five), by ROM hash.
struct CatalogLibrary {
    std::vector<Catalog> catalogs;
    // Parse and add one catalog JSON (throws JsonError).
    void add(std::string_view json);
    const Catalog *find(std::string_view rom) const;
    // Attach a stored-byte sidecar (tools/gen_stored_tables.py) to its
    // catalog's sliders; add the catalogs first. Returns the number of
    // sliders attached (0 if no catalog has its hash). Throws JsonError if it
    // is malformed or does not fit the catalog (a named slider it misses, a
    // program or slider the catalog does not have). A slider the sidecar
    // lists as unmeasurable (its stored byte does not follow the control,
    // e.g. the LARC's SIZE: kept elsewhere) stays unmeasured.
    int addStored(std::string_view json);
};

const char *remoteName(Remote remote);   // "larc", "panel", "panel224"

// app.js tableText: the text of the run holding `raw` (empty if raw is
// below the first run, which the JS returns as null; `found` says which).
std::string tableText(const std::vector<TableRun> &table, int raw, bool *found = nullptr);
// app.js tableRaw: a raw value that shows `text`: the middle of its run, or
// the start of the run whose number is nearest; 128 if the table is empty.
int tableRaw(const std::vector<TableRun> &table, std::string_view text, int rangeMax);
// app.js drawPages: where a slider's control sits for a stored byte and the
// text the firmware shows. The byte itself when there is no table or the
// table agrees with the text (or no text is known), else tableRaw(text).
// A measured slider (Slider::measured) sits at readingFor(stored) when no
// text is known or when the text rule's position does not store the byte
// (a catalog table swept coarser than the stored byte changes, as the
// 224X's step-4 tables), and some control value stores it.
int sliderPosition(const Slider &slider, int stored, std::string_view shownText, int rangeMax);

// The control value (pot reading, LARC fader value) that makes the firmware
// store `stored` (a share payload's byte) for a measured slider: the
// position sliderPosition gives for that byte and the text the firmware
// shows for it, when that value stores it, else the lowest value that does;
// -1 if none does. An unmeasured slider: `stored` itself.
int readingFor(const Slider &slider, int stored);
// For a direction-dependent slider (Slider::storedDown), the end of the
// range to move to before readingFor(stored) (after a move to the other
// end: see lexstate::restoreCommands); -1 if none is needed.
int approachFor(const Slider &slider, int stored);

// JS parseFloat: the longest numeric prefix after leading whitespace, NaN if none.
double jsParseFloat(std::string_view text);

}  // namespace lexcat
