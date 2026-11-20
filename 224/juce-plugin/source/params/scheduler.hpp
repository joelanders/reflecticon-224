// The audio-side scheduler: turns parameter changes into operator tasks, one
// at a time (app.js operate()), holds them while the machine is not ready or
// busy, and publishes what the machine holds for the write-back. JUCE-free,
// allocation-free and lock-free after construction; audio thread only
// (or any single thread driving a machine, e.g. a background restore pump).
//
// ---------------------------------------------------------------------
// Inputs (all become Commands = parameter index + normalized value, stamped
// with a Scheduler frame):
//   pollHost(values)   host automation: the processor reads every
//                      parameter's normalized value at the start of each
//                      block (AudioProcessorParameter::getValue(), lock-free)
//                      and passes them here; changed ones become commands
//                      stamped at the block's first frame. Reading at block
//                      boundaries, never from listener callbacks, is what
//                      makes an offline bounce equal a realtime one: the
//                      stamps depend on the host's block boundaries, not on
//                      wall time or on which thread the host called us from.
//   submit(command)    anything else, e.g. CommandQueue::drainInto (the
//                      editor), or a test timeline. A command with frame 0
//                      (or a frame in the past) is stamped with frame().
// Call order per host block: pollHost, CommandQueue::drainInto, run (the
// host's values first: see pollHost for why loads must come after the
// write-back's echoes).
//
// Time. frame() counts the internal (48 kHz) frames run() has been asked to
// render, dry or live. Decisions happen only on a fixed grid of
// `quantum` frames (default 128, the web's AudioWorklet quantum), counted
// from the frame at which the sink last became ready (or 0): a command
// stamped at frame F takes effect at the first grid point >= F, and an
// operator task's end is noticed at the first grid point after it. run()
// splits the rendering at grid points, so the machine sees the same chunk
// boundaries and the scheduler the same decision points whatever the host
// block size: the same stamped timeline dispatches the same tasks at the
// same frames (tests/params checks block sizes 1..4096).
//   ⚠️ The integrator's render callback must keep that property: the
//   machine's waiters must only be checked at grid points (the `gridEnd`
//   flag), because op::Machine checks them after every render() call and
//   advances its double `time` per call.
//
// Operator parameters (program, variation, slider_k, toggles):
//   pending table   one slot per parameter: latest value wins, keeping the
//                   first arrival's order (app.js pendingMoves).
//   no-op rule      a value equal to what the machine will hold anyway (the
//                   pending value, else the in-flight task's, else the
//                   machine's current value, unless a load ahead of it
//                   will change that) is dropped at admission; a
//                   pending value equal to the machine's value when its turn
//                   comes is dropped at dispatch. This is the ECHO
//                   SUPPRESSION: the write-back sets a host parameter to
//                   exactly the machine's value, so its echo (immediate, or
//                   re-sent by the host later, even jittered within the same
//                   raw step) is a no-op here. It needs no flags and no
//                   timing, and whatever the interleaving, at quiescence the
//                   host's value and the machine's agree.
//   supersede rule  a program request drops pending variation and slider
//                   requests that arrived before it (the load resets them;
//                   slider_k also changes meaning with the program); a
//                   variation request drops earlier pending slider requests.
//                   Requests arriving after a load request are kept and run
//                   after it.
//   dispatch order  one task at a time, when the sink is ready and idle
//                   (and minGapFrames after the previous task ended):
//                   program, then variation, then sliders in arrival order,
//                   then toggles in arrival order (share.js applyShareLink's
//                   order: load, variation, moves, toggles).
//   hold/replay     while the sink is not ready (booting, swapping, stopped)
//                   nothing is dispatched; requests wait in the pending table
//                   and are replayed (latest values, in the order above) once
//                   it is ready. While a task runs (a program load included)
//                   new requests wait the same way. clearPending() drops the
//                   held requests (call it when a restore replaces the machine
//                   so that the restored state wins).
// Direct parameters (level, dry/wet, outputs, analog) are not operator
// tasks: applied at the grid point they become due if the sink is ready,
// otherwise held; all of them are (re)applied when the sink becomes ready.
//
// Output: a Published snapshot in a seqlock (seqlock.hpp), rewritten with a
// new generation whenever anything in it changes: the machine's state (read
// with OperatorSink::readState when the sink becomes ready and after every
// task), which parameters have a request pending or in flight, and the host
// values pollHost last saw. The message thread's WriteBack (writeback.hpp)
// pushes it back to the host parameters.
#pragma once
#include "command_queue.hpp"
#include "layout.hpp"
#include "seqlock.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lexparams {

// What the machine holds, read from its RAM without key presses.
struct MachineState {
    int32_t program = -1;           // index into lexcat::Catalog::programs; -1 = unknown
    int32_t variation = 0;          // 1..8; 0 = unknown
    int32_t sliderCount = 0;        // the program's named sliders (slider_1..slider_count)
    int32_t rangeMin = kLarcRange.min;   // the remote's slider range (Catalog::rawMin/rawMax)
    int32_t rangeMax = kLarcRange.max;
    // slider_k's position at [k-1]: lexcat::sliderPosition(slider, stored
    // byte, shown text, rawMax), i.e. the stored byte except on pages that
    // keep a parameter elsewhere (SIZE pages), where the table's raw for the
    // text the firmware shows.
    uint8_t sliders[kSliders] = {};
    // slider_k's stored byte in the parameter record at [k-1] (Program::
    // generic order): what the share payload / saved state records.
    uint8_t stored[kSliders] = {};
    uint8_t toggles = 0;            // bit t = toggle t on
    uint8_t togglesKnown = 0;       // bit t = toggle t offered by this remote and its state read

    SliderRange range() const {
        return SliderRange{rangeMin, rangeMax};
    }
};

// The machine side, as the scheduler sees it. Implemented by the integrator
// around the operator (source/operator); a fake in tests/params. Called only
// from Scheduler::run, i.e. on the thread that renders the machine; no call
// may block or allocate.
class OperatorSink {
public:
    virtual ~OperatorSink() = default;

    // The machine is live and the operator can take a task (false while
    // booting, before the swap, after the machine stopped).
    virtual bool ready() const = 0;
    // An operator task is running (the Machine's root task is not done).
    virtual bool busy() const = 0;

    // Start one operator task (only called when ready() and !busy()). Return
    // false if it does not apply (no such program/variation/slider in the
    // current catalog and program, toggle not offered by this remote); the
    // write-back then puts the host parameter back.
    //   loadProgram   app.js selectEntry(catalog[index]) (LARC selectProgram /
    //                 panel loadProgram), then describeProgram's RAM read
    //   loadVariation op.loadVariation(v)
    //   moveSlider    op.moveSlider(page, slot, raw) with (page, slot) =
    //                 Program::generic[k-1] of the running program
    //   setToggle     op.setToggle(label, on); MUTE: op.toggleMute() if the
    //                 state differs
    virtual bool loadProgram(int index) = 0;
    virtual bool loadVariation(int variation) = 0;
    virtual bool moveSlider(int k, int raw) = 0;
    virtual bool setToggle(int toggle, bool on) = 0;

    // Direct controls (not operator tasks).
    virtual void setLevelDb(float db) = 0;
    virtual void setDryWet(float wet) = 0;
    virtual void setOutput(int side, int dac) = 0;   // side 0 = L, 1 = R; dac 0..3 = A..D
    virtual void setAnalog(bool on) = 0;

    // Fill `out` from the machine's RAM (program, variation, stored bytes as
    // positions, toggles). Called when the sink becomes ready and at the grid
    // point after each task ends.
    virtual void readState(MachineState &out) = 0;
};

// The snapshot for the message thread.
struct Published {
    uint32_t generation = 0;        // bumped on every change (0 = nothing published)
    uint8_t ready = 0;              // the sink was ready at the last grid point
    int8_t busyParam = -1;          // the parameter whose task is in flight, -1 = idle
    MachineState machine;
    uint64_t pendingMask = 0;       // bit p: parameter p has a request pending or in flight
    float hostSeen[kParamCount] = {};   // the normalized host values pollHost last saw
};
static_assert(kParamCount <= 64, "pendingMask holds one bit per parameter");

struct SchedulerConfig {
    int quantum = 128;              // decision grid, internal frames
    uint64_t minGapFrames = 0;      // rate limit: idle frames between two operator tasks
};

class Scheduler {
public:
    static constexpr int kTimelineCapacity = 512;

    explicit Scheduler(SchedulerConfig config = {}) : config_(config) {
        for (int p = 0; p < kParamCount; p++) {
            seen_[p] = param(p).defaultNorm;
            direct_[p] = param(p).defaultNorm;
        }
        publish();
    }

    uint64_t frame() const {
        return frame_;
    }
    bool ready() const {
        return ready_;
    }
    int busyParam() const {
        return inFlight_;
    }
    const Seqlock<Published> &channel() const {
        return channel_;
    }
    const MachineState &machine() const {
        return machine_;
    }

    // Take a command (see the header). If the timeline is full, its earliest
    // command is admitted at once to make room (order is kept).
    void submit(Command command) {
        if (command.param >= kParamCount) {
            return;
        }
        if (command.frame < frame_) {
            command.frame = frame_;
        }
        if (timelineCount_ == kTimelineCapacity) {
            admit(timeline_[0]);
            std::copy(timeline_.begin() + 1, timeline_.begin() + timelineCount_, timeline_.begin());
            timelineCount_--;
        }
        int at = timelineCount_;
        while (at > 0 && timeline_[at - 1].frame > command.frame) {
            timeline_[at] = timeline_[at - 1];
            at--;
        }
        timeline_[at] = command;
        timelineCount_++;
    }

    // Host automation: `values` holds every parameter's normalized value
    // (kParamCount floats, by index). Changed values become commands stamped
    // at frame().
    //
    // Within one poll the changes have no order of their own, and they are
    // submitted with the loads LAST: sliders, toggles and direct parameters,
    // then the variation, then the program. A load's supersede rule then
    // drops slider values from the same poll (they belonged to the program
    // being replaced). This is what keeps the write-back's echo a no-op: when
    // the write-back pushes the old program's values and the user picks a
    // new program before the next block, both arrive in one poll; were the
    // program admitted first, the echoes would count as moves requested
    // after the load and be replayed onto the new program.
    void pollHost(const float *values) {
        for (int p = kSlider1; p < kParamCount; p++) {
            pollOne(values, p);
        }
        pollOne(values, kVariation);
        pollOne(values, kProgram);
    }

    // Drop every held operator request (not the direct values).
    void clearPending() {
        for (Slot &slot : pending_) {
            slot.pending = false;
        }
        for (int i = 0; i < timelineCount_; i++) {
            if (timeline_[i].param < kParamCount && isOperatorKind(param(timeline_[i].param).kind)) {
                timeline_[i].param = kParamCount;   // tombstone, skipped at admission
            }
        }
    }

    // The sink now stands for a different machine (the processor swapped in
    // a freshly booted or restored one between two run() calls): handle the
    // next grid point as the sink becoming ready, i.e. restart the grid there,
    // read the new machine's state and reapply every direct value. An
    // in-flight task belonged to the old machine and is forgotten.
    void machineReplaced() {
        if (ready_) {
            ready_ = false;
            inFlight_ = -1;
            machine_ = MachineState{};
        }
    }

    // Render `frames` internal frames: render(offset, count, gridEnd) renders
    // frames [offset, offset+count) of this call; gridEnd is true when the
    // chunk ends on a grid point. Chunks never cross a grid point.
    template <typename Render>
    void run(OperatorSink &sink, int frames, Render &&render) {
        int done = 0;
        while (done < frames) {
            if (sink.ready() != ready_) {
                origin_ = frame_;   // the grid restarts where readiness changes
            }
            uint64_t phase = (frame_ - origin_) % uint64_t(config_.quantum);
            if (phase == 0) {
                service(sink);
            }
            int count = int(uint64_t(config_.quantum) - phase);
            if (count > frames - done) {
                count = frames - done;
            }
            render(done, count, phase + uint64_t(count) == uint64_t(config_.quantum));
            done += count;
            frame_ += uint64_t(count);
        }
    }

    // Counters (tests, diagnostics).
    uint64_t dispatchedCount() const {
        return dispatched_;
    }
    uint64_t droppedCount() const {
        return dropped_;
    }
    uint64_t rejectedCount() const {
        return rejected_;
    }
    uint64_t hostChangeCount() const {
        return hostChanges_;
    }

private:
    void pollOne(const float *values, int p) {
        if (values[p] != seen_[p] && !std::isnan(values[p])) {
            seen_[p] = values[p];
            Command command;
            command.param = uint16_t(p);
            command.value = values[p];
            command.frame = frame_;
            submit(command);
            hostChanges_++;
        }
    }

    struct Slot {
        bool pending = false;
        float value = 0.0f;
        uint64_t order = 0;
    };

    // The machine's current value of an operator parameter; false if unknown.
    bool machineValue(int p, int &out) const {
        const ParamInfo &info = param(p);
        switch (info.kind) {
            case Kind::Program:
                out = machine_.program;
                return machine_.program >= 0;
            case Kind::Variation:
                out = machine_.variation;
                return machine_.variation > 0;
            case Kind::Slider:
                if (machine_.program < 0 || info.sub > machine_.sliderCount) {
                    return false;
                }
                out = machine_.sliders[info.sub - 1];
                return true;
            case Kind::Toggle:
                if ((machine_.togglesKnown & (1u << info.sub)) == 0) {
                    return false;
                }
                out = 0;
                if ((machine_.toggles & (1u << info.sub)) != 0) {
                    out = 1;
                }
                return true;
            default:
                return false;
        }
    }

    bool loadAhead(int which) const {
        return pending_[which].pending || inFlight_ == which;
    }

    // What the machine will hold once in-flight work is done (not counting
    // pending requests to p itself); false if unknown. A program load ahead
    // (pending or in flight) resets the variation, the sliders and (on the
    // 224) the toggles, and a variation load the sliders, to values not
    // known until it ends.
    bool settledValue(int p, int &out) const {
        if (inFlight_ == p) {
            out = inFlightValue_;
            return true;
        }
        Kind kind = param(p).kind;
        if (kind != Kind::Program && loadAhead(kProgram)) {
            return false;
        }
        if (kind == Kind::Slider && loadAhead(kVariation)) {
            return false;
        }
        return machineValue(p, out);
    }

    void admit(const Command &command) {
        int p = command.param;
        if (p >= kParamCount) {
            return;   // tombstone
        }
        const ParamInfo &info = param(p);
        if (!isOperatorKind(info.kind)) {
            direct_[p] = command.value;
            directDirty_ |= uint64_t(1) << p;
            return;
        }
        Slot &slot = pending_[p];
        if (!slot.pending) {
            int settled = 0;
            if (settledValue(p, settled) && toInt(p, command.value, range_) == settled) {
                dropped_++;   // a no-op (e.g. the echo of a write-back)
                return;
            }
            slot.order = nextOrder_++;
        }
        // (a pending request is replaced: latest wins, first arrival's order)
        if (info.kind == Kind::Program) {
            pending_[kVariation].pending = false;
            dropSliders();
        } else if (info.kind == Kind::Variation) {
            dropSliders();
        }
        slot.pending = true;
        slot.value = command.value;
    }

    void dropSliders() {
        for (int k = 1; k <= kSliders; k++) {
            pending_[sliderParam(k)].pending = false;
        }
    }

    void admitDue() {
        int due = 0;
        while (due < timelineCount_ && timeline_[due].frame <= frame_) {
            admit(timeline_[due]);
            due++;
        }
        if (due > 0) {
            std::copy(timeline_.begin() + due, timeline_.begin() + timelineCount_, timeline_.begin());
            timelineCount_ -= due;
        }
    }

    // The next pending operator request by the dispatch order, or -1.
    int pick() const {
        if (pending_[kProgram].pending) {
            return kProgram;
        }
        if (pending_[kVariation].pending) {
            return kVariation;
        }
        int best = -1;
        for (int k = 1; k <= kSliders; k++) {
            int p = sliderParam(k);
            if (pending_[p].pending && (best < 0 || pending_[p].order < pending_[best].order)) {
                best = p;
            }
        }
        if (best >= 0) {
            return best;
        }
        for (int t = 0; t < kToggles; t++) {
            int p = toggleParam(t);
            if (pending_[p].pending && (best < 0 || pending_[p].order < pending_[best].order)) {
                best = p;
            }
        }
        return best;
    }

    bool start(OperatorSink &sink, int p, int value) {
        const ParamInfo &info = param(p);
        switch (info.kind) {
            case Kind::Program:
                return sink.loadProgram(value);
            case Kind::Variation:
                return sink.loadVariation(value);
            case Kind::Slider:
                return sink.moveSlider(info.sub, value);
            case Kind::Toggle:
                return sink.setToggle(info.sub, value != 0);
            default:
                return false;
        }
    }

    void dispatch(OperatorSink &sink) {
        for (;;) {
            int p = pick();
            if (p < 0) {
                return;
            }
            pending_[p].pending = false;
            int value = toInt(p, pending_[p].value, range_);
            int current = 0;
            if (machineValue(p, current) && current == value) {
                dropped_++;
                continue;
            }
            if (start(sink, p, value)) {
                inFlight_ = p;
                inFlightValue_ = value;
                dispatched_++;
                return;
            }
            rejected_++;
            rejectedSerial_++;   // publish so that the write-back restores the host value
        }
    }

    void applyDirects(OperatorSink &sink) {
        for (int p = kLevelDb; p < kParamCount; p++) {
            if ((directDirty_ & (uint64_t(1) << p)) == 0) {
                continue;
            }
            float v = direct_[p];
            const ParamInfo &info = param(p);
            if (info.kind == Kind::Level) {
                sink.setLevelDb(levelDbOf(v));
            } else if (info.kind == Kind::DryWet) {
                sink.setDryWet(clamp01(v));
            } else if (info.kind == Kind::Output) {
                sink.setOutput(info.sub, toInt(p, v, range_));
            } else if (info.kind == Kind::Analog) {
                sink.setAnalog(toInt(p, v, range_) != 0);
            }
        }
        directDirty_ = 0;
    }

    void refresh(OperatorSink &sink) {
        MachineState state;
        sink.readState(state);
        machine_ = state;
        range_ = machine_.range();
    }

    void service(OperatorSink &sink) {
        bool nowReady = sink.ready();
        if (nowReady && !ready_) {
            ready_ = true;
            inFlight_ = -1;
            lastEnd_ = frame_;
            refresh(sink);
            for (int p = kLevelDb; p < kParamCount; p++) {
                directDirty_ |= uint64_t(1) << p;
            }
        } else if (!nowReady && ready_) {
            ready_ = false;
            inFlight_ = -1;
            machine_ = MachineState{};
        }
        admitDue();
        if (ready_) {
            applyDirects(sink);
            if (inFlight_ >= 0 && !sink.busy()) {
                inFlight_ = -1;
                lastEnd_ = frame_;
                refresh(sink);
            }
            if (inFlight_ < 0 && !sink.busy() && frame_ - lastEnd_ >= config_.minGapFrames) {
                dispatch(sink);
            }
        }
        publish();
    }

    static bool sameMachine(const MachineState &a, const MachineState &b) {
        return a.program == b.program && a.variation == b.variation && a.sliderCount == b.sliderCount &&
               a.rangeMin == b.rangeMin && a.rangeMax == b.rangeMax && a.toggles == b.toggles &&
               a.togglesKnown == b.togglesKnown && std::memcmp(a.sliders, b.sliders, sizeof a.sliders) == 0 &&
               std::memcmp(a.stored, b.stored, sizeof a.stored) == 0;
    }
    static bool samePublished(const Published &a, const Published &b) {
        if (a.ready != b.ready || a.busyParam != b.busyParam || a.pendingMask != b.pendingMask) {
            return false;
        }
        if (!sameMachine(a.machine, b.machine)) {
            return false;
        }
        for (int p = 0; p < kParamCount; p++) {
            if (a.hostSeen[p] != b.hostSeen[p]) {
                return false;
            }
        }
        return true;
    }

    void publish() {
        Published next;
        std::memset(&next, 0, sizeof next);   // padding too: the seqlock copies bytes
        if (ready_) {
            next.ready = 1;
        }
        next.busyParam = int8_t(inFlight_);
        next.machine = machine_;
        for (int p = 0; p < kParamCount; p++) {
            if (pending_[p].pending || inFlight_ == p) {
                next.pendingMask |= uint64_t(1) << p;
            }
            next.hostSeen[p] = seen_[p];
        }
        // A rejected request changes nothing visible, but the host still
        // holds the rejected value: publish anyway, so the write-back runs.
        if (last_.generation != 0 && rejectedSerial_ == publishedRejected_ && samePublished(next, last_)) {
            return;
        }
        publishedRejected_ = rejectedSerial_;
        next.generation = last_.generation + 1;
        last_ = next;
        channel_.write(next);
    }

    SchedulerConfig config_;
    uint64_t frame_ = 0;
    uint64_t origin_ = 0;
    bool ready_ = false;
    int inFlight_ = -1;
    int inFlightValue_ = 0;
    uint64_t lastEnd_ = 0;
    MachineState machine_;
    SliderRange range_ = kLarcRange;

    std::array<Command, kTimelineCapacity> timeline_{};
    int timelineCount_ = 0;
    std::array<Slot, kParamCount> pending_{};
    uint64_t nextOrder_ = 0;
    std::array<float, kParamCount> seen_{};
    std::array<float, kParamCount> direct_{};
    uint64_t directDirty_ = 0;

    uint64_t dispatched_ = 0;
    uint64_t dropped_ = 0;
    uint64_t rejected_ = 0;
    uint64_t hostChanges_ = 0;
    uint64_t rejectedSerial_ = 0;
    uint64_t publishedRejected_ = 0;

    Published last_{};
    Seqlock<Published> channel_;
};

}  // namespace lexparams
