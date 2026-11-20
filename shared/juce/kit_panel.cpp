#include "kit_panel.hpp"
#include <cmath>

namespace kit {

namespace {

using E = PanelElement;
juce::Colour argb(uint32_t c, uint32_t fallback) { return juce::Colour(c ? c : fallback); }

// Dial: 0 just left of 12 o'clock, 9 at about 10 o'clock, clockwise (the AMS knobs' printed scale).
constexpr float kDialStart = -10.0f * juce::MathConstants<float>::pi / 180.0f;
constexpr float kDialSpan = 288.0f * juce::MathConstants<float>::pi / 180.0f;

// ---- 14-segment font for the alphanumerics (ASCII 32-95; DL1414-style upper case) ----
// bits: 0 A top, 1 B, 2 C, 3 D bottom, 4 E, 5 F, 6 G1, 7 G2, 8 H, 9 J, 10 K, 11 L, 12 M, 13 N
const uint16_t kAlpha14[64] = {
    0x0000, 0x0006, 0x0202, 0x12CE, 0x12ED, 0x0C24, 0x2359, 0x0400, 0x2400, 0x0900, 0x3FC0, 0x12C0, 0x0800, 0x00C0, 0x0000, 0x0C00,
    0x0C3F, 0x0006, 0x00DB, 0x008F, 0x00E6, 0x2069, 0x00FD, 0x0007, 0x00FF, 0x00EF, 0x1200, 0x0A00, 0x2400, 0x00C8, 0x0900, 0x1083,
    0x02BB, 0x00F7, 0x128F, 0x0039, 0x120F, 0x00F9, 0x0071, 0x00BD, 0x00F6, 0x1209, 0x001E, 0x2470, 0x0038, 0x0536, 0x2136, 0x003F,
    0x00F3, 0x203F, 0x20F3, 0x00ED, 0x1201, 0x003E, 0x0C30, 0x2836, 0x2D00, 0x1500, 0x0C09, 0x0039, 0x2100, 0x000F, 0x0C03, 0x0008,
};

// ---- the knobs: a silver knurled skirt and cap with a pointer line ----
class KnobLook : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
        auto r = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(1.0f);
        const float d = std::min(r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre(d, d);
        const auto c = r.getCentre();
        const float ang = a0 + pos * (a1 - a0);
        g.setColour(juce::Colours::black.withAlpha(0.45f));   // shadow
        g.fillEllipse(r.translated(0.0f, d * 0.05f));
        juce::ColourGradient skirt(juce::Colour(0xffe6e6e2), c.x - d * 0.4f, c.y - d * 0.4f, juce::Colour(0xff6c6d6e), c.x + d * 0.4f,
                                   c.y + d * 0.45f, true);
        g.setGradientFill(skirt);
        g.fillEllipse(r);
        g.setColour(juce::Colour(0xff4a4b4c).withAlpha(0.55f));   // knurling
        for (int i = 0; i < 48; i++) {
            const float a = float(i) * juce::MathConstants<float>::twoPi / 48.0f + ang;
            const float s = std::sin(a), co = -std::cos(a);
            g.drawLine(c.x + s * d * 0.40f, c.y + co * d * 0.40f, c.x + s * d * 0.49f, c.y + co * d * 0.49f, std::max(0.6f, d * 0.012f));
        }
        auto cap = r.reduced(d * 0.17f);
        juce::ColourGradient cg(juce::Colour(0xfff4f4f2), cap.getX(), cap.getY(), juce::Colour(0xff9a9b9c), cap.getRight(), cap.getBottom(), false);
        g.setGradientFill(cg);
        g.fillEllipse(cap);
        g.setColour(juce::Colour(0xff58595a));
        g.drawEllipse(cap, std::max(0.6f, d * 0.015f));
        g.setColour(juce::Colour(0xff202020));
        const float s = std::sin(ang), co = -std::cos(ang);
        g.drawLine(c.x + s * d * 0.08f, c.y + co * d * 0.08f, c.x + s * d * 0.33f, c.y + co * d * 0.33f, std::max(1.0f, d * 0.04f));
    }
};

// ---- a momentary key; draws an LED inside it if the layout puts one there ----
class KeyCap : public juce::Component, public juce::SettableTooltipClient {
public:
    KeyCap(PanelView& v, const E& e, const E* led) : v_(v), e_(e), led_(led) {
        setRepaintsOnMouseActivity(false);
        if (!e.hotkey.empty()) setTooltip(juce::String("key: ") + juce::String(e.hotkey).replace("\r", "Return"));
    }
    void mouseDown(const juce::MouseEvent& ev) override {
        v_.grabKeyboardFocus();
        if (ev.mods.isShiftDown() || latched_) {   // shift-click latches (for chords); any click unlatches
            latched_ = !latched_;
            v_.press(e_.id, latched_);
        } else {
            mouse_ = true;
            v_.press(e_.id, true);
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (mouse_) {
            mouse_ = false;
            v_.press(e_.id, false);
        }
        repaint();
    }
    void unlatch() { latched_ = mouse_ = false; }
    void paint(juce::Graphics& g) override {
        const float s = v_.pxPerMm();
        const bool down = v_.holds(e_.id), driver = v_.driverHolds(e_.id);
        auto r = getLocalBounds().toFloat().reduced(0.25f * s);
        if (down || driver) r = r.translated(0.0f, 0.35f * s);
        const float corner = 0.5f * s;
        juce::Colour body, edge = juce::Colour(0xff050505);
        switch (e_.style) {
        case E::KeypadCream: body = juce::Colour(down ? 0xffc9c2a2 : 0xffe9e2c3); edge = juce::Colour(0xff8f8a70); break;
        case E::LightSquare: body = juce::Colour(down ? 0xffb8b8b4 : 0xffdcdcd8); edge = juce::Colour(0xff8a8a86); break;
        default: body = juce::Colour(down ? 0xff0e0e0e : 0xff222222); break;
        }
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(r.translated(0.0f, 0.4f * s), corner);
        juce::ColourGradient grad(body.brighter(0.18f), r.getX(), r.getY(), body.darker(0.25f), r.getX(), r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, corner);
        if (e_.style == E::DarkSquare) {   // the sloped upper face of the AMS function keys
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRoundedRectangle(r.withHeight(r.getHeight() * 0.42f), corner);
        }
        g.setColour(edge);
        g.drawRoundedRectangle(r, corner, std::max(0.8f, 0.15f * s));
        if (driver) {   // easy mode's driver is typing this key
            g.setColour(juce::Colour(0xffffb040).withAlpha(0.85f));
            g.drawRoundedRectangle(r.expanded(0.2f * s), corner, std::max(1.2f, 0.35f * s));
        }
        if (latched_) {
            g.setColour(juce::Colour(0xff60c0ff));
            g.drawRoundedRectangle(r.expanded(0.2f * s), corner, std::max(1.2f, 0.35f * s));
        }
        if (!e_.text.empty()) {
            g.setColour(juce::Colour(0xff151515));
            g.setFont(v_.legendFont(e_.h * 0.35f, false));
            g.drawText(juce::String::fromUTF8(e_.text.c_str()), r, juce::Justification::centred, false);
        }
        if (led_) {
            const float d = led_->w * s;
            const float cx = (led_->x + led_->w / 2 - e_.x) * s, cy = (led_->y + led_->h / 2 - e_.y) * s + (down || driver ? 0.35f * s : 0.0f);
            auto lr = juce::Rectangle<float>(d, d).withCentre({cx, cy});
            const float lv = v_.ledLevel(led_->id) / 255.0f;
            const auto col = argb(led_->colour, 0xffff2a1a);
            g.setColour(juce::Colour(0xff1a0000));
            g.fillEllipse(lr.expanded(0.25f * s));
            g.setColour(col.withMultipliedBrightness(0.18f + 0.82f * lv).withMultipliedSaturation(0.6f + 0.4f * lv));
            g.fillEllipse(lr);
            if (lv > 0) {
                g.setColour(col.withAlpha(0.35f * lv));
                g.fillEllipse(lr.expanded(d * 0.6f));
                g.setColour(juce::Colours::white.withAlpha(0.5f * lv));
                g.fillEllipse(lr.reduced(d * 0.3f).translated(-d * 0.1f, -d * 0.1f));
            }
        }
    }

private:
    PanelView& v_;
    const E& e_;
    const E* led_;
    bool mouse_ = false, latched_ = false;
};

// ---- a knob: a host parameter or a panel input ----
class Knob : public juce::Slider {
public:
    Knob(PanelView& v, const E& e) : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), v_(v), e_(e) {
        setRotaryParameters(kDialStart + juce::MathConstants<float>::twoPi, kDialStart + juce::MathConstants<float>::twoPi + kDialSpan, true);
        setMouseDragSensitivity(220);
        auto& proc = v.processor();
        if (!e.param.empty()) {
            if (auto* prm = dynamic_cast<juce::RangedAudioParameter*>(proc.params.getParameter(e.param))) {
                attachment_ = std::make_unique<juce::SliderParameterAttachment>(*prm, *this);
                setTooltip(prm->getName(64));
            }
        } else {
            for (auto& in : v.layout().inputs)
                if (in.id == e.id) {
                    setRange(in.min, in.max, 0.0);
                    setDoubleClickReturnValue(true, in.def);
                    setTooltip(in.name);
                }
            setValue(proc.host().panel_input_value(e.id), juce::dontSendNotification);
            onValueChange = [this] { v_.processor().host().panel_input(e_.id, float(getValue())); };
        }
    }

private:
    PanelView& v_;
    const E& e_;
    std::unique_ptr<juce::SliderParameterAttachment> attachment_;
};

// ---- a toggle switch: a Bool host parameter, a panel input, or drawn only ----
class Toggle : public juce::Component, public juce::SettableTooltipClient {
public:
    Toggle(PanelView& v, const E& e) : v_(v), e_(e) {
        auto& proc = v.processor();
        if (!e.param.empty()) {
            if (auto* prm = proc.params.getParameter(e.param)) {
                attachment_ = std::make_unique<juce::ParameterAttachment>(*prm, [this](float x) {
                    on_ = x > 0.5f;
                    repaint();
                });
                attachment_->sendInitialUpdate();
                setTooltip(prm->getName(64));
            }
        } else if (e.id >= 0) {
            on_ = proc.host().panel_input_value(e.id) > 0.5f;
        } else {
            on_ = e.style == 1;   // not emulated: shown in its normal position
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
        if (e.id >= 0 || attachment_) setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void mouseDown(const juce::MouseEvent&) override {
        if (attachment_) {
            attachment_->setValueAsCompleteGesture(on_ ? 0.0f : 1.0f);
        } else if (e_.id >= 0) {
            on_ = !on_;
            v_.processor().host().panel_input(e_.id, on_ ? 1.0f : 0.0f);
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        const float s = v_.pxPerMm();
        auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre();
        const bool up = on_ == (e_.style == 1);
        // the nut and bushing (sized from the element: about 0.6 and 0.37 of its width)
        const float W = e_.w * s;
        juce::ColourGradient ng(juce::Colour(0xffc8c8c4), c.x - W * 0.3f, c.y - W * 0.3f, juce::Colour(0xff5a5a58), c.x + W * 0.3f, c.y + W * 0.3f, false);
        g.setGradientFill(ng);
        g.fillEllipse(juce::Rectangle<float>(W * 0.6f, W * 0.6f).withCentre(c));
        g.setColour(juce::Colour(0xff3a3a38));
        g.fillEllipse(juce::Rectangle<float>(W * 0.37f, W * 0.37f).withCentre(c));
        // the bat lever, seen nearly end-on: a short chrome stub up or down
        const float len = e_.h * 0.4f * s, wid = W * 0.24f;
        auto lever = juce::Rectangle<float>(wid, len).withCentre({c.x, up ? c.y - len * 0.45f : c.y + len * 0.45f});
        juce::ColourGradient cg(juce::Colour(0xfff0f0ee), lever.getX(), 0, juce::Colour(0xff7a7a78), lever.getRight(), 0, false);
        g.setGradientFill(cg);
        g.fillRoundedRectangle(lever, wid * 0.45f);
        g.setColour(juce::Colour(0xfff8f8f6));
        g.fillEllipse(juce::Rectangle<float>(wid * 1.25f, wid * 1.25f).withCentre({c.x, up ? lever.getY() + wid * 0.5f : lever.getBottom() - wid * 0.5f}));
    }

private:
    PanelView& v_;
    const E& e_;
    bool on_ = false;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
};

}  // namespace

PanelView::PanelView(Processor& p) : proc_(p), layout_(p.info().panel) {
    setWantsKeyboardFocus(true);
    setOpaque(true);
    knobLook_ = std::make_unique<KnobLook>();
    ledInKey_.assign(layout_.elements.size(), false);
    art_.resize(layout_.elements.size());
    for (size_t i = 0; i < layout_.elements.size(); i++)
        if (layout_.elements[i].kind == E::Path) art_[i] = juce::Drawable::parseSVGPath(juce::String(layout_.elements[i].text));
    for (size_t i = 0; i < layout_.elements.size(); i++) {
        const E& e = layout_.elements[i];
        std::unique_ptr<juce::Component> c;
        if (e.kind == E::Key) {
            const E* led = nullptr;   // an LED whose centre is on this key
            for (size_t j = 0; j < layout_.elements.size(); j++) {
                const E& l = layout_.elements[j];
                const float cx = l.x + l.w / 2, cy = l.y + l.h / 2;
                if (l.kind == E::Led && cx > e.x && cx < e.x + e.w && cy > e.y && cy < e.y + e.h) {
                    led = &l;
                    ledInKey_[j] = true;
                }
            }
            c = std::make_unique<KeyCap>(*this, e, led);
        } else if (e.kind == E::Pot) {
            auto k = std::make_unique<Knob>(*this, e);
            k->setLookAndFeel(knobLook_.get());
            c = std::move(k);
        } else if (e.kind == E::Switch) {
            c = std::make_unique<Toggle>(*this, e);
        }
        if (c) {
            c->getProperties().set("element", int(i));
            addAndMakeVisible(*c);
            controls_.push_back(std::move(c));
        }
    }
    refresh();
}

PanelView::~PanelView() {
    releaseAll();
    for (auto& c : controls_)
        if (auto* k = dynamic_cast<juce::Slider*>(c.get())) k->setLookAndFeel(nullptr);
}

void PanelView::resized() {
    for (auto& c : controls_) {
        const E& e = layout_.elements[size_t(int(c->getProperties()["element"]))];
        c->setBounds(mm(e.x, e.y, e.w, e.h).getSmallestIntegerContainer());
    }
}

juce::Font PanelView::legendFont(float capMm, bool printed) const {
    // a geometric sans close to the panels' lettering; the default sans where it is missing. The em
    // is set as a point height (not JUCE's ascent+descent height) so capMm is Futura's cap height
    // (0.76 em), as the web page draws it.
    const bool bold = printed && layout_.legend_weight >= 650;
    juce::Font f(juce::FontOptions("Futura", 12.0f, juce::Font::plain).withStyle(bold ? "Bold" : "Medium").withPointHeight(capMm * pxPerMm() / 0.76f));
    return f;
}

void PanelView::press(int keyId, bool down) {
    auto it = std::find(down_.begin(), down_.end(), keyId);
    if (down == (it != down_.end())) return;
    if (down) down_.push_back(keyId);
    else down_.erase(it);
    proc_.host().panel_key(keyId, down);
    repaint();
}

void PanelView::releaseAll() {
    for (int id : down_) proc_.host().panel_key(id, false);
    down_.clear();
    keyboard_.clear();
    for (auto& c : controls_)
        if (auto* k = dynamic_cast<KeyCap*>(c.get())) k->unlatch();
}

bool PanelView::holds(int keyId) const { return std::find(down_.begin(), down_.end(), keyId) != down_.end(); }

bool PanelView::driverHolds(int keyId) const {
    return std::find(state_.held.begin(), state_.held.end(), keyId) != state_.held.end();
}

uint8_t PanelView::ledLevel(int ledId) const {
    if (!state_.valid || ledId < 0 || ledId >= int(state_.leds.size())) return 0;
    return state_.leds[size_t(ledId)];
}

void PanelView::refresh() {
    PanelState s = proc_.host().panel();
    const bool same = s.valid == state_.valid && s.digits == state_.digits && s.leds == state_.leds && s.alpha == state_.alpha &&
                      s.held == state_.held;
    state_ = std::move(s);
    if (!same) repaint();
}

void PanelView::focusLost(FocusChangeType) {
    for (auto& k : keyboard_) press(k.second, false);
    keyboard_.clear();
}

void PanelView::visibilityChanged() {
    if (!isVisible()) releaseAll();
}

bool PanelView::keyPressed(const juce::KeyPress& k) {
    const int kc = k.getKeyCode();
    const juce::juce_wchar ch = k.getTextCharacter();
    for (auto& e : layout_.elements) {
        if (e.kind != E::Key || e.hotkey.empty()) continue;
        bool hit = false;
        if (e.hotkey == "UP") hit = kc == juce::KeyPress::upKey;
        else if (e.hotkey == "DOWN") hit = kc == juce::KeyPress::downKey;
        else
            for (char t : e.hotkey) {
                if (t == '\r') hit = hit || kc == juce::KeyPress::returnKey;
                else hit = hit || (ch != 0 && juce::CharacterFunctions::toLowerCase(ch) == juce::juce_wchar(t)) ||
                           (kc < 128 && juce::CharacterFunctions::toLowerCase(juce::juce_wchar(kc)) == juce::juce_wchar(t));
            }
        if (!hit) continue;
        for (auto& h : keyboard_)
            if (h.first == kc) return true;   // auto-repeat
        keyboard_.push_back({kc, e.id});
        press(e.id, true);
        return true;
    }
    return false;
}

bool PanelView::keyStateChanged(bool) {
    for (size_t i = 0; i < keyboard_.size();) {
        if (!juce::KeyPress::isKeyCurrentlyDown(keyboard_[i].first)) {
            const int id = keyboard_[i].second;
            keyboard_.erase(keyboard_.begin() + long(i));
            bool still = false;   // another computer key may hold the same panel key
            for (auto& h : keyboard_) still = still || h.second == id;
            if (!still) press(id, false);
        } else {
            i++;
        }
    }
    return false;
}

// ---- painting ----

void PanelView::paint(juce::Graphics& g) {
    const float s = pxPerMm();
    const auto ink = juce::Colour(layout_.ink);
    g.fillAll(juce::Colour(0xff0a0a0a));
    auto face = getLocalBounds().toFloat();
    juce::ColourGradient fg(juce::Colour(layout_.face).brighter(0.08f), 0, 0, juce::Colour(layout_.face).darker(0.2f), 0, face.getBottom(), false);
    g.setGradientFill(fg);
    g.fillRoundedRectangle(face, 0.8f * s);

    for (size_t i = 0; i < layout_.elements.size(); i++) {
        const E& e = layout_.elements[i];
        const auto r = mm(e.x, e.y, e.w, e.h);
        switch (e.kind) {
        case E::Hole:
            if (e.style == 0) {
                g.setColour(juce::Colour(0xff050505));
                g.fillRoundedRectangle(r, r.getHeight() / 2);
                g.setColour(juce::Colour(0xff3a3a3a));
                g.drawRoundedRectangle(r, r.getHeight() / 2, std::max(0.8f, 0.2f * s));
            } else {
                juce::ColourGradient sg(juce::Colour(0xff4a4a4a), r.getX(), r.getY(), juce::Colour(0xff121212), r.getRight(), r.getBottom(), false);
                g.setGradientFill(sg);
                g.fillEllipse(r);
                g.setColour(juce::Colour(0xff060606));   // hex socket
                juce::Path hex;
                hex.addPolygon(r.getCentre(), 6, r.getWidth() * 0.22f);
                g.fillPath(hex);
            }
            break;
        case E::Frame:
            if (e.style == 1) {
                g.setColour(argb(e.colour, 0xffb0b0b0));
                g.fillRoundedRectangle(r, 0.5f * s);
            } else {
                g.setColour(ink.withAlpha(0.9f));
                g.drawRoundedRectangle(r, 0.4f * s, std::max(1.0f, 0.32f * s));
            }
            break;
        case E::Path:
            g.setColour(argb(e.colour, layout_.ink));
            g.fillPath(art_[i], juce::AffineTransform::scale(s));
            break;
        case E::Window: {
            g.setColour(juce::Colour(0xff070303));
            g.fillRoundedRectangle(r.expanded(0.6f * s), 0.4f * s);
            juce::ColourGradient wg(juce::Colour(0xff2a0a0a), r.getX(), r.getY(), juce::Colour(0xff120404), r.getX(), r.getBottom(), false);
            g.setGradientFill(wg);
            g.fillRect(r);
            g.setColour(juce::Colours::white.withAlpha(0.035f));   // the filter's sheen
            g.fillRect(r.withHeight(r.getHeight() * 0.35f));
            break;
        }
        case E::Text: {
            const auto col = argb(e.colour, layout_.ink);
            g.setColour(col);
            auto f = legendFont(e.size > 0 ? e.size : 2.1f);
            g.setFont(f);
            const float h = f.getHeight();
            const float w = 400.0f * s;
            juce::Rectangle<float> tr;
            juce::Justification j = juce::Justification::centred;
            if (e.style == E::Left) tr = {e.x * s, e.y * s - h / 2, w, h}, j = juce::Justification::centredLeft;
            else if (e.style == E::Right) tr = {e.x * s - w, e.y * s - h / 2, w, h}, j = juce::Justification::centredRight;
            else tr = {e.x * s - w / 2, e.y * s - h / 2, w, h};
            g.drawText(juce::String::fromUTF8(e.text.c_str()), tr, j, false);
            break;
        }
        case E::Led:
            if (!ledInKey_[i]) paintLed(g, e, ledLevel(e.id));
            break;
        case E::Digit: {
            uint8_t segs = 0;
            if (state_.valid && e.id >= 0 && e.id < int(state_.digits.size())) segs = state_.digits[size_t(e.id)];
            paintDigit(g, e, segs);
            break;
        }
        case E::Alpha: paintAlpha(g, e); break;
        case E::Pot: paintDial(g, e); break;
        default: break;
        }
    }
    if (!state_.valid) {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(proc_.host().has_machine() ? "the firmware has stopped" : "no machine: load the ROMs", getLocalBounds().removeFromBottom(int(10 * s)),
                   juce::Justification::centred);
    }
}

void PanelView::paintLed(juce::Graphics& g, const E& e, uint8_t level) {
    const float s = pxPerMm();
    auto r = mm(e.x, e.y, e.w, e.h);
    const auto col = argb(e.colour, 0xffff2a1a);
    const float lv = level / 255.0f;
    g.setColour(juce::Colour(0xff080808));
    g.fillEllipse(r.expanded(0.35f * s));
    // unlit: the coloured lens, dim; lit: bright, with a glow and a highlight
    g.setColour(col.withMultipliedBrightness(0.32f + 0.68f * lv).withMultipliedSaturation(0.6f + 0.4f * lv));
    g.fillEllipse(r);
    if (lv > 0) {
        g.setColour(col.withAlpha(0.3f * lv));
        g.fillEllipse(r.expanded(r.getWidth() * 0.7f));
    }
    g.setColour(juce::Colours::white.withAlpha(0.15f + 0.45f * lv));
    g.fillEllipse(r.reduced(r.getWidth() * 0.32f).translated(-r.getWidth() * 0.12f, -r.getWidth() * 0.12f));
}

void PanelView::paintDigit(juce::Graphics& g, const E& e, uint8_t segs) {
    const auto r = mm(e.x, e.y, e.w, e.h);
    const auto col = argb(e.colour, 0xffff2a1a);
    // a 12 x 18 cell: the digit in 0..10, the point at 10.8; slanted ~7 degrees
    const float sx = r.getWidth() / 12.0f, sy = r.getHeight() / 18.0f, t = 1.55f, gp = 0.28f, slant = 0.12f;
    auto P = [&](float x, float y) { return juce::Point<float>(r.getX() + (x + (18.0f - y) * slant) * sx, r.getY() + y * sy); };
    auto hseg = [&](float x0, float x1, float y) {
        juce::Path p;
        p.startNewSubPath(P(x0, y));
        p.lineTo(P(x0 + t / 2, y - t / 2));
        p.lineTo(P(x1 - t / 2, y - t / 2));
        p.lineTo(P(x1, y));
        p.lineTo(P(x1 - t / 2, y + t / 2));
        p.lineTo(P(x0 + t / 2, y + t / 2));
        p.closeSubPath();
        return p;
    };
    auto vseg = [&](float x, float y0, float y1) {
        juce::Path p;
        p.startNewSubPath(P(x, y0));
        p.lineTo(P(x + t / 2, y0 + t / 2));
        p.lineTo(P(x + t / 2, y1 - t / 2));
        p.lineTo(P(x, y1));
        p.lineTo(P(x - t / 2, y1 - t / 2));
        p.lineTo(P(x - t / 2, y0 + t / 2));
        p.closeSubPath();
        return p;
    };
    auto draw = [&](const juce::Path& p, bool on) {
        if (on) {
            g.setColour(col.withAlpha(0.28f));
            g.strokePath(p, juce::PathStrokeType(std::max(1.0f, sx * 0.9f)));
            g.setColour(col.brighter(0.15f));
        } else {
            g.setColour(col.withAlpha(0.05f));
        }
        g.fillPath(p);
    };
    const float L = 1.0f, R = 9.0f, T = 1.0f, M = 9.0f, B = 17.0f;
    if (e.style == E::Sign) {   // g = minus bar, b = vertical bar (with the minus, a plus)
        draw(hseg(0.6f, 9.4f, M), segs & 0x40);
        draw(vseg(5.0f, 3.2f, M - gp), segs & 0x02);
        draw(vseg(5.0f, M + gp, 14.8f), segs & 0x02);
        return;
    }
    draw(hseg(L + gp, R - gp, T), segs & 0x01);       // a
    draw(vseg(R, T + gp, M - gp), segs & 0x02);       // b
    draw(vseg(R, M + gp, B - gp), segs & 0x04);       // c
    draw(hseg(L + gp, R - gp, B), segs & 0x08);       // d
    draw(vseg(L, M + gp, B - gp), segs & 0x10);       // e
    draw(vseg(L, T + gp, M - gp), segs & 0x20);       // f
    draw(hseg(L + gp, R - gp, M), segs & 0x40);       // g
    juce::Path dp;
    const auto c = P(10.9f, 17.0f);
    dp.addEllipse(c.x - 0.85f * sx, c.y - 0.85f * sx, 1.7f * sx, 1.7f * sx);
    draw(dp, segs & 0x80);
}

void PanelView::paintAlpha(juce::Graphics& g, const E& e) {
    const auto r = mm(e.x, e.y, e.w, e.h);
    const auto col = argb(e.colour, 0xffff2a1a);
    const int n = std::max(1, e.count);
    const float cw = r.getWidth() / float(n);
    for (int i = 0; i < n; i++) {
        char ch = ' ';
        const size_t at = size_t(std::max(0, e.id) + i);
        if (state_.valid && at < state_.alpha.size()) ch = state_.alpha[at];
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
        const uint16_t bits = (ch >= 32 && ch < 96) ? kAlpha14[ch - 32] : 0;
        // a 10 x 14 cell, slanted a little
        auto cell = juce::Rectangle<float>(r.getX() + cw * float(i), r.getY(), cw, r.getHeight()).reduced(cw * 0.12f, 0.0f);
        const float sx = cell.getWidth() / 10.0f, sy = cell.getHeight() / 14.0f;
        auto P = [&](float x, float y) { return juce::Point<float>(cell.getX() + (x + (14.0f - y) * 0.1f) * sx, cell.getY() + y * sy); };
        struct Seg {
            float x0, y0, x1, y1;
        };
        const Seg seg[14] = {{1.3f, 1, 8.7f, 1}, {9, 1.4f, 9, 6.6f}, {9, 7.4f, 9, 12.6f}, {1.3f, 13, 8.7f, 13}, {1, 7.4f, 1, 12.6f},
                             {1, 1.4f, 1, 6.6f}, {1.4f, 7, 4.6f, 7}, {5.4f, 7, 8.6f, 7}, {1.6f, 1.8f, 4.5f, 6.3f}, {5, 1.5f, 5, 6.5f},
                             {8.4f, 1.8f, 5.5f, 6.3f}, {4.5f, 7.7f, 1.6f, 12.2f}, {5, 7.5f, 5, 12.5f}, {5.5f, 7.7f, 8.4f, 12.2f}};
        const float th = std::max(0.9f, sx * 1.05f);
        for (int k = 0; k < 14; k++) {
            const bool on = bits >> k & 1;
            g.setColour(on ? col.brighter(0.2f) : col.withAlpha(0.06f));
            g.drawLine(juce::Line<float>(P(seg[k].x0, seg[k].y0), P(seg[k].x1, seg[k].y1)), th);
        }
    }
}

void PanelView::paintDial(juce::Graphics& g, const E& e) {
    const auto r = mm(e.x, e.y, e.w, e.h);
    const auto c = r.getCentre();
    const int n = int(e.text.size());
    if (n < 2) return;
    const float s = pxPerMm();
    const float cap = e.size > 0 ? e.size : 2.2f;   // numeral cap height
    const float R = r.getWidth() / 2 + (0.75f * cap + 0.8f) * s;
    g.setColour(juce::Colour(layout_.ink));
    auto f = legendFont(cap);
    g.setFont(f);
    for (int i = 0; i < n; i++) {
        const float a = kDialStart + kDialSpan * float(i) / float(n - 1);
        const juce::Point<float> p(c.x + std::sin(a) * R, c.y - std::cos(a) * R);
        g.drawText(juce::String::charToString(juce::juce_wchar(e.text[size_t(i)])), juce::Rectangle<float>(4 * s, 4 * s).withCentre(p),
                   juce::Justification::centred, false);
    }
}

}  // namespace kit
