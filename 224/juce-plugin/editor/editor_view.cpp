#include "editor_view.hpp"

#include <cmath>
#include <cstring>

namespace lexui {

namespace {

constexpr int kMargin = 16;
constexpr int kRow = 28;
constexpr int kPageWidth = 310;
constexpr int kPageGap = 16;
constexpr float kFont = 15.0f;
// A segment of a meter stays lit this long after its last event (web: 300 ms).
constexpr double kMeterHold = 0.3;
// After the user moves a control, the snapshot does not move it back for this long
// (the operator is still carrying the move to the firmware).
constexpr double kUserHold = 1.0;

const char *const kHeadroomText[5] = {"-24", "-18", "-12", "-6", "0 dB"};
const char *const kGainText[4] = {"0 dB", "+6", "+12", "+18"};
const char *const kOutputs[4] = {"A", "B", "C", "D"};

// Texts and tooltips as web/index.html has them.
const char *const kLevelTip =
    "Input gain. 0 dB: a full-scale input reaches the onset of ADC clipping (the red segment); the built-in "
    "sounds sit 6 dB below that. A microphone usually wants +12 to +24 dB.";
const char *const kHeadroomTip =
    "Input headroom, as the hardware measures it: the AIN's five level detectors, 6 dB apart, 0 dB = the onset of "
    "ADC clipping (the firmware lights its front-panel LEDs from the same registers, and its level-dependent "
    "features, Decay Optimization and Dynamic Decay, read them too).";
const char *const kGainsTip =
    "The input board's gain ranger (AGC): for each sample it picks the largest of four gains (0, +6, +12, +18 dB) "
    "that keeps the 12-bit converter below clipping, and the FPC shifts the result back: a 12-bit converter with a "
    "2-bit exponent, about 16 bits of range. Quiet input uses +18 dB. Modeled with the analog boards on; off, the "
    "gain stays at 0 dB.";
const char *const kAnalogTip =
    "The AIN/AOUT boards: elliptic filters (15 kHz on the 224X/224XL, about 8 kHz on the original 224), emphasis, "
    "sample-and-holds and the input gain ranger. Off: the nearest-sample path, for comparison. Right after "
    "switching, the reverb tail still in memory is heard through the other setting (the stored audio is "
    "pre-emphasized when the boards are on).";
const char *const kRomTip =
    "Your Lexicon chip files: a folder, a .zip or the chip files (224, 224X, 224XL). Known chips are recognized "
    "by content, whatever their names. You can also drop them on this window.";

double now() {
    return juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

juce::Font font(float size, bool bold) {
    juce::Font f{juce::FontOptions(size)};
    if (bold) {
        f = f.boldened();
    }
    return f;
}

void heading(juce::Label &label, const juce::String &text, float size) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(font(size, true));
}

std::string cString(const char *text, size_t capacity) {
    return std::string(text, strnlen(text, capacity));
}

}  // namespace

// ---------------------------------------------------------------------------
// The scrolled content: every widget.
// ---------------------------------------------------------------------------
class EditorView::Content : public juce::Component {
public:
    Content(EditorView &owner) : owner_(owner) {
        for (juce::Label *h : {&machineHeading_, &audioHeading_, &programHeading_}) {
            addAndMakeVisible(*h);
        }
        heading(machineHeading_, "Machine and firmware", 20.0f);
        heading(audioHeading_, "Audio", 20.0f);
        heading(inputHeading_, "Input", 17.0f);
        heading(outputHeading_, "Output", 17.0f);
        heading(programHeading_, "Program", 20.0f);
        addAndMakeVisible(inputHeading_);
        addAndMakeVisible(outputHeading_);

        // Machine and firmware
        romButton_.setButtonText("Choose ROMs...");
        romButton_.setTooltip(kRomTip);
        romButton_.onClick = [this] { chooseRoms(); };
        addAndMakeVisible(romButton_);
        setLabel_.setText("Firmware", juce::dontSendNotification);
        addAndMakeVisible(setLabel_);
        setBox_.setTextWhenNothingSelected("(no firmware yet)");
        setBox_.onChange = [this] {
            if (!updating_ && setBox_.getSelectedId() > 0) {
                owner_.commands_.selectRomSet(setBox_.getSelectedId() - 1);
            }
        };
        addAndMakeVisible(setBox_);
        status_.setFont(font(kFont, false));
        addAndMakeVisible(status_);

        // Input
        levelLabel_.setText("Level", juce::dontSendNotification);
        levelLabel_.setTooltip(kLevelTip);
        addAndMakeVisible(levelLabel_);
        level_.setSliderStyle(juce::Slider::LinearHorizontal);
        level_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
        level_.setRange(-24.0, 30.0, 1.0);
        level_.setDoubleClickReturnValue(true, 0.0);
        level_.textFromValueFunction = [](double db) {
            juce::String text(int(std::lround(db)));
            if (db > 0.0) {
                text = "+" + text;
            }
            return text + " dB";
        };
        level_.valueFromTextFunction = [](const juce::String &text) { return text.retainCharacters("+-0123456789").getDoubleValue(); };
        level_.setValue(0.0, juce::dontSendNotification);
        level_.updateText();
        level_.setTooltip(kLevelTip);
        level_.onValueChange = [this] {
            if (!updating_) {
                userMoved(levelUntil_);
                owner_.commands_.setLevelDb(float(level_.getValue()));
            }
        };
        level_.onDragStart = [this] { owner_.commands_.gesture(UiGesture::Level, 0, true); };
        level_.onDragEnd = [this] { owner_.commands_.gesture(UiGesture::Level, 0, false); };
        addAndMakeVisible(level_);
        makeMeter(headroomTitle_, "Headroom", headroom_, 5, kHeadroomText, kHeadroomTip);
        makeMeter(gainsTitle_, "Gain range", gains_, 4, kGainText, kGainsTip);

        // Output
        for (int side = 0; side < 2; side++) {
            juce::Label &label = outLabel_[side];
            if (side == 0) {
                label.setText("Left", juce::dontSendNotification);
            } else {
                label.setText("Right", juce::dontSendNotification);
            }
            addAndMakeVisible(label);
            juce::ComboBox &box = out_[side];
            for (int i = 0; i < 4; i++) {
                box.addItem(kOutputs[i], i + 1);
            }
            box.setTooltip("Which of the machine's four outputs (A-D, DAC order) feeds this channel. Default: left A, right C.");
            box.onChange = [this] {
                if (!updating_ && out_[0].getSelectedId() > 0 && out_[1].getSelectedId() > 0) {
                    owner_.commands_.setOutputPair(out_[0].getSelectedId() - 1, out_[1].getSelectedId() - 1);
                }
            };
            addAndMakeVisible(box);
        }
        out_[0].setSelectedId(1, juce::dontSendNotification);
        out_[1].setSelectedId(3, juce::dontSendNotification);
        wetLabel_.setText("Dry/wet", juce::dontSendNotification);
        addAndMakeVisible(wetLabel_);
        wet_.setSliderStyle(juce::Slider::LinearHorizontal);
        wet_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
        wet_.setRange(0.0, 100.0, 1.0);
        wet_.setTextValueSuffix(" % wet");
        wet_.setValue(100.0, juce::dontSendNotification);
        wet_.onValueChange = [this] {
            if (!updating_) {
                userMoved(wetUntil_);
                owner_.commands_.setDryWet(float(wet_.getValue() / 100.0));
            }
        };
        wet_.onDragStart = [this] { owner_.commands_.gesture(UiGesture::DryWet, 0, true); };
        wet_.onDragEnd = [this] { owner_.commands_.gesture(UiGesture::DryWet, 0, false); };
        addAndMakeVisible(wet_);
        analog_.setButtonText("Analog boards");
        analog_.setTooltip(kAnalogTip);
        analog_.setToggleState(true, juce::dontSendNotification);
        analog_.onClick = [this] {
            if (!updating_) {
                owner_.commands_.setAnalog(analog_.getToggleState());
            }
        };
        addAndMakeVisible(analog_);

        // Program
        program_.setTextWhenNothingSelected("(power on to read the program list)");
        program_.setTextWhenNoChoicesAvailable("(no catalog for this firmware)");
        program_.onChange = [this] {
            if (!updating_ && program_.getSelectedId() > 0) {
                owner_.commands_.selectProgram(program_.getSelectedId() - 1);
            }
        };
        addAndMakeVisible(program_);
        variationLabel_.setText("Variation", juce::dontSendNotification);
        addAndMakeVisible(variationLabel_);
        variation_.onChange = [this] {
            if (!updating_ && variation_.getSelectedId() > 0) {
                owner_.commands_.selectVariation(variation_.getSelectedId());
            }
        };
        addAndMakeVisible(variation_);
        operatorStatus_.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(operatorStatus_);
    }

    // --- snapshot -> widgets ------------------------------------------------
    void apply(const UiSnapshot &s) {
        updating_ = true;
        applyRomSets(s);
        applyStatus(s);
        const lexcat::Catalog *catalog = owner_.catalogs_.find(cString(s.romHash, sizeof s.romHash));
        if (catalog != catalog_) {
            catalog_ = catalog;
            programIndex_ = -2;
            rebuildProgramList();
            rebuildToggles();
        }
        int index = s.program;
        if (catalog_ == nullptr || index < 0 || index >= int(catalog_->programs.size())) {
            index = -1;
        }
        if (index != programIndex_) {
            programIndex_ = index;
            rebuildPages();
        }
        applyProgram(s);
        applyAudio(s);
        applyMeters(s);
        updating_ = false;
    }

    int layout(int width) {
        int x = kMargin;
        int w = juce::jmax(300, width - 2 * kMargin);
        int y = kMargin;

        machineHeading_.setBounds(x, y, w, 30);
        y += 34;
        romButton_.setBounds(x, y, 150, kRow);
        setLabel_.setBounds(x + 162, y, 80, kRow);
        setBox_.setBounds(x + 242, y, juce::jmin(360, w - 242), kRow);
        y += kRow + 6;
        status_.setBounds(x, y, w, 24);
        y += 34;

        audioHeading_.setBounds(x, y, w, 30);
        y += 34;
        int half = (w - 24) / 2;
        int xo = x + half + 24;
        inputHeading_.setBounds(x, y, half, 24);
        outputHeading_.setBounds(xo, y, half, 24);
        y += 28;
        int yi = y;
        levelLabel_.setBounds(x, yi, 60, kRow);
        level_.setBounds(x + 60, yi, half - 60, kRow);
        yi += kRow + 6;
        yi = layoutMeter(headroomTitle_, headroom_, 5, x, yi);
        yi = layoutMeter(gainsTitle_, gains_, 4, x, yi);
        int yo = y;
        outLabel_[0].setBounds(xo, yo, 50, kRow);
        out_[0].setBounds(xo + 50, yo, 64, kRow);
        outLabel_[1].setBounds(xo + 130, yo, 50, kRow);
        out_[1].setBounds(xo + 180, yo, 64, kRow);
        yo += kRow + 6;
        wetLabel_.setBounds(xo, yo, 60, kRow);
        wet_.setBounds(xo + 60, yo, half - 60, kRow);
        yo += kRow + 6;
        analog_.setBounds(xo, yo, half, kRow);
        yo += kRow + 6;
        y = juce::jmax(yi, yo) + 10;

        programHeading_.setBounds(x, y, w, 30);
        y += 34;
        program_.setBounds(x, y, juce::jmin(420, w - 200), kRow);
        variationLabel_.setBounds(program_.getRight() + 12, y, 72, kRow);
        variation_.setBounds(variationLabel_.getRight(), y, 80, kRow);
        y += kRow + 6;
        int tx = x;
        for (auto &toggle : toggles_) {
            int tw = juce::jmax(120, juce::GlyphArrangement::getStringWidthInt(font(kFont, false), toggle->getButtonText()) + 40);
            toggle->setBounds(tx, y, tw, kRow);
            tx += tw + 12;
        }
        if (!toggles_.empty()) {
            y += kRow + 4;
        }
        operatorStatus_.setBounds(x, y, w, 22);
        y += 30;

        // The pages: columns of kPageWidth, wrapping.
        int columns = juce::jmax(1, (w + kPageGap) / (kPageWidth + kPageGap));
        int column = 0;
        int rowTop = y;
        int rowBottom = y;
        for (auto &page : pages_) {
            int px = x + column * (kPageWidth + kPageGap);
            int py = rowTop;
            int height = 52 + int(page->rows.size()) * 50 + 8;
            page->group.setBounds(px, py, kPageWidth, height);
            page->heading.setBounds(px + 12, py + 22, kPageWidth - 24, 20);
            int sy = py + 48;
            for (auto &row : page->rows) {
                row->label.setBounds(px + 12, sy, kPageWidth - 24, 20);
                row->slider.setBounds(px + 8, sy + 20, kPageWidth - 16, 26);
                sy += 50;
            }
            rowBottom = juce::jmax(rowBottom, py + height);
            column++;
            if (column == columns) {
                column = 0;
                rowTop = rowBottom + kPageGap;
            }
        }
        return rowBottom + kMargin;
    }

private:
    struct SliderRow {
        int k = 0;
        const lexcat::Slider *model = nullptr;
        juce::Label label;
        juce::Slider slider;
        double until = 0;
    };
    struct PageBox {
        juce::GroupComponent group;
        juce::Label heading;
        std::vector<std::unique_ptr<SliderRow>> rows;
    };

    void userMoved(double &until) { until = now() + kUserHold; }

    void makeMeter(juce::Label &title, const char *text, std::vector<std::unique_ptr<juce::Label>> &cells, int n,
                   const char *const *names, const char *tip) {
        title.setText(text, juce::dontSendNotification);
        title.setTooltip(tip);
        addAndMakeVisible(title);
        for (int c = 0; c < 2; c++) {
            auto side = std::make_unique<juce::Label>();
            if (c == 0) {
                side->setText("L", juce::dontSendNotification);
            } else {
                side->setText("R", juce::dontSendNotification);
            }
            addAndMakeVisible(*side);
            cells.push_back(std::move(side));
            for (int i = 0; i < n; i++) {
                auto cell = std::make_unique<juce::Label>();
                cell->setText(names[i], juce::dontSendNotification);
                cell->setJustificationType(juce::Justification::centred);
                cell->setFont(font(13.0f, false));
                cell->setColour(juce::Label::outlineColourId, juce::Colours::grey);
                cell->setTooltip(tip);
                addAndMakeVisible(*cell);
                cells.push_back(std::move(cell));
            }
        }
    }

    int layoutMeter(juce::Label &title, std::vector<std::unique_ptr<juce::Label>> &cells, int n, int x, int y) {
        title.setBounds(x, y, 90, 22);
        for (int c = 0; c < 2; c++) {
            int base = c * (n + 1);
            cells[size_t(base)]->setBounds(x + 90, y, 20, 22);
            for (int i = 0; i < n; i++) {
                cells[size_t(base + 1 + i)]->setBounds(x + 112 + i * 50, y, 48, 22);
            }
            y += 24;
        }
        return y + 6;
    }

    void lightMeter(std::vector<std::unique_ptr<juce::Label>> &cells, int n, int c, int i, bool lit,
                    juce::Colour colour) {
        juce::Label &cell = *cells[size_t(c * (n + 1) + 1 + i)];
        juce::Colour background = juce::Colours::transparentBlack;
        juce::Colour text = findColour(juce::Label::textColourId);
        if (lit) {
            background = colour;
            text = juce::Colours::white;
        }
        if (cell.findColour(juce::Label::backgroundColourId) != background) {
            cell.setColour(juce::Label::backgroundColourId, background);
            cell.setColour(juce::Label::textColourId, text);
        }
    }

    void chooseRoms() {
        chooser_ = std::make_unique<juce::FileChooser>("Choose Lexicon ROMs (a folder, a .zip or chip files)",
                                                       juce::File(), "*");
        int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::canSelectDirectories |
                    juce::FileBrowserComponent::canSelectMultipleItems;
        chooser_->launchAsync(flags, [this](const juce::FileChooser &chooser) {
            for (const juce::File &file : chooser.getResults()) {
                owner_.commands_.addRomLocation(file.getFullPathName().toStdString());
            }
        });
    }

    void applyRomSets(const UiSnapshot &s) {
        juce::String signature;
        int count = juce::jlimit(0, kMaxRomSets, int(s.romSetCount));
        for (int i = 0; i < count; i++) {
            signature << cString(s.romSets[i].name, sizeof s.romSets[i].name) << "|" << int(s.romSets[i].unsupported)
                      << ";";
        }
        if (signature != romSignature_) {
            romSignature_ = signature;
            setBox_.clear(juce::dontSendNotification);
            for (int i = 0; i < count; i++) {
                const UiRomSet &set = s.romSets[i];
                juce::String name(cString(set.name, sizeof set.name));
                juce::String label = name;
                if (set.unsupported) {
                    label << " (not supported)";
                } else if (set.model == 1) {
                    label << " (224)";
                }
                setBox_.addItem(label, i + 1);
                setBox_.setItemEnabled(i + 1, !set.unsupported);
            }
        }
        setBox_.setEnabled(count > 0);
        int selected = 0;
        if (s.selectedRomSet >= 0 && s.selectedRomSet < count) {
            selected = s.selectedRomSet + 1;
        }
        if (setBox_.getSelectedId() != selected) {
            setBox_.setSelectedId(selected, juce::dontSendNotification);
        }
    }

    void applyStatus(const UiSnapshot &s) {
        juce::String text;
        juce::Colour colour = findColour(juce::Label::textColourId);
        switch (s.status) {
        case UiStatus::MissingRoms:
            text = "Missing ROMs: running dry. Choose or drop your chip files.";
            colour = juce::Colours::orangered;
            break;
        case UiStatus::Booting:
            text = "Booting...";
            break;
        case UiStatus::Ready:
            text = "Ready";
            break;
        case UiStatus::LoadingProgram:
            text = "Loading program...";
            break;
        case UiStatus::Operating:
            text = "Ready";
            break;
        case UiStatus::MachineStopped:
            text = "The machine stopped: running dry.";
            colour = juce::Colours::orangered;
            break;
        }
        juce::String detail(cString(s.statusText, sizeof s.statusText));
        if (detail.isNotEmpty()) {
            text << "  " << detail;
        }
        if (s.machineSeconds > 0.0 && s.status != UiStatus::MissingRoms) {
            text << "  (machine time " << juce::String(s.machineSeconds, 1) << " s)";
        }
        if (status_.getText() != text) {
            status_.setText(text, juce::dontSendNotification);
        }
        status_.setColour(juce::Label::textColourId, colour);

        juce::String op;
        if (s.status == UiStatus::Operating || s.status == UiStatus::LoadingProgram) {
            op = "talking to the ";
            if (catalog_ != nullptr && catalog_->remote == lexcat::Remote::Larc) {
                op << "LARC...";
            } else {
                op << "front panel...";
            }
        }
        if (operatorStatus_.getText() != op) {
            operatorStatus_.setText(op, juce::dontSendNotification);
        }
    }

    void rebuildProgramList() {
        program_.clear(juce::dontSendNotification);
        variation_.clear(juce::dontSendNotification);
        if (catalog_ == nullptr) {
            return;
        }
        juce::String group = "\x01";   // (no group yet)
        for (size_t i = 0; i < catalog_->programs.size(); i++) {
            const lexcat::Program &p = catalog_->programs[i];
            if (juce::String(p.group) != group) {
                group = p.group;
                if (group.trim().isNotEmpty()) {
                    program_.addSectionHeading(group);
                }
            }
            program_.addItem(p.label, int(i) + 1);
        }
    }

    void rebuildToggles() {
        for (auto &t : toggles_) {
            removeChildComponent(t.get());
        }
        toggles_.clear();
        toggleBits_.clear();
        if (catalog_ == nullptr) {
            return;
        }
        std::vector<lexcat::Toggle> list = catalog_->toggles();
        if (catalog_->hasMute()) {
            list.push_back(lexcat::Catalog::muteToggle());
        }
        for (const lexcat::Toggle &t : list) {
            auto button = std::make_unique<juce::ToggleButton>(t.title);
            button->setTooltip(t.tooltip);
            std::string label = t.label;
            button->onClick = [this, label, b = button.get()] {
                if (!updating_) {
                    owner_.commands_.setToggle(label, b->getToggleState());
                }
            };
            addAndMakeVisible(*button);
            uint8_t bit = kToggleMute;
            if (t.label == "DYN DECAY") {
                bit = kToggleDynDecay;
            } else if (t.label == "MODE ENH") {
                bit = kToggleModeEnh;
            } else if (t.label == "DECAY OPT") {
                bit = kToggleDecayOpt;
            }
            toggleBits_.push_back(bit);
            toggles_.push_back(std::move(button));
        }
        resizeOwner();
    }

    void rebuildPages() {
        for (auto &page : pages_) {
            removeChildComponent(&page->group);
            removeChildComponent(&page->heading);
            for (auto &row : page->rows) {
                removeChildComponent(&row->label);
                removeChildComponent(&row->slider);
            }
        }
        pages_.clear();
        variation_.clear(juce::dontSendNotification);
        if (programIndex_ < 0) {
            resizeOwner();
            return;
        }
        const lexcat::Program &program = catalog_->programs[size_t(programIndex_)];
        for (int v : program.variations) {
            variation_.addItem("V" + juce::String(v), v);
        }
        for (const lexcat::Page &page : program.pages) {
            auto box = std::make_unique<PageBox>();
            juce::String title = "Page " + juce::String(page.page);
            if (!page.label.empty()) {
                title << "  (" << page.label << ")";
            }
            box->group.setText(title);
            box->group.setColour(juce::GroupComponent::textColourId, findColour(juce::Label::textColourId));
            addAndMakeVisible(box->group);
            box->heading.setText(page.heading, juce::dontSendNotification);
            box->heading.setFont(font(13.0f, false));
            box->heading.setColour(juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible(box->heading);
            for (size_t slot = 0; slot < page.sliders.size(); slot++) {
                const lexcat::Slider &slider = page.sliders[slot];
                if (!slider.named()) {
                    continue;
                }
                auto row = std::make_unique<SliderRow>();
                row->k = program.genericIndexOf(page.page, int(slot));
                row->model = &slider;
                juce::String tip(owner_.help_.helpFor(slider.name, page.heading));
                row->label.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), kFont,
                                                                juce::Font::plain)));
                row->label.setTooltip(tip);
                row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
                row->slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
                row->slider.setRange(catalog_->rawMin(), catalog_->rawMax(), 1.0);
                row->slider.setTooltip(tip);
                SliderRow *r = row.get();
                row->slider.onValueChange = [this, r] {
                    if (updating_) {
                        return;
                    }
                    int raw = int(r->slider.getValue());
                    userMoved(r->until);
                    showSliderText(*r, juce::String(lexcat::tableText(r->model->table, raw)));   // instant, from the table
                    owner_.commands_.moveSlider(r->k, raw);
                };
                row->slider.onDragStart = [this, r] { owner_.commands_.gesture(UiGesture::Slider, r->k, true); };
                row->slider.onDragEnd = [this, r] { owner_.commands_.gesture(UiGesture::Slider, r->k, false); };
                addAndMakeVisible(row->label);
                addAndMakeVisible(row->slider);
                box->rows.push_back(std::move(row));
            }
            pages_.push_back(std::move(box));
        }
        resizeOwner();
    }

    void showSliderText(SliderRow &row, const juce::String &text) {
        juce::String line = juce::String(row.model->name) + ": " + text;
        if (row.label.getText() != line) {
            row.label.setText(line, juce::dontSendNotification);
        }
    }

    void applyProgram(const UiSnapshot &s) {
        bool ready = catalog_ != nullptr && s.status == UiStatus::Ready;
        program_.setEnabled(ready);
        variation_.setEnabled(ready && programIndex_ >= 0);
        int wantedProgram = 0;
        if (programIndex_ >= 0) {
            wantedProgram = programIndex_ + 1;
        }
        if (program_.getSelectedId() != wantedProgram) {
            program_.setSelectedId(wantedProgram, juce::dontSendNotification);
        }
        if (variation_.getSelectedId() != s.variation) {
            variation_.setSelectedId(s.variation, juce::dontSendNotification);
        }
        for (size_t i = 0; i < toggles_.size(); i++) {
            juce::ToggleButton &button = *toggles_[i];
            uint8_t bit = toggleBits_[i];
            button.setEnabled(ready && (s.togglesKnown & bit) != 0);
            bool on = (s.toggles & bit) != 0;
            if (button.getToggleState() != on) {
                button.setToggleState(on, juce::dontSendNotification);
            }
        }
        if (programIndex_ < 0) {
            return;
        }
        bool slidersEnabled = s.status != UiStatus::LoadingProgram && s.status != UiStatus::Booting &&
                              s.status != UiStatus::MissingRoms && s.status != UiStatus::MachineStopped;
        double t = now();
        for (auto &page : pages_) {
            for (auto &row : page->rows) {
                row->slider.setEnabled(slidersEnabled);
                if (row->k < 1 || row->k > kSliders) {
                    continue;
                }
                if (row->slider.isMouseButtonDown() || t < row->until) {
                    continue;   // the user's move is on its way
                }
                std::string text = cString(s.sliderText[row->k - 1], kTextLength);
                int stored = s.stored[row->k - 1];
                int position = lexcat::sliderPosition(*row->model, stored, text, catalog_->rawMax());
                if (int(row->slider.getValue()) != position) {
                    row->slider.setValue(position, juce::dontSendNotification);
                }
                if (text.empty()) {
                    text = lexcat::tableText(row->model->table, stored);
                }
                showSliderText(*row, juce::String(text));
            }
        }
    }

    void applyAudio(const UiSnapshot &s) {
        double t = now();
        if (!level_.isMouseButtonDown() && t >= levelUntil_ && std::lround(level_.getValue()) != std::lround(s.levelDb)) {
            level_.setValue(s.levelDb, juce::dontSendNotification);
        }
        if (!wet_.isMouseButtonDown() && t >= wetUntil_ && std::lround(wet_.getValue()) != std::lround(s.dryWet * 100.0f)) {
            wet_.setValue(std::lround(s.dryWet * 100.0f), juce::dontSendNotification);
        }
        if (s.outLeft < 4 && out_[0].getSelectedId() != s.outLeft + 1) {
            out_[0].setSelectedId(s.outLeft + 1, juce::dontSendNotification);
        }
        if (s.outRight < 4 && out_[1].getSelectedId() != s.outRight + 1) {
            out_[1].setSelectedId(s.outRight + 1, juce::dontSendNotification);
        }
        if (analog_.getToggleState() != s.analog) {
            analog_.setToggleState(s.analog, juce::dontSendNotification);
        }
    }

    void applyMeters(const UiSnapshot &s) {
        for (int c = 0; c < 2; c++) {
            for (int k = 0; k < 5; k++) {
                double hit = s.headroomHit[c][k];
                bool lit = hit >= 0.0 && s.machineSeconds - hit < kMeterHold;
                juce::Colour colour(0xff33aa33);
                if (k == 4) {
                    colour = juce::Colour(0xffcc3333);
                }
                lightMeter(headroom_, 5, c, k, lit, colour);
            }
            for (int g = 0; g < 4; g++) {
                double used = s.gainUsed[c][g];
                bool lit = used >= 0.0 && s.machineSeconds - used < kMeterHold;
                lightMeter(gains_, 4, c, g, lit, juce::Colour(0xff3366cc));
            }
        }
    }

    void resizeOwner() {
        owner_.resized();
    }

    EditorView &owner_;
    bool updating_ = false;
    const lexcat::Catalog *catalog_ = nullptr;
    int programIndex_ = -2;
    juce::String romSignature_ = "\x01";

    juce::Label machineHeading_, audioHeading_, inputHeading_, outputHeading_, programHeading_;
    juce::TextButton romButton_;
    juce::Label setLabel_;
    juce::ComboBox setBox_;
    juce::Label status_;
    std::unique_ptr<juce::FileChooser> chooser_;

    juce::Label levelLabel_;
    juce::Slider level_;
    double levelUntil_ = 0;
    juce::Label headroomTitle_, gainsTitle_;
    std::vector<std::unique_ptr<juce::Label>> headroom_, gains_;

    juce::Label outLabel_[2];
    juce::ComboBox out_[2];
    juce::Label wetLabel_;
    juce::Slider wet_;
    double wetUntil_ = 0;
    juce::ToggleButton analog_;

    juce::ComboBox program_;
    juce::Label variationLabel_;
    juce::ComboBox variation_;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;
    std::vector<uint8_t> toggleBits_;
    juce::Label operatorStatus_;
    std::vector<std::unique_ptr<PageBox>> pages_;
};

// ---------------------------------------------------------------------------

EditorView::EditorView(const lexcat::CatalogLibrary &catalogs, const lexcat::ParamHelp &help, SnapshotReader read,
                       UiCommands &commands)
    : catalogs_(catalogs), help_(help), read_(std::move(read)), commands_(commands) {
    content_ = std::make_unique<Content>(*this);
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    setSize(1040, 820);
    refresh();
    startTimerHz(20);
}

EditorView::~EditorView() {
    stopTimer();
    viewport_.setViewedComponent(nullptr, false);
}

void EditorView::refresh() {
    UiSnapshot s = defaultSnapshot();
    if (read_) {
        read_(s);
    }
    content_->apply(s);
}

int EditorView::contentHeight() const {
    return content_->getHeight();
}

juce::Component &EditorView::content() {
    return *content_;
}

void EditorView::resized() {
    viewport_.setBounds(getLocalBounds());
    int width = getWidth() - viewport_.getScrollBarThickness();
    int height = content_->layout(width);
    content_->setSize(width, height);
}

void EditorView::paint(juce::Graphics &g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

bool EditorView::isInterestedInFileDrag(const juce::StringArray &) {
    return true;
}

void EditorView::filesDropped(const juce::StringArray &files, int, int) {
    for (const juce::String &path : files) {
        commands_.addRomLocation(path.toStdString());
    }
}

void EditorView::timerCallback() {
    refresh();
}

}  // namespace lexui
