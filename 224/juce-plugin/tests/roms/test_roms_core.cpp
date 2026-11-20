// The JUCE-free ROM recognition core (source/roms/recognize.hpp, sha256.hpp).
//
//   test_roms_core [--boot] LABEL=PATH [LABEL=PATH ...]
//
// Always: SHA-256 against the FIPS 180-4 vectors, chip_base/model_of against
// the page's regexes, the embedded table against itself. Then, for each
// LABEL=PATH (a folder, walked recursively and sorted by full path as
// RomLibrary::gather does; .zip files are skipped here, the JUCE harness reads
// them), one canonical line per recognised set, in web_reference.mjs's format.
// With --boot, every supported known set is loaded into lexplug::Engine and
// 16 s of silence rendered at 48 kHz; the panel digits / LARC text must not
// be blank.
#include "../../source/roms/recognize.hpp"
#ifdef ROMS_TEST_BOOT
#include "../../source/engine.hpp"
#endif
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace lexplug::roms;

static int failures = 0;

static void check(bool ok, const std::string &what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        failures++;
    }
}

static void test_sha256() {
    auto of = [](const std::string &s) {
        return Sha256::of(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    };
    check(of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
    check(of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
    check(of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 448-bit");
    check(of("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1",
          "sha256 896-bit");
    Sha256 million;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; i++) {
        million.update(reinterpret_cast<const uint8_t *>(chunk.data()), chunk.size());
    }
    check(Sha256::hex(million.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "sha256 million a");
    // Every length across a block boundary agrees with a one-shot hash fed byte by byte.
    for (size_t n = 50; n < 140; n++) {
        std::string s(n, 'x');
        Sha256 bytewise;
        for (char c : s) {
            uint8_t b = uint8_t(c);
            bytewise.update(&b, 1);
        }
        check(Sha256::hex(bytewise.finish()) == of(s), "sha256 bytewise length " + std::to_string(n));
    }
}

static void test_names() {
    struct Case {
        const char *name;
        int base;          // -1000 = none
    };
    const Case cases[] = {
        {"SBC1 2716.BIN", 0x0000}, {"SBC2 2716.BIN", 0x0800}, {"sbc 3.bin", 0x1000},
        {"NVS1 2732.BIN", 0x8000}, {"NVS8 2732.BIN", 0xf000}, {"nvs\t2", 0x9000},
        {"ROM1 2716.BIN", 0x0000}, {"L224 v2_2 ROM4 2716.BIN", 0x1800},
        {"ROM5 2716.BIN", -1000}, {"ROM12.bin", -1000}, {"3500401660_1.BIN", -1000},
        {"ROM 12 ROM2", 0x0800}, {"SBC0.bin", -0x800}, {"x.jpg", -1000},
        {"SBCx SBC2", 0x0800}, {"NVS1 ROM3", 0x8000},
    };
    for (const Case &c : cases) {
        std::optional<int> base = chip_base(c.name);
        int got = -1000;
        if (base.has_value()) {
            got = *base;
        }
        check(got == c.base, std::string("chip_base(\"") + c.name + "\") = " + std::to_string(got));
    }
    check(model_of({"ROM1 2716.BIN", "ROM2 2716.BIN"}) == 1, "model_of 224");
    check(model_of({"SBC1 2716.BIN", "NVS1 2732.BIN"}) == 0, "model_of 224X");
    check(model_of({"SBC ROM1.bin"}) == 0, "model_of ROM with SBC is 224X");
    check(model_of({"ROM5.bin"}) == 0, "model_of ROM5");
}

static void test_table() {
    std::vector<std::string> seen;
    for (int s = 0; s < data::known_set_count; s++) {
        const data::KnownSet &set = data::known_sets[s];
        check(set.chip_count > 0 && set.chip_count <= 16, std::string(set.name) + " chip count");
        for (int c = 0; c < set.chip_count; c++) {
            check(std::string(set.chips[c].sha256).size() == 64, std::string(set.name) + " digest length");
            if (c > 0) {
                check(set.chips[c].base > set.chips[c - 1].base, std::string(set.name) + " bases ascend");
            }
        }
        seen.push_back(set.name);
    }
    check(data::known_set_count == 8, "eight known sets");
}

// The candidates in a folder, as RomLibrary::gather (minus zips).
static std::vector<Candidate> gather(const fs::path &root) {
    std::vector<std::string> paths;
    std::error_code ec;
    if (fs::is_directory(root, ec)) {
        auto options = fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied;
        for (auto it = fs::recursive_directory_iterator(root, options, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) {
                break;
            }
            if (fs::is_regular_file(it->path(), ec)) {
                paths.push_back(it->path().string());
            }
        }
    } else if (fs::is_regular_file(root, ec)) {
        paths.push_back(root.string());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<Candidate> out;
    for (const std::string &path : paths) {
        fs::path p(path);
        std::string ext = p.extension().string();
        for (char &c : ext) {
            c = char(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".zip") {
            std::printf("# skipping zip %s (read by the JUCE harness)\n", path.c_str());
            continue;
        }
        if (fs::file_size(p, ec) > 64 * 1024) {
            continue;
        }
        std::ifstream in(p, std::ios::binary);
        Candidate c;
        c.name = p.filename().string();
        c.origin = path;
        c.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        out.push_back(std::move(c));
    }
    return out;
}

static void print_sets(const std::string &label, const std::vector<RomSet> &sets) {
    if (sets.empty()) {
        std::printf("%s\t(none)\n", label.c_str());
    }
    for (const RomSet &set : sets) {
        std::string bases;
        for (const Chip &chip : set.chips) {
            char text[16];
            std::snprintf(text, sizeof text, "%x", chip.base);
            if (!bases.empty()) {
                bases += ",";
            }
            bases += text;
        }
        std::printf("%s\t%s\tmodel=%d\tknown=%d\tsupported=%d\thash=%s\tbases=%s\n", label.c_str(), set.name.c_str(),
                    set.model, int(set.known), int(set.supported), set.catalog_hash.c_str(), bases.c_str());
    }
}

#ifdef ROMS_TEST_BOOT
static void boot(const RomSet &set) {
    lexplug::Engine engine(set.model);
    load_set(set, engine);
    const int block = 4800;
    std::vector<float> silence(block, 0.0f), a(block), b(block), c(block), d(block);
    float *out[4] = {a.data(), b.data(), c.data(), d.data()};
    std::vector<uint8_t> before(engine.panel_digits(), engine.panel_digits() + 9);
    std::string failure;
    try {
        for (int i = 0; i < 160; i++) {                 // 16 s at 48 kHz
            engine.render(silence.data(), silence.data(), out, block);
        }
    } catch (const std::exception &e) {
        failure = e.what();
    }
    const uint8_t *digits = engine.panel_digits();
    bool lit = false;
    std::string hex;
    for (int i = 0; i < 9; i++) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", digits[i]);
        hex += t;
        if (digits[i] != 0xff && digits[i] != 0x00 && digits[i] != before[size_t(i)]) {
            lit = true;              // a lit segment the firmware wrote
        }
    }
    std::string larc = engine.larc_text();
    bool larc_text = false;
    for (char ch : larc) {
        if (ch != ' ' && ch != 0) {
            larc_text = true;
        }
    }
    std::string status = "ok";
    if (!failure.empty()) {
        status = failure;
    }
    std::printf("boot\t%s\tdigits=%s\tlarc=%d \"%s\"\t%s\n", set.name.c_str(), hex.c_str(), int(engine.larc_connected()),
                larc.c_str(), status.c_str());
    check(failure.empty(), set.name + " boots without stopping");
    check(lit || larc_text, set.name + " shows something after 16 s");
}
#endif

int main(int argc, char **argv) {
    test_sha256();
    test_names();
    test_table();
    bool do_boot = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--boot") {
            do_boot = true;
            continue;
        }
        size_t at = arg.find('=');
        std::string label = arg.substr(0, at);
        std::vector<RomSet> sets = find_sets(gather(arg.substr(at + 1)));
        print_sets(label, sets);
        if (do_boot) {
#ifdef ROMS_TEST_BOOT
            for (const RomSet &set : sets) {
                if (set.known && set.supported) {
                    boot(set);
                }
            }
#else
            std::printf("# --boot needs -DROMS_TEST_BOOT\n");
#endif
        }
    }
    if (failures != 0) {
        std::printf("FAIL (%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
