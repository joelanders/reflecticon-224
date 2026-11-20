// TEMPORARY: a minimal reader of the web demo's catalogs
// (../../../web-demo/page/catalogs/<hash>.json), just what the
// operator tests need. To be replaced by the catalog loader in
// ../catalog/ once that lands. Allocates freely: tests and
// background threads only, never the audio thread.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lexplug::op::mini {

// ---- a small JSON DOM --------------------------------------------------
struct Json {
    enum Kind { null, boolean, number, string, array, object } kind = null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json &operator[](const std::string &key) const {
        for (const auto &member : members) {
            if (member.first == key) {
                return member.second;
            }
        }
        throw std::runtime_error("catalog: no key " + key);
    }
    bool has(const std::string &key) const {
        for (const auto &member : members) {
            if (member.first == key) {
                return true;
            }
        }
        return false;
    }
};

class Parser {
public:
    explicit Parser(const std::string &text) : t_(text) {}
    Json parse() {
        Json value = parse_value();
        skip();
        if (i_ != t_.size()) {
            error("trailing text");
        }
        return value;
    }

private:
    [[noreturn]] void error(const char *what) {
        throw std::runtime_error(std::string("catalog JSON: ") + what + " at " + std::to_string(i_));
    }
    void skip() {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) {
            i_++;
        }
    }
    bool eat(char c) {
        skip();
        if (i_ < t_.size() && t_[i_] == c) {
            i_++;
            return true;
        }
        return false;
    }
    void expect(char c) {
        if (!eat(c)) {
            error("unexpected character");
        }
    }
    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (i_ >= t_.size()) {
                error("unterminated string");
            }
            char c = t_[i_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            char e = t_[i_++];
            if (e == 'n') {
                out += '\n';
            } else if (e == 't') {
                out += '\t';
            } else if (e == 'r') {
                out += '\r';
            } else if (e == 'b') {
                out += '\b';
            } else if (e == 'f') {
                out += '\f';
            } else if (e == 'u') {
                unsigned code = unsigned(std::strtoul(t_.substr(i_, 4).c_str(), nullptr, 16));
                i_ += 4;
                if (code < 0x80) {
                    out += char(code);
                } else if (code < 0x800) {
                    out += char(0xc0 | (code >> 6));
                    out += char(0x80 | (code & 0x3f));
                } else {
                    out += char(0xe0 | (code >> 12));
                    out += char(0x80 | ((code >> 6) & 0x3f));
                    out += char(0x80 | (code & 0x3f));
                }
            } else {
                out += e;
            }
        }
    }
    Json parse_value() {
        skip();
        Json v;
        if (i_ >= t_.size()) {
            error("unexpected end");
        }
        char c = t_[i_];
        if (c == '{') {
            i_++;
            v.kind = Json::object;
            if (eat('}')) {
                return v;
            }
            do {
                skip();
                std::string key = parse_string();
                expect(':');
                v.members.emplace_back(key, parse_value());
            } while (eat(','));
            expect('}');
        } else if (c == '[') {
            i_++;
            v.kind = Json::array;
            if (eat(']')) {
                return v;
            }
            do {
                v.items.push_back(parse_value());
            } while (eat(','));
            expect(']');
        } else if (c == '"') {
            v.kind = Json::string;
            v.s = parse_string();
        } else if (t_.compare(i_, 4, "true") == 0) {
            v.kind = Json::boolean;
            v.b = true;
            i_ += 4;
        } else if (t_.compare(i_, 5, "false") == 0) {
            v.kind = Json::boolean;
            i_ += 5;
        } else if (t_.compare(i_, 4, "null") == 0) {
            i_ += 4;
        } else {
            char *end = nullptr;
            v.kind = Json::number;
            v.n = std::strtod(t_.c_str() + i_, &end);
            if (end == t_.c_str() + i_) {
                error("bad value");
            }
            i_ = size_t(end - t_.c_str());
        }
        return v;
    }
    const std::string &t_;
    size_t i_ = 0;
};

// ---- what the operator tests use ---------------------------------------
struct Page {
    int page = 0;
    unsigned column = 0;
    std::vector<std::string> sliders;   // names; "INACTIVE" or "" for no control
};
struct Program {
    int bank = 0;
    int program = 0;
    std::string name;
    std::vector<int> variations;
    std::vector<Page> pages;
    std::map<int, std::vector<std::vector<int>>> raw;   // variation -> page -> slot -> byte
};
struct Catalog {
    std::string rom;
    std::string remote;
    std::vector<Program> programs;
};

inline Catalog load_catalog(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    Json root = Parser(text).parse();
    Catalog catalog;
    catalog.rom = root["rom"].s;
    catalog.remote = root["remote"].s;
    for (const Json &p : root["programs"].items) {
        Program program;
        if (p.has("bank") && p["bank"].kind == Json::number) {
            program.bank = int(p["bank"].n);
        }
        program.program = int(p["program"].n);
        program.name = p["name"].s;
        for (const Json &v : p["variations"].items) {
            program.variations.push_back(int(v.n));
        }
        for (const Json &page : p["pages"].items) {
            Page out;
            out.page = int(page["page"].n);
            if (page.has("column")) {
                out.column = unsigned(page["column"].n);
            }
            for (const Json &slider : page["sliders"].items) {
                out.sliders.push_back(slider["name"].s);
            }
            program.pages.push_back(out);
        }
        if (p.has("raw")) {
            for (const auto &member : p["raw"].members) {
                std::vector<std::vector<int>> bytes;
                for (const Json &row : member.second.items) {
                    std::vector<int> r;
                    for (const Json &b : row.items) {
                        r.push_back(int(b.n));
                    }
                    bytes.push_back(r);
                }
                program.raw[std::atoi(member.first.c_str())] = bytes;
            }
        }
        catalog.programs.push_back(program);
    }
    return catalog;
}

}  // namespace lexplug::op::mini
