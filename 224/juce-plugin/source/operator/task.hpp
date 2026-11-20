// Coroutine tasks for the operator: the C++ twin of the JS `async function`s
// in ../../../web-demo/page/larc.js, without the heap.
//
//   Task<T>      a coroutine returning T (or void). It starts suspended and
//                runs when awaited (`co_await child()`), or when the Machine
//                starts it as a root task (machine.hpp spawn()).
//   fail(...)    `co_await fail("slider %d did not answer", n)` is the JS
//                `throw new Error(...)`. The failure travels up the chain of
//                awaiting coroutines without resuming them, to the nearest
//                `co_await caught(child())` (the JS try/catch), which resumes
//                with a Result holding the message, or to the root task,
//                which ends failed. No C++ exceptions: throwing allocates.
//   FramePool    every coroutine frame comes from a fixed pool of fixed-size
//                blocks allocated once, when the Machine is built. Frames are
//                allocated from the pool made current by the running thread
//                (PoolScope, set by the Machine around everything that can
//                create or resume a coroutine). Running out, or a frame
//                larger than a block, is a programming error: it aborts with
//                a message rather than touching the heap.
//
// A coroutine's parameters are copied into its frame; reference parameters
// must outlive it. Never make a lambda with captures a coroutine (its
// captures die with the lambda object): write a function taking arguments.
#pragma once
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace lexplug::op {

[[noreturn]] inline void fatal(const char *what) {
    std::fprintf(stderr, "lexplug operator: %s\n", what);
    std::abort();
}

// =====================================================================
// Frames
// =====================================================================
class FramePool {
public:
    // Capacities. Measured on the 224XL operators (record_timeline and the
    // soak): at -O2 clang elides most child frames into their parent (HALO),
    // so at most 2 blocks are in use and the largest frame is 9,152 bytes; at
    // -O0 nothing is elided, 6 blocks are in use and the largest frame is
    // 1,016 bytes. 16 blocks of 16 KB (256 KB per Machine) leave wide margins
    // both ways; exceeding either limit aborts with a message.
    static constexpr std::size_t block_size = 16384;
    static constexpr std::size_t block_count = 16;

    FramePool() {
        storage_ = static_cast<unsigned char *>(::operator new(block_size * block_count, std::align_val_t(header)));
        for (std::size_t i = 0; i < block_count; i++) {
            free_[i] = unsigned(block_count - 1 - i);
        }
        free_count_ = block_count;
    }
    ~FramePool() {
        if (free_count_ != block_count) {
            fatal("a FramePool was destroyed with coroutine frames still alive");
        }
        ::operator delete(storage_, std::align_val_t(header));
    }
    FramePool(const FramePool &) = delete;
    FramePool &operator=(const FramePool &) = delete;

    void *allocate(std::size_t size) {
        if (size + header > block_size) {
            char text[120];
            std::snprintf(text, sizeof text, "a coroutine frame of %zu bytes exceeds the %zu-byte block", size,
                          block_size - header);
            fatal(text);
        }
        if (free_count_ == 0) {
            fatal("the coroutine frame pool is exhausted (FramePool::block_count)");
        }
        unsigned index = free_[--free_count_];
        unsigned char *block = storage_ + std::size_t(index) * block_size;
        FramePool *self = this;
        std::memcpy(block, &self, sizeof self);
        if (size > max_request_) {
            max_request_ = size;
        }
        if (block_count - free_count_ > high_water_) {
            high_water_ = block_count - free_count_;
        }
        return block + header;
    }

    static void release(void *frame) {
        unsigned char *block = static_cast<unsigned char *>(frame) - header;
        FramePool *pool = nullptr;
        std::memcpy(&pool, block, sizeof pool);
        pool->free_[pool->free_count_++] = unsigned((block - pool->storage_) / block_size);
    }

    std::size_t in_use() const {
        return block_count - free_count_;
    }
    std::size_t high_water() const {
        return high_water_;
    }
    std::size_t max_request() const {
        return max_request_;
    }

    // The pool frames are allocated from on this thread.
    static FramePool *&current() {
        static thread_local FramePool *pool = nullptr;
        return pool;
    }

private:
    static constexpr std::size_t header = 16;   // keeps the frame 16-byte aligned
    unsigned char *storage_ = nullptr;
    unsigned free_[block_count];
    std::size_t free_count_ = 0;
    std::size_t high_water_ = 0;
    std::size_t max_request_ = 0;
};

// Makes `pool` current for this thread for the scope's lifetime.
class PoolScope {
public:
    explicit PoolScope(FramePool &pool) : previous_(FramePool::current()) {
        FramePool::current() = &pool;
    }
    ~PoolScope() {
        FramePool::current() = previous_;
    }
    PoolScope(const PoolScope &) = delete;
    PoolScope &operator=(const PoolScope &) = delete;

private:
    FramePool *previous_;
};

// =====================================================================
// Errors
// =====================================================================
struct Error {
    static constexpr std::size_t capacity = 160;
    char text[capacity] = {0};
    bool set = false;

    void assign(const char *message) {
        std::snprintf(text, capacity, "%s", message);
        set = true;
    }
};

// A caught task's outcome: ok and value, or the failure's message.
template <typename T>
struct Result {
    bool ok = false;
    T value{};
    Error error;
};
template <>
struct Result<void> {
    bool ok = false;
    Error error;
};

struct RootState;

struct PromiseBase {
    std::coroutine_handle<> continuation;   // the coroutine awaiting this one
    PromiseBase *parent = nullptr;
    bool catching = false;                  // the parent awaits through caught()
    Error error;
    RootState *root = nullptr;              // set on a root task only

    static void *operator new(std::size_t size) {
        FramePool *pool = FramePool::current();
        if (pool == nullptr) {
            fatal("a coroutine was created outside a PoolScope (use Machine::spawn or run from inside a task)");
        }
        return pool->allocate(size);
    }
    static void operator delete(void *frame) {
        FramePool::release(frame);
    }

    std::suspend_always initial_suspend() noexcept {
        return {};
    }
    void unhandled_exception() {
        fatal("an exception escaped an operator coroutine (use co_await fail(...))");
    }
};

// What a root task reports to the Machine.
struct RootState {
    bool done = false;
    bool failed = false;
    Error error;
};

// Where a coroutine goes when it finishes or fails: to the awaiting
// coroutine, or, at the root, back to whoever resumed it.
inline std::coroutine_handle<> finish(PromiseBase &promise) {
    if (promise.continuation) {
        return promise.continuation;
    }
    if (promise.root != nullptr) {
        promise.root->done = true;
    }
    return std::noop_coroutine();
}

// Carry a failure from `promise` up to the nearest catcher or the root.
inline std::coroutine_handle<> propagate(PromiseBase &promise) {
    PromiseBase *current = &promise;
    while (true) {
        if (current->parent == nullptr) {
            if (current->root != nullptr) {
                current->root->done = true;
                current->root->failed = true;
                current->root->error = current->error;
            }
            return std::noop_coroutine();
        }
        if (current->catching) {
            return current->continuation;   // its awaiter reads our error
        }
        current->parent->error = current->error;
        current = current->parent;
    }
}

template <typename P>
struct FinalAwaiter {
    bool await_ready() noexcept {
        return false;
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept {
        return finish(handle.promise());
    }
    void await_resume() noexcept {}
};

// =====================================================================
// Task<T>
// =====================================================================
template <typename T>
class Task;

template <typename T>
struct Promise : PromiseBase {
    T value{};
    Task<T> get_return_object();
    FinalAwaiter<Promise> final_suspend() noexcept {
        return {};
    }
    void return_value(T v) {
        value = std::move(v);
    }
};

template <>
struct Promise<void> : PromiseBase {
    Task<void> get_return_object();
    FinalAwaiter<Promise> final_suspend() noexcept {
        return {};
    }
    void return_void() {}
};

template <typename T>
class [[nodiscard]] Task {
public:
    using promise_type = Promise<T>;
    using Handle = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(Handle handle) : handle_(handle) {}
    Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task &operator=(Task &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
    ~Task() {
        reset();
    }

    void reset() {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }
    Handle handle() const {
        return handle_;
    }

    // co_await task: run it; its failure fails the awaiting coroutine too.
    struct Awaiter {
        Handle child;
        bool catching;
        bool await_ready() noexcept {
            return false;
        }
        template <typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> parent) noexcept {
            child.promise().continuation = parent;
            child.promise().parent = &parent.promise();
            child.promise().catching = catching;
            return child;
        }
        T await_resume() {
            if constexpr (!std::is_void_v<T>) {
                return std::move(child.promise().value);
            }
        }
    };
    Awaiter operator co_await() && {
        return Awaiter{handle_, false};
    }

    // co_await caught(task): the JS try/catch; resumes with a Result.
    struct CaughtAwaiter {
        Handle child;
        bool await_ready() noexcept {
            return false;
        }
        template <typename P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> parent) noexcept {
            child.promise().continuation = parent;
            child.promise().parent = &parent.promise();
            child.promise().catching = true;
            return child;
        }
        Result<T> await_resume() {
            Result<T> result;
            if (child.promise().error.set) {
                result.error = child.promise().error;
                return result;
            }
            result.ok = true;
            if constexpr (!std::is_void_v<T>) {
                result.value = std::move(child.promise().value);
            }
            return result;
        }
    };

private:
    Handle handle_;
};

template <typename T>
Task<T> Promise<T>::get_return_object() {
    return Task<T>(Task<T>::Handle::from_promise(*this));
}
inline Task<void> Promise<void>::get_return_object() {
    return Task<void>(Task<void>::Handle::from_promise(*this));
}

// The Task temporary lives in the awaiting frame until the full expression
// ends, so the child frame outlives its awaiter's await_resume.
template <typename T>
struct Caught {
    Task<T> task;
    typename Task<T>::CaughtAwaiter operator co_await() && {
        return typename Task<T>::CaughtAwaiter{task.handle()};
    }
};
template <typename T>
Caught<T> caught(Task<T> &&task) {
    return Caught<T>{std::move(task)};
}

// co_await fail("format", ...): the JS `throw new Error(...)`.
struct Fail {
    Error error;
    bool await_ready() noexcept {
        return false;
    }
    template <typename P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept {
        PromiseBase &promise = handle.promise();
        promise.error = error;
        return propagate(promise);
    }
    void await_resume() noexcept {}
};

template <typename... Args>
Fail fail(const char *format, Args... args) {
    Fail f;
    if constexpr (sizeof...(Args) == 0) {
        std::snprintf(f.error.text, Error::capacity, "%s", format);
    } else {
        std::snprintf(f.error.text, Error::capacity, format, args...);
    }
    f.error.set = true;
    return f;
}

}  // namespace lexplug::op
