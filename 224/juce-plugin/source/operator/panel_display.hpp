// The front panel's digits and LEDs, read as the page reads them: the
// `lit`/`display`/`settled` part of createPanelOperator in
// ../../../web-demo/page/panel.js, which panel224.js also reuses ("the
// digits and LEDs read the same way"). Shared by panel_operator.hpp (224X)
// and the 224's operator, so either can include it alone.
//
// Engine::panel_digits() is the host's 9 LED bytes, active low (lex_panel_digits):
//   bytes 0-2   the three 7-segment digits (digit 2 is the leftmost);
//               bit 0 = segment a ... bit 6 = g, bit 7 = DP
//   byte 5      unit LEDs, bits 0-3: SEC, MS, Hz, KHz
//   bytes 6, 7  select LEDs, bits 5-7: parameters 1-3 and 4-6
#pragma once
#include "machine.hpp"
#include "task.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lexplug::op::panel {

// Seven-segment patterns (bit 0 = segment a ... bit 6 = g, bit 7 = DP): the
// character, or 0 for a pattern not in the JS table (shown as '?').
inline char segment_char(uint8_t pattern) {
    switch (pattern) {
    case 0x3f: return '0';
    case 0x06: return '1';
    case 0x5b: return '2';
    case 0x4f: return '3';
    case 0x66: return '4';
    case 0x6d: return '5';
    case 0x7d: return '6';
    case 0x07: return '7';
    case 0x7f: return '8';
    case 0x6f: return '9';
    case 0x00: return ' ';
    case 0x40: return '-';
    case 0x77: return 'A';
    case 0x7c: return 'b';
    case 0x39: return 'C';
    case 0x58: return 'c';
    case 0x5e: return 'd';
    case 0x79: return 'E';
    case 0x71: return 'F';
    case 0x3d: return 'G';
    case 0x76: return 'H';
    case 0x74: return 'h';
    case 0x38: return 'L';
    case 0x54: return 'n';
    case 0x5c: return 'o';
    case 0x73: return 'P';
    case 0x50: return 'r';
    case 0x78: return 't';
    case 0x3e: return 'U';
    case 0x1c: return 'u';
    case 0x6e: return 'y';
    default: return '?';
    }
}

// LED byte 5, bits 0-3: [short name, as the LARC would spell it].
inline constexpr const char *UNITS[4][2] = {{"SEC", "SEC"}, {"MS", "MSEC"}, {"Hz", "HZ"}, {"KHz", "KHZ"}};

// display(): { top, bottom: '', digits, selected }.
struct Display {
    char top[16] = {0};      // "2.6 SEC", "720 HZ", "33"
    char bottom[1] = {0};    // the panel has no second line
    char digits[8] = {0};    // the digits alone, trimmed
    int selected[6] = {0};   // the parameters whose select LED is lit, in order
    int selected_count = 0;
};

// The digits (digit 2 is the leftmost) and the unit LED, as the LARC would
// spell them. `raw` is the 9 active-low LED bytes.
inline Display decode(const uint8_t *raw) {
    uint8_t d[9];
    for (int i = 0; i < 9; i++) {
        d[i] = uint8_t(~raw[i] & 0xff);
    }
    char text[8];
    int k = 0;
    const int order[3] = {2, 1, 0};
    for (int i : order) {
        text[k++] = segment_char(uint8_t(d[i] & 0x7f));
        if (d[i] & 0x80) {
            text[k++] = '.';
        }
    }
    text[k] = 0;
    // .trim(): the table's only whitespace is ' '.
    int from = 0;
    int to = k;
    while (from < to && text[from] == ' ') {
        from++;
    }
    while (to > from && text[to - 1] == ' ') {
        to--;
    }
    Display out;
    std::memcpy(out.digits, text + from, std::size_t(to - from));
    out.digits[to - from] = 0;
    int unit = -1;
    for (int bit = 0; bit < 4 && unit < 0; bit++) {
        if ((d[5] >> bit) & 1) {
            unit = bit;
        }
    }
    if (unit >= 0) {
        std::snprintf(out.top, sizeof out.top, "%s %s", out.digits, UNITS[unit][1]);
    } else {
        std::snprintf(out.top, sizeof out.top, "%s", out.digits);
    }
    // The lit select LEDs (bits 5-7 of LED bytes 6 and 7) say whose value it is.
    for (int p = 0; p < 6; p++) {
        int byte = 6;
        if (p >= 3) {
            byte = 7;
        }
        if ((d[byte] >> (5 + p % 3)) & 1) {
            out.selected[out.selected_count++] = p;
        }
    }
    return out;
}

inline Display display(Machine &m) {
    return decode(m.engine().panel_digits());
}

// settled(timeout = 2, quiet = 0.12, slot = null): wait until the display has
// stayed unchanged for `quiet` seconds (and, given `slot` >= 0, shows that
// parameter: only its select LED lit).
inline Task<bool> settled(Machine &m, double timeout = 2, double quiet = 0.12, int slot = -1) {
    char last[sizeof(Display::top)] = {0};
    bool has_last = false;
    double since = m.time();
    bool ok = co_await m.wait_for(
        [&]() {
            Display d = display(m);
            if (!has_last || std::strcmp(d.top, last) != 0) {
                std::memcpy(last, d.top, sizeof last);
                has_last = true;
                since = m.time();
                return false;
            }
            bool shows = slot < 0 || (d.selected_count == 1 && d.selected[0] == slot);
            return shows && m.time() - since >= quiet;
        },
        timeout);
    co_return ok;
}

}  // namespace lexplug::op::panel
