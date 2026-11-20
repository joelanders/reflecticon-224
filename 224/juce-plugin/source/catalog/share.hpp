// Share-link payloads (web/share.js), which are also the plugin's saved
// state: the same string opens the same settings in the web demo and in the
// plugin.
//
//   fw=eb3a7a765a703ece&p=1.1&v=2&s=1.0.200,2.4.30&t=DYN_DECAY-1,MODE_ENH-0
//
// fw is the ROM hash, p the program key (app.js keyOf), v the variation, s
// the sliders whose stored byte differs from the variation's preset
// (page.slot.byte, page = the firmware's page number, slot 0-based), t the
// toggles (label with spaces as underscores, 1/0). The web writes this as the
// query of a URL; the plugin stores the query without the '?'.
#pragma once
#include "catalog.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lexcat {

struct ShareMove {
    int page = 0;
    int slot = 0;
    int value = 0;
};

struct ShareToggle {
    std::string label;   // with spaces, e.g. "DYN DECAY"
    bool on = false;
};

struct ShareState {
    std::string fw;
    std::string key;
    int variation = 1;
    std::vector<ShareMove> moves;
    std::vector<ShareToggle> toggles;   // in link order
};

// share.js shareLink, without `${origin}${pathname}?`. `stored(column, slot)`
// reads a parameter's stored byte (the firmware's RAM); `toggles` in the
// order the page lists them (app.js TOGGLES order, only those the remote has).
std::string sharePayload(const std::string &fw, const Program &program, int variation,
                         const std::function<int(int column, int slot)> &stored,
                         const std::vector<ShareToggle> &toggles);

// The same, from the current stored bytes by page index then slot (the
// shape of Program::raw[v]).
std::string sharePayload(const std::string &fw, const Program &program, int variation,
                         const std::vector<std::vector<int>> &storedByPage,
                         const std::vector<ShareToggle> &toggles);

// Format a state directly (moves and toggles as given).
std::string sharePayload(const ShareState &state);

// share.js readShareLink: accepts the payload, "?payload", or a whole URL
// (the part after '?', up to '#'). Returns false if it is not a share link
// (no fw or no p). The JS turns a malformed number into NaN; here a move
// with a part that is not a whole number is dropped and described in
// `problems`, and a malformed variation reads as 1.
bool readSharePayload(std::string_view text, ShareState &out, std::vector<std::string> *problems = nullptr);

// JS Number(string) (whitespace-trimmed; '' is 0; NaN if malformed).
double jsNumber(std::string_view text);

}  // namespace lexcat
