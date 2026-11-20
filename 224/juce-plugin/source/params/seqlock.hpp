// A single-writer seqlock for a plain struct: the audio thread publishes,
// any other thread copies. The writer never waits; a reader retries while a
// write is in progress (writes are a few hundred bytes, so a retry is rare
// and short).
//
// The payload is kept in relaxed atomic words, so there is no data race in
// the C++ memory model (a plain memcpy seqlock has one); the sequence
// counter's fences order the words.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace lexparams {

template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>, "a seqlock holds a plain struct");
    static constexpr std::size_t kWords = (sizeof(T) + 7) / 8;

public:
    Seqlock() {
        T empty{};
        write(empty);
    }

    // The one writer thread.
    void write(const T &value) {
        uint64_t words[kWords] = {};
        std::memcpy(words, &value, sizeof(T));
        uint64_t seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_relaxed);   // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < kWords; i++) {
            words_[i].store(words[i], std::memory_order_relaxed);
        }
        seq_.store(seq + 2, std::memory_order_release);
    }

    // Any thread. Returns a consistent copy.
    T read() const {
        uint64_t words[kWords];
        for (;;) {
            uint64_t before = seq_.load(std::memory_order_acquire);
            if ((before & 1) != 0) {
                continue;
            }
            for (std::size_t i = 0; i < kWords; i++) {
                words[i] = words_[i].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == before) {
                break;
            }
        }
        T value;
        std::memcpy(&value, words, sizeof(T));
        return value;
    }

private:
    std::atomic<uint64_t> seq_{0};
    std::atomic<uint64_t> words_[kWords];
};

}  // namespace lexparams
