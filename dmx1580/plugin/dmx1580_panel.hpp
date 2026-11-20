// The DMX 15-80S front panel for the kit's hardware mode (kit::PanelLayout), measured from
// straight-on reference photos (the DMX project's photos/15-80s, local reference only: nothing of
// them is shipped; this is a vector drawing at their positions). 19" 2U, millimetres.
//
// The kit's core is the stereo machine, so this is the 15-80S panel. Its analog controls are the
// plugin's parameters (input A/B, regen A/B = F/B gain, output, VCO speed/depth, the sep/mix,
// norm/lock in, in phase/out and xtal/vco switches). The F/B filter pots of the drawn mono 15-80
// are not on this panel: easy mode only.
//
// Ids:
//   keys     dmx::Machine key codes: 0-9; A B C = 0xA-0xC; # = 0xD; the key printed D = 0xE (store);
//            * = 0xF (the point); nudge UP = 0x10, DOWN = 0x11
//   LEDs     see DmxLed below
//   digits   0-3 left to right
#pragma once
#include "../../shared/core/panel.hpp"

namespace dmx1580plug {

enum DmxLed {
    LedA, LedB, LedC,      // under the display: the channel (a/b) and pitch entry (c), from E6 PC6-4
    LedRate,               // the VCO section's rate LED (once per cycle)
    LedXtal, LedVco,       // beside the display: follow the XTAL/VCO switch
    LedStereo,             // between in sep./out sep.: lit with both switches at sep. (as the DMX plugin)
    LedA6, LedA12, LedA18, // ch.a h/room: 6, 12 (amber, threshold assumed) and 18 dB below clipping
    LedB6, LedB12, LedB18,
    LedPower,
    NumDmxLeds
};

inline kit::PanelLayout dmx1580_layout() {
    using E = kit::PanelElement;
    kit::PanelLayout L;
    L.width = 483.0f;
    L.height = 88.9f;
    L.face = 0xff151515;
    L.ink = 0xffd6d6cf;
    L.leds = NumDmxLeds;
    L.digits = 4;
    auto add = [&](E e) { L.elements.push_back(std::move(e)); };
    auto text = [&](float x, float y, const char* s, float size = 2.6f, int just = E::Centre) {
        E e;
        e.kind = E::Text;
        e.x = x, e.y = y, e.text = s, e.size = size, e.style = just;
        add(e);
    };
    auto frame = [&](float x0, float y0, float x1, float y1) {
        E e;
        e.kind = E::Frame;
        e.x = x0, e.y = y0, e.w = x1 - x0, e.h = y1 - y0;
        add(e);
    };
    auto led = [&](float cx, float cy, int id, uint32_t col, float d = 2.8f) {
        E e;
        e.kind = E::Led;
        e.x = cx - d / 2, e.y = cy - d / 2, e.w = e.h = d, e.id = id, e.colour = col;
        add(e);
    };
    auto key = [&](float cx, float cy, float w, float h, int id, const char* legend, int style, const char* hot = "") {
        E e;
        e.kind = E::Key;
        e.x = cx - w / 2, e.y = cy - h / 2, e.w = w, e.h = h, e.id = id, e.text = legend, e.style = style, e.hotkey = hot;
        add(e);
    };
    auto pot = [&](float cx, float cy, const char* param, float d = 13.5f) {
        E e;
        e.kind = E::Pot;
        e.x = cx - d / 2, e.y = cy - d / 2, e.w = e.h = d, e.param = param, e.text = "0123456789";
        add(e);
    };
    // style 0: the parameter is on with the lever down
    auto sw = [&](float cx, float cy, const char* param, int style = 0) {
        E e;
        e.kind = E::Switch;
        e.x = cx - 3.0f, e.y = cy - 5.5f, e.w = 6.0f, e.h = 11.0f, e.param = param, e.style = style;
        add(e);
    };
    auto hole = [&](float cx, float cy, int style) {
        E e;
        e.kind = E::Hole;
        float w = style == 0 ? 10.0f : 4.4f, h = style == 0 ? 6.4f : 4.4f;
        e.x = cx - w / 2, e.y = cy - h / 2, e.w = w, e.h = h, e.style = style;
        add(e);
    };
    const uint32_t red = 0xffff3020, amber = 0xffffa020, green = 0xff30d040;

    hole(9.5f, 6.3f, 0), hole(473.5f, 6.3f, 0), hole(9.5f, 82.0f, 0), hole(473.5f, 82.0f, 0);
    hole(25.6f, 8.4f, 1), hole(457.4f, 8.4f, 1), hole(25.6f, 79.4f, 1), hole(457.4f, 79.4f, 1);

    // the maker's plate: left empty (no maker's name or logo is drawn)
    frame(33.6f, 1.7f, 109.2f, 36.8f);

    // stereo: in sep./mix, out sep./mix, output
    frame(33.6f, 38.9f, 109.2f, 73.5f);
    text(54.0f, 42.6f, "stereo");
    led(54.0f, 48.3f, LedStereo, green);
    text(44.3f, 46.6f, "in");
    text(44.3f, 49.9f, "sep.");
    sw(43.6f, 58.4f, "monoIn");
    text(43.6f, 68.0f, "mix");
    text(63.6f, 46.6f, "out");
    text(63.6f, 49.9f, "sep.");
    sw(61.6f, 58.4f, "mixOut");
    text(62.6f, 68.0f, "mix");
    text(91.4f, 42.6f, "output");
    pot(89.3f, 60.0f, "out");

    // ch.a / ch.b: input, h/room LEDs, regen
    frame(111.7f, 1.7f, 188.0f, 73.5f);
    text(115.0f, 4.9f, "ch.a", 2.6f, E::Left);
    text(115.0f, 41.4f, "ch.b", 2.6f, E::Left);
    pot(129.6f, 20.6f, "inA");
    pot(170.1f, 20.6f, "fbGainA");
    pot(129.6f, 60.0f, "inB");
    pot(170.1f, 60.0f, "fbGainB");
    text(151.0f, 4.9f, "6dB", 2.3f);
    led(150.6f, 9.8f, LedA6, red, 2.6f);
    led(150.6f, 16.1f, LedA12, amber, 2.6f);
    led(150.6f, 22.6f, LedA18, green, 2.6f);
    text(151.0f, 28.0f, "18dB", 2.3f);
    text(151.0f, 46.6f, "6dB", 2.3f);
    led(150.6f, 51.4f, LedB6, red, 2.6f);
    led(150.6f, 58.0f, LedB12, amber, 2.6f);
    led(150.6f, 64.6f, LedB18, green, 2.6f);
    text(151.0f, 69.6f, "18dB", 2.3f);
    text(126.0f, 37.3f, "input");
    text(138.5f, 37.3f, "\xe2\x80\x94\xe2\x80\x94");
    text(152.0f, 37.3f, "h/room");
    text(162.6f, 37.3f, "\xe2\x80\x94\xe2\x80\x94");
    text(173.0f, 37.3f, "regen");
    text(184.3f, 37.3f, "\xe2\x80\x94\xe2\x80\x94");

    // vco speed / depth
    frame(190.5f, 1.7f, 226.8f, 73.5f);
    pot(209.0f, 16.0f, "vcoSpeed");
    text(209.0f, 32.6f, "speed");
    text(196.4f, 37.3f, "vco");
    led(220.5f, 36.8f, LedRate, amber, 2.6f);
    text(209.0f, 42.2f, "depth");
    pot(209.0f, 59.4f, "vcoDepth");

    // norm/lock in, chan. a in phase/out
    frame(228.9f, 1.7f, 256.6f, 73.5f);
    text(243.0f, 7.6f, "norm");
    sw(243.0f, 17.0f, "lockin");
    text(243.0f, 25.0f, "lock in");
    text(243.0f, 45.2f, "chan. a");
    text(243.0f, 49.9f, "in phase");
    sw(243.0f, 58.4f, "phaseA");
    text(243.0f, 67.6f, "out");

    // display
    frame(259.4f, 1.7f, 354.9f, 43.0f);
    text(265.7f, 5.6f, "xtal");
    led(265.7f, 11.8f, LedXtal, red);
    led(265.7f, 18.9f, -1, 0xffd8c060);   // a third LED here, never seen lit: drawn dark
    led(265.7f, 26.3f, LedVco, green);
    text(265.7f, 31.6f, "vco");
    text(306.6f, 4.6f, "delay \xc2\xb7 milliseconds", 2.2f);
    {
        E w;
        w.kind = E::Window;
        w.x = 272.4f, w.y = 7.4f, w.w = 69.3f, w.h = 24.6f;
        add(w);
    }
    const float dx[4] = {277.0f, 293.8f, 310.6f, 327.4f};
    for (int i = 0; i < 4; i++) {
        E d;
        d.kind = E::Digit;
        d.x = dx[i], d.y = 10.0f, d.w = 11.6f, d.h = 19.0f, d.id = i, d.colour = 0xffff2a1a;
        add(d);
    }
    text(349.2f, 5.6f, "\xe2\x88\xa7", 2.45f);   // the ^ / v arrows: their LEDs are not lit by this firmware
    led(349.2f, 11.8f, -1, red);
    led(349.2f, 18.9f, -1, 0xffd8c060);
    led(349.2f, 26.3f, -1, green);
    text(349.2f, 32.6f, "\xe2\x88\xa8", 2.45f);
    led(282.9f, 36.6f, LedA, amber);
    led(307.7f, 36.6f, LedB, green);
    led(332.2f, 36.6f, LedC, red);
    text(282.9f, 40.6f, "a");
    text(307.7f, 40.6f, "b");
    text(332.2f, 40.6f, "c");

    // xtal/vco, nudge
    frame(259.4f, 45.8f, 354.9f, 73.5f);
    text(268.8f, 49.0f, "xtal");
    sw(268.4f, 58.4f, "vco");
    text(268.8f, 67.4f, "vco");
    key(294.6f, 59.0f, 17.0f, 16.0f, 0x11, "", E::LightSquare, "DOWN");
    text(294.6f, 70.6f, "down");
    led(313.3f, 57.6f, -1, 0xff303030, 2.4f);   // the hole between the nudge keys: not identified
    text(313.3f, 64.4f, "nudge");
    key(332.9f, 59.0f, 17.0f, 16.0f, 0x10, "", E::LightSquare, "UP");
    text(332.9f, 70.6f, "up");

    // keypad
    frame(357.6f, 1.7f, 425.9f, 73.5f);
    {
        E b;
        b.kind = E::Frame;
        b.style = 1;
        b.colour = 0xff0c0c0c;
        b.x = 361.6f, b.y = 4.4f, b.w = 60.5f, b.h = 67.0f;
        add(b);
    }
    const float kx[4] = {373.8f, 386.8f, 399.6f, 412.2f}, ky[4] = {18.9f, 31.7f, 44.3f, 57.3f};
    const char* legend[4][4] = {{"1", "2", "3", "A"}, {"4", "5", "6", "B"}, {"7", "8", "9", "C"}, {"*", "0", "#", "D"}};
    const int code[4][4] = {{1, 2, 3, 0xA}, {4, 5, 6, 0xB}, {7, 8, 9, 0xC}, {0xF, 0, 0xD, 0xE}};
    const char* hot[4][4] = {{"1", "2", "3", "a"}, {"4", "5", "6", "b"}, {"7", "8", "9", "c"}, {"*.", "0", "#\r", "d"}};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) key(kx[c], ky[r], 8.6f, 7.2f, code[r][c], legend[r][c], E::KeypadCream, hot[r][c]);

    // power, reset (drawn; not emulated: the core has no cold restart while streaming)
    frame(428.4f, 1.7f, 449.4f, 43.0f);
    text(439.3f, 7.6f, "on");
    sw(439.3f, 16.4f, "", 1);
    text(439.3f, 25.0f, "off");
    led(439.3f, 32.0f, LedPower, red);
    text(439.0f, 37.8f, "power");
    frame(428.4f, 45.8f, 449.4f, 73.5f);
    sw(439.3f, 58.8f, "", 1);
    text(439.0f, 67.8f, "reset");

    frame(33.6f, 76.7f, 449.4f, 85.1f);
    text(241.5f, 80.9f, "model dmx15-80S: computer controlled stereo digital delay", 2.9f);   // the maker's name and town left off

    L.notes = "Keys go straight to the firmware's keypad scan (shift-click latches a key down; computer keys: "
              "digits, . or * for *, Return or # for #, a b c d, arrows nudge). Knobs and switches are the "
              "plugin's parameters. Not driven: the ^/v LEDs (never lit by this firmware), the LED between the "
              "nudge keys, power and reset. The h/room amber threshold (12 dB) is assumed.";
    return L;
}

}  // namespace dmx1580plug
