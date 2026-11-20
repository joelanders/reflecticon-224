#include "json.hpp"

#include <cmath>
#include <cstdlib>

namespace lexcat {

namespace {
const Json &nullJson() {
    static const Json none;
    return none;
}
}  // namespace

int Json::integer() const {
    double x = number();
    if (x != std::floor(x) || x < -2147483648.0 || x > 2147483647.0) {
        throw JsonError("JSON: expected a whole number");
    }
    return int(x);
}

size_t Json::size() const {
    if (type_ == Type::Array) {
        return items_.size();
    }
    if (type_ == Type::Object) {
        return members_.size();
    }
    return 0;
}

const Json &Json::operator[](size_t i) const {
    expect(Type::Array, "an array");
    if (i >= items_.size()) {
        throw JsonError("JSON: index out of range");
    }
    return items_[i];
}

const Json &Json::operator[](std::string_view key) const {
    if (type_ != Type::Object) {
        return nullJson();
    }
    for (const auto &member : members_) {
        if (member.first == key) {
            return member.second;
        }
    }
    return nullJson();
}

bool Json::has(std::string_view key) const {
    if (type_ != Type::Object) {
        return false;
    }
    for (const auto &member : members_) {
        if (member.first == key) {
            return true;
        }
    }
    return false;
}

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : s_(text) {}

    Json document() {
        Json j = value(0);
        space();
        if (i_ != s_.size()) {
            fail("trailing characters");
        }
        return j;
    }

private:
    std::string_view s_;
    size_t i_ = 0;

    [[noreturn]] void fail(const char *what) const {
        throw JsonError("JSON: " + std::string(what) + " at offset " + std::to_string(i_));
    }

    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
            i_++;
        }
    }

    bool literal(const char *word) {
        std::string_view w(word);
        if (s_.substr(i_, w.size()) == w) {
            i_ += w.size();
            return true;
        }
        return false;
    }

    Json value(int depth) {
        if (depth > 200) {
            fail("nesting too deep");
        }
        space();
        if (i_ >= s_.size()) {
            fail("unexpected end");
        }
        Json j;
        char c = s_[i_];
        if (c == '{') {
            i_++;
            j.type_ = Json::Type::Object;
            space();
            if (i_ < s_.size() && s_[i_] == '}') {
                i_++;
                return j;
            }
            for (;;) {
                space();
                if (i_ >= s_.size() || s_[i_] != '"') {
                    fail("expected a member name");
                }
                std::string key = string();
                space();
                if (i_ >= s_.size() || s_[i_] != ':') {
                    fail("expected ':'");
                }
                i_++;
                Json member = value(depth + 1);
                // (a repeated key: the last one wins, as JSON.parse)
                bool replaced = false;
                for (auto &existing : j.members_) {
                    if (existing.first == key) {
                        existing.second = std::move(member);
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    j.members_.emplace_back(std::move(key), std::move(member));
                }
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    i_++;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == '}') {
                    i_++;
                    return j;
                }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            i_++;
            j.type_ = Json::Type::Array;
            space();
            if (i_ < s_.size() && s_[i_] == ']') {
                i_++;
                return j;
            }
            for (;;) {
                j.items_.push_back(value(depth + 1));
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    i_++;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == ']') {
                    i_++;
                    return j;
                }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            j.type_ = Json::Type::String;
            j.string_ = string();
            return j;
        }
        if (literal("true")) {
            j.type_ = Json::Type::Bool;
            j.bool_ = true;
            return j;
        }
        if (literal("false")) {
            j.type_ = Json::Type::Bool;
            return j;
        }
        if (literal("null")) {
            return j;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            j.type_ = Json::Type::Number;
            j.number_ = number();
            return j;
        }
        fail("unexpected character");
    }

    double number() {
        size_t start = i_;
        if (s_[i_] == '-') {
            i_++;
        }
        auto digits = [&]() {
            size_t from = i_;
            while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
                i_++;
            }
            if (i_ == from) {
                fail("bad number");
            }
        };
        digits();
        if (i_ < s_.size() && s_[i_] == '.') {
            i_++;
            digits();
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            i_++;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
                i_++;
            }
            digits();
        }
        std::string text(s_.substr(start, i_ - start));
        return std::strtod(text.c_str(), nullptr);
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) {
            fail("bad \\u escape");
        }
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') {
                v |= unsigned(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                v |= unsigned(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                v |= unsigned(h - 'A' + 10);
            } else {
                fail("bad \\u escape");
            }
        }
        return v;
    }

    static void utf8(std::string &out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    std::string string() {
        i_++;   // the opening quote
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) {
                fail("unterminated string");
            }
            char c = s_[i_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) {
                fail("unterminated string");
            }
            char e = s_[i_++];
            if (e == '"' || e == '\\' || e == '/') {
                out += e;
            } else if (e == 'b') {
                out += '\b';
            } else if (e == 'f') {
                out += '\f';
            } else if (e == 'n') {
                out += '\n';
            } else if (e == 'r') {
                out += '\r';
            } else if (e == 't') {
                out += '\t';
            } else if (e == 'u') {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp < 0xDC00 && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                    size_t mark = i_;
                    i_ += 2;
                    unsigned low = hex4();
                    if (low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        i_ = mark;
                    }
                }
                utf8(out, cp);
            } else {
                fail("bad escape");
            }
        }
    }
};

Json Json::parse(std::string_view text) {
    JsonReader reader(text);
    return reader.document();
}

}  // namespace lexcat
