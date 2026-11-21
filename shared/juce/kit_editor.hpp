// kit::Editor: a plain, software-native editor built from the machine's parameter table, the same
// for every machine. One row per parameter (slider, menu or switch) with the machine's own readout
// beside it; the front panel's display as a strip when the machine has one; factory presets; the
// ROM chooser (or drop files on the window) and a status line.
//
// A machine with a hardware panel (Info::panel) gets a toggle between that panel (kit::PanelView,
// photo-faithful, raw keys) and these easy controls; both play the same machine.
#pragma once
#include "kit_panel.hpp"
#include "kit_processor.hpp"
#include <juce_gui_basics/juce_gui_basics.h>

namespace kit {

class Editor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit Editor(Processor& p);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void refreshPresets();
    void chooseRoms();
    void setMode(bool hardware);
    void layoutSize();
    void layoutNative();
    int easyHeight() const;   // the rows' part of the easy layout

    struct Row {
        juce::Label name, readout;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
        std::unique_ptr<juce::ComboBox> sync;   // Param::tempo_sync: off or a note value
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncA;
        int group = -1;   // index in groups_, or -1: the shared controls under the boxes
        juce::Component* control() const {
            return slider ? (juce::Component*)slider.get() : combo ? (juce::Component*)combo.get() : toggle.get();
        }
    };

    Processor& proc_;
    juce::Label title_, panel_, status_;
    juce::ComboBox presets_;
    juce::TextButton romsButton_{"Choose ROMs..."};
    juce::TextButton modeButton_;
    std::unique_ptr<PanelView> panelView_;
    juce::TooltipWindow tooltips_{this, 600};
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<Preset> presetList_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool hasPanel_ = false;
    std::vector<juce::String> groups_;          // Param::group names, in order
    std::vector<juce::Rectangle<int>> boxes_;   // their boxes, in the native layout
    int nativeW_ = 0, nativeH_ = 0;   // the layout's own size; the window shows it scaled
    uint64_t seenUpdates_ = ~0ull;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

}  // namespace kit
