// Where the user's Lexicon ROM chips are, and which firmware sets they make.
// The JUCE side of ROM discovery; recognition itself is JUCE-free
// (recognize.hpp, the twin of the web demo's firmware.js).
//
// Search order (RomLibrary::scan):
//   1. the LEXICON224_ROMPATH environment variable (a folder or a .zip);
//   2. the path remembered in the PropertiesFile (App Support), set by remember();
//   3. the drop folder ~/Library/Application Support/<productFolder>/roms/;
//   4. otherwise nothing: the editor offers a folder/zip chooser, then calls
//      scanLocation() on the choice and remember() when it holds a set.
// The first location that holds a known, supported set wins.
//
// A location is a folder (searched recursively, symlinks followed without
// cycles), a .zip (folders inside it too), or a single chip file. Inside a
// folder, .zip files are expanded one level, as the page expands dropped
// zips; zips inside zips are not. As the page, zip entries under __MACOSX/
// are skipped and entries are named by their last path component.
//
// No boot happens here: scanning reads and hashes small files only. ROM bytes
// never leave memory (nothing is written, not even to the settings file,
// which stores a path).
#pragma once
#include "recognize.hpp"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace lexplug::roms {

class RomLibrary {
public:
    // The one place the product's folder/settings name lives: PLUGIN_SUPPORT_FOLDER in
    // CMakeLists.txt, kept from the plugin's old name (Reflexion224) so existing users keep their ROMs.
    static constexpr const char *productFolder = "Reflexion224";
    static constexpr const char *envVar = "LEXICON224_ROMPATH";
    static constexpr const char *settingsKey = "romPath";

    // Scan limits: keep a scan cheap even when pointed at a large folder.
    static constexpr juce::int64 maxChipBytes = 64 * 1024;          // chips are 2-4 KiB
    static constexpr juce::int64 maxZipBytes = 64 * 1024 * 1024;
    static constexpr int maxFiles = 4096;

    struct ScanResult {
        juce::File source;                 // the location that was used, or File() if none
        juce::String origin;               // "LEXICON224_ROMPATH", "remembered", "drop folder", "chosen", or ""
        std::vector<RomSet> sets;          // as find_sets
        juce::String message;              // for the status line when nothing usable was found

        bool usable() const {
            for (const RomSet &set : sets) {
                if (set.known && set.supported) {
                    return true;
                }
            }
            return false;
        }
    };

    // Test hook: LEXICON224_SUPPORT_DIR replaces the App Support folder (and
    // with it the settings file and the drop folder), so tests never touch
    // the user's real Library.
    static constexpr const char *supportDirVar = "LEXICON224_SUPPORT_DIR";

    // ~/Library/Application Support/<productFolder> on macOS.
    static juce::File appSupportDir() {
        const char *over = std::getenv(supportDirVar);
        if (over != nullptr && *over != 0) {
            return juce::File(juce::String::fromUTF8(over));
        }
        juce::File library = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
        library = library.getChildFile("Application Support");
#endif
        return library.getChildFile(productFolder);
    }

    // <appSupportDir>/<productFolder>.settings (where PropertiesFile::Options
    // with osxLibrarySubFolder "Application Support" would put it on macOS).
    static juce::File settingsFile() {
        return appSupportDir().getChildFile(juce::String(productFolder) + ".settings");
    }

    static juce::PropertiesFile::Options settingsOptions() {
        juce::PropertiesFile::Options o;
        o.applicationName = productFolder;
        o.filenameSuffix = ".settings";
        o.folderName = productFolder;
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    static juce::File dropFolder() {
        return appSupportDir().getChildFile("roms");
    }

    // The path remember() stored, or File() if none.
    static juce::File remembered() {
        juce::PropertiesFile props(settingsFile(), settingsOptions());
        juce::String saved = props.getValue(settingsKey);
        if (saved.isEmpty()) {
            return {};
        }
        return juce::File(saved);
    }

    // Remember a folder or zip the user chose (a path, never the bytes).
    static void remember(const juce::File &location) {
        juce::PropertiesFile props(settingsFile(), settingsOptions());
        props.setValue(settingsKey, location.getFullPathName());
        props.saveIfNeeded();
    }

    static void forget() {
        juce::PropertiesFile props(settingsFile(), settingsOptions());
        props.removeValue(settingsKey);
        props.saveIfNeeded();
    }

    // The locations scan() tries, in order, with their origin labels.
    static std::vector<std::pair<juce::File, juce::String>> searchOrder() {
        std::vector<std::pair<juce::File, juce::String>> order;
        const char *env = std::getenv(envVar);
        if (env != nullptr && *env != 0) {
            order.emplace_back(juce::File(juce::String::fromUTF8(env)), juce::String(envVar));
        }
        juce::File saved = remembered();
        if (saved != juce::File()) {
            order.emplace_back(saved, juce::String("remembered"));
        }
        order.emplace_back(dropFolder(), juce::String("drop folder"));
        return order;
    }

    // Try the search order; the first location with a known, supported set wins.
    static ScanResult scan() {
        juce::StringArray tried;
        for (const auto &[location, origin] : searchOrder()) {
            ScanResult result;
            result.sets = scanLocation(location);
            if (result.usable()) {
                result.source = location;
                result.origin = origin;
                return result;
            }
            tried.add(origin + ": " + location.getFullPathName());
        }
        ScanResult none;
        none.message = "No Lexicon ROM set found. Looked in " + tried.joinIntoString("; ") +
                       ". Choose a folder or .zip with your chip files.";
        return none;
    }

    // The sets in one location (a folder, a .zip, or a chip file).
    static std::vector<RomSet> scanLocation(const juce::File &location) {
        return find_sets(gather(location));
    }

    // The candidate files in one location, in a stable order (folders: by full
    // path; zips: in archive order, as the page).
    static std::vector<Candidate> gather(const juce::File &location) {
        std::vector<Candidate> out;
        if (location.isDirectory()) {
            juce::Array<juce::File> found = location.findChildFiles(
                juce::File::findFiles, true, "*", juce::File::FollowSymlinks::noCycles);
            std::vector<std::string> paths;
            for (const juce::File &file : found) {
                paths.push_back(file.getFullPathName().toStdString());
            }
            std::sort(paths.begin(), paths.end());
            for (const std::string &path : paths) {
                if (int(out.size()) >= maxFiles) {
                    break;
                }
                addFile(juce::File(juce::String::fromUTF8(path.c_str())), out);
            }
        } else if (location.existsAsFile()) {
            addFile(location, out);
        }
        return out;
    }

private:
    static bool isZip(const juce::File &file) {
        return file.getFileExtension().equalsIgnoreCase(".zip");
    }

    static void addFile(const juce::File &file, std::vector<Candidate> &out) {
        if (!file.existsAsFile()) {
            return;                                    // (a broken symlink)
        }
        if (isZip(file)) {
            if (file.getSize() <= maxZipBytes) {
                addZip(file, out);
            }
            return;
        }
        if (file.getSize() > maxChipBytes) {
            return;
        }
        juce::MemoryBlock block;
        if (!file.loadFileAsData(block)) {
            return;
        }
        Candidate candidate;
        candidate.name = file.getFileName().toStdString();
        candidate.origin = file.getFullPathName().toStdString();
        const auto *bytes = static_cast<const uint8_t *>(block.getData());
        candidate.bytes.assign(bytes, bytes + block.getSize());
        out.push_back(std::move(candidate));
    }

    static void addZip(const juce::File &file, std::vector<Candidate> &out) {
        juce::ZipFile zip(file);
        for (int i = 0; i < zip.getNumEntries(); i++) {
            if (int(out.size()) >= maxFiles) {
                return;
            }
            const juce::ZipFile::ZipEntry *entry = zip.getEntry(i);
            if (entry == nullptr) {
                continue;
            }
            const juce::String path = entry->filename;
            if (path.endsWith("/") || path.startsWith("__MACOSX/") || entry->isSymbolicLink) {
                continue;
            }
            if (entry->uncompressedSize > maxChipBytes) {
                continue;
            }
            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(i));
            if (stream == nullptr) {
                continue;
            }
            juce::MemoryBlock block;
            stream->readIntoMemoryBlock(block);
            Candidate candidate;
            candidate.name = path.fromLastOccurrenceOf("/", false, false).toStdString();
            candidate.origin = (file.getFullPathName() + ":" + path).toStdString();
            const auto *bytes = static_cast<const uint8_t *>(block.getData());
            candidate.bytes.assign(bytes, bytes + block.getSize());
            out.push_back(std::move(candidate));
        }
    }
};

}  // namespace lexplug::roms
