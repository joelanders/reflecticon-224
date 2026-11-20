// Test support: what the 224 operator tests need from a 224
// catalog (page/catalogs/<hash>.json or catalogs-extra/<hash>.json): program
// identities, pages with slider names and value tables, the raw presets and
// the "layout" field. Uses mini_catalog.hpp's JSON parser. Allocates freely:
// tests only.
#pragma once
#include "../../source/operator/mini_catalog.hpp"
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace lexplug::op::panel224_test {

struct Slider {
    std::string name;
    std::vector<std::pair<int, std::string>> table;   // [raw, text], run-length
};
struct Page {
    int page = 0;
    unsigned column = 0;
    std::vector<Slider> sliders;
};
struct Program {
    unsigned identity = 0;
    std::string name;
    std::vector<int> variations;
    std::vector<Page> pages;
    std::vector<std::vector<int>> raw;   // raw["1"]: page -> slot -> byte
};
struct Catalog {
    std::string rom;
    std::string layout;   // "" (v4) or "v3.2"
    std::vector<Program> programs;
};

inline Catalog load(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    mini::Json root = mini::Parser(text).parse();
    Catalog catalog;
    catalog.rom = root["rom"].s;
    if (root.has("layout")) {
        catalog.layout = root["layout"].s;
    }
    for (const mini::Json &p : root["programs"].items) {
        Program program;
        program.identity = unsigned(p["identity"].n);
        program.name = p["name"].s;
        for (const mini::Json &v : p["variations"].items) {
            program.variations.push_back(int(v.n));
        }
        for (const mini::Json &page : p["pages"].items) {
            Page out;
            out.page = int(page["page"].n);
            out.column = unsigned(page["column"].n);
            for (const mini::Json &s : page["sliders"].items) {
                Slider slider;
                slider.name = s["name"].s;
                if (s.has("table")) {
                    for (const mini::Json &entry : s["table"].items) {
                        slider.table.push_back({int(entry.items[0].n), entry.items[1].s});
                    }
                }
                out.sliders.push_back(slider);
            }
            program.pages.push_back(out);
        }
        for (const mini::Json &row : p["raw"]["1"].items) {
            std::vector<int> r;
            for (const mini::Json &b : row.items) {
                r.push_back(int(b.n));
            }
            program.raw.push_back(r);
        }
        catalog.programs.push_back(program);
    }
    return catalog;
}

// The catalog for a set hash: the first of the directories that has it.
inline Catalog find(const std::vector<std::string> &directories, const std::string &hash) {
    for (const std::string &directory : directories) {
        std::string path = directory + "/" + hash + ".json";
        if (std::filesystem::exists(path)) {
            return load(path);
        }
    }
    throw std::runtime_error("no catalog for " + hash);
}

}  // namespace lexplug::op::panel224_test
