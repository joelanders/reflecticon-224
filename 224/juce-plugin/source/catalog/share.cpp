#include "share.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace lexcat {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string join(const std::vector<std::string> &items, const char *separator) {
    std::string out;
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) {
            out += separator;
        }
        out += items[i];
    }
    return out;
}

std::vector<std::string> split(std::string_view s, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t at = s.find(separator, start);
        if (at == std::string_view::npos) {
            parts.emplace_back(s.substr(start));
            return parts;
        }
        parts.emplace_back(s.substr(start, at - start));
        start = at + 1;
    }
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// application/x-www-form-urlencoded decoding, as URLSearchParams.
std::string formDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && hexDigit(s[i + 1]) >= 0 &&
                   hexDigit(s[i + 2]) >= 0) {
            out += char(hexDigit(s[i + 1]) * 16 + hexDigit(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

struct Query {
    std::vector<std::pair<std::string, std::string>> pairs;
    // URLSearchParams.get: the first value for `name`, or nullptr.
    const std::string *get(const char *name) const {
        for (const auto &pair : pairs) {
            if (pair.first == name) {
                return &pair.second;
            }
        }
        return nullptr;
    }
};

Query parseQuery(std::string_view text) {
    size_t question = text.find('?');
    if (question != std::string_view::npos) {
        text = text.substr(question + 1);
        size_t hash = text.find('#');
        if (hash != std::string_view::npos) {
            text = text.substr(0, hash);
        }
    }
    Query q;
    for (const std::string &item : split(text, '&')) {
        if (item.empty()) {
            continue;
        }
        size_t equals = item.find('=');
        if (equals == std::string::npos) {
            q.pairs.emplace_back(formDecode(item), std::string());
        } else {
            q.pairs.emplace_back(formDecode(std::string_view(item).substr(0, equals)),
                                 formDecode(std::string_view(item).substr(equals + 1)));
        }
    }
    return q;
}

bool wholeInt(double x, int &out) {
    if (!std::isfinite(x) || x != std::floor(x) || std::fabs(x) > 2147483647.0) {
        return false;
    }
    out = int(x);
    return true;
}

std::string underscored(const std::string &label) {
    std::string out = label;
    for (char &c : out) {
        if (c == ' ') {
            c = '_';
        }
    }
    return out;
}

}  // namespace

double jsNumber(std::string_view s) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    while (!s.empty() && isSpace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isSpace(s.back())) {
        s.remove_suffix(1);
    }
    if (s.empty()) {
        return 0;
    }
    if (s.size() > 2 && s[0] == '0') {
        int base = 0;
        if (s[1] == 'x' || s[1] == 'X') {
            base = 16;
        } else if (s[1] == 'o' || s[1] == 'O') {
            base = 8;
        } else if (s[1] == 'b' || s[1] == 'B') {
            base = 2;
        }
        if (base != 0) {
            double v = 0;
            for (size_t i = 2; i < s.size(); i++) {
                int d = hexDigit(s[i]);
                if (d < 0 || d >= base) {
                    return nan;
                }
                v = v * base + d;
            }
            return v;
        }
    }
    size_t i = 0;
    bool negative = false;
    if (s[0] == '+' || s[0] == '-') {
        negative = s[0] == '-';
        i = 1;
    }
    if (s.substr(i) == "Infinity") {
        if (negative) {
            return -std::numeric_limits<double>::infinity();
        }
        return std::numeric_limits<double>::infinity();
    }
    size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        i++;
        digits++;
    }
    if (i < s.size() && s[i] == '.') {
        i++;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
            digits++;
        }
    }
    if (digits == 0) {
        return nan;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        size_t exponent = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
            exponent++;
        }
        if (exponent == 0) {
            return nan;
        }
    }
    if (i != s.size()) {
        return nan;
    }
    std::string text(s);
    return std::strtod(text.c_str(), nullptr);
}

std::string sharePayload(const ShareState &state) {
    // (every value is digits, letters and . , _ - : nothing needs escaping)
    std::vector<std::string> params{"fw=" + state.fw, "p=" + state.key, "v=" + std::to_string(state.variation)};
    std::vector<std::string> moved;
    for (const ShareMove &m : state.moves) {
        moved.push_back(std::to_string(m.page) + "." + std::to_string(m.slot) + "." + std::to_string(m.value));
    }
    if (!moved.empty()) {
        params.push_back("s=" + join(moved, ","));
    }
    std::vector<std::string> flags;
    for (const ShareToggle &t : state.toggles) {
        std::string flag = underscored(t.label) + "-";
        if (t.on) {
            flag += "1";
        } else {
            flag += "0";
        }
        flags.push_back(flag);
    }
    if (!flags.empty()) {
        params.push_back("t=" + join(flags, ","));
    }
    return join(params, "&");
}

std::string sharePayload(const std::string &fw, const Program &program, int variation,
                         const std::function<int(int column, int slot)> &stored,
                         const std::vector<ShareToggle> &toggles) {
    ShareState state;
    state.fw = fw;
    state.key = program.key;
    state.variation = variation;
    state.toggles = toggles;
    const std::vector<std::vector<int>> *preset = program.rawFor(variation);
    for (size_t i = 0; i < program.pages.size(); i++) {
        const Page &page = program.pages[i];
        for (size_t slot = 0; slot < page.sliders.size(); slot++) {
            if (!page.sliders[slot].named() || page.column < 0) {
                continue;
            }
            int value = stored(page.column, int(slot));
            bool same = false;
            if (preset != nullptr && i < preset->size() && slot < (*preset)[i].size()) {
                same = value == (*preset)[i][slot];
            }
            if (!same) {
                state.moves.push_back({page.page, int(slot), value});
            }
        }
    }
    return sharePayload(state);
}

std::string sharePayload(const std::string &fw, const Program &program, int variation,
                         const std::vector<std::vector<int>> &storedByPage,
                         const std::vector<ShareToggle> &toggles) {
    // (the pages' columns are distinct, so a column names one page)
    auto stored = [&](int column, int slot) {
        for (size_t i = 0; i < program.pages.size(); i++) {
            if (program.pages[i].column == column) {
                return storedByPage.at(i).at(size_t(slot));
            }
        }
        return 0;
    };
    return sharePayload(fw, program, variation, stored, toggles);
}

bool readSharePayload(std::string_view text, ShareState &out, std::vector<std::string> *problems) {
    Query q = parseQuery(text);
    const std::string *fw = q.get("fw");
    const std::string *key = q.get("p");
    if (fw == nullptr || fw->empty() || key == nullptr || key->empty()) {
        return false;
    }
    out = ShareState();
    out.fw = *fw;
    out.key = *key;
    // Number(params.get('v') || 1)
    const std::string *v = q.get("v");
    if (v != nullptr && !v->empty()) {
        int variation = 1;
        if (wholeInt(jsNumber(*v), variation)) {
            out.variation = variation;
        } else if (problems != nullptr) {
            problems->push_back("variation '" + *v + "' is not a whole number; using 1");
        }
    }
    const std::string *s = q.get("s");
    if (s != nullptr) {
        for (const std::string &item : split(*s, ',')) {
            if (item.empty()) {
                continue;
            }
            std::vector<std::string> parts = split(item, '.');
            ShareMove move;
            bool ok = parts.size() >= 3 && wholeInt(jsNumber(parts[0]), move.page) &&
                      wholeInt(jsNumber(parts[1]), move.slot) && wholeInt(jsNumber(parts[2]), move.value);
            if (ok) {
                out.moves.push_back(move);
            } else if (problems != nullptr) {
                problems->push_back("slider move '" + item + "' is not page.slot.value");
            }
        }
    }
    const std::string *t = q.get("t");
    if (t != nullptr) {
        for (const std::string &item : split(*t, ',')) {
            if (item.empty()) {
                continue;
            }
            std::vector<std::string> parts = split(item, '-');
            std::string label = parts[0];
            for (char &c : label) {
                if (c == '_') {
                    c = ' ';
                }
            }
            bool on = parts.size() > 1 && parts[1] == "1";
            // (a JS object: a repeated label keeps its first place, takes the last value)
            bool replaced = false;
            for (ShareToggle &existing : out.toggles) {
                if (existing.label == label) {
                    existing.on = on;
                    replaced = true;
                }
            }
            if (!replaced) {
                out.toggles.push_back({label, on});
            }
        }
    }
    return true;
}

}  // namespace lexcat
