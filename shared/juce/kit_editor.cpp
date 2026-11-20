#include "kit_editor.hpp"

namespace kit {

namespace {
const juce::Colour kBg{0xff1d1f22}, kPanel{0xff0b0b0b}, kLed{0xffff3b2f}, kText{0xffe6e6e6}, kDim{0xff7a7d82},
    kAccent{0xffd9a441};
constexpr int kW = 560, kRowH = 30, kPad = 16, kNameW = 150, kReadW = 110;
constexpr float kPanelPxPerMm = 2.4f;   // the hardware panel's scale (a 19" face is ~1160 px)
}  // namespace

Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc_(p) {
    const auto& info = proc_.info();
    auto& lf = getLookAndFeel();
    lf.setColour(juce::Slider::thumbColourId, kAccent);
    lf.setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.6f));
    lf.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff34373c));
    lf.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2a2d31));
    lf.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3c4045));
    lf.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2d31));

    title_.setText(info.name, juce::dontSendNotification);
    title_.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(title_);

    panel_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold));
    panel_.setColour(juce::Label::textColourId, kLed);
    panel_.setColour(juce::Label::backgroundColourId, kPanel);
    panel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(panel_);

    for (auto& prm : info.params) {
        auto r = std::make_unique<Row>();
        r->name.setText(prm.name, juce::dontSendNotification);
        r->name.setColour(juce::Label::textColourId, kText);
        r->readout.setColour(juce::Label::textColourId, kAccent);
        r->readout.setJustificationType(juce::Justification::centredRight);
        r->readout.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        addAndMakeVisible(r->name);
        addAndMakeVisible(r->readout);
        if (prm.kind == Param::Choice) {
            r->combo = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < prm.choices.size(); i++) r->combo->addItem(prm.choices[i], int(i) + 1);
            r->ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc_.params, prm.id, *r->combo);
        } else if (prm.kind == Param::Bool) {
            r->toggle = std::make_unique<juce::ToggleButton>();
            r->ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc_.params, prm.id, *r->toggle);
        } else {
            r->slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
            r->sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc_.params, prm.id, *r->slider);
        }
        addAndMakeVisible(r->control());
        r->name.setTooltip(prm.help);
        if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(r->control())) tc->setTooltip(prm.help);
        rows_.push_back(std::move(r));
    }
    {   // the kit's wet/dry, after the machine's own controls
        auto r = std::make_unique<Row>();
        r->name.setText("Mix", juce::dontSendNotification);
        r->name.setColour(juce::Label::textColourId, kText);
        r->readout.setColour(juce::Label::textColourId, kAccent);
        r->readout.setJustificationType(juce::Justification::centredRight);
        r->readout.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        r->slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        r->sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc_.params, "mix", *r->slider);
        const char* help = "Dry signal against the effect, after the machine: 100 % is the effect alone, as the hardware gives it.";
        r->name.setTooltip(help);
        r->slider->setTooltip(help);
        addAndMakeVisible(r->name);
        addAndMakeVisible(r->readout);
        addAndMakeVisible(r->control());
        rows_.push_back(std::move(r));
    }

    presets_.setTextWhenNothingSelected("Factory presets");
    presets_.setTextWhenNoChoicesAvailable("(load the ROMs first)");
    presets_.onChange = [this] {
        int i = presets_.getSelectedId() - 1;
        if (i >= 0 && i < int(presetList_.size())) proc_.applyPreset(presetList_[size_t(i)]);
        presets_.setSelectedId(0, juce::dontSendNotification);
    };
    addAndMakeVisible(presets_);
    romsButton_.onClick = [this] { chooseRoms(); };
    addAndMakeVisible(romsButton_);
    status_.setColour(juce::Label::textColourId, kDim);
    status_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(status_);

    if (proc_.hasPanel()) {
        panelView_ = std::make_unique<PanelView>(proc_);
        panelView_->setTooltip(juce::String::fromUTF8(info.panel.notes.c_str()));
        addChildComponent(*panelView_);
        modeButton_.onClick = [this] { setMode(!proc_.hardwareMode()); };
        modeButton_.setTooltip("Switch between the hardware panel (the real keys, displays and LEDs) and the easy controls");
        addAndMakeVisible(modeButton_);
    }
    timerCallback();
    setResizable(true, true);   // scaled as a whole, its proportions kept (layoutSize)
    setMode(proc_.hardwareMode());
    startTimerHz(30);
}

void Editor::setMode(bool hardware) {
    proc_.setHardwareMode(hardware);
    hardware = proc_.hardwareMode();
    modeButton_.setButtonText(hardware ? "Easy controls" : "Hardware panel");
    if (panelView_) {
        panelView_->setVisible(hardware);
        if (!hardware) panelView_->releaseAll();
    }
    panel_.setVisible(!hardware);
    for (auto& r : rows_) {
        r->name.setVisible(!hardware);
        r->readout.setVisible(!hardware);
        r->control()->setVisible(!hardware);
    }
    layoutSize();
}

// The layout is made at a native size (one per mode) and drawn scaled to the window: the window
// keeps the native proportions, between half and three times the native size.
void Editor::layoutSize() {
    if (proc_.hardwareMode()) {
        const auto& L = proc_.info().panel;
        const int w = int(std::lround(L.width * kPanelPxPerMm)), h = int(std::lround(L.height * kPanelPxPerMm));
        nativeW_ = w + 2 * kPad;
        nativeH_ = kPad + 30 + 8 + h + 12 + 28 + 8 + 20 + kPad;
    } else {
        nativeW_ = kW;
        nativeH_ = kPad + 30 + 8 + 52 + 12 + int(rows_.size()) * kRowH + 12 + 28 + 8 + 20 + kPad;
    }
    const float scale = std::clamp(proc_.editorScale, 0.5f, 3.0f);
    setResizeLimits(nativeW_ / 2, nativeH_ / 2, nativeW_ * 3, nativeH_ * 3);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio(double(nativeW_) / double(nativeH_));
    setSize(int(std::lround(nativeW_ * scale)), int(std::lround(nativeH_ * scale)));
    resized();
}

Editor::~Editor() { stopTimer(); }

void Editor::paint(juce::Graphics& g) { g.fillAll(kBg); }

void Editor::resized() {
    if (nativeW_ <= 0 || nativeH_ <= 0) return;
    const float scale = float(getWidth()) / float(nativeW_);
    proc_.editorScale = scale;
    layoutNative();
    // The resize corner is placed by JUCE in window coordinates, so it stays unscaled.
    for (auto* c : getChildren())
        if (c != &tooltips_ && c != resizableCorner.get()) c->setTransform(juce::AffineTransform::scale(scale));
}

void Editor::layoutNative() {
    auto b = juce::Rectangle<int>(nativeW_, nativeH_).reduced(kPad);
    auto top = b.removeFromTop(30);
    if (panelView_) modeButton_.setBounds(top.removeFromRight(140).reduced(0, 2));
    title_.setBounds(top);
    if (proc_.hardwareMode() && panelView_) {
        b.removeFromTop(8);
        const auto& L = proc_.info().panel;
        panelView_->setBounds(b.removeFromTop(int(std::lround(L.height * kPanelPxPerMm))).withWidth(int(std::lround(L.width * kPanelPxPerMm))));
        b.removeFromTop(12);
        auto bar = b.removeFromTop(28);
        romsButton_.setBounds(bar.removeFromRight(140));
        bar.removeFromRight(8);
        presets_.setBounds(bar.removeFromRight(320));
        b.removeFromTop(8);
        status_.setBounds(b.removeFromTop(20));
        return;
    }
    b.removeFromTop(8);
    panel_.setBounds(b.removeFromTop(52));
    b.removeFromTop(12);
    for (auto& r : rows_) {
        auto row = b.removeFromTop(kRowH);
        r->name.setBounds(row.removeFromLeft(kNameW));
        r->readout.setBounds(row.removeFromRight(kReadW));
        auto c = row.reduced(6, 3);
        if (r->toggle) c = c.withWidth(40);
        r->control()->setBounds(c);
    }
    b.removeFromTop(12);
    auto bar = b.removeFromTop(28);
    romsButton_.setBounds(bar.removeFromRight(140));
    bar.removeFromRight(8);
    presets_.setBounds(bar);
    b.removeFromTop(8);
    status_.setBounds(b.removeFromTop(20));
}

void Editor::timerCallback() {
    if (panelView_ && panelView_->isVisible()) panelView_->refresh();
    auto r = proc_.host().readouts();
    const auto& info = proc_.info();
    hasPanel_ = !r.panel.empty();
    panel_.setText(hasPanel_ ? juce::String(r.panel) : juce::String("- - -"), juce::dontSendNotification);
    bool have = proc_.host().has_machine();
    for (size_t i = 0; i < rows_.size(); i++) {
        auto& row = *rows_[i];
        juce::String text;
        bool used = true;
        const juce::String id = i < info.params.size() ? juce::String(info.params[i].id) : juce::String("mix");
        if (have && i < info.params.size() && i < r.param.size()) {
            text = r.param[i];
            used = !text.isEmpty();
        } else if (auto* prm = proc_.params.getParameter(id)) {
            text = prm->getCurrentValueAsText();
        }
        row.readout.setText(used ? text : juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")), juce::dontSendNotification);
        row.name.setAlpha(used ? 1.0f : 0.4f);
        row.control()->setAlpha(used ? 1.0f : 0.4f);
    }
    juce::String st = proc_.status();
    if (!r.status.empty()) st += "  |  " + juce::String(r.status);
    status_.setText(st, juce::dontSendNotification);
    if (have && presetList_.empty()) refreshPresets();
    presets_.setVisible(!have || !presetList_.empty());   // machines without factory presets: no menu
}

void Editor::refreshPresets() {
    presetList_ = proc_.host().presets();
    presets_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < presetList_.size(); i++) presets_.addItem(presetList_[i].name, int(i) + 1);
}

void Editor::chooseRoms() {
    chooser_ = std::make_unique<juce::FileChooser>("Choose the ROM files or their folder", juce::File(), "*");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectDirectories |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& fc) {
                              juce::StringArray paths;
                              for (auto& f : fc.getResults()) paths.add(f.getFullPathName());
                              if (!paths.isEmpty()) {
                                  proc_.loadRoms(paths);
                                  presetList_.clear();
                              }
                          });
}

void Editor::filesDropped(const juce::StringArray& files, int, int) {
    proc_.loadRoms(files);
    presetList_.clear();
}

}  // namespace kit
