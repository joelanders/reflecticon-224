// The C++ catalog model against the web demo's own answers.
//
//   node tests/catalog/js_reference.mjs build-d/js_reference.json
//   catalog_test <web dir> build-d/js_reference.json
//
// Checks, per catalog (all five): it parses; programs, keys, labels, groups,
// variations, pages, headings, columns, slider names and table lengths match
// the JSON; the generic slider_k map is the named sliders in page/slot
// order; tableText/tableRaw agree with app.js; helpFor agrees with app.js for
// every (slider name, page heading) in the catalog; share payloads encode to
// exactly share.js's string, decode to exactly readShareLink's answer, and
// round-trip. Exit status 0 = all passed.
#include "../../source/catalog/catalog.hpp"
#include "../../source/catalog/help.hpp"
#include "../../source/catalog/share.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
    if (!in) {
        throw std::runtime_error("cannot read " + path);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// A JS readShareLink answer (JSON; NaN became null) against ours.
static void compareRead(const Json &js, bool ours, const ShareState &state, const std::vector<std::string> &problems,
                        const std::string &what) {
    if (js.isNull()) {
        check(!ours, what + ": JS says not a link");
        return;
    }
    check(ours, what + ": JS reads a link");
    if (!ours) {
        return;
    }
    check(js["fw"].string() == state.fw, what + ": fw");
    check(js["key"].string() == state.key, what + ": key");
    if (js["variation"].isNumber()) {
        check(js["variation"].number() == state.variation, what + ": variation");
    } else {
        check(state.variation == 1 && !problems.empty(), what + ": malformed variation reads as 1, reported");
    }
    // Our moves = the JS moves whose three parts are whole numbers.
    std::vector<ShareMove> wanted;
    size_t malformed = 0;
    for (const Json &m : js["moves"].array()) {
        bool whole = true;
        for (const char *part : {"page", "slot", "value"}) {
            if (!m[part].isNumber() || m[part].number() != std::floor(m[part].number())) {
                whole = false;
            }
        }
        if (whole) {
            wanted.push_back({m["page"].integer(), m["slot"].integer(), m["value"].integer()});
        } else {
            malformed++;
        }
    }
    check(wanted.size() == state.moves.size(), what + ": move count");
    for (size_t i = 0; i < wanted.size() && i < state.moves.size(); i++) {
        check(wanted[i].page == state.moves[i].page && wanted[i].slot == state.moves[i].slot &&
                  wanted[i].value == state.moves[i].value,
              what + ": move " + std::to_string(i));
    }
    check(malformed <= problems.size(), what + ": malformed moves reported");
    const auto &toggles = js["toggles"].object();
    check(toggles.size() == state.toggles.size(), what + ": toggle count");
    for (size_t i = 0; i < toggles.size() && i < state.toggles.size(); i++) {
        check(toggles[i].first == state.toggles[i].label && toggles[i].second.boolean() == state.toggles[i].on,
              what + ": toggle " + toggles[i].first);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: catalog_test WEB_DIR JS_REFERENCE_JSON\n");
        return 2;
    }
    std::string web = argv[1];
    Json reference = Json::parse(readFile(argv[2]));
    ParamHelp help = ParamHelp::parse(readFile(web + "/param_help.json"));

    int helpCases = 0;
    int shareCases = 0;
    int tableCases = 0;
    for (const Json &entry : reference["catalogs"].array()) {
        std::string file = entry["file"].string();
        std::string text = readFile(web + "/catalogs/" + file);
        Catalog catalog;
        try {
            catalog = Catalog::parse(text);
        } catch (const std::exception &e) {
            check(false, file + ": parse: " + e.what());
            continue;
        }
        Json raw = Json::parse(text);
        check(catalog.rom == entry["rom"].string(), file + ": rom");
        check(remoteName(catalog.remote) == entry["remote"].string(), file + ": remote");
        const auto &programs = entry["programs"].array();
        check(catalog.programs.size() == programs.size(), file + ": program count");
        size_t mostNamed = 0;
        for (size_t p = 0; p < programs.size() && p < catalog.programs.size(); p++) {
            const Json &want = programs[p];
            const Program &got = catalog.programs[p];
            std::string where = file + " " + got.name;
            check(got.key == want["key"].string(), where + ": key " + got.key);
            check(got.label == want["label"].string(), where + ": label " + got.label);
            check(got.group == want["group"].string(), where + ": group " + got.group);
            check(got.bank == want["bank"].integer(), where + ": bank");
            check(catalog.indexOf(got.key) == int(p), where + ": key is unique");
            const auto &variations = want["variations"].array();
            check(got.variations.size() == variations.size(), where + ": variation count");
            for (size_t v = 0; v < variations.size() && v < got.variations.size(); v++) {
                check(got.variations[v] == variations[v].integer(), where + ": variation");
                check(got.presets.count(got.variations[v]) == 1 && got.raw.count(got.variations[v]) == 1,
                      where + ": presets and raw for each variation");
            }
            const auto &pages = want["pages"].array();
            check(got.pages.size() == pages.size(), where + ": page count");
            for (size_t i = 0; i < pages.size() && i < got.pages.size(); i++) {
                const Page &page = got.pages[i];
                check(page.page == pages[i]["page"].integer(), where + ": page number");
                check(page.heading == pages[i]["heading"].string(), where + ": heading");
                check(page.column == pages[i]["column"].integer(), where + ": column");
                const auto &names = pages[i]["names"].array();
                const auto &lengths = pages[i]["tableLengths"].array();
                check(page.sliders.size() == names.size(), where + ": slider count");
                for (size_t s = 0; s < names.size() && s < page.sliders.size(); s++) {
                    check(page.sliders[s].name == names[s].string(), where + ": slider name");
                    check(int(page.sliders[s].table.size()) == lengths[s].integer(), where + ": table length");
                }
                for (const auto &[v, rows] : got.raw) {
                    check(rows.size() == got.pages.size() && rows[i].size() == page.sliders.size(),
                          where + ": raw shape");
                }
            }
            const auto &named = want["named"].array();
            check(got.generic.size() == named.size(), where + ": named slider count");
            for (size_t k = 0; k < named.size() && k < got.generic.size(); k++) {
                check(got.generic[k].pageIndex == named[k][0].integer() && got.generic[k].page == named[k][1].integer() &&
                          got.generic[k].slot == named[k][2].integer(),
                      where + ": slider_" + std::to_string(k + 1) + " mapping");
                check(got.genericIndexOf(got.generic[k].page, got.generic[k].slot) == int(k) + 1,
                      where + ": genericIndexOf");
            }
            if (got.generic.size() > mostNamed) {
                mostNamed = got.generic.size();
            }
        }
        check(mostNamed <= size_t(kGenericSliders), file + ": fits slider_1..36");

        for (const Json &h : entry["help"].array()) {
            std::string got = help.helpFor(h[0].string(), h[1].string());
            check(got == h[2].string(), file + ": helpFor('" + h[0].string() + "', '" + h[1].string() + "') = '" + got +
                                            "', JS '" + h[2].string() + "'");
            helpCases++;
            if (h[0].string() != "INACTIVE" && !h[0].string().empty()) {
                check(!got.empty(), file + ": no help for " + h[0].string());
            }
        }

        for (const Json &t : entry["tables"].array()) {
            std::vector<TableRun> table;
            for (const Json &run : t["table"].array()) {
                table.push_back({run[0].integer(), run[1].string()});
            }
            int rangeMax = t["rangeMax"].integer();
            check(rangeMax == catalog.rawMax(), file + ": range");
            for (const auto &[rawText, text] : t["texts"].object()) {
                bool found = false;
                std::string got = tableText(table, std::stoi(rawText), &found);
                if (text.isNull()) {
                    check(!found, file + ": tableText null at " + rawText);
                } else {
                    check(found && got == text.string(), file + ": tableText at " + rawText);
                }
                tableCases++;
            }
            for (const Json &back : t["back"].array()) {
                int got = tableRaw(table, back[0].string(), rangeMax);
                check(got == back[1].integer(), file + ": tableRaw('" + back[0].string() + "') = " +
                                                    std::to_string(got) + ", JS " + std::to_string(back[1].integer()));
                tableCases++;
            }
        }

        for (const Json &s : entry["shares"].array()) {
            const Program *program = catalog.find(s["key"].string());
            check(program != nullptr, file + ": share program " + s["key"].string());
            if (program == nullptr) {
                continue;
            }
            std::vector<std::vector<int>> stored;
            for (const Json &row : s["stored"].array()) {
                std::vector<int> line;
                for (const Json &cell : row.array()) {
                    line.push_back(cell.integer());
                }
                stored.push_back(line);
            }
            std::vector<ShareToggle> toggles;
            for (const Json &t : s["toggles"].array()) {
                toggles.push_back({t[0].string(), t[1].boolean()});
            }
            // The toggle list is the remote's, in the page's order.
            std::vector<Toggle> offered = catalog.toggles();
            check(offered.size() == toggles.size(), file + ": toggles offered");
            for (size_t i = 0; i < offered.size() && i < toggles.size(); i++) {
                check(offered[i].label == toggles[i].label, file + ": toggle order");
            }
            std::string payload = s["payload"].string();
            std::string ours = sharePayload(catalog.rom, *program, s["variation"].integer(), stored, toggles);
            check(ours == payload, file + ": share encode '" + ours + "' vs JS '" + payload + "'");

            ShareState state;
            std::vector<std::string> problems;
            bool ok = readSharePayload(payload, state, &problems);
            compareRead(s["read"], ok, state, problems, file + ": share decode " + payload);
            check(problems.empty(), file + ": no problems decoding a real link");
            check(sharePayload(state) == payload, file + ": share round trip " + payload);
            ShareState again;
            check(readSharePayload("?" + payload, again) && sharePayload(again) == payload, file + ": '?' prefix");
            // Apply the decoded moves to the preset: the stored bytes come back.
            if (ok) {
                const std::vector<std::vector<int>> *preset = program->rawFor(state.variation);
                std::vector<std::vector<int>> rebuilt = *preset;
                for (const ShareMove &m : state.moves) {
                    for (size_t i = 0; i < program->pages.size(); i++) {
                        if (program->pages[i].page == m.page) {
                            rebuilt[i][size_t(m.slot)] = m.value;
                        }
                    }
                }
                bool same = true;
                for (const SliderRef &ref : program->generic) {
                    if (rebuilt[size_t(ref.pageIndex)][size_t(ref.slot)] != stored[size_t(ref.pageIndex)][size_t(ref.slot)]) {
                        same = false;
                    }
                }
                check(same, file + ": decoded moves restore the stored bytes");
            }
            shareCases++;
        }
    }

    for (const Json &h : reference["helpExtra"].array()) {
        std::string got = help.helpFor(h[0].string(), h[1].string());
        check(got == h[2].string(), "extra helpFor('" + h[0].string() + "', '" + h[1].string() + "') = '" + got +
                                        "', JS '" + h[2].string() + "'");
        helpCases++;
    }
    for (const Json &m : reference["shareMalformed"].array()) {
        ShareState state;
        std::vector<std::string> problems;
        bool ok = readSharePayload(m[0].string(), state, &problems);
        compareRead(m[1], ok, state, problems, "link '" + m[0].string() + "'");
        shareCases++;
    }

    // Number() as the links use it.
    check(jsNumber("") == 0 && jsNumber(" 12 ") == 12 && jsNumber("0x10") == 16 && jsNumber("1e2") == 100 &&
              std::isnan(jsNumber("1.2.3")) && std::isnan(jsNumber("x")) && std::isnan(jsNumber("0x")),
          "jsNumber");

    std::printf("catalog tests: %d checks, %d failed (%d helpFor cases, %d share links, %d table lookups)\n", checks,
                failures, helpCases, shareCases, tableCases);
    if (failures == 0) {
        return 0;
    }
    return 1;
}
