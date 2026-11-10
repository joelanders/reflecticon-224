// The JUCE plugin's non-header sources the core needs, compiled here so that
// Rack's build (which puts objects at build/<source path>.o) and the checks
// never write into ../juce-plugin.
#include "../../juce-plugin/source/catalog/catalog.cpp"
#include "../../juce-plugin/source/catalog/help.cpp"
#include "../../juce-plugin/source/catalog/json.cpp"
#include "../../juce-plugin/plugin/operator_port.cpp"
