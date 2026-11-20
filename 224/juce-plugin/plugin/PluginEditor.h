// The plugin's editor: EditorView (editor/editor_view.hpp), fed
// by the processor's UiSnapshot and sending the processor's UiCommands.
// Resizable; the view scrolls its content.
#pragma once
#include "../editor/editor_view.hpp"
#include <juce_audio_processors/juce_audio_processors.h>

class PluginProcessor;

class PluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(PluginProcessor &processor);
    ~PluginEditor() override;

    void resized() override;

private:
    lexui::EditorView view_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
