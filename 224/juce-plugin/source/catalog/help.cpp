#include "help.hpp"

#include <vector>

namespace lexcat {

namespace {

// JS \s for the ASCII range (the catalogs and headings are ASCII).
bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// s.replace(/\s+/g, ' ').trim()
std::string collapse(std::string_view s) {
    std::string out;
    bool pendingSpace = false;
    for (char c : s) {
        if (isSpace(c)) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !out.empty()) {
            out += ' ';
        }
        pendingSpace = false;
        out += c;
    }
    return out;
}

// One alternative of SUFFIX's group, matched against a whole token:
// \([LR]\) | L\+R\) | LR | [LR]\)[A-D]* | [LR]
bool isSuffixToken(std::string_view t) {
    if (t == "(L)" || t == "(R)" || t == "L+R)" || t == "LR" || t == "L" || t == "R") {
        return true;
    }
    if (t.size() >= 2 && (t[0] == 'L' || t[0] == 'R') && t[1] == ')') {
        for (size_t i = 2; i < t.size(); i++) {
            if (t[i] < 'A' || t[i] > 'D') {
                return false;
            }
        }
        return true;
    }
    return false;
}

// heading.match(/\[[^\]]*\]|[^\[\]]+/g)
std::vector<std::string> headingGroups(std::string_view h) {
    std::vector<std::string> groups;
    size_t i = 0;
    while (i < h.size()) {
        if (h[i] == '[') {
            size_t close = h.find(']', i + 1);
            if (close != std::string_view::npos) {
                groups.emplace_back(h.substr(i, close + 1 - i));
                i = close + 1;
                continue;
            }
            i++;   // an unclosed '[' matches neither alternative
            continue;
        }
        if (h[i] == ']') {
            i++;
            continue;
        }
        size_t end = i;
        while (end < h.size() && h[end] != '[' && h[end] != ']') {
            end++;
        }
        groups.emplace_back(h.substr(i, end - i));
        i = end;
    }
    return groups;
}

// group.replace(/[[\]]/g, '').replace(/\(.*\)/, '').replace(/\s+/g, ' ').trim()
std::string groupWords(const std::string &group) {
    std::string s;
    for (char c : group) {
        if (c != '[' && c != ']') {
            s += c;
        }
    }
    // /\(.*\)/ (no g, '.' stops at line ends): from the first '(' that has a
    // ')' after it on its line, to the last ')' on that line.
    for (size_t open = s.find('('); open != std::string::npos; open = s.find('(', open + 1)) {
        size_t lineEnd = s.find_first_of("\n\r", open);
        if (lineEnd == std::string::npos) {
            lineEnd = s.size();
        }
        size_t close = s.rfind(')', lineEnd - 1);
        if (close != std::string::npos && close > open) {
            s.erase(open, close + 1 - open);
            break;
        }
    }
    return collapse(s);
}

}  // namespace

ParamHelp ParamHelp::parse(std::string_view text) {
    Json j = Json::parse(text);
    ParamHelp out;
    for (const auto &[name, entry] : j["params"].object()) {
        std::string help;
        if (entry["help"].isString()) {
            help = entry["help"].string();
        }
        out.params_[name] = help;
    }
    for (const auto &[name, target] : j["aliases"].object()) {
        out.aliases_[name] = target.string();
    }
    return out;
}

std::string ParamHelp::stripSuffix(std::string_view name, std::string *suffix) {
    std::string full = collapse(name);
    size_t space = full.rfind(' ');
    if (space != std::string::npos && isSuffixToken(std::string_view(full).substr(space + 1))) {
        if (suffix != nullptr) {
            *suffix = full.substr(space + 1);
        }
        return full.substr(0, space);
    }
    if (suffix != nullptr) {
        suffix->clear();
    }
    return full;
}

std::string ParamHelp::helpFor(std::string_view name, std::string_view heading) const {
    if (params_.empty() && aliases_.empty()) {
        return {};   // (app.js: no param_help.json loaded)
    }
    std::string suffix;
    std::string bare = stripSuffix(name, &suffix);
    std::string canonical;
    bool known = false;
    auto alias = aliases_.find(bare);
    if (alias != aliases_.end()) {
        canonical = alias->second;
        known = true;
    }
    // "LEVEL n" and "DELAY n" mean different things per program: the page heading says which.
    if (known && (canonical == "LEVEL" || canonical == "DELAY")) {
        for (const std::string &group : headingGroups(heading)) {
            auto hit = aliases_.find(groupWords(group));
            if (hit != aliases_.end() && !hit->second.empty() && hit->second != canonical) {
                canonical = hit->second;
                break;
            }
        }
    }
    std::string text;
    if (known) {
        auto param = params_.find(canonical);
        if (param != params_.end()) {
            text = param->second;
        }
    }
    if (!suffix.empty()) {
        // /^(L\+R|[LR])\)([A-D]*)$/
        std::string input;
        std::string outputs;
        bool path = false;
        size_t paren = suffix.find(')');
        if (paren != std::string::npos) {
            std::string head = suffix.substr(0, paren);
            if (head == "L+R" || head == "L" || head == "R") {
                path = true;
                input = head;
                outputs = suffix.substr(paren + 1);
                for (char c : outputs) {
                    if (c < 'A' || c > 'D') {
                        path = false;
                    }
                }
            }
        }
        std::string side;
        if (input == "L") {
            side = "left input";
        } else if (input == "R") {
            side = "right input";
        } else if (input == "L+R") {
            side = "both inputs";
        }
        if (path && !outputs.empty()) {
            text += " Signal path: " + side + " to output";
            if (outputs.size() > 1) {
                text += "s";
            }
            text += " ";
            for (size_t i = 0; i < outputs.size(); i++) {
                if (i > 0) {
                    text += ", ";
                }
                text += outputs[i];
            }
            text += ".";
        } else if (path) {
            text += " Fed from the " + side + ".";
        } else if (suffix == "L" || suffix == "R") {
            text += " Acts on the ";
            if (suffix == "L") {
                text += "left";
            } else {
                text += "right";
            }
            text += " half of this split program.";
        } else if (suffix == "LR") {
            text += " Shared by both halves of this split program.";
        }
    }
    return text;
}

}  // namespace lexcat
