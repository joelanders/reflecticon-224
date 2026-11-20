// Commands from the message thread to the audio thread: a preallocated,
// lock-free single-producer single-consumer ring of POD commands, with a
// coalescing backlog on the producer side so that nothing is ever dropped
// and nothing ever allocates.
//
//   message thread (ONE producer: the editor's UiCommands, state restore on
//   a live machine)             CommandQueue::push / flush
//   audio thread (ONE consumer) CommandQueue::drainInto(Scheduler&)
//
// A command is a parameter index and its NORMALIZED host value (layout.hpp),
// the same representation for every source: the ring, host automation
// (Scheduler::pollHost) and state replay. Converting a slider's normalized
// value to a raw byte waits until the command is used, with the range of
// the machine then running (so a change held across a firmware-set change
// is not converted with a stale range).
//
// Coalescing happens in two places, both "latest value wins, keeping the
// position of the first arrival" (app.js pendingMoves):
//   - here, only when the ring is full: the overflow waits in a per-parameter
//     backlog (47 slots, one per parameter) and is flushed in arrival order;
//   - in the Scheduler's pending table, for everything (the ring is FIFO).
//
// Capacity: 1024 commands. A UI produces at most a few hundred per second
// and the audio thread drains the ring every block, so the backlog is only
// used when the audio thread is not running (no playback, a stalled device).
// Call flush() from a message-thread timer so a backlog drains once the
// audio thread runs again.
#pragma once
#include "layout.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lexparams {

struct Command {
    uint16_t param = 0;     // parameter index (layout.hpp)
    float value = 0.0f;     // its normalized value, 0..1
    uint64_t frame = 0;     // Scheduler frame at which it takes effect (0 = as soon as seen)
};
static_assert(std::is_trivially_copyable_v<Command>, "Command must be POD");

// A lock-free SPSC ring: one producer thread, one consumer thread.
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "ring elements must be POD");

public:
    // Producer only. False if full.
    bool tryPush(const T &item) {
        uint64_t head = head_.load(std::memory_order_relaxed);
        uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail == Capacity) {
            return false;
        }
        slots_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    // Consumer only. False if empty.
    bool tryPop(T &out) {
        uint64_t tail = tail_.load(std::memory_order_relaxed);
        uint64_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        out = slots_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    // Approximate (exact from either side when the other is idle).
    std::size_t size() const {
        return std::size_t(head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire));
    }
    static constexpr std::size_t capacity() {
        return Capacity;
    }

private:
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};
    alignas(64) std::array<T, Capacity> slots_{};
};

class CommandQueue {
public:
    static constexpr std::size_t kCapacity = 1024;

    // Message thread. Never blocks, never allocates, never loses a command:
    // if the ring is full it waits in the backlog (latest value per parameter).
    void push(const Command &command) {
        flush();
        if (backlogCount_ == 0 && ring_.tryPush(command)) {
            return;
        }
        Backlog &slot = backlog_[command.param];
        if (!slot.used) {
            slot.used = true;
            slot.order = nextOrder_++;
            backlogCount_++;
        }
        slot.command = command;
        coalesced_++;
    }
    void push(int param, float value) {
        Command command;
        command.param = uint16_t(param);
        command.value = value;
        push(command);
    }

    // Message thread: move the backlog into the ring, oldest first, as far as
    // it fits. True if the backlog is empty afterwards.
    bool flush() {
        while (backlogCount_ > 0) {
            int oldest = -1;
            for (int p = 0; p < kParamCount; p++) {
                if (backlog_[p].used && (oldest < 0 || backlog_[p].order < backlog_[oldest].order)) {
                    oldest = p;
                }
            }
            if (!ring_.tryPush(backlog_[oldest].command)) {
                return false;
            }
            backlog_[oldest].used = false;
            backlogCount_--;
        }
        return true;
    }

    // Audio thread: pop everything into the scheduler (Scheduler::submit
    // stamps a command with frame 0 at the scheduler's current frame).
    template <typename Sink>
    int drainInto(Sink &scheduler) {
        int count = 0;
        Command command;
        while (ring_.tryPop(command)) {
            scheduler.submit(command);
            count++;
        }
        return count;
    }

    // For tests and diagnostics (message thread).
    int backlogCount() const {
        return backlogCount_;
    }
    uint64_t coalescedCount() const {
        return coalesced_;
    }

private:
    struct Backlog {
        bool used = false;
        uint64_t order = 0;
        Command command;
    };
    SpscRing<Command, kCapacity> ring_;
    // Producer-side only (never touched by the audio thread).
    std::array<Backlog, kParamCount> backlog_{};
    int backlogCount_ = 0;
    uint64_t nextOrder_ = 0;
    uint64_t coalesced_ = 0;
};

}  // namespace lexparams
