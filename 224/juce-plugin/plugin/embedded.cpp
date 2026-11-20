#include "embedded.hpp"
#include "LexplugData.h"
#include <cstring>
#include <string_view>

namespace lexplug {

namespace {

bool endsWith(std::string_view text, std::string_view tail) {
    return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
}

lexcat::CatalogLibrary parseCatalogs() {
    lexcat::CatalogLibrary library;
    for (int i = 0; i < LexplugData::namedResourceListSize; i++) {
        std::string_view file = LexplugData::originalFilenames[i];
        if (!endsWith(file, ".json") || file == "param_help.json" || endsWith(file, ".stored.json")) {
            continue;
        }
        int size = 0;
        const char *data = LexplugData::getNamedResource(LexplugData::namedResourceList[i], size);
        if (data == nullptr) {
            continue;
        }
        try {
            library.add(std::string_view(data, size_t(size)));
        } catch (const std::exception &) {
            // A malformed catalog is left out (tests/catalog checks them all).
        }
    }
    // The measured stored-byte tables (catalogs-extra/stored/), onto their catalogs.
    for (int i = 0; i < LexplugData::namedResourceListSize; i++) {
        std::string_view file = LexplugData::originalFilenames[i];
        if (!endsWith(file, ".stored.json")) {
            continue;
        }
        int size = 0;
        const char *data = LexplugData::getNamedResource(LexplugData::namedResourceList[i], size);
        if (data == nullptr) {
            continue;
        }
        try {
            library.addStored(std::string_view(data, size_t(size)));
        } catch (const std::exception &) {
            // One that does not fit its catalog is left out (tests/stored_tables checks them all).
        }
    }
    return library;
}

lexcat::ParamHelp parseHelp() {
    for (int i = 0; i < LexplugData::namedResourceListSize; i++) {
        if (std::strcmp(LexplugData::originalFilenames[i], "param_help.json") != 0) {
            continue;
        }
        int size = 0;
        const char *data = LexplugData::getNamedResource(LexplugData::namedResourceList[i], size);
        if (data == nullptr) {
            break;
        }
        try {
            return lexcat::ParamHelp::parse(std::string_view(data, size_t(size)));
        } catch (const std::exception &) {
            break;
        }
    }
    return {};
}

}  // namespace

const lexcat::CatalogLibrary &embeddedCatalogs() {
    static const lexcat::CatalogLibrary library = parseCatalogs();
    return library;
}

const lexcat::ParamHelp &embeddedHelp() {
    static const lexcat::ParamHelp help = parseHelp();
    return help;
}

}  // namespace lexplug
