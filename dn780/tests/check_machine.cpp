// check_machine ROM_DIR: the DN780 machine on the kit (needs the user's v2.2 EPROM image; nothing
// in this repository holds it).
//   - the kit's generic host checks (block sizes, allocation, parameter changes, instances);
//   - the readouts against the seven video settings, read from the firmware's own 8279 display
//     (the dn780 project's notes/2026-11-10-0244 §4);
//   - the factory presets from the firmware's table.
#define KIT_COUNT_ALLOCATIONS
#include "../../shared/tests/host_checks.hpp"
#include "../plugin/dn780_machine.hpp"
#include <dirent.h>
#include <fstream>

static std::vector<kit::RomFile> read_dir(const std::string& dir) {
    std::vector<kit::RomFile> files;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name, path = dir + "/" + n;
            std::ifstream f(path, std::ios::binary);
            std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
            if (b.size() == 0x4000 || b.size() == 0x8000) files.push_back({n, path, b});
        }
        closedir(d);
    }
    return files;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s ROM_DIR\n", argv[0]);
        return 2;
    }
    const auto& t = dn780plug::machine_type();
    auto files = read_dir(argv[1]);
    // pick the v2.2 image when both are in the folder
    std::vector<kit::RomFile> v22;
    for (auto& f : files)
        if (f.bytes.size() == 0x8000) v22.push_back(f);
    auto roms = kit::recognise(t.info.roms, v22);
    std::printf("ROMs: %s\n", roms.describe(t.info.roms).c_str());
    if (!roms.complete()) return 1;
    int fails = kit_test::run_host_checks(t, roms, 6 /* decay */, 20);

    // v1.5 is recognised and refused with a reason
    for (auto& f : files)
        if (f.bytes.size() == 0x4000) {
            auto r15 = kit::recognise(t.info.roms, {f});
            std::string err;
            auto m = t.boot(r15, "", &err);
            bool ok = !m && r15.slots[0].known && r15.slots[0].label == "v1.5";
            std::printf("  %s v1.5 recognised and refused: %s\n", ok ? "ok  " : "FAIL", err.c_str());
            fails += !ok;
        }

    // the video settings: program index, E031..E037, expected readouts
    struct Case {
        const char* name;
        float v[8];
        const char* want[8];
    } cases[] = {
        {"H1", {0, 0x23, 8, 4, 4, 5, 6, 1}, {"Hall", "35 ms", "8", "4", "-3", "-2", "1.3 s", "20 m"}},
        {"r1", {3, 0, 3, 3, 9, 0, 1, 0}, {"Room", "0 ms", "3", "3", "+2", "-7", "0.3 s", "8.0 m"}},
        {"H4", {0, 0x28, 9, 7, 1, 4, 0x14, 6}, {"Hall", "40 ms", "9", "7", "-6", "-3", "3.3 s", "45 m"}},
        {"rE", {6, 0x6B, 9, 0, 0, 0, 4, 0}, {"Reverse", "114 ms", "9", "", "-7", "-7", "5", ""}},
        {"nL", {5, 0, 9, 0, 4, 5, 2, 0}, {"Non-Linear", "0 ms", "9", "", "-3", "-2", "3", ""}},
        {"rE2", {6, 0x1A, 9, 0, 0, 0, 2, 0}, {"Reverse", "26 ms", "9", "", "-7", "-7", "3", ""}},
        {"P3", {1, 0x7A, 6, 8, 0, 1, 0x0E, 1}, {"Plate", "144 ms", "6", "8", "-7", "-6", "1.9 s", "12 m"}},
    };
    std::printf("video settings\n");
    for (auto& c : cases) {
        kit_test::Params p(t.info);
        for (int k = 0; k < 8; k++) p.v[size_t(k)].store(c.v[k]);
        kit::Host host(t, p.ptrs());
        std::string err;
        host.install(t.boot(roms, "", &err));
        auto r = host.readouts();
        bool ok = true;
        for (int k = 0; k < 8; k++) ok &= r.param[size_t(k)] == c.want[k];
        std::printf("  %s %-4s [%s]", ok ? "ok  " : "FAIL", c.name, r.panel.c_str());
        if (!ok)
            for (int k = 0; k < 8; k++) std::printf(" '%s'", r.param[size_t(k)].c_str());
        std::printf("\n");
        fails += !ok;
    }

    {   // the hardware-panel hook: the 8279 display RAM in the kit's segment order
        std::string err;
        auto m = t.boot(roms, "", &err);
        m->prepare(48000, 512);
        m->set_params({0, 0x23, 8, 4, 4, 5, 6, 1, 0});   // H1 from the video: predelay 35 ms, decay 1.3 s
        auto ps = m->panel();
        auto ch = [](uint8_t v) {
            const uint8_t pat[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
            for (int i = 0; i < 10; i++)
                if ((v & 0x7F) == pat[i] || (i == 6 && (v & 0x7F) == 0x7C) || (i == 7 && (v & 0x7F) == 0x27) || (i == 9 && (v & 0x7F) == 0x67))
                    return char('0' + i);
            return v ? '?' : ' ';
        };
        std::string pre, dec;
        for (int i : {13, 14, 15}) pre += ch(ps.digits.size() == 16 ? ps.digits[size_t(i)] : 0);
        for (int i : {3, 1}) {
            uint8_t v = ps.digits.size() == 16 ? ps.digits[size_t(i)] : 0;
            dec += ch(v);
            if (v & 0x80) dec += '.';
        }
        bool ok = ps.valid && pre.find("35") != std::string::npos && dec == "1.3";
        std::printf("  %s panel() digits in kit order: predelay [%s], decay [%s]\n", ok ? "ok  " : "FAIL", pre.c_str(), dec.c_str());
        fails += !ok;
    }
    {
        kit_test::Params p(t.info);
        kit::Host host(t, p.ptrs());
        std::string err;
        host.install(t.boot(roms, "", &err));
        auto ps = host.presets();
        std::printf("presets: %zu:", ps.size());
        for (auto& x : ps) std::printf(" [%s]", x.name.c_str());
        std::printf("\n");
        bool ok = ps.size() == 26 && ps[0].name == "01 Hall 1" && ps[0].values[1] == 0x16 && ps.back().name == "33 Multitap Echo";
        std::printf("  %s 26 factory memories (20 reverbs, AL nL rE, dL At Ec), H1 predelay 16h\n", ok ? "ok  " : "FAIL");
        fails += !ok;
    }
    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
