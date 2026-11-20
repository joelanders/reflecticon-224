// The measured stored-byte sidecars (catalogs-extra/stored/*.stored.json,
// tools/gen_stored_tables.py) against the plugin's C++ catalog model: each
// attaches to its catalog (every named slider measured), and for every
// measured slider and every stored byte some control value gives:
//   - readingFor(byte) is a control value in the remote's range that stores it;
//   - the editor (sliderPosition, clamped to the range) puts the slider at
//     readingFor with no text, and at a value storing the byte with the
//     text the firmware shows for it;
//   - (counted) bytes whose table text varies over the values storing them
//     (the LARC's display follows the fader past a clamp).
// Where a catalog table was swept coarser than the stored byte changes (the
// 224X's step-4 tables), the text position may differ from readingFor while
// storing the byte: counted, not a failure.
// Also counts, per set, the catalog preset bytes no control value stores
// (informational: a share only records moved sliders, and moves store
// reachable bytes).
//
//   sh tests/stored_tables/run.sh
#include "../../source/catalog/catalog.hpp"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace lexcat;

static int failures = 0;
static int checks = 0;

static void check(bool ok, const std::string &what) {
    checks++;
    if (!ok) {
        failures++;
        if (failures <= 40) {
            std::printf("FAIL %s\n", what.c_str());
        }
    }
}

static std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The byte a move to `value` leaves, after a move to `approach` (-1: none,
// a one-way slider), as the operator moves: a move to the value the fader
// holds steps beside it first (up, except from the top).
static int leaves(const Slider &slider, int value, int approach, int rangeMax) {
    if (slider.storedDown.empty() || approach < 0) {
        return slider.stored[size_t(value)];
    }
    bool up = value > approach;
    if (value == approach) {
        up = value == rangeMax;
    }
    if (up) {
        return slider.stored[size_t(value)];
    }
    return slider.storedDown[size_t(value)];
}

static bool endsWith(const std::string &text, const std::string &tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: stored_tables_test CATALOG.json... SIDECAR.stored.json...\n");
        return 2;
    }
    CatalogLibrary library;
    std::vector<std::string> sidecars;
    for (int i = 1; i < argc; i++) {
        std::string path = argv[i];
        if (endsWith(path, ".stored.json")) {
            sidecars.push_back(path);
        } else {
            library.add(readFile(path));
        }
    }
    for (const std::string &path : sidecars) {
        int attached = 0;
        std::string error;
        try {
            attached = library.addStored(readFile(path));
        } catch (const std::exception &e) {
            error = e.what();
        }
        check(error.empty() && attached > 0, path + ": attaches (" + error + ")");
        std::printf("%s: %d sliders attached\n", path.c_str(), attached);
    }
    for (const Catalog &catalog : library.catalogs) {
        int measured = 0;
        int bytes = 0;
        int unreachablePresets = 0;
        int presets = 0;
        int coarse = 0;
        int twoWay = 0;   // direction-dependent sliders
        int textVaries = 0;   // bytes whose table text is not constant over the values storing them
        int overridden = 0;   // (program, variation) pairs with their own tables
        for (const Program &program : catalog.programs) {
          // Variation 1's sliders (the pages), then each variation with its own tables.
          std::vector<std::pair<int, std::vector<std::vector<Slider>>>> sets;
          std::vector<std::vector<Slider>> base;
          for (const Page &page : program.pages) {
              base.push_back(page.sliders);
          }
          sets.push_back({0, base});
          for (const auto &[variation, sliders] : program.measuredByVariation) {
              sets.push_back({variation, sliders});
              overridden++;
          }
          for (const auto &[variation, pageSliders] : sets) {
            for (size_t pi = 0; pi < program.pages.size(); pi++) {
                const Page &page = program.pages[pi];
                for (size_t slot = 0; slot < page.sliders.size(); slot++) {
                    const Slider &slider = pageSliders[pi][slot];
                    if (!slider.measured()) {
                        continue;
                    }
                    measured++;
                    std::string where = catalog.rom + " " + program.key + " v" + std::to_string(variation) + " " +
                                        std::to_string(page.page) + "." + std::to_string(slot) + " " + slider.name;
                    if (!slider.storedDown.empty()) {
                        twoWay++;
                    }
                    for (int b = 0; b < 256; b++) {
                        int first = -1;
                        int last = -1;
                        for (int v = catalog.rawMin(); v <= catalog.rawMax(); v++) {
                            // reachable by a replay: a move approaching from either end of the range
                            bool reachable = leaves(slider, v, catalog.rawMin(), catalog.rawMax()) == b ||
                                             leaves(slider, v, catalog.rawMax(), catalog.rawMax()) == b;
                            if (reachable) {
                                if (first < 0) {
                                    first = v;
                                }
                                last = v;
                            }
                        }
                        int r = readingFor(slider, b);
                        if (first < 0) {
                            check(r == -1, where + ": byte " + std::to_string(b) + " is unreachable, readingFor " +
                                               std::to_string(r));
                            continue;
                        }
                        bytes++;
                        int approach = approachFor(slider, b);
                        check(r >= catalog.rawMin() && r <= catalog.rawMax() &&
                                  leaves(slider, r, approach, catalog.rawMax()) == b,
                              where + ": readingFor(" + std::to_string(b) + ") = " + std::to_string(r) +
                                  " (approach " + std::to_string(approach) + ") does not store it");
                        check(sliderPosition(slider, b, "", catalog.rawMax()) == r,
                              where + ": the editor with no text puts " + std::to_string(b) + " elsewhere");
                        if (slider.table.empty()) {
                            continue;
                        }
                        bool found = false;
                        std::string text = tableText(slider.table, first, &found);
                        if (slider.storedDown.empty()) {
                            for (int v = first; v <= last; v++) {
                                if (slider.stored[size_t(v)] == b && tableText(slider.table, v) != text) {
                                    textVaries++;   // (the LARC shows the fader's text past a clamp)
                                    break;
                                }
                            }
                        }
                        if (found) {
                            int position = sliderPosition(slider, b, text, catalog.rawMax());
                            // (the live sink and the editor clamp it to the range)
                            if (position < catalog.rawMin()) {
                                position = catalog.rawMin();
                            }
                            if (position > catalog.rawMax()) {
                                position = catalog.rawMax();
                            }
                            check(slider.stores(position, b),
                                  where + ": the editor with the text puts " + std::to_string(b) + " at " +
                                      std::to_string(position) + ", which does not store it");
                            Slider plain;   // the text rule alone, as for an unmeasured slider
                            plain.table = slider.table;
                            int byText = sliderPosition(plain, b, text, catalog.rawMax());
                            if (!slider.stores(byText, b)) {
                                coarse++;
                            }
                        }
                    }
                    for (const auto &[presetVariation, raw] : program.raw) {
                        if (variation != 0 && presetVariation != variation) {
                            continue;
                        }
                        if (pi < raw.size() && slot < raw[pi].size()) {
                            presets++;
                            if (readingFor(slider, raw[pi][slot]) < 0) {
                                unreachablePresets++;
                            }
                        }
                    }
                }
            }
          }
        }
        if (measured > 0) {
            std::printf("%s (%s): %d measured sliders, %d reachable (slider, byte) pairs; %d of %d preset bytes "
                        "no control value stores; %d bytes the text rule alone would misplace (readingFor used); %d "
                        "direction-dependent sliders; %d bytes with a varying table text; %d variations with their own tables\n",
                        catalog.rom.c_str(), remoteName(catalog.remote), measured, bytes, unreachablePresets, presets,
                        coarse, twoWay, textVaries, overridden);
        }
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    if (failures == 0) {
        std::printf("PASS stored tables\n");
        return 0;
    }
    std::printf("FAIL stored tables\n");
    return 1;
}
