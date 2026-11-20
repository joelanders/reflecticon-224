// The RMX16's front panel for the kit's hardware mode (kit::PanelLayout), measured from straight-on
// reference photos of a v3.x unit (the RMX16 project's photos/, local reference only: nothing of
// them is shipped; this is a vector drawing at their positions). 19" 2U, millimetres. Positions were
// re-measured 2026-11-20 (retrogear-2952 for the panel, the lit soundgas photo for the display: digit
// centres from its lit segments, cross-checked against the dim segments of the unlit unit).
//
// Ids:
//   keys     rmxc_key codes: 1-10 = S1-S10 (pot, program, predelay, decay time, low, high, save,
//            recall, nudge down, nudge up); 100 + 10*col + row = keypad
//   LEDs     0-9 = the firmware's LED1-LED10 (LED10..LED3 = S1..S8's key LEDs, LED2 nudge down,
//            LED1 nudge up); 10 = power. The four o/flo LEDs are analog (input level) and not
//            modelled: drawn dark.
//   digits   0-12 = E39 E5 ... E15 E40 (7 and 9 are the TIL327 sign digits, from E23)
//   alpha    0-7 = the two DL1414s
//   inputs   0 DELAY pot ("pot"), 1 INPUT, 2 OUTPUT (0-255, read by the firmware; the analog
//            level stage itself is not modelled), 3 power (0/1)
#pragma once
#include "../../shared/core/panel.hpp"

namespace rmx16plug {

enum PanelIn { InPot = 0, InInput = 1, InOutput = 2, InPower = 3 };

inline kit::PanelLayout rmx16_layout() {
    using E = kit::PanelElement;
    kit::PanelLayout L;
    L.width = 483.0f;
    L.height = 88.9f;
    L.face = 0xff161616;
    L.ink = 0xffd6d6cf;
    L.legend_weight = 700;   // the panel's lettering is a heavy geometric sans
    L.leds = 11;
    L.digits = 13;
    L.alpha = 8;
    auto add = [&](E e) { L.elements.push_back(std::move(e)); };
    auto text = [&](float x, float y, const char* s, float size = 3.1f, int just = E::Centre) {
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
    auto pot = [&](float cx, float cy, float d, int id) {
        E e;
        e.kind = E::Pot;
        e.x = cx - d / 2, e.y = cy - d / 2, e.w = e.h = d, e.id = id, e.text = "0123456789", e.size = 2.7f;
        add(e);
    };
    auto hole = [&](float cx, float cy, int style) {
        E e;
        e.kind = E::Hole;
        float w = style == 0 ? 10.0f : (cx < 30 || cx > 455 ? 4.4f : 3.2f), h = style == 0 ? 6.4f : w;   // section screws are smaller
        e.x = cx - w / 2, e.y = cy - h / 2, e.w = w, e.h = h, e.style = style;
        add(e);
    };

    // rack ears and the panel screws
    hole(8.0f, 5.2f, 0), hole(475.0f, 5.2f, 0), hole(8.0f, 83.7f, 0), hole(475.0f, 83.7f, 0);
    hole(25.5f, 9.0f, 1), hole(457.5f, 9.0f, 1), hole(25.5f, 80.5f, 1), hole(457.5f, 80.5f, 1);

    // the maker's plate: left empty (no maker's name or logo is drawn)
    frame(33.3f, 2.6f, 100.3f, 36.8f);

    // input / o/flo / output
    frame(33.3f, 38.6f, 100.3f, 78.8f);
    hole(38.3f, 43.3f, 1);
    text(47.6f, 44.3f, "in", 3.25f);
    text(66.4f, 46.8f, "o/flo", 3.25f);
    text(85.6f, 44.3f, "out", 3.25f);
    pot(47.5f, 62.4f, 19.5f, InInput);
    pot(85.6f, 62.4f, 19.5f, InOutput);
    led(66.4f, 51.6f, -1, 0xffff3020, 3.8f);
    led(66.4f, 57.9f, -1, 0xffff3020, 3.8f);
    led(66.4f, 65.2f, -1, 0xffffa020, 3.8f);
    led(66.4f, 71.8f, -1, 0xff30d040, 3.8f);

    // pot
    frame(102.4f, 2.6f, 132.6f, 78.8f);
    pot(117.8f, 22.2f, 19.5f, InPot);
    text(117.5f, 45.6f, "pot", 3.25f);
    hole(106.3f, 74.1f, 1);
    key(117.5f, 60.0f, 15.5f, 14.5f, 1, "", E::DarkSquare);
    led(117.5f, 55.6f, 9, 0xffff2a1a, 2.2f);

    // display and the function keys
    frame(135.2f, 2.6f, 356.0f, 78.8f);
    {
        E w;
        w.kind = E::Window;
        w.x = 144.6f, w.y = 12.6f, w.w = 202.6f, w.h = 19.2f;
        add(w);
    }
    hole(140.0f, 9.7f, 1), hole(350.3f, 9.7f, 1), hole(350.3f, 70.1f, 1), hole(221.0f, 46.5f, 1);
    // 0.3" digits (7.9 mm glyphs); centres measured, grouped as the display card mounts them
    const float dc[13] = {151.1f, 161.0f, 175.2f, 185.1f, 195.0f, 208.7f, 218.2f, 226.9f, 238.0f, 250.4f, 258.8f, 272.1f, 281.9f};
    for (int i = 0; i < 13; i++) {
        E d;
        d.kind = E::Digit;
        const bool sign = i == 7 || i == 9;
        d.w = sign ? 4.2f : 5.4f, d.x = dc[i] - d.w / 2, d.y = 18.0f, d.h = 8.1f, d.id = i;
        d.style = sign ? E::Sign : E::Seven;
        d.colour = 0xffff2a1a;
        add(d);
    }
    {
        E a;
        a.kind = E::Alpha;
        a.x = 307.7f, a.y = 19.75f, a.w = 34.6f, a.h = 4.7f, a.id = 0, a.count = 8, a.colour = 0xffff2a1a;
        add(a);
    }
    // labels: single-line at y 45, two-line headings at 41.6 over 47.6
    text(157.2f, 44.4f, "program");
    text(182.4f, 44.4f, "predelay");
    text(206.8f, 44.4f, "decay time");
    text(242.5f, 41.4f, "decay filter");
    text(233.6f, 47.0f, "low");
    text(251.4f, 47.0f, "high");
    text(284.4f, 41.4f, "store control");
    text(275.6f, 47.0f, "save");
    text(293.2f, 47.0f, "recall");
    text(327.3f, 41.4f, "nudge");
    text(318.4f, 47.0f, "down");
    text(336.2f, 47.0f, "up");
    struct FK {
        float cx;
        int code, led;
        const char* hot;
    };
    const FK fk[9] = {{157.2f, 2, 8, ""}, {182.4f, 3, 7, ""}, {206.8f, 4, 6, ""}, {233.6f, 5, 5, ""}, {251.4f, 6, 4, ""},
                      {275.6f, 7, 3, ""}, {293.2f, 8, 2, ""}, {318.4f, 9, 1, "DOWN"}, {336.2f, 10, 0, "UP"}};
    for (auto& f : fk) {
        key(f.cx, 60.0f, 17.4f, 14.5f, f.code, "", E::DarkSquare, f.hot);
        led(f.cx, 55.6f, f.led, 0xffff2a1a, 2.2f);
    }

    // keypad (cream keys on a black rubber bezel)
    frame(358.3f, 2.6f, 429.2f, 78.8f);
    {
        E b;
        b.kind = E::Frame;
        b.style = 1;
        b.colour = 0xff0c0c0c;
        b.x = 364.0f, b.y = 7.0f, b.w = 59.6f, b.h = 66.0f;
        add(b);
    }
    const float kx[4] = {374.0f, 388.5f, 401.9f, 414.8f}, ky[4] = {21.0f, 34.1f, 47.6f, 60.4f};
    const char* legend[4][4] = {{"1", "2", "3", "A"}, {"4", "5", "6", "B"}, {"7", "8", "9", "C"}, {"*", "0", "#", "D"}};
    const char* hot[4][4] = {{"1", "2", "3", "a"}, {"4", "5", "6", "b"}, {"7", "8", "9", "c"}, {"*.", "0", "#\r", "d"}};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) key(kx[c], ky[r], 9.6f, 8.0f, 100 + 10 * c + (r + 1), legend[r][c], E::KeypadCream, hot[r][c]);

    // power
    frame(431.3f, 2.6f, 452.8f, 78.8f);
    text(442.0f, 13.6f, "on", 3.25f);
    {
        E s;
        s.kind = E::Switch;
        s.x = 436.0f, s.y = 16.0f, s.w = 12.0f, s.h = 12.0f, s.id = InPower, s.style = 1;
        add(s);
    }
    text(442.0f, 31.2f, "off", 3.25f);
    hole(446.4f, 45.5f, 1);
    led(442.0f, 61.0f, 10, 0xffff2a1a, 3.6f);
    text(442.0f, 74.0f, "power", 3.25f);

    // the model strip
    frame(33.3f, 80.1f, 452.8f, 87.4f);
    text(242.0f, 83.75f, "model  rmx 16 : digital  reverberation  system", 4.0f);   // the maker's name and town left off

    L.inputs = {{InPot, "pot", 0, 255, 128}, {InInput, "in", 0, 255, 128}, {InOutput, "out", 0, 255, 128}, {InPower, "power", 0, 1, 1}};
    L.notes = "Keys go straight to the firmware's keyboard scan (shift-click latches a key down for chords; "
              "computer keys: digits, . or * for *, Return or # for #, arrows nudge). The in/out pots are read "
              "by the firmware; their analog level stage and the o/flo LEDs are not modelled (drawn dark).";
    return L;
}

}  // namespace rmx16plug
