// Firmware -> host write-back, on the message thread.
//
// After a program or variation load the firmware has set every slider to
// its preset (and a 224 load turns its toggles on again); after a move the
// firmware may hold a different byte than the one asked for. The host's
// parameters must follow, or automation, the host's generic UI and a saved
// project would disagree with what the machine plays.
//
// Channel: the Scheduler (audio thread) publishes a Published snapshot in a
// seqlock with a generation counter (scheduler.hpp). The message thread
// calls WriteBack::poll from a timer (the editor's ~20 Hz timer, or a
// processor timer when there is no editor); when the generation changed it
// pushes, for each operator parameter (program, variation, slider_1..count,
// the toggles whose state is known):
//
//   host.set(p, fromInt(machine value))     (setValueNotifyingHost)
//
// but only if ALL of these hold:
//   1. the snapshot says the machine is ready and the value is known;
//   2. p has no request pending or in flight (pendingMask): a held or
//      running request will change the machine to the host's value, and
//      pushing the old value would undo the user's move. Likewise nothing
//      is pushed while a program load is pending or in flight, and no
//      slider while a variation load is: those values are about to change,
//      and an echo of them arriving during the load could not be told from
//      a move (the scheduler cannot call it a no-op: the value the load
//      will leave is not known yet);
//   3. the host's value is still the one the audio side last saw
//      (hostSeen): if the host moved since, its move is on its way to the
//      scheduler and wins; the next snapshot is looked at again;
//   4. the host's value does not already mean the machine's value
//      (toInt(host) == machine value): nothing to do (this also keeps the
//      host's exact normalized value, e.g. a jittered automation point).
//
// Echo suppression is not done here: it is the Scheduler's no-op rule. The
// pushed value is exactly the machine's, so when the host (or our own
// parameter listener) hands it back, the scheduler sees a request equal to
// what the machine holds and drops it. No "pushing" flag, no time window:
// a host that echoes late, twice, or jittered within the raw step is still a
// no-op, and at quiescence host and machine agree.
//
// Direct parameters (level, dry/wet, outputs, analog) are never written
// back: the machine does not change them.
#pragma once
#include "layout.hpp"
#include "scheduler.hpp"
#include <cstdint>

namespace lexparams {

// The host's parameters as the write-back sees them (the processor
// implements it over its APVTS parameters; message thread only).
class HostParams {
public:
    virtual ~HostParams() = default;
    // The parameter's current normalized value (AudioProcessorParameter::getValue).
    virtual float get(int param) const = 0;
    // Set it and tell the host (setValueNotifyingHost; begin/endChangeGesture
    // around it is the integrator's choice).
    virtual void set(int param, float value) = 0;
};

// The machine's value of an operator parameter in a snapshot; false if unknown.
inline bool publishedValue(const Published &s, int p, int &out) {
    const ParamInfo &info = param(p);
    const MachineState &m = s.machine;
    switch (info.kind) {
        case Kind::Program:
            out = m.program;
            return m.program >= 0;
        case Kind::Variation:
            out = m.variation;
            return m.variation > 0;
        case Kind::Slider:
            if (m.program < 0 || info.sub > m.sliderCount) {
                return false;
            }
            out = m.sliders[info.sub - 1];
            return true;
        case Kind::Toggle:
            if ((m.togglesKnown & (1u << info.sub)) == 0) {
                return false;
            }
            out = 0;
            if ((m.toggles & (1u << info.sub)) != 0) {
                out = 1;
            }
            return true;
        default:
            return false;
    }
}

class WriteBack {
public:
    // Message thread. Returns the number of parameters pushed.
    int poll(const Seqlock<Published> &channel, HostParams &host) {
        Published s = channel.read();
        if (s.generation == lastGeneration_) {
            return 0;
        }
        lastGeneration_ = s.generation;
        if (s.ready == 0) {
            return 0;
        }
        SliderRange range = s.machine.range();
        bool programAhead = (s.pendingMask & (uint64_t(1) << kProgram)) != 0;
        bool variationAhead = (s.pendingMask & (uint64_t(1) << kVariation)) != 0;
        int pushed = 0;
        for (int p = 0; p < kParamCount; p++) {
            Kind kind = param(p).kind;
            if (!isOperatorKind(kind)) {
                continue;
            }
            if (programAhead || (variationAhead && kind == Kind::Slider)) {
                continue;   // rule 2: a load ahead will change these values
            }
            int value = 0;
            if (!publishedValue(s, p, value)) {
                continue;
            }
            if ((s.pendingMask & (uint64_t(1) << p)) != 0) {
                continue;
            }
            float now = host.get(p);
            if (now != s.hostSeen[p]) {
                skippedMoved_++;
                continue;
            }
            if (toInt(p, now, range) == value) {
                continue;
            }
            host.set(p, fromInt(p, value, range));
            pushed++;
        }
        pushed_ += uint64_t(pushed);
        return pushed;
    }

    // Forget the last generation (e.g. after the editor reopens) so the next
    // poll looks again.
    void reset() {
        lastGeneration_ = 0;
    }

    uint64_t pushedCount() const {
        return pushed_;
    }
    uint64_t skippedMovedCount() const {
        return skippedMoved_;
    }

private:
    uint32_t lastGeneration_ = 0;
    uint64_t pushed_ = 0;
    uint64_t skippedMoved_ = 0;
};

}  // namespace lexparams
