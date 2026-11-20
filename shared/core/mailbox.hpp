// A one-slot "latest value" hand-off from a control thread to the audio thread (from
// dn780::Engine). The writer may overwrite a value the reader has not taken yet; it waits only
// while the reader copies the slot. The reader never waits. T must be trivially copyable and
// small enough to copy in well under a microsecond.
#pragma once
#include <atomic>
#include <type_traits>

namespace kit {

template <class T>
class Mailbox {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    // control thread
    void post(const T& v) {
        for (;;) {
            int s = state_.load(std::memory_order_acquire);
            if (s == Reading) continue;   // the reader is copying
            if (state_.compare_exchange_weak(s, Writing, std::memory_order_acq_rel)) break;
        }
        slot_ = v;
        state_.store(Ready, std::memory_order_release);
    }
    // audio thread: true and the newest value if one was posted since the last take
    bool take(T& out) {
        int s = Ready;
        if (!state_.compare_exchange_strong(s, Reading, std::memory_order_acq_rel)) return false;
        out = slot_;
        state_.store(Idle, std::memory_order_release);
        return true;
    }

private:
    enum : int { Idle, Writing, Ready, Reading };
    std::atomic<int> state_{Idle};
    T slot_{};
};

}  // namespace kit
