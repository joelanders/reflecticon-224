// kit::PanelView: draws a machine's hardware panel from its kit::PanelLayout (core/panel.hpp) and
// the PanelState the host caches, and plays it: momentary keys (held while the mouse is down;
// shift-click latches a key for chords; computer keys from PanelElement::hotkey, several at once),
// knobs and switches bound to host parameters or panel inputs. Nothing machine-specific here.
#pragma once
#include "kit_processor.hpp"
#include <juce_gui_basics/juce_gui_basics.h>

namespace kit {

class PanelView : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit PanelView(Processor& p);
    ~PanelView() override;

    // Copy the host's cached panel state and repaint what changed (message thread, ~30 Hz).
    void refresh();
    // Release every key this view holds down (mouse, latch or computer key).
    void releaseAll();

    float pxPerMm() const { return getWidth() / std::max(1.0f, layout_.width); }
    juce::Rectangle<float> mm(float x, float y, float w, float h) const {
        const float s = pxPerMm();
        return {x * s, y * s, w * s, h * s};
    }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusLost(FocusChangeType) override;
    void visibilityChanged() override;

    // ---- for the child controls ----
    void press(int keyId, bool down);
    bool driverHolds(int keyId) const;
    bool holds(int keyId) const;
    uint8_t ledLevel(int ledId) const;
    Processor& processor() { return proc_; }
    const PanelLayout& layout() const { return layout_; }
    juce::Font legendFont(float capMm, bool printed = true) const;   // printed: the layout's legend weight; else key caps

private:
    void paintDigit(juce::Graphics&, const PanelElement&, uint8_t segs);
    void paintAlpha(juce::Graphics&, const PanelElement&);
    void paintLed(juce::Graphics&, const PanelElement&, uint8_t level);
    void paintDial(juce::Graphics&, const PanelElement&);

    Processor& proc_;
    const PanelLayout& layout_;
    PanelState state_;
    std::unique_ptr<juce::LookAndFeel> knobLook_;   // outlives the knobs (declared before them)
    std::vector<std::unique_ptr<juce::Component>> controls_;
    std::vector<juce::Path> art_;                 // per element: a Path element's outline (mm)
    std::vector<bool> ledInKey_;                    // per element: an LED a key cap draws itself
    std::vector<int> down_;                       // key ids this view holds down (each once)
    std::vector<std::pair<int, int>> keyboard_;   // (juce key code, key id) held from the keyboard

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelView)
};

}  // namespace kit
