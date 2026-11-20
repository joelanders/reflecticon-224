#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "embedded.hpp"

PluginEditor::PluginEditor(PluginProcessor &processor)
    : AudioProcessorEditor(processor),
      view_(lexplug::embeddedCatalogs(), lexplug::embeddedHelp(),
            [&processor](lexui::UiSnapshot &s) { processor.readUiSnapshot(s); }, processor.uiCommands()) {
    addAndMakeVisible(view_);
    setResizable(true, true);
    setResizeLimits(560, 360, 2400, 2000);
    setSize(1040, 820);
}

PluginEditor::~PluginEditor() = default;

void PluginEditor::resized() {
    view_.setBounds(getLocalBounds());
}
