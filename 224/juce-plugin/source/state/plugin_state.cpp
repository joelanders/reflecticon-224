#include "plugin_state.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#if !defined(__APPLE__)
#include <locale.h>
#else
#include <xlocale.h>
#endif

namespace lexstate {

namespace {

// Numbers are written and read in the "C" locale whatever the host set
// (a host in a comma-decimal locale must not change the blob). The MSVC
// CRT has the same calls with an underscore, the locale after the format.
#ifdef _WIN32
_locale_t cLocale() {
    static _locale_t locale = _create_locale(LC_ALL, "C");
    return locale;
}
int snprintf_l(char *text, size_t size, _locale_t locale, const char *format, double value) {
    return _snprintf_l(text, size, format, locale, value);
}
float strtof_l(const char *text, char **end, _locale_t locale) {
    return _strtof_l(text, end, locale);
}
#elif defined(__APPLE__)
locale_t cLocale() {
    static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}
#else
// glibc has no snprintf_l/strtof_l: switch this thread to the "C" locale
// around each call instead. uselocale() is thread-local, so a host's locale
// on its other threads is untouched.
locale_t cLocale() {
    static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}

class ScopedCLocale {
public:
    ScopedCLocale() : previous_(uselocale(cLocale())) {}
    ~ScopedCLocale() { uselocale(previous_); }

    ScopedCLocale(const ScopedCLocale &) = delete;
    ScopedCLocale &operator=(const ScopedCLocale &) = delete;

private:
    locale_t previous_;
};

int snprintf_l(char *text, size_t size, locale_t, const char *format, double value) {
    ScopedCLocale guard;
    return std::snprintf(text, size, format, value);
}
#endif

std::string formatFloat(float value) {
    char text[48];
    snprintf_l(text, sizeof text, cLocale(), "%.9g", double(value));
    return text;
}

bool parseFloat(const std::string &text, float &out) {
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    float value = strtof_l(text.c_str(), &end, cLocale());
    if (end != text.c_str() + text.size() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool parseInt(const std::string &text, int &out) {
    if (text.empty() || text.size() > 9) {
        return false;
    }
    int value = 0;
    size_t i = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        i = 1;
    }
    if (i == text.size()) {
        return false;
    }
    for (; i < text.size(); i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10 + (text[i] - '0');
    }
    if (negative) {
        value = -value;
    }
    out = value;
    return true;
}

const char *kDacNames[4] = {"A", "B", "C", "D"};

bool parseDac(const std::string &text, int &out) {
    for (int i = 0; i < 4; i++) {
        if (text == kDacNames[i]) {
            out = i;
            return true;
        }
    }
    return false;
}

bool knownKey(std::string_view key) {
    return key == "rom" || key == "share" || key == "level_db" || key == "dry_wet" || key == "output_left" ||
           key == "output_right" || key == "analog";
}

// A value may not hold a line break (it would split the line on reading).
std::string oneLine(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c != '\n' && c != '\r') {
            out += c;
        }
    }
    return out;
}

}  // namespace

std::string serialise(const PluginState &state) {
    std::string out = std::string(kMagic) + " " + std::to_string(kStateVersion) + "\n";
    out += "rom " + oneLine(state.rom) + "\n";
    out += "share " + oneLine(state.share) + "\n";
    out += "level_db " + formatFloat(state.levelDb) + "\n";
    out += "dry_wet " + formatFloat(state.dryWet) + "\n";
    int left = state.outLeft;
    int right = state.outRight;
    if (left < 0 || left > 3) {
        left = 0;
    }
    if (right < 0 || right > 3) {
        right = 2;
    }
    out += std::string("output_left ") + kDacNames[left] + "\n";
    out += std::string("output_right ") + kDacNames[right] + "\n";
    if (state.analog) {
        out += "analog 1\n";
    } else {
        out += "analog 0\n";
    }
    for (const auto &[key, value] : state.extra) {
        if (key.empty() || knownKey(key) || key.find(' ') != std::string::npos) {
            continue;
        }
        out += oneLine(key) + " " + oneLine(value) + "\n";
    }
    return out;
}

bool parse(std::string_view blob, PluginState &out, std::string *error, std::vector<std::string> *problems) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= blob.size()) {
        size_t end = blob.find('\n', start);
        if (end == std::string_view::npos) {
            end = blob.size();
        }
        std::string line(blob.substr(start, end - start));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        start = end + 1;
    }
    std::string magic = std::string(kMagic) + " ";
    if (lines.empty() || lines[0].compare(0, magic.size(), magic) != 0) {
        if (error != nullptr) {
            *error = "not a plugin state (no '" + std::string(kMagic) + "' line)";
        }
        return false;
    }
    int version = 0;
    if (!parseInt(lines[0].substr(magic.size()), version) || version < 1) {
        if (error != nullptr) {
            *error = "bad state version: " + lines[0];
        }
        return false;
    }
    PluginState state;
    state.version = version;
    auto problem = [&](const std::string &what) {
        if (problems != nullptr) {
            problems->push_back(what);
        }
    };
    for (size_t i = 1; i < lines.size(); i++) {
        const std::string &line = lines[i];
        if (line.empty()) {
            continue;
        }
        size_t space = line.find(' ');
        std::string key = line.substr(0, space);
        std::string value;
        if (space != std::string::npos) {
            value = line.substr(space + 1);
        }
        if (key == "rom") {
            state.rom = value;
        } else if (key == "share") {
            state.share = value;
        } else if (key == "level_db") {
            float db = 0.0f;
            if (parseFloat(value, db) && db >= lexparams::kLevelMinDb && db <= lexparams::kLevelMaxDb) {
                state.levelDb = db;
            } else {
                problem("level_db: bad value '" + value + "'");
            }
        } else if (key == "dry_wet") {
            float wet = 0.0f;
            if (parseFloat(value, wet) && wet >= 0.0f && wet <= 1.0f) {
                state.dryWet = wet;
            } else {
                problem("dry_wet: bad value '" + value + "'");
            }
        } else if (key == "output_left") {
            if (!parseDac(value, state.outLeft)) {
                problem("output_left: bad value '" + value + "'");
            }
        } else if (key == "output_right") {
            if (!parseDac(value, state.outRight)) {
                problem("output_right: bad value '" + value + "'");
            }
        } else if (key == "analog") {
            if (value == "1") {
                state.analog = true;
            } else if (value == "0") {
                state.analog = false;
            } else {
                problem("analog: bad value '" + value + "'");
            }
        } else {
            state.extra.emplace_back(key, value);
        }
    }
    out = std::move(state);
    return true;
}

std::string shareFromMachine(const lexcat::Catalog &catalog, const lexparams::MachineState &machine) {
    if (machine.program < 0 || size_t(machine.program) >= catalog.programs.size()) {
        return "";
    }
    const lexcat::Program &program = catalog.programs[size_t(machine.program)];
    if (!program.allPagesHaveColumns()) {
        return "";
    }
    // Stored bytes by page index and slot: the named sliders from the
    // machine; the others are never written (sharePayload skips them).
    std::vector<std::vector<int>> stored;
    for (const lexcat::Page &page : program.pages) {
        stored.emplace_back(page.sliders.size(), 0);
    }
    for (size_t k = 1; k <= program.generic.size() && k <= size_t(lexparams::kSliders); k++) {
        const lexcat::SliderRef &ref = program.generic[k - 1];
        stored[size_t(ref.pageIndex)][size_t(ref.slot)] = machine.stored[k - 1];
    }
    std::vector<lexcat::ShareToggle> toggles;
    for (const lexcat::Toggle &toggle : catalog.toggles()) {
        for (int t = 0; t < lexparams::kToggles; t++) {
            if (toggle.label == lexparams::kToggleLabels[t] && (machine.togglesKnown & (1u << t)) != 0) {
                toggles.push_back({toggle.label, (machine.toggles & (1u << t)) != 0});
            }
        }
    }
    int variation = machine.variation;
    if (variation < 1) {
        variation = 1;
    }
    return lexcat::sharePayload(catalog.rom, program, variation, stored, toggles);
}

bool expectedStored(const lexcat::Catalog &catalog, const lexcat::ShareState &share,
                    std::vector<std::vector<int>> &out) {
    const lexcat::Program *program = catalog.find(share.key);
    if (program == nullptr) {
        return false;
    }
    const std::vector<std::vector<int>> *preset = program->rawFor(share.variation);
    if (preset == nullptr) {
        return false;
    }
    out = *preset;
    for (const lexcat::ShareMove &move : share.moves) {
        for (size_t i = 0; i < program->pages.size(); i++) {
            if (program->pages[i].page == move.page && i < out.size() && size_t(move.slot) < out[i].size()) {
                out[i][size_t(move.slot)] = move.value;
            }
        }
    }
    return true;
}

bool restoreCommands(const lexcat::Catalog &catalog, const lexcat::ShareState &share,
                     std::vector<lexparams::Command> &out, std::vector<std::string> *problems, int *unreachable,
                     std::vector<std::vector<lexparams::Command>> *approaches) {
    using namespace lexparams;
    if (unreachable != nullptr) {
        *unreachable = 0;
    }
    int index = catalog.indexOf(share.key);
    if (index < 0) {
        if (problems != nullptr) {
            problems->push_back("the program " + share.key + " is not in this firmware");
        }
        return false;
    }
    if (index >= kProgramSlots) {
        if (problems != nullptr) {
            problems->push_back("program index past the program parameter's range");
        }
        return false;
    }
    const lexcat::Program &program = catalog.programs[size_t(index)];
    SliderRange range{catalog.rawMin(), catalog.rawMax()};
    auto add = [&](int p, float value) {
        Command command;
        command.param = uint16_t(p);
        command.value = value;
        out.push_back(command);
    };
    add(kProgram, fromInt(kProgram, index, range));
    add(kVariation, fromInt(kVariation, share.variation, range));
    for (const lexcat::ShareMove &move : share.moves) {
        int k = program.genericIndexOf(move.page, move.slot);
        if (k < 1 || k > kSliders) {
            if (problems != nullptr) {
                problems->push_back("move " + std::to_string(move.page) + "." + std::to_string(move.slot) +
                                    " is not a named slider of " + program.key);
            }
            continue;
        }
        const lexcat::Slider &slider = program.measuredSlider(program.generic[size_t(k - 1)], share.variation);
        int value = lexcat::readingFor(slider, move.value);
        if (value < 0) {
            if (problems != nullptr) {
                problems->push_back("move " + std::to_string(move.page) + "." + std::to_string(move.slot) + "." +
                                    std::to_string(move.value) + ": no pot reading stores that byte (left out)");
            }
            if (unreachable != nullptr) {
                (*unreachable)++;
            }
            continue;
        }
        if (value < range.min || value > range.max) {
            if (problems != nullptr) {
                problems->push_back("move " + std::to_string(move.page) + "." + std::to_string(move.slot) + "." +
                                    std::to_string(move.value) + " is outside the remote's range (clamped)");
            }
        }
        int approach = lexcat::approachFor(slider, move.value);
        if (approach >= 0 && approaches != nullptr) {
            approaches->resize(2);
            int far = range.max;
            if (approach == range.max) {
                far = range.min;
            }
            Command command;
            command.param = uint16_t(sliderParam(k));
            command.value = fromInt(sliderParam(k), far, range);
            (*approaches)[0].push_back(command);
            command.value = fromInt(sliderParam(k), approach, range);
            (*approaches)[1].push_back(command);
        }
        add(sliderParam(k), fromInt(sliderParam(k), value, range));
    }
    for (const lexcat::ShareToggle &toggle : share.toggles) {
        for (int t = 0; t < kToggles; t++) {
            if (toggle.label == kToggleLabels[t]) {
                add(toggleParam(t), fromInt(toggleParam(t), toggle.on, range));
            }
        }
    }
    return true;
}

void directValues(const PluginState &state, float values[lexparams::kParamCount]) {
    using namespace lexparams;
    SliderRange range;
    values[kLevelDb] = levelNormOf(state.levelDb);
    values[kDryWet] = clamp01(state.dryWet);
    values[kOutputLeft] = fromInt(kOutputLeft, state.outLeft, range);
    values[kOutputRight] = fromInt(kOutputRight, state.outRight, range);
    values[kAnalog] = fromInt(kAnalog, state.analog, range);
}

}  // namespace lexstate
