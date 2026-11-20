// One running machine in the plugin: the engine (../source/engine.hpp), the
// number of 48 kHz frames it has rendered since power-on, and the control
// inputs scheduled for it at machine frames. No JUCE here.
//
// Controls are stamped in machine time (internal frames), never in host
// time: an input stamped for frame F is given to the machine just before
// frame F is rendered, however the host's blocks happen to fall. That is the
// rule of web-demo/wasm/web.cpp's callers (page/larc.js renderInto: an action due at or
// before the machine's time runs before the next render) and of
// tests/real_ir_render.cpp.
#pragma once
#include "../source/engine.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace lexplug {

// A raw control input, as bench/record_timeline.mjs writes them.
enum class ControlKind : uint8_t { Key, Fader, Poke, Button, Pot };

struct ControlEvent {
    uint64_t frame;     // given before this internal frame is rendered
    ControlKind kind;
    unsigned a = 0, b = 0;
};

class Session {
public:
    // model: 0 the 224X/224XL, 1 the original 224 (Engine's).
    explicit Session(int model) : engine(model) {}

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    Engine engine;

    // Internal frames rendered since power-on.
    uint64_t frame() const {
        return frame_;
    }

    // Replace the scheduled controls (sorted here by frame, stable). Not on
    // the audio thread: this allocates.
    void set_controls(std::vector<ControlEvent> events) {
        std::stable_sort(events.begin(), events.end(),
                         [](const ControlEvent &x, const ControlEvent &y) { return x.frame < y.frame; });
        controls_ = std::move(events);
        next_ = 0;
        while (next_ < controls_.size() && controls_[next_].frame < frame_) {
            next_++;   // already in the past: dropped
        }
    }

    // Render `frames` internal frames, giving each scheduled control just
    // before its frame. Never allocates (the engine's own allocations aside:
    // see tests/rt_alloc).
    void render(const float *left, const float *right, float *const out[4], int frames) {
        render_with(left, right, out, frames, [this](const float *l, const float *r, float *const o[4], int n) {
            engine.render(l, r, o, n);
            return true;
        });
    }

    // The same, with the machine's frames rendered by `machine(left, right,
    // out, count)` (bool: false once the machine stopped), e.g. the
    // operator's OperatorPort::render, which gives its own inputs on the way.
    // Returns false (and stops) as soon as `machine` does.
    template <class Machine>
    bool render_with(const float *left, const float *right, float *const out[4], int frames, Machine &&machine) {
        int done = 0;
        while (done < frames) {
            apply_due();
            int count = frames - done;
            if (next_ < controls_.size()) {
                uint64_t until = controls_[next_].frame;
                if (until - frame_ < uint64_t(count)) {
                    count = int(until - frame_);
                }
            }
            float *o[4] = {out[0] + done, out[1] + done, out[2] + done, out[3] + done};
            if (!machine(left + done, right + done, o, count)) {
                return false;
            }
            done += count;
            frame_ += uint64_t(count);
        }
        return true;
    }

private:
    void apply_due() {
        while (next_ < controls_.size() && controls_[next_].frame <= frame_) {
            const ControlEvent &e = controls_[next_++];
            switch (e.kind) {
                case ControlKind::Key:
                    engine.key(e.a, e.b != 0);
                    break;
                case ControlKind::Fader:
                    engine.fader(e.a, e.b);
                    break;
                case ControlKind::Poke:
                    engine.poke(uint16_t(e.a), uint8_t(e.b));
                    break;
                case ControlKind::Button:
                    engine.button(e.a, e.b);
                    break;
                case ControlKind::Pot:
                    engine.pot(e.a, e.b);
                    break;
            }
        }
    }

    uint64_t frame_ = 0;
    std::vector<ControlEvent> controls_;
    size_t next_ = 0;
};

}  // namespace lexplug
