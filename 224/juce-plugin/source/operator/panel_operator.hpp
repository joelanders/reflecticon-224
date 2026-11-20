// The 224X front-panel operator (v8.1): `createPanelOperator` of
// ../../../web-demo/page/panel.js, line for line, as coroutines: the
// control head's counterpart of the LARC operator (larc_operator.hpp), with
// the same shape. Every constant is the JS's.
//
// The v8.1 control head (224X service manual, table 3.2) has three 7-segment
// digits, unit and selection LEDs, six slide pots and three button banks:
//   bank 0  PROGRAM 1-8
//   bank 1  IMED, SET, CALL, SHIFT, REG A-D
//   bank 2  display select: BASS, MID, CROSS-OVER, TREBLE DECAY, DEPTH, PRE-DELAY
// What the firmware does with them (read from the v8.1 code):
//   CALL, then PROGRAM buttons pressed together   load the factory program whose
//                                                 identity is that button mask (8204)
//   PROGRAM n alone                                variation n of the current program
//   IMED                                          next page of six parameters (83FC)
//   a select button, or moving a pot              show that parameter on the digits
// A parameter's value is the pot's 8-bit reading, clamped to a per-parameter
// limit (860C). After a load or a page change a pot is under soft pickup: it
// takes over once it reaches or crosses the stored value (8D3F). The scan
// records pot readings with hysteresis (0A0E, see setPot).
//
// Not ported: scanCatalog and describeProgramFully (they build a catalog,
// which the plugin loads precomputed), nameFromLarc and panelCatalogProblems
// (catalog building). readPages and sweep, which only they use, are ported.
#pragma once
#include "larc_operator.hpp"   // Echo, PageReading, PagesReading, SweepTable
#include "machine.hpp"
#include "panel_display.hpp"
#include "task.hpp"
#include "text.hpp"
#include <cstdio>
#include <cstring>

namespace lexplug::op {

namespace panel {
inline constexpr uint16_t SCAN = 0x3c06;        // the firmware's copy of banks 0-2, active low (FF = nothing held)
inline constexpr uint16_t POTS = 0x3c00;        // its last accepted reading of each pot
inline constexpr uint16_t MODE = 0x3c43;        // bit 2: CALL pending
inline constexpr uint16_t PAGE = 0x3c50;        // current page, from 0
inline constexpr uint16_t COLUMN = 0x3c52;      // the record column the pots edit on this page (a page can skip one)
inline constexpr uint16_t VARIATION = 0x3c58;   // the loaded variation, as its PROGRAM button mask
inline constexpr uint16_t RECORD = 0x3c9a;      // working record: identity, then 6 parameter bytes per column
inline constexpr uint16_t LIVE = 0x3c0a;        // per parameter (6 per column): bit 7 = the pot has picked it up
inline constexpr uint16_t DIRECTORY = 0xa000;   // factory records: 18 slots of 0x2AA bytes, first byte = identity
inline constexpr unsigned CALL = 0x04, IMED = 0x01;
inline constexpr const char *POT_NAMES[6] = {"BASS", "MID", "CROSS-OVER", "TREBLE DECAY", "DEPTH", "PRE-DELAY"};

// Programs by button mask: "PROG 1+3".
inline void buttons_for(unsigned identity, char *out, std::size_t n) {
    int k = std::snprintf(out, n, "PROG ");
    bool first = true;
    for (unsigned b = 0; b < 8; b++) {
        if ((identity >> b) & 1) {
            if (!first) {
                k += std::snprintf(out + k, n - std::size_t(k), "+");
            }
            k += std::snprintf(out + k, n - std::size_t(k), "%u", b + 1);
            first = false;
        }
    }
}
}  // namespace panel

class PanelOperator {
public:
    static constexpr const char *kind = "panel";

    explicit PanelOperator(Machine &machine) : m(machine) {}

    struct Current {
        unsigned identity = 0;
        int variation = 1;
    };
    Current current;
    int currentPage = 1;

    panel::Display display() {
        return panel::display(m);
    }

    // Hold buttons until the firmware's scan has seen them, then release them
    // and wait until it has seen that too.
    Task<bool> press(unsigned bank, unsigned mask) {
        Machine &machine = m;
        m.schedule(m.time(), Input::button, bank, mask);
        bool seen = co_await m.wait_for(
            [&]() { return machine.peek(uint16_t(panel::SCAN + bank)) == (~mask & 0xff); }, 0.5);
        m.schedule(m.time(), Input::button, bank, 0);
        co_await m.wait_for([&]() { return scanIdle(bank); }, 0.5);
        co_return seen;
    }
    // Wait until the display has stayed unchanged for `quiet` seconds (and,
    // given `slot` >= 0, shows that parameter: only its select LED lit).
    Task<bool> settled(double timeout = 2, double quiet = 0.12, int slot = -1) {
        return panel::settled(m, timeout, quiet, slot);
    }

    // Move a pot so that the firmware records exactly `value`. The scan
    // (0A0E) takes any fall as it is, ignores a rise of 1 or 2, and records a
    // bigger rise as the reading minus 2: so come down onto the value, or rise
    // to value + 2. Rising, the highest value a pot can record is 253.
    Task<bool> setPot(unsigned slot, unsigned value) {
        if (value > 253) {
            value = 253;
        }
        unsigned recorded = m.peek(uint16_t(panel::POTS + slot));
        if (recorded == value) {
            co_return true;
        }
        unsigned reading = value + 2;
        if (value < recorded) {
            reading = value;
        }
        m.schedule(m.time(), Input::pot, slot, reading);
        Machine &machine = m;
        co_return co_await m.wait_for([&]() { return machine.peek(uint16_t(panel::POTS + slot)) == value; }, 0.5);
    }

    // CALL, then the program's buttons together.
    Task<bool> loadProgram(unsigned identity) {
        Machine &machine = m;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (attempt) {
                co_await m.sleep(0.5);
            }
            co_await press(1, panel::CALL);
            if (!(m.peek(panel::MODE) & 4)) {
                continue;
            }
            co_await press(0, identity);
            if (co_await m.wait_for(
                    [&]() {
                        return machine.peek(panel::RECORD) == identity && machine.peek(panel::VARIATION) == 1;
                    },
                    1)) {
                current = Current{identity, 1};
                currentPage = m.peek(panel::PAGE) + 1;
                co_await recordSettled();
                co_return true;
            }
        }
        co_return false;
    }
    Task<bool> selectProgram(unsigned identity) {
        return loadProgram(identity);
    }
    // PROGRAM n: a missing variation leaves the current one loaded.
    Task<bool> loadVariation(int v) {
        unsigned mask = 1u << (v - 1);
        co_await press(0, mask);
        Machine &machine = m;
        unsigned identity = current.identity;
        bool ok = co_await m.wait_for(
            [&]() { return machine.peek(panel::VARIATION) == mask && machine.peek(panel::RECORD) == identity; }, 1);
        if (ok) {
            current.variation = v;
            currentPage = m.peek(panel::PAGE) + 1;
            co_await recordSettled();
        }
        co_return ok;
    }

    Task<void> gotoPage(int page) {
        Machine &machine = m;
        for (int i = 0; i < 9 && m.peek(panel::PAGE) + 1 != page; i++) {
            unsigned before = m.peek(panel::PAGE);
            co_await press(1, panel::IMED);
            co_await m.wait_for([&]() { return machine.peek(panel::PAGE) != before; }, 0.5);
        }
        currentPage = m.peek(panel::PAGE) + 1;
        if (currentPage != page) {
            co_await fail("could not reach page %d", page);
        }
    }
    // A parameter's stored byte, by record column (a page's column is in the catalog).
    uint8_t stored(unsigned column, unsigned slot) {
        return m.peek(uint16_t(panel::RECORD + 1 + 6 * column + slot));
    }
    // After a load the firmware may go on rebuilding the program for a while:
    // wait until the working record is still.
    Task<bool> recordSettled() {
        Machine &machine = m;
        uint8_t last[48] = {0};
        bool has_last = false;
        double since = m.time();
        bool ok = co_await m.wait_for(
            [&]() {
                uint8_t now[48];
                for (unsigned i = 0; i < 48; i++) {
                    now[i] = machine.peek(uint16_t(panel::RECORD + i));
                }
                if (!has_last || std::memcmp(now, last, 48) != 0) {
                    std::memcpy(last, now, 48);
                    has_last = true;
                    since = machine.time();
                    return false;
                }
                return machine.time() - since >= 0.3;
            },
            4);
        co_return ok;
    }
    // The six parameters of the current page: raw byte and displayed value.
    Task<void> readSliders(PageReading &out) {
        uint16_t base = uint16_t(panel::RECORD + 1 + 6 * m.peek(panel::COLUMN));
        for (int slot = 0; slot < 6; slot++) {
            co_await press(2, 1u << slot);
            if (!co_await settled(0.6, 0.1, slot)) {
                co_await fail("parameter %d did not settle", slot + 1);
            }
            SliderReading &reading = out.sliders[slot];
            reading.shown = text::Slider{};
            std::snprintf(reading.shown.name, sizeof reading.shown.name, "%s", panel::POT_NAMES[slot]);
            std::snprintf(reading.shown.value, sizeof reading.shown.value, "%s", display().top);
            reading.raw = m.peek(uint16_t(base + slot));
        }
    }
    // Every page (IMED cycles 1 -> N -> 1), leaving the panel on page 1. IMED
    // shows the new page's label on the digits ("2dd", "3LE", ... "1--").
    Task<void> readPages(PagesReading &out) {
        co_await gotoPage(1);
        out.count = 0;
        char labels[10][8] = {};
        Machine &machine = m;
        for (int page = 1; page <= 8; page++) {
            currentPage = page;
            PageReading &reading = out.pages[out.count++];
            reading = PageReading{};
            reading.page = page;
            reading.column = m.peek(panel::COLUMN);
            co_await readSliders(reading);
            co_await press(1, panel::IMED);
            unsigned left = unsigned(page - 1);
            co_await m.wait_for([&]() { return machine.peek(panel::PAGE) != left; }, 0.5);
            co_await settled(0.5, 0.05);
            std::snprintf(labels[m.peek(panel::PAGE) + 1], sizeof labels[0], "%s", display().digits);
            if (m.peek(panel::PAGE) == 0) {
                break;   // wrapped round
            }
        }
        currentPage = m.peek(panel::PAGE) + 1;
        for (int i = 0; i < out.count; i++) {
            std::snprintf(out.pages[i].heading, sizeof out.pages[i].heading, "%s", labels[out.pages[i].page]);
        }
    }

    // Take over a parameter without changing it, then move its pot. Soft
    // pickup (8D3F) ignores a pot until it reaches or crosses the stored
    // value; the operator sets the parameter's pickup bit (bit 7), the state
    // the firmware sets itself after a crossing, so no pot but this one moves.
    // A pot already reading `value` has to move for the firmware to look.
    // Returns the settled display ("2.6 SEC"), or none (JS null).
    Task<Echo> moveSlider(int page, unsigned slot, unsigned value) {
        co_await gotoPage(page);
        Machine &machine = m;
        uint16_t index = uint16_t(6 * m.peek(panel::COLUMN) + slot);
        auto live = [&]() { return (machine.peek(uint16_t(panel::LIVE + index)) & 0x80) != 0; };
        if (!live()) {
            m.poke(uint16_t(panel::LIVE + index), uint8_t(m.peek(uint16_t(panel::LIVE + index)) | 0x80));
            if (!co_await m.wait_for(live, 0.2)) {
                co_await fail("page %d pot %u did not take the pickup", page, slot + 1);
            }
        }
        unsigned clamped = value;
        if (clamped > 253) {
            clamped = 253;
        }
        if (m.peek(uint16_t(panel::POTS + slot)) == clamped) {
            unsigned beside = value + 1;
            if (value >= 253) {
                beside = 252;
            }
            co_await setPot(slot, beside);
        }
        Echo echo;
        if (!co_await setPot(slot, value)) {
            co_return echo;
        }
        if (co_await settled(1)) {
            echo.present = true;
            std::snprintf(echo.value, sizeof echo.value, "%s", display().top);
        }
        co_return echo;
    }
    // A raw -> display table: step the pot 0..253 by 4, run-length encoding
    // the settled display text.
    Task<void> sweep(int page, unsigned slot, SweepTable &table) {
        co_await moveSlider(page, slot, 0);
        table.count = 0;
        unsigned raw = 0;
        while (raw <= 253) {
            if (!co_await setPot(slot, raw)) {
                co_await fail("pot %u did not take %u", slot + 1, raw);
            }
            if (raw == 0) {
                co_await press(2, 1u << slot);   // show it: the pot may not have moved
            }
            if (!co_await settled(2, 0.12, int(slot))) {
                co_await fail("sweep at raw %u: no settled display of pot %u", raw, slot + 1);
            }
            panel::Display d = display();
            if (table.count == 0 || std::strcmp(table.entries[table.count - 1].text, d.top) != 0) {
                table.entries[table.count].raw = raw;
                std::snprintf(table.entries[table.count].text, sizeof table.entries[0].text, "%s", d.top);
                table.count++;
            }
            if (raw == 252) {
                raw = 253;
            } else {
                raw = raw + 4;
            }
        }
    }

private:
    bool scanIdle(unsigned bank) {
        return m.peek(uint16_t(panel::SCAN + bank)) == 0xff;
    }

    Machine &m;
};

}  // namespace lexplug::op
