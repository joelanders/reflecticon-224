// The plugin: the machine run inside the audio callback at 48 kHz, with
// rate_bridge.hpp between it and the host's rate.
//
// Threads and what each owns:
//   boot thread     (one at a time, a "job") finds the user's ROM sets
//                   (source/roms RomLibrary), builds a LiveMachine for the
//                   chosen set, boots it with silence for boot_frames,
//                   replays a restored state into it (its own Scheduler,
//                   pumped), and offers it through `incoming_`.
//   audio thread    processBlock: takes an offered machine at the start of a
//                   block (the old one is parked in `retired_`), drains the
//                   editor's CommandQueue, polls the host parameters
//                   (Scheduler::pollHost), and renders through the Scheduler
//                   (operator tasks and direct parameters at its 128-frame
//                   grid) and the rate bridge. Until a machine runs: a dry wire.
//   message thread  a timer: WriteBack::poll (firmware -> host parameters),
//                   CommandQueue::flush, the UiSnapshot for the editor, and
//                   destroying retired machines. The editor's UiCommands.
// No machine is ever created or destroyed on the audio thread.
//
// Parameters: the APVTS holds lexparams' 47 parameters in index order
// (source/params/layout.hpp; IDs append-only). State: source/state's text
// format (ROM-set hash + share payload + direct parameters); restore boots
// the saved set and replays the payload on the boot thread, then swaps.
// The operator (source/operator) plugs in behind OperatorPort
// (operator_port.hpp); until then operator tasks are refused.
#pragma once
#include "live_machine.hpp"
#include "rate_bridge.hpp"
#include "session.hpp"
#include "../editor/ui_interface.hpp"
#include "../source/params/command_queue.hpp"
#include "../source/params/scheduler.hpp"
#include "../source/params/seqlock.hpp"
#include "../source/params/writeback.hpp"
#include "../source/roms/recognize.hpp"
#include "../source/state/plugin_state.hpp"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class PluginProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    // Internal frames of silence the machine runs before it is heard: 16 s of
    // machine time, the web page's boot (bench/record_timeline.mjs sleeps 16 s).
    static constexpr uint64_t boot_frames = 16 * 48000;

    enum class Status { Idle, NoRoms, Booting, Running, Stopped };

    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String &) override;

    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    Status status() const {
        return status_.load();
    }

    // Whether a booted machine is running or waiting for the next block.
    bool machine_offered() const {
        return incoming_.load() != nullptr || status_.load() == Status::Running;
    }

    // Internal frames the live machine has rendered since power-on (0 before
    // it is live). Updated at the end of each block.
    uint64_t machine_frame() const {
        return machine_frame_.load();
    }

    // Block until the current boot job has finished (its machine offered, or
    // no ROMs, or stopped), at most `seconds`. True if it finished. Not on
    // the audio thread. (LEXPLUG_WAIT_FOR_BOOT=1 makes prepareToPlay call it.)
    bool wait_for_boot(double seconds);

    // Test hook: raw control inputs at internal frames, given to the machine
    // exactly as Session::render does. Must be called before the first
    // prepareToPlay (the first boot takes them).
    void schedule_controls(std::vector<lexplug::ControlEvent> events);

    // Which DAC outputs (0-3 = A-D) go to the left and right host channels:
    // sets the output_left/output_right parameters.
    void set_output_pair(int left, int right);

    // The message thread's periodic work (the timer calls it; console tests
    // without a message loop may call it themselves).
    void service_message_thread();

    // For the editor (message thread).
    void readUiSnapshot(lexui::UiSnapshot &out) const;
    lexui::UiCommands &uiCommands();
    juce::AudioProcessorValueTreeState &parameters() {
        return apvts_;
    }

    // Host-visible text: the program's number and name, a slider's value
    // from the running program's catalog table (any thread).
    juce::String slider_text(int k, float value) const;
    juce::String program_text(float value) const;

    // Diagnostics for tests (read only between processBlock calls).
    const lexparams::Scheduler &scheduler() const {
        return scheduler_;
    }

    // A host parameter by lexparams index.
    juce::RangedAudioParameter *param(int index) const {
        return params_[index];
    }

private:
    class Commands;
    class Host;

    // What a boot job does (see run_job).
    struct Job {
        enum class Source { Scan, Location, Set };
        Source source = Source::Scan;
        juce::File location;                          // Source::Location
        lexplug::roms::RomSet set;                    // Source::Set
        bool restore = false;                         // replay `state`
        lexstate::PluginState state;
        std::vector<lexplug::ControlEvent> controls;
    };

    // Meters, published by the audio thread each block.
    struct Meters {
        double seconds = 0.0;
        double headroomHit[2][5];
        double gainUsed[2][4];
    };

    void timerCallback() override;
    void start_job(Job job);
    void run_job(Job job, uint32_t id);
    void offer(std::unique_ptr<lexplug::LiveMachine> machine);
    void collect_retired();
    void cancel_job();
    void render(const float *l, const float *r, float *out_l, float *out_r, int frames);
    void render_chunk(const float *l, const float *r, float *out_l, float *out_r, int frames);
    void publish_meters();
    void set_status_text(const std::string &text);
    void set_param(int index, float value);
    const lexcat::Catalog *live_catalog() const;

    juce::AudioProcessorValueTreeState apvts_;
    juce::RangedAudioParameter *params_[lexparams::kParamCount] = {};
    float host_values_[lexparams::kParamCount] = {};

    // Audio thread.
    lexplug::RateBridge bridge_;
    std::vector<float> four_[4];            // the four DAC outputs of one chunk
    std::vector<float> gained_[2];          // the input after the level
    lexplug::DirectMix mix_;
    lexplug::MachineSink sink_{mix_};
    lexparams::Scheduler scheduler_;
    lexplug::LiveMachine *live_ = nullptr;
    bool stopped_now_ = false;
    Meters meters_{};
    lexparams::Seqlock<Meters> meter_channel_;

    // Between threads.
    lexparams::CommandQueue queue_;          // message thread -> audio thread
    std::atomic<lexplug::LiveMachine *> incoming_{nullptr};
    std::atomic<lexplug::LiveMachine *> retired_{nullptr};
    std::atomic<Status> status_{Status::Idle};
    std::atomic<uint64_t> machine_frame_{0};
    std::atomic<uint32_t> live_job_{0};
    std::atomic<const lexcat::Catalog *> live_catalog_{nullptr};

    // Boot jobs.
    std::mutex job_mutex_;                   // start_job / cancel_job
    std::thread job_thread_;
    std::atomic<bool> job_cancel_{false};
    std::atomic<bool> job_running_{false};
    uint32_t job_id_ = 0;
    bool prepared_ = false;
    bool first_boot_done_ = false;
    std::vector<lexplug::ControlEvent> pending_controls_;
    bool pending_restore_ = false;
    lexstate::PluginState pending_state_;

    // Shared with the boot thread (under info_mutex_).
    mutable std::mutex info_mutex_;
    std::vector<lexplug::roms::RomSet> sets_;             // the sets found, for the dropdown
    std::map<uint32_t, std::string> job_hash_;           // job id -> its set's catalog hash
    std::string status_text_;
    std::string booting_name_;
    lexstate::StateKeeper keeper_;

    // Message thread.
    lexparams::WriteBack write_back_;
    std::unique_ptr<Host> host_params_;
    std::unique_ptr<Commands> commands_;
    lexparams::Seqlock<lexui::UiSnapshot> ui_;
    uint32_t ui_serial_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
