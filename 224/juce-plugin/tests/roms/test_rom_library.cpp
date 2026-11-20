// The JUCE ROM layer (source/roms/rom_library.h): folders, zips, search order.
//
//   test_rom_library SCRATCH_HOME ROMS_FOR_DROP ZIP_FOR_REMEMBER DIR_FOR_ENV EMPTY_DIR [LABEL=PATH ...]
//
// LEXICON224_SUPPORT_DIR is pointed at SCRATCH_HOME first, so the settings
// file and the drop folder live there, never in the user's real Library. Each LABEL=PATH is
// scanned with RomLibrary::scanLocation and printed in web_reference.mjs's
// canonical format (for diffing against the page's own findSets).
#include "../../source/roms/rom_library.h"
#include <cstdio>
#include <cstdlib>

using lexplug::roms::RomLibrary;
using lexplug::roms::RomSet;

static int failures = 0;

static void check(bool ok, const juce::String &what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.toRawUTF8());
        failures++;
    }
}

static void print_sets(const std::string &label, const std::vector<RomSet> &sets) {
    if (sets.empty()) {
        std::printf("%s\t(none)\n", label.c_str());
    }
    for (const RomSet &set : sets) {
        std::string bases;
        for (const auto &chip : set.chips) {
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

static juce::String first_known(const RomLibrary::ScanResult &r) {
    for (const RomSet &set : r.sets) {
        if (set.known && set.supported) {
            return set.name;
        }
    }
    return "-";
}

static void expect_scan(const juce::String &origin, const juce::String &set_name, const juce::String &step) {
    RomLibrary::ScanResult r = RomLibrary::scan();
    std::printf("scan %-28s origin=%-20s source=%s first=%s\n", step.toRawUTF8(), r.origin.toRawUTF8(),
                r.source.getFullPathName().toRawUTF8(), first_known(r).toRawUTF8());
    check(r.origin == origin, step + ": origin " + r.origin + ", expected " + origin);
    if (set_name.isNotEmpty()) {
        check(first_known(r) == set_name, step + ": first set " + first_known(r) + ", expected " + set_name);
    } else {
        check(!r.usable() && r.message.isNotEmpty(), step + ": expected nothing usable, with a message");
        std::printf("  message: %s\n", r.message.toRawUTF8());
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        std::printf("usage: test_rom_library SCRATCH_HOME ROMS_FOR_DROP ZIP_FOR_REMEMBER DIR_FOR_ENV EMPTY_DIR [LABEL=PATH ...]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;           // (not needed for files; harmless)
    setenv(RomLibrary::supportDirVar, argv[1], 1);
    unsetenv(RomLibrary::envVar);
    const juce::File drop_source(juce::String::fromUTF8(argv[2]));
    const juce::File zip(juce::String::fromUTF8(argv[3]));
    const juce::File env_dir(juce::String::fromUTF8(argv[4]));
    const juce::File empty_dir(juce::String::fromUTF8(argv[5]));

    std::printf("drop folder: %s\n", RomLibrary::dropFolder().getFullPathName().toRawUTF8());
    if (!RomLibrary::dropFolder().getFullPathName().startsWith(juce::String::fromUTF8(argv[1])) ||
        !RomLibrary::settingsFile().getFullPathName().startsWith(juce::String::fromUTF8(argv[1]))) {
        std::printf("FAIL: the support folder is not the scratch one; stopping before touching it\n");
        return 1;
    }
    RomLibrary::dropFolder().deleteRecursively();
    RomLibrary::forget();

    expect_scan("", "", "nothing anywhere");
    RomLibrary::dropFolder().createDirectory();
    for (const juce::File &chip : drop_source.findChildFiles(juce::File::findFiles, false)) {
        chip.copyFileTo(RomLibrary::dropFolder().getChildFile(chip.getFileName()));
    }
    expect_scan("drop folder", "224 v4.3", "drop folder filled");
    RomLibrary::remember(zip);
    check(RomLibrary::remembered() == zip, "remembered() returns the zip");
    expect_scan("remembered", "224XL v8.21", "remembered zip");
    setenv(RomLibrary::envVar, argv[4], 1);
    expect_scan(RomLibrary::envVar, "224 v4.3", "env var");
    setenv(RomLibrary::envVar, argv[5], 1);
    expect_scan("remembered", "224XL v8.21", "env var with no set");
    unsetenv(RomLibrary::envVar);
    RomLibrary::forget();
    expect_scan("drop folder", "224 v4.3", "forgotten");
    RomLibrary::dropFolder().deleteRecursively();

    for (int i = 6; i < argc; i++) {
        std::string arg = argv[i];
        size_t at = arg.find('=');
        print_sets(arg.substr(0, at), RomLibrary::scanLocation(juce::File(juce::String::fromUTF8(arg.substr(at + 1).c_str()))));
    }
    if (failures != 0) {
        std::printf("FAIL (%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
