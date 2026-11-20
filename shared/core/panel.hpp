// The hardware panel, JUCE-free: what a machine's real front panel looks like (PanelLayout, static,
// in kit::Info) and what it shows at a moment (PanelState, from Machine::panel()). The kit's JUCE
// editor and the web page draw any machine from these; a machine adds no drawing code.
//
// Two layers over one core:
//   hardware mode   the panel drawn from the photos; key down/up (holds, chords) go straight to the
//                   emulated keyboard scan through Machine::panel_key; the displays and LEDs are
//                   what the firmware wrote. Analog pots and switches move host parameters (when
//                   the machine has them as parameters) or panel inputs (pots the firmware reads).
//   easy mode       the generic editor: parameters that the machine's own driver types on the same
//                   keys, so it never reaches anything hardware mode cannot.
//
// When the panel is used directly, the machine reports the settings it now holds as parameter
// values (PanelState::adopt); the host writes them into its parameters without typing them back.
//
// Coordinates are millimetres from the panel's top-left corner (a 19" 2U face is 483 x 88.9).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace kit {

struct PanelElement {
    enum Kind {
        Frame,    // a printed outline (rounded rectangle), `text` = caption on its top edge
        Window,   // a display window (dark filter glass)
        Text,     // a printed legend: `text`, `size` = cap height (mm, 0 = 2.1), `style` = Justify
        Key,      // a momentary key: `id` = key id for Machine::panel_key, `text` = legend on the cap,
                  // `style` = KeyStyle, `hotkey` = computer keys that press it
        Led,      // an indicator: `id` = index into PanelState::leds, `colour` = its lit colour.
                  // An LED the emulation does not drive has id -1 and is drawn dark.
        Digit,    // a 7-segment digit: `id` = index into PanelState::digits, `style` = DigitStyle
        Alpha,    // alphanumeric characters: `id` = first index into PanelState::alpha, `count` chars
        Pot,      // a knob: `param` (host parameter) or `id` (PanelInput), `text` = dial numerals, `size` =
                  // their cap height (0 = 2.2 mm)
        Switch,   // a toggle: `param` (a Bool host parameter) or `id` (PanelInput, 0/1); `text` =
                  // "upper|lower" position legends are separate Text elements, `style` 1 = up is on
        Hole,     // a rack-ear slot or a screw head (`style` 0 slot, 1 screw)
        Path,     // printed artwork (a logo): `text` = SVG path data in panel millimetres (M L H V C Q A Z,
                  // absolute or relative), filled with `colour` (non-zero winding); x/y/w/h = its bounds
    };
    enum Justify { Centre = 0, Left = 1, Right = 2 };
    enum KeyStyle { DarkSquare = 0, KeypadCream = 1, LightSquare = 2 };
    enum DigitStyle { Seven = 0, Sign = 1 };   // Sign: a TIL327-style +/- digit (g = minus, b = vertical)

    Kind kind = Text;
    float x = 0, y = 0, w = 0, h = 0;
    int id = -1;
    std::string param;
    std::string text;
    uint32_t colour = 0;   // ARGB; 0 = the renderer's default for the kind
    float size = 0;
    int style = 0;
    int count = 1;
    std::string hotkey;    // e.g. "1", "#", "\\r" (return), "UP" (arrow), "DOWN"
};

// A pot or switch the firmware reads that is not a host parameter (e.g. the RMX16's pots).
struct PanelInput {
    int id = 0;
    std::string name;
    float min = 0, max = 1, def = 0;
};

struct PanelLayout {
    float width = 0, height = 0;   // mm
    uint32_t face = 0xff141414;    // panel colour
    uint32_t ink = 0xffd8d8d0;     // printed legends and frames
    int legend_weight = 500;       // the printed legends' font weight (CSS: 500 medium, 700 bold); key caps stay medium
    std::vector<PanelElement> elements;
    std::vector<PanelInput> inputs;
    int leds = 0, digits = 0, alpha = 0;   // sizes of the PanelState vectors
    std::string notes;             // what the drawing does not show or drive (for the editor's tooltip)
    bool empty() const { return elements.empty(); }
};

struct PanelState {
    bool valid = false;               // false: not booted, or the firmware has stopped
    std::vector<uint8_t> digits;      // per digit: bit 0 a .. bit 6 g, bit 7 dp (1 = lit)
    std::vector<uint8_t> leds;        // per LED: 0 dark .. 255 fully lit
    std::string alpha;                // alphanumeric display text
    std::vector<int> held;            // keys the machine's own driver (easy mode) is holding now
    // Settings the panel has put the machine in, as plain values for the first adopt.size()
    // parameters (NaN = leave that one); the host adopts them when adopt_serial changes.
    std::vector<float> adopt;
    uint32_t adopt_serial = 0;
};

// Panel keys from the control thread to the thread that runs the emulation: a single-producer,
// single-consumer ring (no locks, no allocation), plus the minimum hold. The emulation side calls
// poll() with its own clock at least every few milliseconds of machine time.
template <int N = 64>
class PanelKeyQueue {
public:
    // producer (control thread). false if full (the press is dropped).
    bool push(int id, bool down) {
        unsigned w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
        if (w - r >= unsigned(N)) return false;
        ring_[w % N] = {id, down};
        w_.store(w + 1, std::memory_order_release);
        return true;
    }

    // consumer. Applies queued events through apply(id, down); an up comes no sooner than
    // min_hold ticks after its down. A press that follows a quick tap (its up still waiting out
    // the hold, same key or another) waits until that up is applied and min_hold/2 more, so fast
    // typing ("0 0", "3 #") reaches the machine as separate presses, not a chord or one long
    // press; keys really held together still chord. Returns true if anything was applied.
    template <class F>
    bool poll(uint64_t now, uint64_t min_hold, F&& apply) {
        bool any = false;
        unsigned r = r_.load(std::memory_order_relaxed), w = w_.load(std::memory_order_acquire);
        for (; r != w; r++) {
            Ev e = ring_[r % N];
            Pend* slot = find(e.id);
            if (e.down && (now < gap_until_ || tapped())) break;   // retried next poll
            if (e.down) {
                if (!slot) slot = find(-1);
                if (slot) *slot = {e.id, now, false};
                apply(e.id, true);
                last_key_ = e.id;
            } else if (slot) {
                if (now - slot->down_at >= min_hold) {
                    apply(e.id, false);
                    slot->id = -1;
                } else {
                    slot->up_pending = true;
                }
            } else {
                apply(e.id, false);
            }
            last_at_ = now;
            events_++;
            any = true;
        }
        r_.store(r, std::memory_order_release);
        // pending ups live in pend_ with up_pending set: release those whose hold is over
        for (auto& p : pend_)
            if (p.id >= 0 && p.up_pending && now - p.down_at >= min_hold) {
                apply(p.id, false);
                p.id = -1;
                last_at_ = now;
                gap_until_ = now + min_hold / 2;
                any = true;
            }
        return any;
    }
    // consumer: whether a key is held now (down and its up not yet applied)
    bool any_held() const {
        for (auto& p : pend_)
            if (p.id >= 0) return true;
        return false;
    }
    uint64_t last_at() const { return last_at_; }   // consumer clock of the last event
    int last_key() const { return last_key_; }      // the last key that went down
    uint64_t events() const { return events_; }

private:
    struct Ev {
        int id;
        bool down;
    };
    struct Pend {
        int id = -1;
        uint64_t down_at = 0;
        bool up_pending = false;
    };
    bool tapped() const {
        for (auto& p : pend_)
            if (p.id >= 0 && p.up_pending) return true;
        return false;
    }
    Pend* find(int id) {
        for (auto& p : pend_)
            if (p.id == id) return &p;
        return nullptr;
    }
    std::array<Ev, N> ring_{};
    std::atomic<unsigned> w_{0}, r_{0};
    std::array<Pend, 16> pend_{};
    uint64_t last_at_ = 0, events_ = 0, gap_until_ = 0;
    int last_key_ = -1;
};

}  // namespace kit
