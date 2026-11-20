// The data the plugin carries (juce_add_binary_data in CMakeLists.txt): the
// web demo's catalogs (../../web-demo/page/catalogs/*.json, plus
// catalogs-extra/*.json for 224 v3.2 and 224 TEST), the measured stored-byte
// tables (catalogs-extra/stored/*.stored.json) and the slider tooltips
// (page/param_help.json). Hashes, names and display tables only; never ROM
// bytes.
//
// Parsed once per process, on first use (thread-safe), from whichever thread
// asks first (the boot thread or the editor), never the audio thread.
#pragma once
#include "../source/catalog/catalog.hpp"
#include "../source/catalog/help.hpp"

namespace lexplug {

const lexcat::CatalogLibrary &embeddedCatalogs();
const lexcat::ParamHelp &embeddedHelp();

}  // namespace lexplug
