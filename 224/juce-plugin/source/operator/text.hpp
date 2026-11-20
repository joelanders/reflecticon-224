// The few regular expressions the operators apply to display lines, as
// allocation-free matchers (std::regex allocates). A pattern is literal text
// with two wildcards:
//   '#'  one digit            (JS \d)
//   '~'  any run of spaces    (JS \s*)
// search() finds the first match anywhere in the line, as RegExp.test/exec.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace lexplug::op::text {

inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Match `pattern` at the start of `s`; on success *end is one past the match.
inline bool match_here(const char *s, const char *pattern, const char **end) {
    while (*pattern != 0) {
        if (*pattern == '~') {
            // \s* before a non-space literal: greedy is exact here.
            while (is_space(*s)) {
                s++;
            }
            pattern++;
            continue;
        }
        if (*s == 0) {
            return false;
        }
        if (*pattern == '#') {
            if (!is_digit(*s)) {
                return false;
            }
        } else if (*pattern != *s) {
            return false;
        }
        s++;
        pattern++;
    }
    *end = s;
    return true;
}

// The first match of `pattern` in `s`: its start, or null.
inline const char *search(const char *s, const char *pattern) {
    for (const char *p = s;; p++) {
        const char *end = nullptr;
        if (match_here(p, pattern, &end)) {
            return p;
        }
        if (*p == 0) {
            return nullptr;
        }
    }
}
inline bool contains(const char *s, const char *pattern) {
    return search(s, pattern) != nullptr;
}
inline bool starts_with(const char *s, const char *prefix) {
    return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// NOT_A_SLIDER: a slider display is "NAME VALUE", never a program, page or shift line.
inline bool not_a_slider(const char *top) {
    return contains(top, "B# P# V#") || contains(top, "B# PROGRAM") || contains(top, "PAGE #") ||
           contains(top, "BANK #") || contains(top, "SECOND~FUNCTION") || contains(top, "ALL SLIDERS") ||
           contains(top, "ENTER");
}
inline bool shifted(const char *top) {
    return contains(top, "SECOND~FUNCTION");
}

// Copy s[from, to) trimmed of whitespace into out (capacity n).
inline void trimmed(const char *s, std::size_t from, std::size_t to, char *out, std::size_t n) {
    std::size_t length = std::strlen(s);
    if (to > length) {
        to = length;
    }
    while (from < to && is_space(s[from])) {
        from++;
    }
    while (to > from && is_space(s[to - 1])) {
        to--;
    }
    std::size_t k = 0;
    for (std::size_t i = from; i < to && k + 1 < n; i++) {
        out[k++] = s[i];
    }
    out[k] = 0;
}

// parseSlider: name = top.slice(0, 12).trim(); value = top.slice(12).trim()
// with every whitespace run made one space.
struct Slider {
    char name[13] = {0};
    char value[25] = {0};
};
inline Slider parse_slider(const char *top) {
    Slider slider;
    trimmed(top, 0, 12, slider.name, sizeof slider.name);
    char value[25];
    trimmed(top, 12, 24, value, sizeof value);
    std::size_t k = 0;
    bool in_space = false;
    for (std::size_t i = 0; value[i] != 0; i++) {
        if (is_space(value[i])) {
            if (!in_space) {
                slider.value[k++] = ' ';
            }
            in_space = true;
        } else {
            slider.value[k++] = value[i];
            in_space = false;
        }
    }
    slider.value[k] = 0;
    return slider;
}

}  // namespace lexplug::op::text
