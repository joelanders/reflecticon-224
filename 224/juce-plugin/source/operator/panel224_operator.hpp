// The original Lexicon 224's front-panel operator: createPanel224Operator of
// ../../../web-demo/page/panel224.js, line for line, as coroutines,
// with every RAM address taken from a layout (panel224_layouts.hpp) as
// ../../tools/panel224_layouts.mjs does, so one operator drives v4.x (224
// v4.3, v4.4, TEST) and v3.2. Every constant is the JS's; they came from
// bugs (a held button waits up to 0.5 s for the scan; program state still
// for 0.3 s; display settled 0.1-0.12 s).
//
// How the v4.x firmware reads the head (from the v4.3 code; see panel224.js
// for the full account):
//   01EC  the scan loop; pots POTS+0..5, banks SCAN+0..2 (active low).
//   0203  pot hysteresis: a fall is taken as read, a rise of 1 or 2 ignored,
//         a bigger rise recorded as reading - 2.
//   0406  bank 1: IMED, SET, CALL (MODE = 2), SHIFT (a latch: toggles the
//         SHIFT LED), REG A-D.
//   030B  bank 0: PROG 1-6 load that factory program (PROGRAM bits 0-5 = its
//         button); PROG 7, 8 toggle PROGRAM bits 6, 7. Ignored while
//         the SHIFT LED is lit or SHIFT is held.
//   051F  bank 2 (display select), while held: SHOWN picks what the digits show.
//   099F  soft pickup: a pot takes over its parameter when its quantized
//         reading reaches or crosses the stored value (pickup bit 7 set).
// The 224 has no variations: "variation 1" is the program loaded again.
// Pages: 1 = the six pots; 2 ("SHIFT", v4 only) = DIFFUSION on the DEPTH pot.
//
// Not ported: scanCatalog and describeProgramFully (they build a catalog,
// which the plugin loads precomputed), and unexplainedLoadDifferences (a
// check against a catalog's value tables; the soak test has it,
// tests/soak_panel224/panel224_soak.cpp). readPages and sweep are ported.
//
// Composition as in the JS: the digits and LEDs are read by the 224X
// panel's decoder (panel.js display/settled), panel_display.hpp.
#pragma once
#include "larc_operator.hpp"
#include "machine.hpp"
#include "panel_display.hpp"
#include "panel224_layouts.hpp"
#include "task.hpp"
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace lexplug::op {

namespace panel224 {
inline constexpr unsigned CALL = 0x04, SHIFT = 0x08;
inline constexpr unsigned PROG7 = 0x40, PROG8 = 0x80;
inline constexpr unsigned DEPTH_SLOT = 4;
inline constexpr unsigned PREDELAY_SLOT = 5;
}  // namespace panel224

using Panel224Display = panel::Display;

// A displayed value ("2.6 SEC").
struct PanelText {
    char text[25] = {0};
};

// readPages: at most 2 pages of 6 pots.
struct Panel224Pages {
    int count = 0;
    struct Page {
        int page = 0;
        const char *heading = "";
        unsigned column = 0;
        struct Slider {
            const char *name = "INACTIVE";
            char value[25] = {0};
            uint8_t raw = 0;
        } sliders[6];
    } pages[2];
};

// sweep: the raw -> display table, run-length encoded (pot 0..253, step 1 or 4).
struct Panel224Table {
    int count = 0;
    struct Entry {
        unsigned raw = 0;
        char text[25] = {0};
    } entries[254];
};

class Panel224Operator {
public:
    Panel224Operator(Machine &machine, const Panel224Layout &layout) : m(machine), L(layout) {}

    // The layout this operator drives.
    const Panel224Layout &layout() const {
        return L;
    }

    struct Current {
        unsigned identity = 0;
        int variation = 1;
    };
    Current current;
    int currentPage = 1;

    Panel224Display display() const {
        return panel::display(m);
    }
    Task<bool> settled(double timeout = 2, double quiet = 0.12, int slot = -1) {
        return panel::settled(m, timeout, quiet, slot);
    }

    // Pages the layout has: 1, or 2 with the SHIFT page (DIFFUSION).
    int pageCount() const {
        if (L.has_diffusion) {
            return 2;
        }
        return 1;
    }
    // The parameter a page's pot edits, or null (INACTIVE).
    const Panel224Parameter *parameter(int page, unsigned slot) const {
        if (slot >= 6) {
            return nullptr;
        }
        if (page == 1) {
            return &L.pots[slot];
        }
        if (page == 2 && L.has_diffusion && slot == panel224::DEPTH_SLOT) {
            return &L.diffusion;
        }
        return nullptr;
    }
    // A slider's name as the catalog lists it ("INACTIVE" if none).
    const char *sliderName(int page, unsigned slot) const {
        const Panel224Parameter *p = parameter(page, slot);
        if (p == nullptr) {
            return "INACTIVE";
        }
        return p->name;
    }

    // Hold exactly `mask` in `bank` (0 releases), until the scan has seen it.
    Task<bool> hold(unsigned bank, unsigned mask) {
        m.schedule(m.time(), Input::button, bank, mask);
        Machine &machine = m;
        uint16_t address = uint16_t(L.SCAN + bank);
        uint8_t wanted = uint8_t(~mask & 0xff);
        co_return co_await m.wait_for([&]() { return machine.peek(address) == wanted; }, 0.5);
    }
    bool shiftLit() const {
        return (m.peek(L.LEDS) & panel224::SHIFT) != 0;
    }

    Task<bool> press(unsigned bank, unsigned mask) {
        bool seen = co_await hold(bank, mask);
        co_await hold(bank, 0);
        co_return seen;
    }
    // SHIFT is a latch (04FD): pressed alone it toggles the SHIFT LED, and a
    // lit SHIFT LED makes the PROGRAM buttons do nothing.
    Task<bool> cancelShift() {
        for (unsigned bank = 0; bank < 3; bank++) {
            co_await hold(bank, 0);
        }
        if (shiftLit()) {
            co_await press(1, panel224::SHIFT);
        }
        co_return !shiftLit();
    }

    // As panel.js: the scan (0203) takes any fall as it is, ignores a rise
    // of 1 or 2 and records a bigger rise as the reading minus 2.
    Task<bool> setPot(unsigned slot, unsigned value) {
        if (value > 253) {
            value = 253;
        }
        uint16_t address = uint16_t(L.POTS + slot);
        unsigned recorded = m.peek(address);
        if (recorded == value) {
            co_return true;
        }
        unsigned reading = value + 2;
        if (value < recorded) {
            reading = value;
        }
        m.schedule(m.time(), Input::pot, slot, reading);
        Machine &machine = m;
        co_return co_await m.wait_for([&]() { return machine.peek(address) == value; }, 0.5);
    }

    // CALL mode (MODE = 2), then the program's button.
    Task<bool> loadProgram(unsigned identity) {
        Machine &machine = m;
        const Panel224Layout &layout = L;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (attempt) {
                co_await m.sleep(0.5);
            }
            if (!co_await cancelShift()) {
                continue;
            }
            if (m.peek(L.MODE) != 2) {
                co_await press(1, panel224::CALL);
                if (!co_await m.wait_for([&]() { return machine.peek(layout.MODE) == 2; }, 0.5)) {
                    continue;
                }
            }
            co_await press(0, identity);
            if (co_await m.wait_for([&]() { return (machine.peek(layout.PROGRAM) & 0x3f) == identity; }, 1)) {
                current = Current{identity, 1};
                currentPage = 1;
                co_await recordSettled();
                co_return true;
            }
        }
        co_return false;
    }
    Task<bool> selectProgram(unsigned identity) {
        return loadProgram(identity);
    }
    // The 224 has no variations: "variation 1" is the program itself, loaded
    // again (its factory values, as after any load).
    Task<bool> loadVariation(int v) {
        if (v != 1) {
            co_return false;
        }
        co_return co_await loadProgram(current.identity);
    }
    Task<void> gotoPage(int page) {
        currentPage = page;
        co_return;
    }

    // A parameter's stored byte (column 0: the pots, 1: the SHIFT page).
    uint8_t stored(unsigned column, unsigned slot) const {
        const Panel224Parameter *p = parameter(int(column) + 1, slot);
        if (p == nullptr) {
            return 0;
        }
        return m.peek(p->cell);
    }
    // Wait until the program state (PROGRAM and the 11 bytes after it) is
    // still: a load compiles.
    Task<bool> recordSettled() {
        Machine &machine = m;
        uint16_t base = L.PROGRAM;
        uint8_t last[12] = {0};
        bool has_last = false;
        double since = m.time();
        bool ok = co_await m.wait_for(
            [&]() {
                uint8_t now[12];
                for (unsigned i = 0; i < 12; i++) {
                    now[i] = machine.peek(uint16_t(base + i));
                }
                if (!has_last || std::memcmp(now, last, 12) != 0) {
                    std::memcpy(last, now, 12);
                    has_last = true;
                    since = machine.time();
                    return false;
                }
                return machine.time() - since >= 0.3;
            },
            4);
        co_return ok;
    }

    // Show a parameter: hold its select button (with SHIFT for page 2), run
    // `during` (an unstarted task) while the digits show it, then release,
    // whether or not it failed (the JS try/finally).
    template <typename T>
    Task<T> showing(int page, unsigned slot, Task<T> during) {
        if (page == 2) {
            if (!co_await hold(1, panel224::SHIFT)) {
                co_await fail("SHIFT was not seen");
            }
        }
        if (!co_await hold(2, 1u << slot)) {
            co_await fail("select %u was not seen", slot + 1);
        }
        static constexpr uint8_t selects[6] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20};
        uint8_t shown = selects[slot];
        if (page == 2) {
            shown = 0x40;
        }
        Machine &machine = m;
        uint16_t address = L.SHOWN;
        if (!co_await m.wait_for([&]() { return machine.peek(address) == shown; }, 0.5)) {
            co_await fail("the display did not select %u", slot + 1);
        }
        Result<T> r = co_await caught(std::move(during));
        co_await hold(2, 0);
        if (page == 2) {
            co_await hold(1, 0);
        }
        co_await cancelShift();
        if (!r.ok) {
            co_await fail("%s", r.error.text);
        }
        if constexpr (!std::is_void_v<T>) {
            co_return r.value;
        }
    }

    Task<PanelText> readValue(int page, unsigned slot) {
        co_return co_await showing(page, slot, readShown(slot));
    }
    Task<void> readPages(Panel224Pages &out) {
        out.count = 0;
        for (int page = 1; page <= pageCount(); page++) {
            Panel224Pages::Page &reading = out.pages[out.count++];
            reading.page = page;
            reading.heading = "";
            if (page == 2) {
                reading.heading = "SHIFT";
            }
            reading.column = unsigned(page - 1);
            for (unsigned slot = 0; slot < 6; slot++) {
                Panel224Pages::Page::Slider &s = reading.sliders[slot];
                s.name = sliderName(page, slot);
                if (parameter(page, slot) == nullptr) {
                    s.value[0] = 0;
                    s.raw = 0;
                    continue;
                }
                PanelText value = co_await readValue(page, slot);
                std::memcpy(s.value, value.text, sizeof s.value);
                s.raw = stored(reading.column, slot);
            }
        }
    }

    // Take a parameter over without changing it, then move its pot: set its
    // pickup byte's bit 7, the state the firmware reaches itself when the pot
    // crosses the stored value (099F), so no other parameter moves.
    Task<void> takeOver(int page, unsigned slot) {
        const Panel224Parameter *p = parameter(page, slot);
        if (p == nullptr) {
            co_await fail("page %d pot %u is not a parameter", page, slot + 1);
        }
        if (!(m.peek(p->pickup) & 0x80)) {
            m.poke(p->pickup, uint8_t(m.peek(p->pickup) | 0x80));
            Machine &machine = m;
            uint16_t pickup = p->pickup;
            if (!co_await m.wait_for([&]() { return (machine.peek(pickup) & 0x80) != 0; }, 0.2)) {
                co_await fail("%s did not take the pickup", p->name);
            }
        }
    }
    // A pot already reading `value` has to move for the scan to report it.
    // Returns the parameter's displayed value afterwards, or none if the pot
    // did not take the value.
    Task<Echo> moveSlider(int page, unsigned slot, unsigned value) {
        co_await takeOver(page, slot);
        if (page == 2 && !co_await hold(1, panel224::SHIFT)) {
            co_await fail("SHIFT was not seen");
        }
        // (try/finally in the JS; nothing inside can fail.)
        unsigned clamped = value;
        if (clamped > 253) {
            clamped = 253;
        }
        if (m.peek(uint16_t(L.POTS + slot)) == clamped) {
            unsigned nudge = value + 1;
            if (value >= 253) {
                nudge = 252;
            }
            co_await setPot(slot, nudge);
        }
        bool taken = co_await setPot(slot, value);
        if (page == 2) {
            co_await hold(1, 0);
        }
        co_await cancelShift();
        Echo echo;
        if (!taken) {
            co_return echo;
        }
        co_await recordSettled();
        PanelText shown = co_await readValue(page, slot);
        echo.present = true;
        std::memcpy(echo.value, shown.text, sizeof echo.value);
        co_return echo;
    }

    // A raw -> display table: the pot stepped 0..253 with its parameter
    // shown, run-length encoding the settled display text. The decay,
    // crossover and treble pots quantize by 8 and diffusion by 4, so steps of
    // 4 see every value; DEPTH (x 72/256) and PRE-DELAY (1 or 2 raw per
    // millisecond) are stepped by 1.
    Task<void> sweep(int page, unsigned slot, Panel224Table &table) {
        unsigned step = 4;
        if (page == 1 && (slot == panel224::DEPTH_SLOT || slot == panel224::PREDELAY_SLOT)) {
            step = 1;
        }
        co_await moveSlider(page, slot, 0);
        co_await showing(page, slot, sweepShown(slot, step, table));
    }

    // PROGRAM 7 and 8: Mode Enhancement and Decay Optimization (PROGRAM bits
    // 6, 7). Toggles::value[0] (DYN DECAY) stays -1: the 224 has none. A
    // layout without toggles reads all -1.
    Task<Toggles> readToggles() {
        Toggles state;
        if (L.has_toggles) {
            uint8_t p = m.peek(L.PROGRAM);
            state.value[1] = 0;
            if (p & 0x40) {
                state.value[1] = 1;
            }
            state.value[2] = 0;
            if (p & 0x80) {
                state.value[2] = 1;
            }
        }
        co_return state;
    }
    // which: 1 MODE ENH, 2 DECAY OPT (Toggles::labels); anything else only reads.
    Task<Toggles> setToggle(int which, bool on) {
        unsigned bit = 0;
        if (L.has_toggles && which == 1) {
            bit = 0x40;
        }
        if (L.has_toggles && which == 2) {
            bit = 0x80;
        }
        if (bit != 0 && ((m.peek(L.PROGRAM) & bit) != 0) != on) {
            co_await cancelShift();
            unsigned button = panel224::PROG8;
            if (bit == 0x40) {
                button = panel224::PROG7;
            }
            co_await press(0, button);
            Machine &machine = m;
            uint16_t address = L.PROGRAM;
            co_await m.wait_for([&]() { return ((machine.peek(address) & bit) != 0) == on; }, 1);
        }
        co_return co_await readToggles();
    }

private:
    // readValue's `during`: the shown parameter's settled display.
    Task<PanelText> readShown(unsigned slot) {
        if (!co_await settled(0.6, 0.1, int(slot))) {
            co_await fail("parameter %u did not settle", slot + 1);
        }
        PanelText out;
        std::snprintf(out.text, sizeof out.text, "%s", display().top);
        co_return out;
    }
    // sweep's `during`.
    Task<void> sweepShown(unsigned slot, unsigned step, Panel224Table &table) {
        table.count = 0;
        unsigned raw = 0;
        while (true) {
            if (!co_await setPot(slot, raw)) {
                co_await fail("pot %u did not take %u", slot + 1, raw);
            }
            if (!co_await settled(2, 0.12, int(slot))) {
                co_await fail("sweep at raw %u: no settled display of pot %u", raw, slot + 1);
            }
            Panel224Display d = display();
            if (table.count == 0 || std::strcmp(table.entries[table.count - 1].text, d.top) != 0) {
                table.entries[table.count].raw = raw;
                std::snprintf(table.entries[table.count].text, sizeof table.entries[0].text, "%s", d.top);
                table.count++;
            }
            if (raw == 253) {
                break;
            }
            raw = raw + step;
            if (raw > 253) {
                raw = 253;
            }
        }
    }

    Machine &m;
    const Panel224Layout &L;
};

}  // namespace lexplug::op
