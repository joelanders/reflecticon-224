#include "catalog.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace lexcat {

namespace {

std::string toHex(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%x", unsigned(value));
    return buffer;
}

std::string stringOr(const Json &j, const char *fallback) {
    if (j.isString()) {
        return j.string();
    }
    return fallback;
}

// A variation number from an object key ("1".."9").
int variationKey(const std::string &key) {
    char *end = nullptr;
    long v = std::strtol(key.c_str(), &end, 10);
    if (end == key.c_str() || *end != '\0') {
        throw JsonError("catalog: variation key '" + key + "' is not a number");
    }
    return int(v);
}

Program readProgram(const Json &p, Remote remote) {
    Program out;
    out.name = stringOr(p["name"], "");
    out.bankName = stringOr(p["bankName"], "");
    out.buttons = stringOr(p["buttons"], "");
    if (p["identity"].isNumber()) {
        out.identity = p["identity"].integer();
    }
    if (p["program"].isNumber()) {
        out.program = p["program"].integer();
    }
    // app.js startEasyUI: bank ?? (larc ? larc.bank : 0)
    if (p["bank"].isNumber()) {
        out.bank = p["bank"].integer();
    } else if (p["larc"].isObject() && p["larc"]["bank"].isNumber()) {
        out.bank = p["larc"]["bank"].integer();
    }

    bool panel = remote != Remote::Larc;
    if (panel) {
        out.key = "x" + toHex(out.identity);
        out.where = out.buttons;   // (every panel catalog entry has its buttons; app.js falls back to buttonsFor)
        out.group = out.bankName;
    } else {
        out.key = std::to_string(out.bank) + "." + std::to_string(out.program);
        out.where = "B" + std::to_string(out.bank) + " P" + std::to_string(out.program);
        out.group = std::to_string(out.bank) + " " + out.bankName;
    }
    out.label = out.name + " (" + out.where + ")";

    for (const Json &v : p["variations"].array()) {
        out.variations.push_back(v.integer());
    }

    for (const Json &pg : p["pages"].array()) {
        Page page;
        page.page = pg["page"].integer();
        page.heading = stringOr(pg["heading"], "");
        page.label = stringOr(pg["label"], "");
        if (pg["column"].isNumber()) {
            page.column = pg["column"].integer();
        }
        for (const Json &s : pg["sliders"].array()) {
            Slider slider;
            slider.name = stringOr(s["name"], "");
            if (s["table"].isArray()) {
                for (const Json &run : s["table"].array()) {
                    slider.table.push_back({run[0].integer(), run[1].string()});
                }
            }
            page.sliders.push_back(std::move(slider));
        }
        out.pages.push_back(std::move(page));
    }

    for (const auto &[key, rows] : p["presets"].object()) {
        std::vector<std::vector<std::string>> texts;
        for (const Json &row : rows.array()) {
            std::vector<std::string> line;
            for (const Json &cell : row.array()) {
                line.push_back(cell.string());
            }
            texts.push_back(std::move(line));
        }
        out.presets[variationKey(key)] = std::move(texts);
    }
    for (const auto &[key, rows] : p["raw"].object()) {
        std::vector<std::vector<int>> bytes;
        for (const Json &row : rows.array()) {
            std::vector<int> line;
            for (const Json &cell : row.array()) {
                line.push_back(cell.integer());
            }
            bytes.push_back(std::move(line));
        }
        out.raw[variationKey(key)] = std::move(bytes);
    }

    for (size_t i = 0; i < out.pages.size(); i++) {
        const Page &page = out.pages[i];
        for (size_t slot = 0; slot < page.sliders.size(); slot++) {
            if (page.sliders[slot].named()) {
                out.generic.push_back({int(i), page.page, int(slot)});
            }
        }
    }
    if (out.generic.size() > size_t(kGenericSliders)) {
        throw JsonError("catalog: program " + out.name + " has more than " + std::to_string(kGenericSliders) +
                        " named sliders");
    }
    return out;
}

bool isJsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

}  // namespace

const std::vector<std::vector<int>> *Program::rawFor(int variation) const {
    auto hit = raw.find(variation);
    if (hit != raw.end()) {
        return &hit->second;
    }
    hit = raw.find(1);
    if (hit != raw.end()) {
        return &hit->second;
    }
    return nullptr;
}

int Program::presetVariation(int variation) const {
    if (presets.count(variation) != 0) {
        return variation;
    }
    return 1;
}

bool Program::allPagesHaveColumns() const {
    for (const Page &page : pages) {
        if (page.column < 0) {
            return false;
        }
    }
    return true;
}

int Program::genericIndexOf(int page, int slot) const {
    for (size_t k = 0; k < generic.size(); k++) {
        if (generic[k].page == page && generic[k].slot == slot) {
            return int(k) + 1;
        }
    }
    return 0;
}

Catalog Catalog::parse(std::string_view text) {
    Json j = Json::parse(text);
    Catalog out;
    out.version = j["version"].integer();
    out.rom = j["rom"].string();
    out.made = stringOr(j["made"], "");
    out.layout = stringOr(j["layout"], "");
    std::string remote = j["remote"].string();
    if (remote == "larc") {
        out.remote = Remote::Larc;
    } else if (remote == "panel") {
        out.remote = Remote::Panel;
    } else if (remote == "panel224") {
        out.remote = Remote::Panel224;
    } else {
        throw JsonError("catalog: unknown remote '" + remote + "'");
    }
    for (const Json &p : j["programs"].array()) {
        out.programs.push_back(readProgram(p, out.remote));
    }
    return out;
}

const Program *Catalog::find(std::string_view key) const {
    int i = indexOf(key);
    if (i < 0) {
        return nullptr;
    }
    return &programs[size_t(i)];
}

int Catalog::indexOf(std::string_view key) const {
    for (size_t i = 0; i < programs.size(); i++) {
        if (programs[i].key == key) {
            return int(i);
        }
    }
    return -1;
}

int Catalog::rawMin() const {
    if (remote == Remote::Larc) {
        return 2;
    }
    return 0;
}

int Catalog::rawMax() const {
    if (remote == Remote::Larc) {
        return 254;
    }
    return 253;
}

// The checkbox titles and tooltips are web/index.html's.
static const Toggle kDynDecay{
    "DYN DECAY", "Dynamic decay",
    "224XL: while you play, the decay is the MID and LF DECAY settings; when the input goes quiet (its running "
    "level falls well below the level it held), the firmware glides MID and LF decay to the STOP DECAY settings "
    "(page 2), recompiling as it goes, and jumps straight back when the input returns."};
static const Toggle kModeEnh{
    "MODE ENH", "Mode enhancement",
    "Keeps room modes from ringing in the tail by continuously modulating certain delay lines: the firmware keeps "
    "rewriting the modulated taps' pairs of interpolation coefficients (the rows marked rewritten under Running "
    "program). Off parks those taps."};
static const Toggle kDecayOpt{
    "DECAY OPT", "Decay optimization",
    "Reduces the reverb's diffusion and coloration dynamically in response to the input level (Universal Audio's "
    "224 manual). In the firmware: when the input stops, an amount (up to 12) is subtracted from every diffusion "
    "allpass coefficient and the rows are recompiled, then it steps back to 0 as the level it held decays (about "
    "1.5 s)."};

std::vector<Toggle> Catalog::toggles() const {
    if (remote == Remote::Larc) {
        return {kDynDecay, kModeEnh, kDecayOpt};
    }
    if (remote == Remote::Panel224) {
        return {kModeEnh, kDecayOpt};   // PROGRAM 7 and 8
    }
    return {};
}

Toggle Catalog::muteToggle() {
    return {"MUTE", "Output mute", "224XL: the LARC's MUTE key; the firmware mutes the outputs."};
}

void CatalogLibrary::add(std::string_view json) {
    catalogs.push_back(Catalog::parse(json));
}

namespace {

// Slider::readingOf and approachOf for a measured slider. Per stored byte:
// the position sliderPosition gives for it with the text shown at the
// first control value that stores it, clamped to the range, if that value
// stores it; else that first value. A direction-dependent slider prefers a
// move up (approach from the range's low end, which the catalog's tables
// were swept in), else a move down (approach from the high end). A move to
// the value the fader already holds ends moving the other way (the
// operator steps beside it first: up, except from the top).
void canonicalReadings(Slider &slider, int rangeMin, int rangeMax) {
    slider.readingOf.assign(256, -1);
    slider.approachOf.assign(256, -1);
    Slider plain;   // the text rule alone (no measured branch)
    plain.table = slider.table;
    bool twoWay = !slider.storedDown.empty();
    // The byte a move to `value` stores after approaching from `approach`.
    auto after = [&](int value, int approach) {
        if (!twoWay) {
            return int(slider.stored[size_t(value)]);
        }
        bool up = value > approach;
        if (value == approach) {
            up = value == rangeMax;   // beside it first: value + 1 (down again), or 253 (up again)
        }
        if (up) {
            return int(slider.stored[size_t(value)]);
        }
        return int(slider.storedDown[size_t(value)]);
    };
    int approaches[2] = {rangeMin, rangeMax};
    for (int b = 0; b < 256; b++) {
        for (int approach : approaches) {
            int first = -1;
            for (int v = rangeMin; v <= rangeMax; v++) {
                if (after(v, approach) == b) {
                    first = v;
                    break;
                }
            }
            if (first < 0) {
                continue;
            }
            int position = b;
            if (!slider.table.empty()) {
                bool found = false;
                std::string text = tableText(slider.table, first, &found);
                if (found) {
                    position = sliderPosition(plain, b, text, rangeMax);
                }
            }
            // (as the live sink and the editor do: the position clamped to the range)
            if (position < rangeMin) {
                position = rangeMin;
            }
            if (position > rangeMax) {
                position = rangeMax;
            }
            int value = first;
            if (after(position, approach) == b) {
                value = position;
            }
            slider.readingOf[size_t(b)] = int16_t(value);
            if (twoWay) {
                slider.approachOf[size_t(b)] = int16_t(approach);
            }
            break;
        }
    }
}

}  // namespace

const Slider &Program::measuredSlider(const SliderRef &ref, int variation) const {
    auto found = measuredByVariation.find(variation);
    if (found == measuredByVariation.end()) {
        return slider(ref);
    }
    return found->second[size_t(ref.pageIndex)][size_t(ref.slot)];
}

namespace {

// Put one sidecar slider entry ([page, slot, up, down?]) onto `slider`.
void attachTables(Slider &slider, const Json &entry, const std::vector<std::vector<int16_t>> &tables) {
    int index = entry[2].integer();
    int down = -1;
    if (entry.size() > 3) {
        down = entry[3].integer();
    }
    slider.stored = tables[size_t(index)];
    slider.storedDown.clear();
    if (down >= 0 && down != index) {
        slider.storedDown = tables[size_t(down)];
    }
}

// The slider a sidecar entry names in `pages` (page number, slot), or null.
Slider *namedSlider(std::vector<Page> &pages, const Json &entry) {
    int page = entry[0].integer();
    int slot = entry[1].integer();
    for (Page &pg : pages) {
        if (pg.page == page && slot >= 0 && size_t(slot) < pg.sliders.size() && pg.sliders[size_t(slot)].named()) {
            return &pg.sliders[size_t(slot)];
        }
    }
    return nullptr;
}

}  // namespace

int CatalogLibrary::addStored(std::string_view json) {
    Json root = Json::parse(json);
    if (root["version"].integer() != 1) {
        throw JsonError("stored tables: unknown version");
    }
    Catalog *catalog = nullptr;
    for (Catalog &c : catalogs) {
        if (c.rom == root["rom"].string()) {
            catalog = &c;
        }
    }
    if (catalog == nullptr) {
        return 0;
    }
    int low = root["low"].integer();
    int high = root["high"].integer();
    if (low != catalog->rawMin() || high != catalog->rawMax()) {
        throw JsonError("stored tables: measured over another range than the remote's");
    }
    std::vector<std::vector<int16_t>> tables;
    for (const Json &t : root["tables"].array()) {
        if (int(t.size()) != high - low + 1) {
            throw JsonError("stored tables: a table of the wrong length");
        }
        std::vector<int16_t> table(256, -1);
        for (int i = 0; i <= high - low; i++) {
            int b = t[size_t(i)].integer();
            if (b < 0 || b > 255) {
                throw JsonError("stored tables: a byte out of range");
            }
            table[size_t(low + i)] = int16_t(b);
        }
        tables.push_back(std::move(table));
    }
    // Attach to a copy, so a sidecar that does not fit leaves the catalog as it was.
    std::vector<Program> programs = catalog->programs;
    int attached = 0;
    std::vector<std::pair<std::string, int>> unmeasurable;   // (program key, page * 16 + slot)
    struct Override {
        Program *program;
        int variation;
        const Json *entries;
    };
    std::vector<Override> overrides;
    auto checkEntry = [&](const Json &entry, const std::string &key) {
        size_t n = entry.size();
        bool ok = n >= 3 && n <= 4 && entry[2].integer() >= 0 && size_t(entry[2].integer()) < tables.size();
        if (ok && n == 4) {
            ok = entry[3].integer() >= 0 && size_t(entry[3].integer()) < tables.size();
        }
        if (!ok) {
            throw JsonError("stored tables: program " + key + ": a malformed slider entry");
        }
    };
    for (const Json &p : root["programs"].array()) {
        const std::string &key = p["key"].string();
        Program *program = nullptr;
        for (Program &candidate : programs) {
            if (candidate.key == key) {
                program = &candidate;
            }
        }
        if (program == nullptr) {
            throw JsonError("stored tables: program " + key + " is not in the catalog");
        }
        if (p["unmeasurable"].isArray()) {
            for (const Json &u : p["unmeasurable"].array()) {
                unmeasurable.emplace_back(key, u[0].integer() * 16 + u[1].integer());
            }
        }
        for (const Json &s : p["sliders"].array()) {
            checkEntry(s, key);
            Slider *slider = namedSlider(program->pages, s);
            if (slider == nullptr) {
                throw JsonError("stored tables: program " + key + " has no named slider " +
                                std::to_string(s[0].integer()) + "." + std::to_string(s[1].integer()));
            }
            attachTables(*slider, s, tables);
            attached++;
        }
        if (p["variations"].isObject()) {
            for (const auto &[variation, entries] : p["variations"].object()) {
                overrides.push_back({program, std::atoi(variation.c_str()), &entries});
            }
        }
    }
    for (Program &program : programs) {
        for (Page &page : program.pages) {
            for (size_t slot = 0; slot < page.sliders.size(); slot++) {
                Slider &slider = page.sliders[slot];
                bool listed = false;
                for (const auto &u : unmeasurable) {
                    if (u.first == program.key && u.second == page.page * 16 + int(slot)) {
                        listed = true;
                    }
                }
                if (slider.named() && !slider.measured() && !listed) {
                    throw JsonError("stored tables: program " + program.key + " slider " + slider.name +
                                    " is not measured");
                }
                if (slider.measured()) {
                    canonicalReadings(slider, catalog->rawMin(), catalog->rawMax());
                }
            }
        }
    }
    for (const Override &o : overrides) {
        if (o.variation < 1) {
            throw JsonError("stored tables: program " + o.program->key + ": a bad variation");
        }
        std::vector<Page> pages = o.program->pages;
        if (o.program->measuredByVariation.count(o.variation) != 0) {
            throw JsonError("stored tables: program " + o.program->key + ": variation given twice");
        }
        const Json &entries = *o.entries;
        if (entries["sliders"].isArray()) {
            for (const Json &s : entries["sliders"].array()) {
                checkEntry(s, o.program->key);
                Slider *slider = namedSlider(pages, s);
                if (slider == nullptr) {
                    throw JsonError("stored tables: program " + o.program->key + " variation " +
                                    std::to_string(o.variation) + ": no such named slider");
                }
                attachTables(*slider, s, tables);
                canonicalReadings(*slider, catalog->rawMin(), catalog->rawMax());
            }
        }
        if (entries["unmeasurable"].isArray()) {
            for (const Json &u : entries["unmeasurable"].array()) {
                Slider *slider = namedSlider(pages, u);
                if (slider == nullptr) {
                    throw JsonError("stored tables: program " + o.program->key + " variation " +
                                    std::to_string(o.variation) + ": no such named slider");
                }
                slider->stored.clear();
                slider->storedDown.clear();
                slider->readingOf.clear();
                slider->approachOf.clear();
            }
        }
        std::vector<std::vector<Slider>> sliders;
        for (Page &page : pages) {
            sliders.push_back(std::move(page.sliders));
        }
        o.program->measuredByVariation[o.variation] = std::move(sliders);
    }
    catalog->programs = std::move(programs);
    return attached;
}

const Catalog *CatalogLibrary::find(std::string_view rom) const {
    for (const Catalog &catalog : catalogs) {
        if (catalog.rom == rom) {
            return &catalog;
        }
    }
    return nullptr;
}

const char *remoteName(Remote remote) {
    if (remote == Remote::Panel) {
        return "panel";
    }
    if (remote == Remote::Panel224) {
        return "panel224";
    }
    return "larc";
}

std::string tableText(const std::vector<TableRun> &table, int raw, bool *found) {
    const std::string *text = nullptr;
    for (const TableRun &run : table) {
        if (run.raw > raw) {
            break;
        }
        text = &run.text;
    }
    if (found != nullptr) {
        *found = text != nullptr;
    }
    if (text == nullptr) {
        return {};
    }
    return *text;
}

double jsParseFloat(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && isJsSpace(s[i])) {
        i++;
    }
    size_t start = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        i++;
    }
    if (s.substr(i, 8) == "Infinity") {
        double inf = std::numeric_limits<double>::infinity();
        if (s[start] == '-') {
            return -inf;
        }
        return inf;
    }
    size_t digitsBefore = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        i++;
        digitsBefore++;
    }
    size_t digitsAfter = 0;
    if (i < s.size() && s[i] == '.') {
        size_t dot = i;
        i++;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
            digitsAfter++;
        }
        if (digitsBefore == 0 && digitsAfter == 0) {
            i = dot;
        }
    }
    if (digitsBefore == 0 && digitsAfter == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t mark = i;
        i++;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        size_t exponentDigits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
            exponentDigits++;
        }
        if (exponentDigits == 0) {
            i = mark;
        }
    }
    std::string number(s.substr(start, i - start));
    return std::strtod(number.c_str(), nullptr);
}

int tableRaw(const std::vector<TableRun> &table, std::string_view text, int rangeMax) {
    for (size_t i = 0; i < table.size(); i++) {
        if (table[i].text == text) {
            int end = rangeMax;
            if (i + 1 < table.size()) {
                end = table[i + 1].raw - 1;
            }
            // Math.round: halves round up
            return int(std::floor((table[i].raw + end) / 2.0 + 0.5));
        }
    }
    double wanted = jsParseFloat(text);
    int best = 128;
    double distance = std::numeric_limits<double>::infinity();
    for (const TableRun &run : table) {
        double d = std::fabs(jsParseFloat(run.text) - wanted);
        if (d < distance) {
            distance = d;
            best = run.raw;
        }
    }
    return best;
}

int approachFor(const Slider &slider, int stored) {
    if (!slider.measured() || stored < 0 || size_t(stored) >= slider.approachOf.size()) {
        return -1;
    }
    return slider.approachOf[size_t(stored)];
}

int readingFor(const Slider &slider, int stored) {
    if (!slider.measured()) {
        return stored;
    }
    if (stored < 0 || size_t(stored) >= slider.readingOf.size()) {
        return -1;
    }
    return slider.readingOf[size_t(stored)];
}

int sliderPosition(const Slider &slider, int stored, std::string_view shownText, int rangeMax) {
    int position = stored;
    if (!slider.table.empty() && !shownText.empty()) {
        bool found = false;
        std::string text = tableText(slider.table, stored, &found);
        if (!found || text != shownText) {
            position = tableRaw(slider.table, shownText, rangeMax);
        }
    }
    // A measured slider: where the text rule (or, with no text, the byte)
    // puts it, if that control value stores the byte; else readingFor.
    if (slider.measured()) {
        if (shownText.empty() || !slider.stores(position, stored)) {
            int reading = readingFor(slider, stored);
            if (reading >= 0) {
                return reading;
            }
        }
    }
    return position;
}

}  // namespace lexcat
