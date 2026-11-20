// kit::Processor: the AudioProcessor every machine plugin shares. A machine plugin supplies only
// a kit::MachineType (machine.hpp) and a createPluginFilter() returning a Processor for it
// (KIT_DEFINE_PLUGIN below).
//
// - Host parameters come from the machine's parameter table (APVTS); the kit::Host worker hands
//   changes to the machine.
// - Bring your own ROMs: $<rom_env> (a folder), else the paths this user chose last time (any
//   instance), else the editor's chooser or a file drop. Only paths are saved, never bytes.
// - Saved state: the parameters, the ROM paths and the machine's own state.
#pragma once
#include "../core/host.hpp"
#include <juce_audio_processors/juce_audio_processors.h>

namespace kit {

class Processor : public juce::AudioProcessor, private juce::Timer {
public:
    explicit Processor(const MachineType& type);
    ~Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return type_.info.tail_seconds; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- for the editor (message thread) ----
    juce::AudioProcessorValueTreeState params;
    const Info& info() const { return type_.info; }
    Host& host() { return *host_; }
    // Files or folders; boots on success and remembers the paths for new instances.
    void loadRoms(const juce::StringArray& paths);
    juce::String status() const { return status_; }
    // Set the parameters from a machine preset, as one gesture each (hosts record it).
    void applyPreset(const Preset& p);
    // The editor shown: true = the hardware panel (only if the machine has one). Saved with the
    // session.
    bool hasPanel() const { return !type_.info.panel.empty(); }
    bool hardwareMode() const { return hasPanel() && hardware_; }
    void setHardwareMode(bool on) { hardware_ = on; }
    // Take settings the panel put the machine in into the host parameters (PanelState::adopt).
    // Called by the timer; public for tools that run without a message loop.
    void adoptPanelSettings();

private:
    void timerCallback() override;
    bool readRoms(const juce::StringArray& paths, RomSet& out);
    void findRomsAtStart();
    void boot(const std::string& state);
    juce::PropertiesFile& settings();

    const MachineType& type_;
    std::unique_ptr<Host> host_;
    RomSet roms_;
    juce::StringArray romPaths_;
    juce::String status_ = "no ROMs";
    std::unique_ptr<juce::PropertiesFile> settings_;
    juce::AudioBuffer<float> scratch_;   // the mono mix, and a copy of a mono input
    int maxBlock_ = 0;
    bool hardware_ = false;
    uint32_t adoptedSerial_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

}  // namespace kit

// In the machine plugin's one .cpp: KIT_DEFINE_PLUGIN(dn780plug::machine_type())
#define KIT_DEFINE_PLUGIN(type_expr) \
    juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new kit::Processor(type_expr); }
