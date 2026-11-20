// The LARC operator for the 224XL (v8.21, v8.1A): `createOperator` of
// ../../../web-demo/page/larc.js, line for line, as coroutines. Short
// scripts that press real LARC keys and wait for the firmware's own display
// or RAM to answer. Every constant is the JS's; they came from bugs (key taps
// 30 ms down, 100 ms in all; display stable 0.12 s; record stable 0.3 s; a
// SIZE move rebuilds the program for ~0.4 s).
//
// Not ported: scanCatalog and describeProgramFully (they build a catalog,
// which the plugin loads precomputed; they would need growing containers).
#pragma once
#include "machine.hpp"
#include "task.hpp"
#include "text.hpp"
#include <cstdio>
#include <cstring>

namespace lexplug::op {

namespace larc {
inline constexpr unsigned PROG = 0x21, BANK = 0x22, VAR = 0x26, PAGE_KEY = 0x3e, PARAM = 0x2e, MUTE = 0x36,
                          SECOND = 0x3a;
inline constexpr unsigned DIGIT[10] = {0x3d, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x2d, 0x31, 0x35, 0x39};
inline constexpr unsigned SELECT[6] = {0x23, 0x27, 0x2b, 0x2f, 0x33, 0x37};   // show slider n's name and value
inline constexpr uint16_t PICKUP = 0x3c20;     // per-slot soft-pickup state: 1 live, 4 fader below the value, 2 above
inline constexpr uint16_t PHYSICAL = 0x3c00;   // per-slot last received fader position
inline constexpr uint16_t PAGE = 0x3c32;       // the page the LARC is on, from 0
inline constexpr uint16_t ENTRY = 0x3c11;      // the key awaiting a digit: PROG 1, BANK 2, VAR 6

inline unsigned entry_of(unsigned key) {
    if (key == PROG) {
        return 1;
    }
    if (key == BANK) {
        return 2;
    }
    if (key == VAR) {
        return 6;
    }
    return 0xffff;
}
}  // namespace larc

// A slider's echo after a move: the value text, or none (JS null).
struct Echo {
    bool present = false;
    char value[25] = {0};
};

// readSliders / readPages results (fixed size: 6 sliders, at most 9 pages).
struct SliderReading {
    text::Slider shown;
    uint8_t raw = 0;
};
struct PageReading {
    int page = 0;
    char heading[25] = {0};
    unsigned column = 0;
    SliderReading sliders[6];
};
struct PagesReading {
    int count = 0;
    PageReading pages[9];
};
// sweep's raw -> display table (run-length encoded: 2..254 by 4, then 255).
struct SweepTable {
    int count = 0;
    struct Entry {
        unsigned raw = 0;
        char text[25] = {0};
    } entries[66];
};
// readToggles: -1 unknown, 0 off, 1 on; DYN DECAY, MODE ENH, DECAY OPT.
struct Toggles {
    static constexpr const char *labels[3] = {"DYN DECAY", "MODE ENH", "DECAY OPT"};
    int value[3] = {-1, -1, -1};
};

class LarcOperator {
public:
    explicit LarcOperator(Machine &machine) : m(machine) {}

    struct Current {
        int bank = 1;
        int program = 1;
        int variation = 1;
    };
    Current current;
    int currentPage = 1;

    Machine::Display display() const {
        return m.display();
    }

    // Press and release a key (release = code & ~0x20), leaving the firmware time to act.
    Task<void> tap(unsigned code) {
        double t = m.time();
        m.schedule(t, Input::key, code, 1);
        m.schedule(t + 0.03, Input::key, code, 0);
        co_await m.sleep(0.1);
    }

    // Wait until the display shows a slider line (of slider `name`, if
    // given) that has stayed unchanged for `quiet` seconds. Some parameters,
    // such as SIZE, take ~0.4 s to answer, so a late echo of another slider
    // can arrive after a move: naming the slider rules that out.
    Task<bool> settledSlider(double timeout = 2, double quiet = 0.12, const char *name = nullptr) {
        char wanted[13] = {0};
        bool named = name != nullptr;
        if (named) {
            std::snprintf(wanted, sizeof wanted, "%s", name);
        }
        Machine &machine = m;
        char last[25] = {0};
        bool has_last = false;
        double since = m.time();
        bool ok = co_await m.wait_for(
            [&]() {
                Machine::Display d = machine.display();
                if (!has_last || std::strcmp(d.top, last) != 0) {
                    std::memcpy(last, d.top, sizeof last);
                    has_last = true;
                    since = machine.time();
                    return false;
                }
                if (text::not_a_slider(d.top)) {
                    return false;
                }
                if (named && std::strcmp(text::parse_slider(d.top).name, wanted) != 0) {
                    return false;
                }
                return machine.time() - since >= quiet;
            },
            timeout);
        co_return ok;
    }

    // Where the firmware keeps the parameters: its fader handler (v8.21 at
    // 862E) computes base + 6 * [column] + slot with LDA column; ADD A;
    // MOV H,A; ADD A; ADD H; ADD B; MOV E,A; MVI D,0; LXI H,base.
    // False if this firmware has no such handler (the JS throws).
    bool recordLayout() {
        if (layout_found_) {
            return true;
        }
        static constexpr int pattern[14] = {0x3a, -1, -1, 0x87, 0x67, 0x87, 0x84, 0x80, 0x5f, 0x16, 0x00, 0x21, -1, -1};
        for (unsigned a = 0x8000; a < 0x10000 - 14; a++) {
            bool all = true;
            for (unsigned i = 0; i < 14 && all; i++) {
                if (pattern[i] >= 0 && m.peek(uint16_t(a + i)) != pattern[i]) {
                    all = false;
                }
            }
            if (all) {
                layout_column_ = uint16_t(m.peek(uint16_t(a + 1)) | m.peek(uint16_t(a + 2)) << 8);
                layout_base_ = uint16_t(m.peek(uint16_t(a + 12)) | m.peek(uint16_t(a + 13)) << 8);
                layout_found_ = true;
                return true;
            }
        }
        return false;
    }
    uint16_t recordBase() {
        if (!recordLayout()) {
            fatal("this firmware's parameter record was not found (not a LARC firmware?)");
        }
        return layout_base_;
    }
    // A parameter's stored byte, by record column (a page's column is in the catalog).
    uint8_t stored(unsigned column, unsigned slot) {
        return m.peek(uint16_t(recordBase() + 6 * column + slot));
    }
    unsigned column() {
        recordBase();
        return m.peek(layout_column_);
    }

    // After a load the firmware may go on rebuilding the program for a second
    // or two (SIZE), using the record as scratch: wait until it is still.
    Task<bool> recordSettled() {
        if (!recordLayout()) {
            co_await fail("this firmware's parameter record was not found");
        }
        Machine &machine = m;
        uint16_t base = layout_base_;
        uint8_t last[48] = {0};
        bool has_last = false;
        double since = m.time();
        bool ok = co_await m.wait_for(
            [&]() {
                uint8_t now[48];
                for (unsigned i = 0; i < 48; i++) {
                    now[i] = machine.peek(uint16_t(base + i));
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

    // 2nd F is a shift key: if the key after it is lost, the next BANK means
    // "ENTER LABEL FOR BANK" (bank renaming). Every 2nd F use is verified, and
    // a pending shift is cancelled by pressing 2nd F again.
    Task<void> cancelShift() {
        if (text::shifted(display().top)) {
            co_await tap(larc::SECOND);
            co_await m.sleep(0.1);
        }
    }

    // 2nd F + PAGE ("ALL SLIDERS") makes every slider on the page live AND
    // sets each one to its fader's position, so it is not a harmless
    // activation: unmoved faders sit at 0. Only the raw LARC view uses it.
    Task<bool> activateSliders() {
        co_await settledSlider(2, 0.2);
        for (int attempt = 0; attempt < 5; attempt++) {
            if (attempt) {
                co_await m.sleep(0.5);
            }
            co_await cancelShift();
            co_await tap(larc::SECOND);
            if (!co_await shows_within("SECOND~FUNCTION", 0.8)) {
                continue;
            }
            co_await tap(larc::PAGE_KEY);
            if (co_await shows_within("ALL SLIDERS", 0.8)) {
                co_return true;
            }
        }
        co_await cancelShift();
        co_await fail("could not activate the sliders");
        co_return false;
    }

    // PROG, BANK or VAR, then a digit. A key pressed while the firmware is
    // busy can be lost, and the digit alone would then mean something else
    // (after a lost VAR it selects a program): wait until the firmware is
    // awaiting the digit for this key (ENTRY), retrying the key.
    Task<void> entry(unsigned key, unsigned digit) {
        bool armed = false;
        Machine &machine = m;
        unsigned expected = larc::entry_of(key);
        for (int attempt = 0; attempt < 4 && !armed; attempt++) {
            if (attempt) {
                co_await m.sleep(0.3);
            }
            co_await tap(key);
            armed = co_await m.wait_for([&]() { return machine.peek(larc::ENTRY) == expected; }, 0.4);
        }
        if (!armed) {
            co_await fail("the firmware did not take key 0x%x", key);
        }
        co_await tap(larc::DIGIT[digit]);
    }

    // BANK n: the firmware shows "NAME BANK n", or the current bank if n does not exist.
    Task<bool> selectBank(int bank) {
        co_await entry(larc::BANK, unsigned(bank));
        co_await shows_within("BANK #", 0.3);
        Machine::Display d = display();
        const char *match = text::search(d.top, "BANK #");
        co_return match != nullptr && match[5] - '0' == bank;
    }

    // PROG n: a real program keeps the "PROGRAM n" prompt until it has loaded
    // (up to about a second), then shows "NAME Bn Pm Vv"; a missing one reverts
    // at once to the current program.
    Task<bool> loadProgram(int bank, int program) {
        co_await entry(larc::PROG, unsigned(program));
        // The old program's line can show first: wait for this one (a program
        // that does not exist never appears).
        char target[16];
        std::snprintf(target, sizeof target, "B%d P%d V#", bank, program);
        Machine &machine = m;
        bool seen = co_await m.wait_for([&]() { return text::contains(machine.display().top + 12, target); }, 2.5);
        if (!seen) {
            co_return false;
        }
        Machine::Display d = display();
        const char *match = text::search(d.top + 12, target);
        current = Current{bank, program, match[std::strlen(target) - 1] - '0'};
        currentPage = m.peek(larc::PAGE) + 1;
        co_await recordSettled();   // let the firmware finish compiling
        co_return true;
    }
    Task<bool> selectProgram(int bank, int program) {
        if (current.bank != bank) {
            co_await selectBank(bank);
        }
        co_return co_await loadProgram(bank, program);
    }
    Task<bool> loadVariation(int v) {
        co_await entry(larc::VAR, unsigned(v));
        // The old variation's line can show first: wait for this one.
        char target[16];
        std::snprintf(target, sizeof target, "B%d P%d V%d", current.bank, current.program, v);
        Machine &machine = m;
        bool ok = co_await m.wait_for([&]() { return text::starts_with(machine.display().top + 12, target); }, 2.5);
        if (ok) {
            current.variation = v;
            currentPage = m.peek(larc::PAGE) + 1;
            co_await recordSettled();
        }
        co_return ok;
    }

    // Six sliders of the current page: name and value, each verified.
    Task<void> readSliders(PageReading &out) {
        for (int slot = 0; slot < 6; slot++) {
            bool ok = false;
            for (int attempt = 0; attempt < 3 && !ok; attempt++) {
                co_await tap(larc::SELECT[slot]);
                ok = co_await settledSlider(0.6, 0.1);
            }
            if (!ok) {
                co_await fail("slider %d did not answer: \"%s\"", slot + 1, display().top);
            }
            out.sliders[slot].shown = text::parse_slider(display().top);
            out.sliders[slot].raw = stored(column(), unsigned(slot));
        }
    }
    // Every page (PAGE cycles 1 -> N -> 1), leaving the LARC on page 1. The
    // firmware's own page byte says where it is.
    Task<void> readPages(PagesReading &out) {
        co_await gotoPage(1);
        out.count = 0;
        for (int page = 1; page <= 9; page++) {
            PageReading &reading = out.pages[out.count++];
            reading.page = page;
            text::trimmed(display().bottom, 0, 24, reading.heading, sizeof reading.heading);
            reading.column = column();
            co_await readSliders(reading);
            if (!co_await nextPage()) {
                co_await fail("PAGE did not leave page %d", page);
            }
            if (m.peek(larc::PAGE) == 0) {
                break;   // wrapped round
            }
        }
        currentPage = 1;
    }
    // PAGE, retried if the firmware was busy and dropped it.
    Task<bool> nextPage() {
        unsigned before = m.peek(larc::PAGE);
        Machine &machine = m;
        for (int attempt = 0; attempt < 3; attempt++) {
            co_await tap(larc::PAGE_KEY);
            if (co_await m.wait_for([&]() { return machine.peek(larc::PAGE) != before; }, 0.5)) {
                co_await m.sleep(0.1);
                co_return true;
            }
        }
        co_return false;
    }
    Task<void> gotoPage(int page) {
        for (int i = 0; i < 10 && m.peek(larc::PAGE) + 1 != page; i++) {
            if (!co_await nextPage()) {
                break;
            }
        }
        currentPage = m.peek(larc::PAGE) + 1;
        if (currentPage != page) {
            co_await fail("could not reach page %d", page);
        }
    }

    // Moving a slider on any page: go to that page, take the slot over
    // without changing its value, then send the fader. Soft pickup (87A0)
    // ignores a fader until it matches or crosses the stored value; that
    // cannot be arranged for every slider (faders send 2..254, some values
    // are stored as 0, 1 or 255; SIZE-type pages store elsewhere), and 2nd F
    // + PAGE would move every slider on the page to its fader. So the operator
    // sets the one pickup byte to "live" (1), the state the firmware sets
    // itself after a crossing, and the fader message then goes through the
    // firmware's own handler.
    Task<void> takeOver(unsigned slot) {
        if (m.peek(uint16_t(larc::PICKUP + slot)) == 1) {
            co_return;
        }
        m.poke(uint16_t(larc::PICKUP + slot), 1);
        Machine &machine = m;
        if (!co_await m.wait_for([&]() { return machine.peek(uint16_t(larc::PICKUP + slot)) == 1; }, 0.2)) {
            co_await fail("slider %u did not take the pickup", slot + 1);
        }
    }
    // Returns the firmware's echo, or none. A fader message equal to the
    // last one is ignored by the firmware: step next to it first.
    Task<Echo> moveSlider(int page, unsigned slot, unsigned value) {
        co_await gotoPage(page);
        co_await takeOver(slot);
        if (m.peek(uint16_t(larc::PHYSICAL + slot)) == value) {
            unsigned beside = value + 1;
            if (value == 254) {
                beside = 253;
            }
            m.fader(slot, beside);
            co_await m.sleep(0.07);
        }
        m.fader(slot, value);
        co_await m.sleep(0.07);
        Echo echo;
        if (co_await settledSlider(1)) {
            echo.present = true;
            std::memcpy(echo.value, text::parse_slider(display().top).value, sizeof echo.value);
        }
        co_return echo;
    }

    // A raw -> display table for one slider: step the fader 2..254 by 4 and
    // run-length encode the settled display text. The other sliders keep
    // their values.
    Task<void> sweep(int page, unsigned slot, const char *name, SweepTable &table) {
        char wanted[13];
        std::snprintf(wanted, sizeof wanted, "%s", name);
        co_await moveSlider(page, slot, 2);
        table.count = 0;
        unsigned raw = 2;
        while (raw <= 254) {
            if (raw != 2) {
                m.fader(slot, raw);
            }
            co_await m.sleep(0.07);
            // A late echo of another slider, or a busy firmware, can leave the
            // display elsewhere: show this slider again (its select key) and retry.
            bool shown = false;
            if (raw != 2) {
                shown = co_await settledSlider(1, 0.12, wanted);
            }
            for (int attempt = 0; attempt < 3 && !shown; attempt++) {
                co_await tap(larc::SELECT[slot]);
                shown = co_await settledSlider(1, 0.12, wanted);
            }
            if (!shown) {
                co_await fail("sweep at raw %u: no settled display of %s: \"%s\"", raw, wanted, display().top);
            }
            text::Slider parsed = text::parse_slider(display().top);
            if (table.count == 0 || std::strcmp(table.entries[table.count - 1].text, parsed.value) != 0) {
                table.entries[table.count].raw = raw;
                std::memcpy(table.entries[table.count].text, parsed.value, sizeof parsed.value);
                table.count++;
            }
            if (raw == 254) {
                raw = 255;
            } else {
                raw = raw + 4;
                if (raw > 254) {
                    raw = 254;
                }
            }
        }
    }

    // PARAM cycles DYN DECAY / MODE ENH / DECAY OPT; a digit sets the one shown.
    Task<Toggles> readToggles() {
        Toggles state;
        for (int i = 0; i < 3; i++) {
            co_await tap(larc::PARAM);
            Machine::Display d = display();
            for (int k = 0; k < 3; k++) {
                char pattern[24];
                std::snprintf(pattern, sizeof pattern, "%s~[#]", Toggles::labels[k]);
                const char *match = text::search(d.top, pattern);
                if (match != nullptr) {
                    const char *digit = std::strchr(match + std::strlen(Toggles::labels[k]), '[') + 1;
                    state.value[k] = 0;
                    if (*digit == '1') {
                        state.value[k] = 1;
                    }
                }
            }
        }
        co_await tap(larc::SELECT[0]);   // leave the PARAM display
        co_return state;
    }
    Task<Toggles> setToggle(int which, bool on) {
        for (int i = 0; i < 4 && !text::starts_with(display().top, Toggles::labels[which]); i++) {
            co_await tap(larc::PARAM);
        }
        unsigned digit = 0;
        if (on) {
            digit = 1;
        }
        co_await tap(larc::DIGIT[digit]);
        co_await m.sleep(0.1);
        co_return co_await readToggles();
    }
    Task<bool> toggleMute() {
        co_await tap(larc::MUTE);
        co_await shows_within("MUTE", 0.3);
        Machine::Display d = display();
        co_return text::contains(d.top, "ACTIVATED") && !text::contains(d.top, "DEACTIVATED");
    }

    // waitFor(shows(pattern), seconds)
    Task<bool> shows_within(const char *pattern, double seconds) {
        Machine &machine = m;
        co_return co_await m.wait_for([&]() { return text::contains(machine.display().top, pattern); }, seconds);
    }

private:
    Machine &m;
    bool layout_found_ = false;
    uint16_t layout_column_ = 0;
    uint16_t layout_base_ = 0;
};

}  // namespace lexplug::op
