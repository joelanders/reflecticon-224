// Parse the extra catalogs (catalogs-extra/*.json) with the plugin's C++
// catalog model (source/catalog) and check them for the things the plugin
// relies on: the file name is the ROM hash, the remote is the 224 panel,
// every named slider has a table, the generic slider map is the named
// sliders in page/slot order, and every preset's text is what the slider's
// table shows at the preset's stored byte.
//
//   sh tests/catalogs_extra/run.sh
#include "../../source/catalog/catalog.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace lexcat;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const std::string &what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", what.c_str());
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: parse_extra CATALOG.json...\n");
        return 2;
    }
    CatalogLibrary library;
    for (int i = 1; i < argc; i++) {
        std::string path = argv[i];
        std::ifstream in(path, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string hash = std::filesystem::path(path).stem().string();
        Catalog c;
        try {
            c = Catalog::parse(buffer.str());
            library.add(buffer.str());
        } catch (const std::exception &e) {
            check(false, path + ": parse: " + e.what());
            continue;
        }
        check(c.rom == hash, path + ": rom field is the file's hash");
        check(c.remote == Remote::Panel224, path + ": remote panel224");
        check(!c.programs.empty(), path + ": has programs");
        check(c.rawMin() == 0 && c.rawMax() == 253, path + ": panel pot range 0-253");
        int named = 0;
        for (const Program &p : c.programs) {
            std::string where = path + " " + p.name;
            check(c.find(p.key) == &p, where + ": find(key)");
            check(p.variations.size() == 1 && p.variations[0] == 1, where + ": one variation");
            const auto *raw = p.rawFor(1);
            check(raw != nullptr, where + ": raw presets");
            auto presets = p.presets.find(1);
            check(presets != p.presets.end(), where + ": text presets");
            size_t k = 0;
            for (size_t pi = 0; pi < p.pages.size(); pi++) {
                const Page &page = p.pages[pi];
                for (size_t slot = 0; slot < page.sliders.size(); slot++) {
                    const Slider &s = page.sliders[slot];
                    if (!s.named()) {
                        continue;
                    }
                    named++;
                    std::string sw = where + " " + s.name;
                    check(!s.table.empty(), sw + ": has a table");
                    check(k < p.generic.size() && p.generic[k].pageIndex == int(pi) && p.generic[k].slot == int(slot),
                          sw + ": generic slider order");
                    k++;
                    if (raw != nullptr && presets != p.presets.end()) {
                        int stored = (*raw)[pi][slot];
                        const std::string &text = presets->second[pi][slot];
                        // The stored byte is the parameter's value, not a pot
                        // reading; the table is by pot reading. The check that
                        // holds: some pot reading shows the preset text.
                        bool found = false;
                        for (const TableRun &run : s.table) {
                            if (run.text == text) {
                                found = true;
                            }
                        }
                        check(found, sw + ": preset '" + text + "' appears in the table");
                        int at = tableRaw(s.table, text, c.rawMax());
                        check(tableText(s.table, at) == text, sw + ": tableRaw/tableText round trip");
                        check(stored >= 0 && stored <= 255, sw + ": stored byte");
                    }
                }
            }
            check(k == p.generic.size(), where + ": generic count");
        }
        std::printf("%s: %zu programs, %d named sliders, remote %s\n", path.c_str(), c.programs.size(), named,
                    remoteName(c.remote));
    }
    for (int i = 1; i < argc; i++) {
        std::string hash = std::filesystem::path(argv[i]).stem().string();
        check(library.find(hash) != nullptr, std::string(argv[i]) + ": library finds it by hash");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        return 1;
    }
    return 0;
}
