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
    return l;
}

Processor::Processor(const MachineType& type)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "params", makeLayout(type.info)), type_(type) {
    std::vector<const std::atomic<float>*> raw;
    for (auto& p : type_.info.params) raw.push_back(params.getRawParameterValue(p.id));
    host_ = std::make_unique<Host>(type_, raw);
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

void Processor::timerCallback() {
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
        const float* ins[2] = {s0, s1};
        host_->process(ins, buffer.getWritePointer(0, at), buffer.getWritePointer(1, at), k);
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destData) {
    juce::ValueTree t(juce::Identifier(type_.info.state_tag));
    t.setProperty("version", 1, nullptr);
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
    host_->sync();   // the session's parameters before the next block (offline renders)
}

}  // namespace kit
