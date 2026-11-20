// The machine driver: the C++ twin of `createMachine` in
// ../../../web-demo/page/larc.js, around lexplug::Engine.
//
// Inputs to the machine (keys, faders, pokes, buttons, pots) are actions
// fired at an exact machine time; waits are conditions checked after each
// rendered block, resolving true when the condition holds or false at their
// deadline. Coroutines (task.hpp) await them, so operator code reads like
// the JS: `co_await m.sleep(0.1)`, `co_await m.wait_for(test, 0.4)`.
//
// Two ways to drive it, running the same operator code:
//   render(...)       live: the audio callback renders its host block;
//   pump_until_idle() synchronous: renders silence up to each next wake-up
//                     until nothing is scheduled or waiting, as the JS pump()
//                     does with audio stopped. For boot, restore and the soak
//                     test, on any thread. It is render() fed silence.
// Both follow the JS pump's segmentation (see render()), so the machine gets
// its inputs at the same frames however rendering is split into calls:
// live in any host block size, offline, or pumped. Waiters are checked, and
// coroutines resumed, only at the pump's segment ends, never at call
// boundaries (tests/operator_equiv/chunks.cpp checks chunk sizes 1, 7, 128,
// 511, 4096 and random against the pump and the JS).
//
// Time. `time` is the JS machine clock, a double in seconds advanced by
// count / 48000 per completed renderInto chunk, with the JS's own arithmetic
// (action times and deadlines are doubles computed exactly as the JS
// computes them), so the C++ operator gives the machine its inputs at the
// same frames as the JS one; it is a function of the machine's own
// segmentation only. `frame` counts rendered frames exactly and is the stamp
// for everything recorded. Wall time is never read.
//
// Memory. Everything is allocated in the constructor: the frame pool
// (task.hpp), the action and waiter arrays, and the silent pump buffers.
// LEXPLUG_OPERATOR_PROBE(bool), if defined, is called around every stretch
// of operator code (spawn, resumption): tests use it to count heap
// allocations made by operator code (tests/operator_equiv/live.cpp).
//
// Capacities (fail loudly when exceeded, never grow):
//   actions   64   the operator schedules at most 2 at a time (a key tap)
//   waiters   8    one operator task awaits one thing at a time
//   roots     4    root tasks alive at once (the plugin runs one operator
//                  task at a time, as app.js operate() does)
#pragma once
#include "../engine.hpp"
#include "task.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace lexplug::op {

inline constexpr double RATE = 48000.0;

enum class Input : uint8_t { key, fader, poke, button, pot };

// Receives every input the machine is given, with the frame it precedes
// (the "frame kind a b" lines of bench/record_timeline.mjs).
class Recorder {
public:
    virtual ~Recorder() = default;
    virtual void input(int64_t frame, Input kind, unsigned a, unsigned b) = 0;
};

class Machine {
public:
    static constexpr std::size_t action_capacity = 64;
    static constexpr std::size_t waiter_capacity = 8;
    static constexpr std::size_t root_capacity = 4;
    static constexpr int pump_max_frames = 4800;
    static constexpr int pump_min_frames = 48;

    explicit Machine(Engine &engine) : engine_(engine) {
        silence_.fill(0.0f);
    }
    ~Machine() {
        cancel_all();
    }
    Machine(const Machine &) = delete;
    Machine &operator=(const Machine &) = delete;

    Engine &engine() {
        return engine_;
    }
    FramePool &pool() {
        return pool_;
    }
    double time() const {
        return time_;
    }
    int64_t frame() const {
        return frame_;
    }
    // Math.round(m.time * RATE): the frame the JS recorder writes.
    int64_t js_frame() const {
        return int64_t(std::floor(time_ * RATE + 0.5));
    }
    bool failed() const {
        return failed_;
    }
    const char *failure() const {
        return failure_;
    }
    void set_recorder(Recorder *recorder) {
        recorder_ = recorder;
    }
    bool idle() const {
        return action_count_ == 0 && waiter_count_ == 0;
    }
    // Times a waiter resolved while other work was still pending (the JS
    // would then resume the awaiting code at a wall-clock-dependent point;
    // the operators never do this, and this counter shows it).
    unsigned resumed_while_busy() const {
        return resumed_while_busy_;
    }

    // ------------------------------------------------------------------
    // Actions
    // ------------------------------------------------------------------
    struct Action {
        double at = 0;
        Input kind = Input::key;
        unsigned a = 0;
        unsigned b = 0;
    };

    // After every action already at or before `at` (a stable sort, as the JS).
    void schedule(double at, Input kind, unsigned a, unsigned b) {
        if (action_count_ == action_capacity) {
            fatal("the action queue is full (Machine::action_capacity)");
        }
        std::size_t i = action_count_;
        while (i > 0 && actions_[i - 1].at > at) {
            actions_[i] = actions_[i - 1];
            i--;
        }
        actions_[i] = Action{at, kind, a, b};
        action_count_++;
    }
    // m.poke / m.fader: at the current time.
    void poke(uint16_t address, uint8_t value) {
        schedule(time_, Input::poke, address, value);
    }
    void fader(unsigned slot, unsigned value) {
        schedule(time_, Input::fader, slot, value);
    }

    uint8_t peek(uint16_t address) const {
        return engine_.peek(address);
    }

    // The LARC's two 24-character lines, as the JS display() reads them
    // (UTF8ToString up to 48 bytes, stopping at a NUL, padded with spaces).
    struct Display {
        char top[25];
        char bottom[25];
    };
    Display display() const {
        const char *text = engine_.larc_text();
        char line[48];
        bool ended = false;
        for (int i = 0; i < 48; i++) {
            if (text[i] == 0) {
                ended = true;
            }
            if (ended) {
                line[i] = ' ';
            } else {
                line[i] = text[i];
            }
        }
        Display d;
        std::memcpy(d.top, line, 24);
        d.top[24] = 0;
        std::memcpy(d.bottom, line + 24, 24);
        d.bottom[24] = 0;
        return d;
    }

    // ------------------------------------------------------------------
    // Waits
    // ------------------------------------------------------------------
    template <typename Test>
    struct WaitAwaiter {
        Machine &machine;
        Test test;
        double seconds;
        bool result = false;

        bool await_ready() noexcept {
            return false;
        }
        void await_suspend(std::coroutine_handle<> handle) {
            machine.add_waiter(machine.time_ + seconds, this, &WaitAwaiter::call, handle, &result);
        }
        bool await_resume() noexcept {
            return result;
        }
        static bool call(void *self) {
            return static_cast<WaitAwaiter *>(self)->test();
        }
    };
    struct Never {
        bool operator()() const {
            return false;
        }
    };

    // Resolves true when test() holds after a rendered block, false once
    // `seconds` of machine time have passed. The test lives in the awaiting
    // frame; it may keep state (mutable lambda).
    template <typename Test>
    WaitAwaiter<Test> wait_for(Test test, double seconds) {
        return WaitAwaiter<Test>{*this, std::move(test), seconds};
    }
    WaitAwaiter<Never> sleep(double seconds) {
        return WaitAwaiter<Never>{*this, Never{}, seconds};
    }

    // ------------------------------------------------------------------
    // Root tasks
    // ------------------------------------------------------------------
    // Start a root task: `make` is called with the pool current and returns
    // a Task<void> (call a coroutine function; never a capturing coroutine
    // lambda). The task runs until its first wait. Returns a root index.
    // Only while no other task is running (idle), between render() calls.
    template <typename Make>
    int spawn(Make make) {
        int index = -1;
        for (std::size_t i = 0; i < root_capacity; i++) {
            if (!roots_[i].task.handle()) {
                index = int(i);
                break;
            }
        }
        if (index < 0) {
            fatal("too many root tasks (Machine::root_capacity)");
        }
        // Only between tasks: a task started while another is waiting would
        // see the clock at a point that depends on how rendering was split.
        if (!idle() || segment_left_ != 0 || chunk_left_ != 0) {
            fatal("spawn() while another operator task is running (one task at a time)");
        }
        PoolScope scope(pool_);
#ifdef LEXPLUG_OPERATOR_PROBE
        LEXPLUG_OPERATOR_PROBE(true);
#endif
        Root &root = roots_[std::size_t(index)];
        root.state = RootState{};
        root.task = make();
        root.task.handle().promise().root = &root.state;
        root.task.handle().resume();
#ifdef LEXPLUG_OPERATOR_PROBE
        LEXPLUG_OPERATOR_PROBE(false);
#endif
        return index;
    }
    bool done(int root) const {
        return roots_[std::size_t(root)].state.done;
    }
    // The root's failure message, or null. Frees the root slot when done.
    const RootState &state(int root) const {
        return roots_[std::size_t(root)].state;
    }
    void release(int root) {
        if (!roots_[std::size_t(root)].state.done) {
            fatal("release() of a root task that is still running");
        }
        roots_[std::size_t(root)].task.reset();
    }

    // Spawn, pump until it finishes; returns its state (copied).
    template <typename Make>
    RootState run_task(Make make) {
        int root = spawn(make);
        while (!done(root) && !failed_) {
            if (idle()) {
                fatal("a root task is suspended with nothing scheduled or waiting");
            }
            pump_step();
        }
        RootState result = state(root);
        if (!done(root)) {
            result.failed = true;
            result.error.assign(failure_);
            cancel_all();
        } else {
            release(root);
        }
        return result;
    }

    // Drop everything scheduled and destroy every root task (their frames
    // go back to the pool). For replacing the machine or giving up.
    void cancel_all() {
        waiter_count_ = 0;
        action_count_ = 0;
        segment_left_ = 0;   // (a chunk in progress still completes)
        for (auto &root : roots_) {
            root.task.reset();
        }
    }

    // ------------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------------
    // The JS pump renders silence in segments (to the next wake-up, 48-4800
    // frames); within a segment renderInto renders chunks, split at action
    // times, advancing the clock by count / RATE per chunk; the waiters are
    // checked at the end of each segment. Here render() follows exactly that
    // segmentation whatever the host's block size: a host block may end in
    // the middle of a chunk, and the chunk simply continues in the next block
    // (the clock advances when the chunk completes, as in the JS). So the
    // machine gets its inputs at the same frames live, offline, in any block
    // size, and under the synchronous pump, which is this same code fed
    // silence one segment at a time; and those frames are the JS pump's.
    //
    // While nothing is scheduled or waiting there are no segments: the
    // machine just renders, and the clock is recomputed from the frame count
    // (frame / RATE), so that it does not depend on the host's block sizes
    // either. Start root tasks (spawn) between render() calls; the plugin
    // splits a host block at a command's stamped frame to do that, and
    // starts one task at a time, when the previous one has finished (at a
    // segment end), as app.js operate() does.
    //
    // Render `frames` frames of the given input into out[0..3] (outputs A-D).
    // False once the machine has stopped (the engine threw); it then stays
    // stopped and renders nothing more.
    bool render(const float *left, const float *right, float *const out[4], int frames) {
        int done_frames = 0;
        while (done_frames < frames) {
            if (failed_) {
                return false;
            }
            if (chunk_left_ > 0) {
                int n = frames - done_frames;
                if (int64_t(n) > chunk_left_) {
                    n = int(chunk_left_);
                }
                if (!render_frames(left, right, out, done_frames, n)) {
                    return false;
                }
                done_frames += n;
                chunk_left_ -= n;
                segment_left_ -= n;
                if (chunk_left_ == 0) {
                    time_ += double(chunk_total_) / RATE;
                    if (segment_left_ == 0) {
                        check_waiters();
                    }
                }
                continue;
            }
            if (idle() && segment_left_ == 0) {
                int n = frames - done_frames;
                if (!render_frames(left, right, out, done_frames, n)) {
                    return false;
                }
                done_frames += n;
                time_ = double(frame_) / RATE;
                continue;
            }
            if (segment_left_ == 0) {
                segment_left_ = next_segment();
            }
            // renderInto's loop head: fire what is due, else size the next chunk.
            if (action_count_ > 0 && actions_[0].at <= time_) {
                Action action = actions_[0];
                for (std::size_t i = 1; i < action_count_; i++) {
                    actions_[i - 1] = actions_[i];
                }
                action_count_--;
                fire(action);
                continue;
            }
            double count = double(segment_left_);
            if (action_count_ > 0) {
                double until = std::ceil((actions_[0].at - time_) * RATE);
                count = std::max(1.0, std::min(count, until));
            }
            chunk_total_ = int64_t(count);
            chunk_left_ = chunk_total_;
        }
        return !failed_;
    }

    // One JS pump step: silence to the end of the current segment.
    bool pump_step() {
        if (segment_left_ == 0) {
            segment_left_ = next_segment();
        }
        int n = int(segment_left_);
        float *out[4] = {scratch_[0].data(), scratch_[1].data(), scratch_[2].data(), scratch_[3].data()};
        return render(silence_.data(), silence_.data(), out, n);
    }
    // Run silently until nothing is scheduled or waiting (or the machine stops).
    bool pump_until_idle() {
        while (!idle() || segment_left_ != 0) {
            if (!pump_step()) {
                return false;
            }
        }
        return true;
    }

private:
    struct Waiter {
        double deadline = 0;
        void *context = nullptr;
        bool (*test)(void *) = nullptr;
        std::coroutine_handle<> handle;
        bool *result = nullptr;
    };
    struct Root {
        Task<void> task;
        RootState state;
    };

    void add_waiter(double deadline, void *context, bool (*test)(void *), std::coroutine_handle<> handle,
                    bool *result) {
        if (waiter_count_ == waiter_capacity) {
            fatal("too many waiters (Machine::waiter_capacity)");
        }
        waiters_[waiter_count_++] = Waiter{deadline, context, test, handle, result};
    }

    // The pump's block: to the next wake-up, in 1-100 ms.
    int64_t next_segment() const {
        double next = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < waiter_count_; i++) {
            next = std::min(next, waiters_[i].deadline);
        }
        if (action_count_ > 0) {
            next = std::min(next, actions_[0].at);
        }
        double wanted = std::ceil((next - time_) * RATE);
        double frames = pump_max_frames;
        if (std::isfinite(wanted)) {
            frames = std::min(double(pump_max_frames), wanted);
        }
        frames = std::max(double(pump_min_frames), frames);
        return int64_t(frames);
    }

    bool render_frames(const float *left, const float *right, float *const out[4], int offset, int n) {
        float *part[4] = {out[0] + offset, out[1] + offset, out[2] + offset, out[3] + offset};
        try {
            engine_.render(left + offset, right + offset, part, n);
        } catch (const std::exception &e) {
            std::snprintf(failure_, sizeof failure_, "the machine stopped: %s", e.what());
            failed_ = true;
            return false;
        }
        frame_ += n;
        return true;
    }

    void fire(const Action &action) {
        if (recorder_ != nullptr) {
            recorder_->input(frame_, action.kind, action.a, action.b);
        }
        switch (action.kind) {
        case Input::key:
            engine_.key(action.a, action.b != 0);
            break;
        case Input::fader:
            engine_.fader(action.a, action.b);
            break;
        case Input::poke:
            engine_.poke(uint16_t(action.a), uint8_t(action.b));
            break;
        case Input::button:
            engine_.button(action.a, action.b);
            break;
        case Input::pot:
            engine_.pot(action.a, action.b);
            break;
        }
    }

    // The JS filter: test first, then the deadline; resolved coroutines
    // resume in order once every waiter has been checked.
    void check_waiters() {
        std::array<std::coroutine_handle<>, waiter_capacity> ready;
        std::size_t ready_count = 0;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < waiter_count_; i++) {
            Waiter &w = waiters_[i];
            if (w.test(w.context)) {
                *w.result = true;
                ready[ready_count++] = w.handle;
            } else if (time_ >= w.deadline) {
                *w.result = false;
                ready[ready_count++] = w.handle;
            } else {
                waiters_[kept++] = w;
            }
        }
        waiter_count_ = kept;
        if (ready_count == 0) {
            return;
        }
        if (!idle()) {
            resumed_while_busy_++;
        }
        PoolScope scope(pool_);
#ifdef LEXPLUG_OPERATOR_PROBE
        LEXPLUG_OPERATOR_PROBE(true);
#endif
        for (std::size_t i = 0; i < ready_count; i++) {
            ready[i].resume();
        }
#ifdef LEXPLUG_OPERATOR_PROBE
        LEXPLUG_OPERATOR_PROBE(false);
#endif
    }

    Engine &engine_;
    // The pool is declared before the roots so that it outlives their frames.
    FramePool pool_;
    std::array<Action, action_capacity> actions_{};
    std::size_t action_count_ = 0;
    std::array<Waiter, waiter_capacity> waiters_{};
    std::size_t waiter_count_ = 0;
    std::array<Root, root_capacity> roots_{};
    double time_ = 0;
    int64_t frame_ = 0;
    int64_t segment_left_ = 0;   // frames left in the current pump segment
    int64_t chunk_left_ = 0;     // frames left in the current renderInto chunk
    int64_t chunk_total_ = 0;
    bool failed_ = false;
    char failure_[200] = {0};
    Recorder *recorder_ = nullptr;
    unsigned resumed_while_busy_ = 0;
    std::array<float, pump_max_frames> silence_{};
    std::array<std::array<float, pump_max_frames>, 4> scratch_{};
};

}  // namespace lexplug::op
