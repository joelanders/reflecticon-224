#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "embedded.hpp"
#include "../source/roms/rom_library.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#include <memory>
#include <stdexcept>

namespace {

// The machine's analog boards run in double precision and must see the same
// arithmetic as the web build: denormals included. A host may call us with
// flush-to-zero set (JUCE's ScopedNoDenormals, and many DAWs do), which would
// change the output, so the render clears it and puts the host's mode back.
class DenormalsOn {
public:
    DenormalsOn() : saved_(read()) {
        write(saved_ & ~flush_bits);
    }

    ~DenormalsOn() {
        write(saved_);
    }

    DenormalsOn(const DenormalsOn &) = delete;
    DenormalsOn &operator=(const DenormalsOn &) = delete;

private:
#if defined(__aarch64__)
    static constexpr uint64_t flush_bits = 1u << 24;    // FPCR.FZ
    static uint64_t read() {
        uint64_t r;
        asm volatile("mrs %0, fpcr" : "=r"(r));
        return r;
    }
    static void write(uint64_t r) {
        asm volatile("msr fpcr, %0" : : "r"(r));
    }
#elif defined(__x86_64__) || defined(_M_X64)
    static constexpr uint64_t flush_bits = 0x8040;      // MXCSR.FZ | MXCSR.DAZ
    static uint64_t read() {
        return _mm_getcsr();
    }
    static void write(uint64_t r) {
        _mm_setcsr(unsigned(r));
    }
#else
#error "DenormalsOn: unsupported architecture"
#endif

    uint64_t saved_;
};

void copy_text(char *to, size_t capacity, const std::string &text) {
    std::memset(to, 0, capacity);
    std::strncpy(to, text.c_str(), capacity - 1);
}

bool env_flag(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

// A share payload records stored bytes (share.js), but a slider parameter
// is the remote's control value: a LARC fader position or a panel POT
// READING, the catalog tables' key. They do not always agree (the 224 keeps
// BASS as pot >> 3), and a stored byte must never be sent as a pot reading.
// On a measured slider (catalogs-extra/stored/) restoreCommands has already
// turned the byte into a pot reading that stores it: kept. On an unmeasured
// one the domains agree when its table shows, at the variation's preset
// stored byte, the preset's own text. Moves on unmeasured sliders whose
// domains differ (or cannot be told) are dropped from the replay; returns
// how many. (web/share.js applyShareLink sends every stored byte as a
// control value.)
int keep_exact_moves(const lexcat::Catalog &catalog, const lexcat::ShareState &share,
                     std::vector<lexparams::Command> &commands) {
    const lexcat::Program *program = catalog.find(share.key);
    if (program == nullptr) {
        return 0;
    }
    int v = program->presetVariation(share.variation);
    auto raw = program->raw.find(v);
    auto text = program->presets.find(v);
    int dropped = 0;
    std::vector<lexparams::Command> kept;
    for (const lexparams::Command &command : commands) {
        int k = int(command.param) - lexparams::kSlider1 + 1;
        if (k < 1 || k > lexparams::kSliders || size_t(k) > program->generic.size()) {
            kept.push_back(command);
            continue;
        }
        const lexcat::SliderRef &ref = program->generic[size_t(k - 1)];
        const lexcat::Slider &slider = program->measuredSlider(ref, share.variation);
        bool agree = slider.table.empty() || slider.measured();
        if (!agree && raw != program->raw.end() && text != program->presets.end()) {
            size_t page = size_t(ref.pageIndex);
            size_t slot = size_t(ref.slot);
            if (page < raw->second.size() && slot < raw->second[page].size() && page < text->second.size() &&
                slot < text->second[page].size()) {
                bool found = false;
                std::string shown = lexcat::tableText(slider.table, raw->second[page][slot], &found);
                agree = found && shown == text->second[page][slot];
            }
        }
        if (agree) {
            kept.push_back(command);
        } else {
            dropped++;
        }
    }
    commands = kept;
    return dropped;
}

// Per-frame machine time for meters.
constexpr double kRate = 48000.0;

// The APVTS layout: lexparams' table in index order (layout.hpp's rules).
// Text callbacks are filled in by the processor's constructor through `owner`.
juce::AudioProcessorValueTreeState::ParameterLayout make_layout(PluginProcessor *owner) {
    using namespace lexparams;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kParamCount; i++) {
        const ParamInfo &info = param(i);
        juce::ParameterID id{info.id, info.versionHint};
        switch (info.kind) {
            case Kind::Program: {
                auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [owner](float value, int) { return owner->program_text(value); });
                layout.add(std::make_unique<juce::AudioParameterFloat>(id, info.name, juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                       info.defaultNorm, attributes));
                break;
            }
            case Kind::Variation: {
                juce::StringArray choices;
                for (int v = 1; v <= kVariations; v++) {
                    choices.add(juce::String(v));
                }
                layout.add(std::make_unique<juce::AudioParameterChoice>(id, info.name, choices, 0));
                break;
            }
            case Kind::Slider: {
                int k = info.sub;
                auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                    [owner, k](float value, int) { return owner->slider_text(k, value); });
                layout.add(std::make_unique<juce::AudioParameterFloat>(id, info.name, juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                       info.defaultNorm, attributes));
                break;
            }
            case Kind::Toggle:
            case Kind::Analog: {
                bool on = info.defaultNorm >= 0.5f;
                layout.add(std::make_unique<juce::AudioParameterBool>(id, info.name, on));
                break;
            }
            case Kind::Level: {
                auto attributes = juce::AudioParameterFloatAttributes().withLabel("dB");
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    id, info.name, juce::NormalisableRange<float>(kLevelMinDb, kLevelMaxDb), 0.0f, attributes));
                break;
            }
            case Kind::DryWet: {
                layout.add(std::make_unique<juce::AudioParameterFloat>(id, info.name, juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                       info.defaultNorm));
                break;
            }
            case Kind::Output: {
                int index = toInt(i, info.defaultNorm, kLarcRange);
                layout.add(std::make_unique<juce::AudioParameterChoice>(id, info.name,
                                                                        juce::StringArray{"A", "B", "C", "D"}, index));
                break;
            }
        }
    }
    return layout;
}

}  // namespace

// ---------------------------------------------------------------------
// The host's parameters for the write-back (message thread).
// ---------------------------------------------------------------------
class PluginProcessor::Host final : public lexparams::HostParams {
public:
    explicit Host(PluginProcessor &p) : p_(p) {}
    float get(int index) const override {
        return p_.params_[index]->getValue();
    }
    void set(int index, float value) override {
        juce::RangedAudioParameter *param = p_.params_[index];
        param->beginChangeGesture();
        param->setValueNotifyingHost(value);
        param->endChangeGesture();
    }

private:
    PluginProcessor &p_;
};

// ---------------------------------------------------------------------
// The editor's commands (message thread): parameter changes go to the host
// parameters (so hosts record them as automation) and to the command queue;
// the Scheduler's no-op rule makes the second arrival of a value a no-op.
// ---------------------------------------------------------------------
class PluginProcessor::Commands final : public lexui::UiCommands {
public:
    explicit Commands(PluginProcessor &p) : p_(p) {}

    void addRomLocation(const std::string &path) override {
        Job job;
        job.source = Job::Source::Location;
        job.location = juce::File(juce::String::fromUTF8(path.c_str()));
        p_.start_job(std::move(job));
    }

    void selectRomSet(int index) override {
        lexplug::roms::RomSet set;
        {
            std::lock_guard<std::mutex> lock(p_.info_mutex_);
            if (index < 0 || size_t(index) >= p_.sets_.size() || !p_.sets_[size_t(index)].supported) {
                return;
            }
            set = p_.sets_[size_t(index)];
        }
        Job job;
        job.source = Job::Source::Set;
        job.set = std::move(set);
        p_.start_job(std::move(job));
    }

    void selectProgram(int programIndex) override {
        send(lexparams::kProgram, lexparams::fromInt(lexparams::kProgram, programIndex, range()));
    }
    void selectVariation(int variation) override {
        send(lexparams::kVariation, lexparams::fromInt(lexparams::kVariation, variation, range()));
    }
    void moveSlider(int k, int raw) override {
        if (k < 1 || k > lexparams::kSliders) {
            return;
        }
        int index = lexparams::sliderParam(k);
        send(index, lexparams::fromInt(index, raw, range()));
    }
    void setToggle(const std::string &label, bool on) override {
        for (int t = 0; t < lexparams::kToggles; t++) {
            if (label == lexparams::kToggleLabels[t]) {
                int index = lexparams::toggleParam(t);
                int value = 0;
                if (on) {
                    value = 1;
                }
                send(index, lexparams::fromInt(index, value, range()));
            }
        }
    }
    void setLevelDb(float db) override {
        send(lexparams::kLevelDb, lexparams::levelNormOf(db));
    }
    void setDryWet(float wet) override {
        send(lexparams::kDryWet, lexparams::clamp01(wet));
    }
    void setOutputPair(int left, int right) override {
        send(lexparams::kOutputLeft, lexparams::fromInt(lexparams::kOutputLeft, left, range()));
        send(lexparams::kOutputRight, lexparams::fromInt(lexparams::kOutputRight, right, range()));
    }
    void setAnalog(bool on) override {
        int value = 0;
        if (on) {
            value = 1;
        }
        send(lexparams::kAnalog, lexparams::fromInt(lexparams::kAnalog, value, range()));
    }
    void gesture(lexui::UiGesture which, int k, bool begin) override {
        int index = -1;
        if (which == lexui::UiGesture::Slider && k >= 1 && k <= lexparams::kSliders) {
            index = lexparams::sliderParam(k);
        } else if (which == lexui::UiGesture::Level) {
            index = lexparams::kLevelDb;
        } else if (which == lexui::UiGesture::DryWet) {
            index = lexparams::kDryWet;
        }
        if (index < 0 || open_[index] == begin) {
            return;
        }
        open_[index] = begin;
        if (begin) {
            p_.params_[index]->beginChangeGesture();
        } else {
            p_.params_[index]->endChangeGesture();
        }
    }

private:
    lexparams::SliderRange range() const {
        lexparams::Published s = p_.scheduler_.channel().read();
        return s.machine.range();
    }

    void send(int index, float value) {
        juce::RangedAudioParameter *param = p_.params_[index];
        if (open_[index]) {
            param->setValueNotifyingHost(value);
        } else {
            param->beginChangeGesture();
            param->setValueNotifyingHost(value);
            param->endChangeGesture();
        }
        p_.queue_.push(index, value);
    }

    PluginProcessor &p_;
    bool open_[lexparams::kParamCount] = {};
};

// ---------------------------------------------------------------------

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "params", make_layout(this)) {
    for (int i = 0; i < lexparams::kParamCount; i++) {
        params_[i] = apvts_.getParameter(lexparams::param(i).id);
        jassert(params_[i] != nullptr);
        host_values_[i] = params_[i]->getValue();
    }
    host_params_ = std::make_unique<Host>(*this);
    commands_ = std::make_unique<Commands>(*this);
    lexui::UiSnapshot initial = lexui::defaultSnapshot();
    initial.status = lexui::UiStatus::Booting;
    ui_.write(initial);
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < 5; k++) {
            meters_.headroomHit[c][k] = -1.0;
        }
        for (int g = 0; g < 4; g++) {
            meters_.gainUsed[c][g] = -1.0;
        }
    }
    meter_channel_.write(meters_);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
        startTimerHz(25);
    }
}

PluginProcessor::~PluginProcessor() {
    stopTimer();
    cancel_job();
    delete incoming_.exchange(nullptr);
    delete retired_.exchange(nullptr);
    delete live_;
    live_ = nullptr;
}

void PluginProcessor::schedule_controls(std::vector<lexplug::ControlEvent> events) {
    pending_controls_ = std::move(events);
}

void PluginProcessor::set_output_pair(int left, int right) {
    lexparams::SliderRange range;
    set_param(lexparams::kOutputLeft, lexparams::fromInt(lexparams::kOutputLeft, std::clamp(left, 0, 3), range));
    set_param(lexparams::kOutputRight, lexparams::fromInt(lexparams::kOutputRight, std::clamp(right, 0, 3), range));
}

void PluginProcessor::set_param(int index, float value) {
    juce::RangedAudioParameter *param = params_[index];
    param->beginChangeGesture();
    param->setValueNotifyingHost(value);
    param->endChangeGesture();
}

// ---------------------------------------------------------------------
// Lifecycle and boot jobs
// ---------------------------------------------------------------------

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    collect_retired();
    // The block size is only a hint: room for at least 16384 samples, and
    // bigger blocks are split (RateBridge::process).
    int capacity = std::max(samplesPerBlock, 16384);
    bridge_.setup(int(std::lround(sampleRate)), capacity);
    for (auto &channel : four_) {
        channel.assign(size_t(bridge_.max_internal()), 0.0f);
    }
    for (auto &channel : gained_) {
        channel.assign(size_t(bridge_.max_internal()), 0.0f);
    }
    setLatencySamples(bridge_.latency());
    prepared_ = true;
    if (!first_boot_done_) {
        first_boot_done_ = true;
        Job job;
        job.controls = std::move(pending_controls_);
        if (pending_restore_) {
            job.restore = true;
            job.state = pending_state_;
            pending_restore_ = false;
        }
        start_job(std::move(job));
    }
    if (env_flag("LEXPLUG_WAIT_FOR_BOOT")) {
        wait_for_boot(300.0);
    }
}

void PluginProcessor::releaseResources() {
    collect_retired();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
           layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

bool PluginProcessor::wait_for_boot(double seconds) {
    auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (job_running_.load()) {
        if (std::chrono::steady_clock::now() > until) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

void PluginProcessor::cancel_job() {
    std::lock_guard<std::mutex> lock(job_mutex_);
    job_cancel_.store(true);
    if (job_thread_.joinable()) {
        job_thread_.join();
    }
    job_cancel_.store(false);
}

void PluginProcessor::start_job(Job job) {
    std::lock_guard<std::mutex> lock(job_mutex_);
    job_cancel_.store(true);
    if (job_thread_.joinable()) {
        job_thread_.join();
    }
    job_cancel_.store(false);
    uint32_t id = ++job_id_;
    job_running_.store(true);
    if (live_job_.load() == 0 || status_.load() != Status::Running) {
        status_.store(Status::Booting);
    }
    job_thread_ = std::thread([this, id, job = std::move(job)]() mutable {
        run_job(std::move(job), id);
        job_running_.store(false);
    });
}

void PluginProcessor::set_status_text(const std::string &text) {
    std::lock_guard<std::mutex> lock(info_mutex_);
    status_text_ = text;
}

// On the boot thread: find the set, build and boot its machine, replay a
// restored state, offer it to the audio thread.
void PluginProcessor::run_job(Job job, uint32_t id) {
    namespace roms = lexplug::roms;
    collect_retired();
    std::vector<roms::RomSet> sets;
    std::string wanted;
    if (job.restore) {
        wanted = job.state.rom;
    }
    bool have_list = false;
    if (job.source == Job::Source::Scan) {
        roms::RomLibrary::ScanResult result = roms::RomLibrary::scan();
        sets = result.sets;
        have_list = true;
        if (!wanted.empty() && roms::by_catalog_hash(sets, wanted) == nullptr) {
            // The project's set may be in another location of the search order.
            for (const auto &[location, origin] : roms::RomLibrary::searchOrder()) {
                juce::ignoreUnused(origin);
                std::vector<roms::RomSet> found = roms::RomLibrary::scanLocation(location);
                if (roms::by_catalog_hash(found, wanted) != nullptr) {
                    sets = found;
                    break;
                }
            }
        }
        if (sets.empty()) {
            set_status_text(result.message.toStdString());
        }
    } else if (job.source == Job::Source::Location) {
        sets = roms::RomLibrary::scanLocation(job.location);
        have_list = true;
        bool usable = false;
        for (const roms::RomSet &set : sets) {
            if (set.known && set.supported) {
                usable = true;
            }
        }
        if (usable) {
            roms::RomLibrary::remember(job.location);
        } else {
            set_status_text("No known, supported ROM set in " + job.location.getFullPathName().toStdString());
            if (live_job_.load() == 0) {
                status_.store(Status::NoRoms);
            }
            return;
        }
    }
    if (have_list) {
        std::lock_guard<std::mutex> lock(info_mutex_);
        sets_ = sets;
    }

    const roms::RomSet *chosen = nullptr;
    if (job.source == Job::Source::Set) {
        chosen = &job.set;
    } else if (!wanted.empty()) {
        chosen = roms::by_catalog_hash(sets, wanted);
        if (chosen == nullptr) {
            set_status_text("This project uses the firmware set " + wanted +
                            ", which was not found. Its settings are kept; add the ROMs to use them.");
            if (live_job_.load() == 0) {
                status_.store(Status::NoRoms);
            }
            return;
        }
    } else {
        chosen = roms::first_supported(sets);
    }
    if (chosen == nullptr || !chosen->supported) {
        if (live_job_.load() == 0) {
            status_.store(Status::NoRoms);
        }
        if (chosen != nullptr) {
            set_status_text(chosen->name + ": " + chosen->unsupported);
        }
        return;
    }

    const lexcat::Catalog *catalog = lexplug::embeddedCatalogs().find(chosen->catalog_hash);
    {
        std::lock_guard<std::mutex> lock(info_mutex_);
        booting_name_ = chosen->name;
        job_hash_[id] = chosen->catalog_hash;
        pending_hash_ = chosen->catalog_hash;
        pending_job_ = id;
        status_text_.clear();
    }
    // The dropdown shows this set from now on, until its machine goes live (the
    // message thread clears the marker then) or the job gives up here.
    struct PendingSet {
        PluginProcessor &processor;
        bool offered = false;
        ~PendingSet() {
            if (!offered) {
                processor.clear_pending_set();
            }
        }
    } pending_set{*this};
    auto machine = std::make_unique<lexplug::LiveMachine>(chosen->model, catalog);
    machine->job = id;
    copy_text(machine->hash, sizeof machine->hash, chosen->catalog_hash);
    roms::load_set(*chosen, machine->session.engine);
    bool has_test_controls = !job.controls.empty();
    machine->session.set_controls(std::move(job.controls));
    machine->session.engine.set_analog(params_[lexparams::kAnalog]->getValue() >= 0.5f);

    constexpr int chunk = 4800;
    std::vector<float> zero(chunk, 0.0f);
    std::vector<float> scratch[4];
    for (auto &s : scratch) {
        s.assign(chunk, 0.0f);
    }
    float *out[4] = {scratch[0].data(), scratch[1].data(), scratch[2].data(), scratch[3].data()};
    try {
        while (machine->session.frame() < boot_frames) {
            if (job_cancel_.load()) {
                return;
            }
            int n = int(std::min<uint64_t>(chunk, boot_frames - machine->session.frame()));
            if (!machine->render(zero.data(), zero.data(), out, n)) {
                throw std::runtime_error("the operator reported a stop");
            }
        }
        machine->reader.prepare(machine->session.engine, catalog);
        // Read the machine once as booted, before the operator's start changes
        // the display: the LARC shows the running program's "Bn Pm Vv" only
        // now, and the reader keeps the last program it saw.
        {
            lexparams::MachineState booted;
            machine->reader.read(machine->session.engine, booted);
        }
        // The operator's own start (the LARC reads its toggles), unless a
        // test timeline drives the machine.
        if (!has_test_controls && machine->port->startup()) {
            while (machine->port->busy()) {
                if (job_cancel_.load()) {
                    return;
                }
                if (!machine->render(zero.data(), zero.data(), out, chunk)) {
                    throw std::runtime_error("the operator reported a stop");
                }
            }
        }

        // Restore: replay the saved share payload through the operator, on this
        // thread, with a Scheduler of its own (share.js applyShareLink's order).
        if (job.restore && !job.state.share.empty() && catalog != nullptr) {
            lexcat::ShareState share;
            std::vector<lexparams::Command> commands;
            bool applied = false;
            int inexact = 0;
            int unreachable = 0;
            std::vector<std::vector<lexparams::Command>> approaches;
            if (lexcat::readSharePayload(job.state.share, share) &&
                lexstate::restoreCommands(*catalog, share, commands, nullptr, &unreachable, &approaches)) {
                inexact = keep_exact_moves(*catalog, share, commands) + unreachable;
                lexplug::DirectMix mix;
                lexplug::MachineSink sink(mix);
                sink.attach(machine.get());
                lexparams::Scheduler replay;
                // Phases, each run until the replay is idle: the program and
                // variation (with the first approach moves of direction-
                // dependent sliders, lexcat::approachFor), the second approach
                // moves, then the moves and toggles.
                std::vector<std::vector<lexparams::Command>> phases(1);
                std::vector<lexparams::Command> last;
                for (const lexparams::Command &command : commands) {
                    if (command.param == lexparams::kProgram || command.param == lexparams::kVariation) {
                        phases[0].push_back(command);
                    } else {
                        last.push_back(command);
                    }
                }
                for (size_t i = 0; i < approaches.size(); i++) {
                    if (i > 0) {
                        phases.emplace_back();
                    }
                    phases.back().insert(phases.back().end(), approaches[i].begin(), approaches[i].end());
                }
                phases.push_back(last);
                const uint64_t limit = 120 * 48000;   // machine time allowed for each phase
                for (const std::vector<lexparams::Command> &phase : phases) {
                    for (const lexparams::Command &command : phase) {
                        replay.submit(command);
                    }
                    uint64_t ran = 0;
                    bool stopped = false;
                    while (ran < limit && !job_cancel_.load()) {
                        replay.run(sink, 4096, [&](int, int count, bool) {
                            if (!stopped && !machine->render(zero.data(), zero.data(), out, count)) {
                                stopped = true;
                            }
                        });
                        ran += 4096;
                        if (stopped) {
                            throw std::runtime_error("the operator reported a stop during the restore");
                        }
                        lexparams::Published s = replay.channel().read();
                        if (s.ready != 0 && s.pendingMask == 0 && s.busyParam < 0) {
                            break;
                        }
                    }
                }
                applied = replay.rejectedCount() == 0 && replay.dispatchedCount() > 0 && inexact == 0;
            }
            machine->restored = true;
            if (applied) {
                std::lock_guard<std::mutex> lock(info_mutex_);
                keeper_.applied();
            } else if (inexact > 0) {
                set_status_text(std::to_string(inexact) +
                                " saved slider value(s) could not be replayed exactly (no control position stores "
                                "them) and were not replayed; the saved settings are kept.");
            } else {
                set_status_text("The saved settings could not be replayed; they are kept.");
            }
        } else if (job.restore) {
            std::lock_guard<std::mutex> lock(info_mutex_);
            if (job.state.share.empty()) {
                keeper_.applied();
            }
        }
    } catch (const std::exception &e) {
        set_status_text(std::string("the machine stopped while booting: ") + e.what());
        if (live_job_.load() == 0) {
            status_.store(Status::Stopped);
        }
        return;
    }
    if (job_cancel_.load()) {
        return;
    }
    offer(std::move(machine));
    pending_set.offered = true;
}

// The boot thread: a job ended without its machine (stopped, or superseded).
void PluginProcessor::clear_pending_set() {
    std::lock_guard<std::mutex> lock(info_mutex_);
    pending_job_ = 0;
    pending_hash_.clear();
}

void PluginProcessor::offer(std::unique_ptr<lexplug::LiveMachine> machine) {
    delete incoming_.exchange(machine.release());
}

// Destroy the machine the audio thread let go of. Never on the audio thread.
void PluginProcessor::collect_retired() {
    delete retired_.exchange(nullptr);
}

// ---------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------

void PluginProcessor::render_chunk(const float *l, const float *r, float *out_l, float *out_r, int frames) {
    if (live_ == nullptr || stopped_now_) {
        std::copy(l, l + frames, out_l);
        std::copy(r, r + frames, out_r);
        return;
    }
    const float *in_l = l;
    const float *in_r = r;
    if (mix_.gain != 1.0) {
        for (int i = 0; i < frames; i++) {
            gained_[0][size_t(i)] = float(mix_.gain * double(l[i]));
            gained_[1][size_t(i)] = float(mix_.gain * double(r[i]));
        }
        in_l = gained_[0].data();
        in_r = gained_[1].data();
    }
    float *four[4] = {four_[0].data(), four_[1].data(), four_[2].data(), four_[3].data()};
    if (!live_->render(in_l, in_r, four, frames)) {
        stopped_now_ = true;
        std::fill(out_l, out_l + frames, 0.0f);
        std::fill(out_r, out_r + frames, 0.0f);
        return;
    }
    const float *wet_l = four[std::clamp(mix_.outLeft, 0, 3)];
    const float *wet_r = four[std::clamp(mix_.outRight, 0, 3)];
    if (mix_.wet == 1.0) {
        std::copy(wet_l, wet_l + frames, out_l);
        std::copy(wet_r, wet_r + frames, out_r);
    } else if (mix_.wet == 0.0) {
        std::copy(in_l, in_l + frames, out_l);
        std::copy(in_r, in_r + frames, out_r);
    } else {
        // app.js: wet * out + (1 - wet) * dry, in double, stored as float.
        double wet = mix_.wet;
        for (int i = 0; i < frames; i++) {
            double left = wet * double(wet_l[i]) + (1.0 - wet) * double(in_l[i]);
            double right = wet * double(wet_r[i]) + (1.0 - wet) * double(in_r[i]);
            out_l[i] = float(left);
            out_r[i] = float(right);
        }
    }
}

void PluginProcessor::render(const float *l, const float *r, float *out_l, float *out_r, int frames) {
    scheduler_.run(sink_, frames, [&](int offset, int count, bool) {
        render_chunk(l + offset, r + offset, out_l + offset, out_r + offset, count);
    });
}

void PluginProcessor::publish_meters() {
    lexplug::Engine &engine = live_->session.engine;
    double now = double(live_->session.frame()) / kRate;
    meters_.seconds = now;
    for (int c = 0; c < 2; c++) {
        uint8_t held = engine.headroom(unsigned(c));
        for (int k = 0; k < 5; k++) {
            if (((held >> k) & 1) == 0) {
                meters_.headroomHit[c][k] = now;
            }
        }
    }
    unsigned used = engine.take_input_gains();
    for (int c = 0; c < 2; c++) {
        for (int g = 0; g < 4; g++) {
            if (((used >> (4 * c + g)) & 1) != 0) {
                meters_.gainUsed[c][g] = now;
            }
        }
    }
    meter_channel_.write(meters_);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &) {
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); c++) {
        buffer.clear(c, 0, n);
    }
    // Take a freshly booted machine. The old one (if any) is parked for the
    // other threads to destroy; while one is still parked, wait a block.
    if (incoming_.load(std::memory_order_acquire) != nullptr && retired_.load(std::memory_order_acquire) == nullptr) {
        lexplug::LiveMachine *next = incoming_.exchange(nullptr, std::memory_order_acq_rel);
        if (live_ != nullptr) {
            retired_.store(live_, std::memory_order_release);
        }
        live_ = next;
        stopped_now_ = false;
        sink_.attach(live_);
        sink_.set_stopped(false);
        scheduler_.machineReplaced();
        if (live_->restored) {
            scheduler_.clearPending();
        }
        live_catalog_.store(live_->catalog);
        live_job_.store(live_->job);
        status_.store(Status::Running);
    }
    // The host's values first, then the editor's commands (Scheduler::pollHost).
    for (int i = 0; i < lexparams::kParamCount; i++) {
        host_values_[i] = params_[i]->getValue();
    }
    scheduler_.pollHost(host_values_);
    queue_.drainInto(scheduler_);

    // Until a machine runs: a dry wire (the input is already in place). The
    // scheduler still sees time pass, and holds what it is asked.
    if (live_ == nullptr || stopped_now_ || buffer.getNumChannels() < 2) {
        scheduler_.run(sink_, n, [](int, int, bool) {});
        return;
    }
    const float *in_l = buffer.getReadPointer(0);
    const float *in_r = buffer.getReadPointer(1);
    float *out_l = buffer.getWritePointer(0);
    float *out_r = buffer.getWritePointer(1);
    DenormalsOn denormals;
    try {
        bridge_.process(in_l, in_r, out_l, out_r, n,
                        [this](const float *l, const float *r, float *ol, float *orr, int frames) {
                            render(l, r, ol, orr, frames);
                        });
    } catch (const std::exception &) {
        // The machine stopped (the row machine throws on an impossible
        // state). This block is silenced and the plugin is a dry wire from
        // the next one; the machine stays until replaced.
        stopped_now_ = true;
        buffer.clear();
    }
    if (stopped_now_) {
        sink_.set_stopped(true);
        status_.store(Status::Stopped);
    }
    machine_frame_.store(live_->session.frame(), std::memory_order_relaxed);
    publish_meters();
}

// ---------------------------------------------------------------------
// Message thread
// ---------------------------------------------------------------------

void PluginProcessor::timerCallback() {
    service_message_thread();
}

const lexcat::Catalog *PluginProcessor::live_catalog() const {
    return live_catalog_.load();
}

void PluginProcessor::service_message_thread() {
    collect_retired();
    write_back_.poll(scheduler_.channel(), *host_params_);
    queue_.flush();

    lexui::UiSnapshot s = lexui::defaultSnapshot();
    lexparams::Published pub = scheduler_.channel().read();
    Meters meters = meter_channel_.read();
    Status status = status_.load();
    std::string text;
    std::string live_hash;
    std::string booting;
    {
        std::lock_guard<std::mutex> lock(info_mutex_);
        // A job's set is what the dropdown shows until its machine goes live.
        if (pending_job_ != 0 && live_job_.load() == pending_job_) {
            pending_job_ = 0;
            pending_hash_.clear();
        }
        text = status_text_;
        booting = booting_name_;
        auto found = job_hash_.find(live_job_.load());
        if (found != job_hash_.end()) {
            live_hash = found->second;
        }
        s.romSetCount = int32_t(std::min<size_t>(sets_.size(), lexui::kMaxRomSets));
        s.selectedRomSet = -1;
        s.pendingRomSet = -1;
        for (int i = 0; i < s.romSetCount; i++) {
            const lexplug::roms::RomSet &set = sets_[size_t(i)];
            copy_text(s.romSets[i].name, sizeof s.romSets[i].name, set.name);
            copy_text(s.romSets[i].hash, sizeof s.romSets[i].hash, set.catalog_hash);
            s.romSets[i].model = uint8_t(set.model);
            s.romSets[i].unsupported = !set.supported;
            if (!live_hash.empty() && set.catalog_hash == live_hash) {
                s.selectedRomSet = i;
            }
            if (!pending_hash_.empty() && set.catalog_hash == pending_hash_) {
                s.pendingRomSet = i;
            }
        }
    }
    switch (status) {
        case Status::Idle:
            s.status = lexui::UiStatus::Booting;
            if (text.empty()) {
                text = "waiting for the host to start audio";
            }
            break;
        case Status::NoRoms:
            s.status = lexui::UiStatus::MissingRoms;
            break;
        case Status::Booting:
            s.status = lexui::UiStatus::Booting;
            if (text.empty()) {
                text = "booting " + booting;
            }
            break;
        case Status::Stopped:
            s.status = lexui::UiStatus::MachineStopped;
            break;
        case Status::Running:
            s.status = lexui::UiStatus::Ready;
            if (pub.ready == 0) {
                s.status = lexui::UiStatus::Booting;
            } else if (pub.busyParam == lexparams::kProgram || pub.busyParam == lexparams::kVariation) {
                s.status = lexui::UiStatus::LoadingProgram;
            } else if (pub.busyParam >= 0) {
                s.status = lexui::UiStatus::Operating;
            }
            if (job_running_.load() && text.empty()) {
                text = "booting " + booting + " (the current machine plays meanwhile)";
            }
            break;
    }
    copy_text(s.statusText, sizeof s.statusText, text);
    copy_text(s.romHash, sizeof s.romHash, live_hash);
    s.machineSeconds = meters.seconds;
    s.program = pub.machine.program;
    s.variation = pub.machine.variation;
    if (s.variation < 1) {
        s.variation = 1;
    }
    lexplug::MachineSink::Texts texts = sink_.texts().read();
    for (int k = 0; k < lexparams::kSliders; k++) {
        s.stored[k] = pub.machine.stored[k];
        copy_text(s.sliderText[k], sizeof s.sliderText[k], texts.text[k]);
    }
    s.toggles = pub.machine.toggles;
    s.togglesKnown = pub.machine.togglesKnown;
    lexparams::SliderRange range;
    s.levelDb = lexparams::levelDbOf(params_[lexparams::kLevelDb]->getValue());
    s.dryWet = params_[lexparams::kDryWet]->getValue();
    s.outLeft = uint8_t(lexparams::toInt(lexparams::kOutputLeft, params_[lexparams::kOutputLeft]->getValue(), range));
    s.outRight = uint8_t(lexparams::toInt(lexparams::kOutputRight, params_[lexparams::kOutputRight]->getValue(), range));
    s.analog = params_[lexparams::kAnalog]->getValue() >= 0.5f;
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < 5; k++) {
            s.headroomHit[c][k] = meters.headroomHit[c][k];
        }
        for (int g = 0; g < 4; g++) {
            s.gainUsed[c][g] = meters.gainUsed[c][g];
        }
    }
    s.serial = ++ui_serial_;
    ui_.write(s);
}

void PluginProcessor::readUiSnapshot(lexui::UiSnapshot &out) const {
    out = ui_.read();
}

lexui::UiCommands &PluginProcessor::uiCommands() {
    return *commands_;
}

juce::String PluginProcessor::program_text(float value) const {
    int index = lexparams::toInt(lexparams::kProgram, value, lexparams::kLarcRange);
    const lexcat::Catalog *catalog = live_catalog();
    if (catalog != nullptr && index >= 0 && size_t(index) < catalog->programs.size()) {
        return juce::String(index + 1) + " " + juce::String(catalog->programs[size_t(index)].label);
    }
    return juce::String(index + 1);
}

juce::String PluginProcessor::slider_text(int k, float value) const {
    lexparams::Published pub = scheduler_.channel().read();
    int raw = lexparams::toInt(lexparams::sliderParam(k), value, pub.machine.range());
    const lexcat::Catalog *catalog = live_catalog();
    int program = pub.machine.program;
    if (catalog != nullptr && program >= 0 && size_t(program) < catalog->programs.size()) {
        const lexcat::Program &p = catalog->programs[size_t(program)];
        if (k >= 1 && size_t(k) <= p.generic.size()) {
            bool found = false;
            std::string text = lexcat::tableText(p.slider(p.generic[size_t(k - 1)]).table, raw, &found);
            if (found) {
                return juce::String(text).trim();
            }
        }
    }
    return juce::String(raw);
}

// ---------------------------------------------------------------------
// Editor, identity, programs
// ---------------------------------------------------------------------

juce::AudioProcessorEditor *PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const {
    return true;
}

const juce::String PluginProcessor::getName() const {
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const {
    return false;
}

bool PluginProcessor::producesMidi() const {
    return false;
}

bool PluginProcessor::isMidiEffect() const {
    return false;
}

double PluginProcessor::getTailLengthSeconds() const {
    // An infinite decay ("--") holds forever; say a generous bounce tail
    // rather than infinity.
    return 30.0;
}

int PluginProcessor::getNumPrograms() {
    return 1;
}

int PluginProcessor::getCurrentProgram() {
    return 0;
}

void PluginProcessor::setCurrentProgram(int) {}

const juce::String PluginProcessor::getProgramName(int) {
    return {};
}

void PluginProcessor::changeProgramName(int, const juce::String &) {}

// ---------------------------------------------------------------------
// State (source/state/plugin_state.hpp)
// ---------------------------------------------------------------------

void PluginProcessor::getStateInformation(juce::MemoryBlock &block) {
    lexstate::PluginState live;
    lexparams::Published pub = scheduler_.channel().read();
    const lexcat::Catalog *catalog = live_catalog();
    {
        std::lock_guard<std::mutex> lock(info_mutex_);
        auto found = job_hash_.find(live_job_.load());
        if (found != job_hash_.end()) {
            live.rom = found->second;
        }
    }
    if (catalog != nullptr && pub.ready != 0) {
        live.share = lexstate::shareFromMachine(*catalog, pub.machine);
    }
    lexparams::SliderRange range;
    live.levelDb = lexparams::levelDbOf(params_[lexparams::kLevelDb]->getValue());
    live.dryWet = params_[lexparams::kDryWet]->getValue();
    live.outLeft = lexparams::toInt(lexparams::kOutputLeft, params_[lexparams::kOutputLeft]->getValue(), range);
    live.outRight = lexparams::toInt(lexparams::kOutputRight, params_[lexparams::kOutputRight]->getValue(), range);
    live.analog = params_[lexparams::kAnalog]->getValue() >= 0.5f;
    lexstate::PluginState save;
    {
        std::lock_guard<std::mutex> lock(info_mutex_);
        save = keeper_.forSave(live);
    }
    std::string text = lexstate::serialise(save);
    block.replaceAll(text.data(), text.size());
}

void PluginProcessor::setStateInformation(const void *data, int size) {
    if (data == nullptr || size <= 0) {
        return;
    }
    lexstate::PluginState state;
    if (!lexstate::parse(std::string_view(static_cast<const char *>(data), size_t(size)), state)) {
        return;
    }
    float values[lexparams::kParamCount] = {};
    lexstate::directValues(state, values);
    for (int p = lexparams::kLevelDb; p < lexparams::kParamCount; p++) {
        set_param(p, values[p]);
    }
    std::string live_hash;
    {
        std::lock_guard<std::mutex> lock(info_mutex_);
        keeper_.restored(state);
        auto found = job_hash_.find(live_job_.load());
        if (found != job_hash_.end()) {
            live_hash = found->second;
        }
        // Nothing to replay on the machine already running: done.
        if (state.share.empty() && (state.rom.empty() || state.rom == live_hash)) {
            keeper_.applied();
            return;
        }
    }
    if (!first_boot_done_) {
        // Before the first prepareToPlay: that boot restores it (no boot here:
        // a plugin scan must stay cheap).
        pending_restore_ = true;
        pending_state_ = state;
        return;
    }
    Job job;
    job.restore = true;
    job.state = state;
    start_job(std::move(job));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
