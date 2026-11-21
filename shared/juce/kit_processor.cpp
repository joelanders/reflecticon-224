#include "kit_processor.hpp"
#include "kit_editor.hpp"

namespace kit {

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const Info& info) {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& p : info.params) {
        ParameterID id{p.id, 1};
        switch (p.kind) {
        case Param::Choice: {
            StringArray c;
            for (auto& s : p.choices) c.add(s);
            l.add(std::make_unique<AudioParameterChoice>(id, p.name, c, int(p.def)));
            break;
        }
        case Param::Bool: l.add(std::make_unique<AudioParameterBool>(id, p.name, p.def > 0.5f)); break;
        case Param::Int:
            l.add(std::make_unique<AudioParameterInt>(id, p.name, int(p.min), int(p.max), int(p.def),
                                                      AudioParameterIntAttributes().withLabel(p.unit)));
            break;
        case Param::Float:
            l.add(std::make_unique<AudioParameterFloat>(id, p.name, NormalisableRange<float>(p.min, p.max), p.def,
                                                        AudioParameterFloatAttributes().withLabel(p.unit)));
            break;
        }
    }
    // Sync menus for the times that may follow the host tempo
    for (auto& p : info.params)
        if (p.tempo_sync)
            l.add(std::make_unique<AudioParameterChoice>(ParameterID{Processor::syncId(p), 1}, p.name + " sync",
                                                         Processor::syncChoices(), 0));
    // The kit's own: wet/dry, after the machine (the hardware gives the effect only).
    l.add(std::make_unique<AudioParameterFloat>(
        ParameterID{"mix", 1}, "Mix", NormalisableRange<float>(0.0f, 100.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
            [](float v, int) { return String(roundToInt(v)) + " % wet"; })));
    return l;
}

Processor::Processor(const MachineType& type)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "params", makeLayout(type.info)), type_(type) {
    std::vector<const std::atomic<float>*> raw;
    for (size_t i = 0; i < type_.info.params.size(); i++) {
        auto& p = type_.info.params[i];
        raw.push_back(params.getRawParameterValue(p.id));
        if (p.tempo_sync) {   // the machine reads the synced value, which is the control's when off
            auto s = std::make_unique<Synced>();
            s->index = i;
            s->knob = raw.back();
            s->choice = params.getRawParameterValue(syncId(p));
            s->value = s->knob->load();
            raw.back() = &s->value;
            synced_.push_back(std::move(s));
        }
    }
    host_ = std::make_unique<Host>(type_, raw);
    mix_ = params.getRawParameterValue("mix");
    // a new instance opens the easy editor; KIT_EDITOR=hardware opens the panel (tests, pluginval)
    if (const char* ed = std::getenv("KIT_EDITOR"); ed && std::string(ed) == "hardware") hardware_ = true;
    findRomsAtStart();
    startTimerHz(10);
}

Processor::~Processor() { stopTimer(); }

juce::PropertiesFile& Processor::settings() {
    if (!settings_) {
        juce::PropertiesFile::Options o;
        o.applicationName = type_.info.settings_name;
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = type_.info.settings_name;
        settings_ = std::make_unique<juce::PropertiesFile>(o);
    }
    return *settings_;
}

void Processor::findRomsAtStart() {
    juce::StringArray tries;
    if (const char* env = std::getenv(type_.info.rom_env.c_str()); env && *env) tries.add(env);
    juce::StringArray remembered;
    remembered.addLines(settings().getValue("romPaths"));
    remembered.removeEmptyStrings();
    RomSet set;
    if (!tries.isEmpty() && readRoms(tries, set) && set.complete()) {
        roms_ = set;
        romPaths_ = tries;
    } else if (!remembered.isEmpty() && readRoms(remembered, set) && set.complete()) {
        roms_ = set;
        romPaths_ = remembered;
    } else {
        status_ = juce::String("no ROMs: choose or drop the files (or set ") + type_.info.rom_env + ")";
        return;
    }
    boot({});
}

bool Processor::readRoms(const juce::StringArray& paths, RomSet& out) {
    std::vector<RomFile> files;
    auto add = [&](const juce::File& f) {
        if (!any_size_fits(type_.info.roms, size_t(f.getSize()))) return;
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) return;
        auto* p = static_cast<const uint8_t*>(mb.getData());
        files.push_back({f.getFileName().toStdString(), f.getFullPathName().toStdString(),
                         std::vector<uint8_t>(p, p + mb.getSize())});
    };
    for (auto& path : paths) {
        juce::File f(path);
        if (f.isDirectory())
            for (auto& c : f.findChildFiles(juce::File::findFiles, false)) add(c);
        else if (f.existsAsFile())
            add(f);
    }
    out = recognise(type_.info.roms, files);
    return !files.empty();
}

void Processor::loadRoms(const juce::StringArray& paths) {
    RomSet set;
    if (!readRoms(paths, set) || !set.complete()) {
        status_ = "not loaded: " + juce::String(set.describe(type_.info.roms));
        return;
    }
    std::string err;
    if (!type_.boot(set, {}, &err)) {   // try it before forgetting the old set
        status_ = "cannot run: " + juce::String(err);
        return;
    }
    roms_ = set;
    romPaths_ = paths;
    settings().setValue("romPaths", paths.joinIntoString("\n"));
    settings().saveIfNeeded();
    boot({});
}

void Processor::boot(const std::string& state) {
    if (!roms_.complete()) return;
    std::string err;
    auto m = type_.boot(roms_, state, &err);
    if (!m) {
        status_ = "cannot run: " + juce::String(err);
        return;
    }
    host_->install(std::move(m));
    setLatencySamples(int(host_->latency()));
    status_ = juce::String(roms_.describe(type_.info.roms));
}

const juce::StringArray& Processor::syncChoices() {
    static const juce::StringArray c{"Off",  "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                     "1/4T", "1/4",  "1/4D",  "1/2T", "1/2",   "1/2D", "1/1",
                                     "2/1",  "4/1"};   // append only: sessions save the index
    return c;
}

double Processor::syncBeats(int choice) {
    static const double b[] = {0,         0.125, 0.25 * 2 / 3, 0.25, 0.375, 0.5 * 2 / 3, 0.5, 0.75,
                               2.0 / 3.0, 1.0,   1.5,          4.0 / 3.0, 2.0, 3.0, 4.0, 8.0, 16.0};
    return choice > 0 && choice < int(std::size(b)) ? b[choice] : 0.0;
}

int Processor::syncChoice(size_t i) const {
    for (auto& s : synced_)
        if (s->index == i) return int(std::lround(s->choice->load()));
    return 0;
}

void Processor::updateSynced() {
    const double bpm = bpm_.load();
    for (auto& s : synced_) {
        const auto& p = type_.info.params[s->index];
        const double beats = syncBeats(int(std::lround(s->choice->load())));
        float v = s->knob->load();
        if (beats > 0 && bpm > 0) {
            // a time in ms is the note's length; a rate in Hz is one cycle per note
            const double x = p.unit == "Hz" ? bpm / 60.0 / beats : 60000.0 / bpm * beats;
            v = float(std::clamp(x, double(p.min), double(p.max)));
            if (p.kind == Param::Int) v = std::round(v);
        }
        s->value.store(v);
    }
}

void Processor::timerCallback() {
    updateSynced();
    int l = int(host_->latency());
    if (l != getLatencySamples()) setLatencySamples(l);
    adoptPanelSettings();
}

void Processor::adoptPanelSettings() {
    if (!hasPanel()) return;
    const PanelState ps = host_->panel();
    if (ps.adopt_serial == adoptedSerial_) return;
    adoptedSerial_ = ps.adopt_serial;
    if (ps.adopt.empty()) return;
    host_->adopt([&] {
        for (size_t i = 0; i < ps.adopt.size() && i < type_.info.params.size(); i++) {
            if (std::isnan(ps.adopt[i])) continue;
            auto* prm = dynamic_cast<juce::RangedAudioParameter*>(params.getParameter(type_.info.params[i].id));
            if (!prm) continue;
            if (syncChoice(i) > 0) {   // following the tempo: a new time set on the panel turns that off
                float now = 0;
                for (auto& s : synced_)
                    if (s->index == i) now = s->value.load();
                if (std::abs(ps.adopt[i] - now) < 0.5f) continue;
                if (auto* sp = params.getParameter(syncId(type_.info.params[i]))) {
                    sp->beginChangeGesture();
                    sp->setValueNotifyingHost(0.0f);
                    sp->endChangeGesture();
                }
            }
            const float v = prm->convertTo0to1(prm->getNormalisableRange().snapToLegalValue(ps.adopt[i]));
            if (std::abs(v - prm->getValue()) < 1e-6f) continue;
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(v);
            prm->endChangeGesture();
        }
    });
}

void Processor::applyPreset(const Preset& p) {
    for (size_t i = 0; i < p.values.size() && i < type_.info.params.size(); i++) {
        auto* prm = params.getParameter(type_.info.params[i].id);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(p.values[i]));
        prm->endChangeGesture();
    }
}

void Processor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    maxBlock_ = std::max(1, samplesPerBlock);
    host_->prepare(int(std::lround(sampleRate)), maxBlock_);
    setLatencySamples(int(host_->latency()));
    scratch_.setSize(2, maxBlock_);
    dry_.setSize(2, kDryRing);
    dry_.clear();
    dryAt_ = 0;
    mixPrev_ = mix_->load() * 0.01f;
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() &&
           (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nin = getTotalNumInputChannels();
    if (buffer.getNumChannels() < 2 || n == 0) return;
    const bool mono = type_.info.inputs == 1;
    if (maxBlock_ <= 0 || scratch_.getNumSamples() < maxBlock_) {   // not prepared
        buffer.clear();
        return;
    }
    if (!synced_.empty()) {   // times that follow the tempo
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm(); bpm && *bpm > 1.0) bpm_.store(*bpm);
        updateSynced();
    }
    // in chunks of the prepared block (a host may exceed it); the inputs are copied or mixed into
    // scratch_ first, since the outputs overwrite the buffer in place
    for (int at = 0; at < n; at += maxBlock_) {
        const int k = std::min(maxBlock_, n - at);
        const float* l = buffer.getReadPointer(0, at);
        const float* r = nin > 1 ? buffer.getReadPointer(1, at) : l;
        float* s0 = scratch_.getWritePointer(0);
        float* s1 = scratch_.getWritePointer(1);
        if (mono) {
            for (int i = 0; i < k; i++) s0[i] = nin > 1 ? 0.5f * (l[i] + r[i]) : l[i];
        } else {
            std::copy(l, l + k, s0);
            std::copy(r, r + k, s1);
        }
        // the dry signal, for the mix: kept until the machine's latency has passed
        float* d0 = dry_.getWritePointer(0);
        float* d1 = dry_.getWritePointer(1);
        for (int i = 0; i < k; i++) {
            d0[(dryAt_ + i) & (kDryRing - 1)] = l[i];
            d1[(dryAt_ + i) & (kDryRing - 1)] = r[i];
        }
        const float* ins[2] = {s0, s1};
        float* o0 = buffer.getWritePointer(0, at);
        float* o1 = buffer.getWritePointer(1, at);
        host_->process(ins, o0, o1, k);
        const float mix = std::clamp(mix_->load() * 0.01f, 0.0f, 1.0f);
        if (mix < 1.0f || mixPrev_ < 1.0f) {
            const int lat = std::clamp(getLatencySamples(), 0, kDryRing - maxBlock_ - 1);
            for (int i = 0; i < k; i++) {
                const float m = mixPrev_ + (mix - mixPrev_) * float(i + 1) / float(k);   // no zipper
                const int j = (dryAt_ + i - lat) & (kDryRing - 1);
                o0[i] = o0[i] * m + d0[j] * (1.0f - m);
                o1[i] = o1[i] * m + d1[j] * (1.0f - m);
            }
        }
        mixPrev_ = mix;
        dryAt_ = (dryAt_ + k) & (kDryRing - 1);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree t(juce::Identifier(type_.info.state_tag));
    t.setProperty("version", 1, nullptr);
    t.setProperty("editorScale", editorScale, nullptr);
    if (bpm_.load() > 0) t.setProperty("bpm", bpm_.load(), nullptr);   // synced times are right at load
    t.setProperty("roms", romPaths_.joinIntoString("\n"), nullptr);
    if (hasPanel()) {
        t.setProperty("editor", hardware_ ? "hardware" : "easy", nullptr);
        juce::ValueTree in("panelInputs");
        for (auto& pi : type_.info.panel.inputs) in.setProperty(juce::Identifier("i" + juce::String(pi.id)), host_->panel_input_value(pi.id), nullptr);
        t.addChild(in, -1, nullptr);
    }
    std::string ms = host_->machine_state();
    if (!ms.empty()) t.setProperty("machine", juce::Base64::toBase64(ms.data(), ms.size()), nullptr);
    t.addChild(params.copyState(), -1, nullptr);
    juce::MemoryOutputStream s(destData, false);
    t.writeToStream(s);
}

void Processor::setStateInformation(const void* data, int sizeInBytes) {
    auto t = juce::ValueTree::readFromData(data, size_t(sizeInBytes));
    if (!t.hasType(juce::Identifier(type_.info.state_tag))) return;
    if (auto p = t.getChildWithName(params.state.getType()); p.isValid()) params.replaceState(p);
    if (t.hasProperty("bpm") && bpm_.load() <= 0) bpm_.store(double(t.getProperty("bpm")));
    if (t.hasProperty("editorScale")) editorScale = std::clamp(float(t.getProperty("editorScale")), 0.5f, 3.0f);
    if (hasPanel()) {
        hardware_ = t.getProperty("editor").toString() == "hardware";
        if (auto in = t.getChildWithName("panelInputs"); in.isValid())
            for (auto& pi : type_.info.panel.inputs)
                if (in.hasProperty(juce::Identifier("i" + juce::String(pi.id))))
                    host_->panel_input(pi.id, float(in.getProperty(juce::Identifier("i" + juce::String(pi.id)))));
    }
    std::string ms;
    juce::MemoryOutputStream mb;
    if (juce::Base64::convertFromBase64(mb, t.getProperty("machine").toString()))
        ms.assign(static_cast<const char*>(mb.getData()), mb.getDataSize());
    juce::StringArray paths;
    paths.addLines(t.getProperty("roms").toString());
    paths.removeEmptyStrings();
    RomSet set;
    if (!paths.isEmpty() && paths != romPaths_ && readRoms(paths, set) && set.complete()) {
        roms_ = set;
        romPaths_ = paths;
    }
    if (!ms.empty() || !host_->has_machine()) boot(ms);
    updateSynced();
    host_->sync();   // the session's parameters before the next block (offline renders)
}

}  // namespace kit
