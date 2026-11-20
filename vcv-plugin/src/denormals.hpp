// The machine's analog boards run in double precision and must see the same
// arithmetic as the web build: denormals included. Rack's engine threads run
// with flush-to-zero set (system::resetFpuFlags), which would change the
// output, so the core's render clears it and puts the thread's mode back:
// the JUCE plugin's DenormalsOn (../juce-plugin/plugin/PluginProcessor.cpp).
#pragma once
#include <cstdint>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace lexvcv {

class DenormalsOn {
public:
    DenormalsOn() : saved_(read()) {
        if (saved_ & flush_bits) {
            write(saved_ & ~flush_bits);
        }
    }

    ~DenormalsOn() {
        if (saved_ & flush_bits) {
            write(saved_);
        }
    }

    DenormalsOn(const DenormalsOn &) = delete;
    DenormalsOn &operator=(const DenormalsOn &) = delete;

    // For the checks: whether the calling thread flushes, and setting it.
    static bool flushing() {
        return (read() & flush_bits) != 0;
    }
    static void set_flushing(bool on) {
        write(on ? read() | flush_bits : read() & ~flush_bits);
    }

private:
#if defined(__aarch64__)
    static constexpr uint64_t flush_bits = 1u << 24;    // FPCR.FZ
    static uint64_t read() {
        uint64_t r;
        asm volatile("mrs %0, fpcr" : "=r"(r));
        return r;
    }
    static void write(uint64_t r) {
        asm volatile("msr fpcr, %0" : : "r"(r));
    }
#elif defined(__x86_64__) || defined(_M_X64)
    static constexpr uint64_t flush_bits = 0x8040;      // MXCSR.FZ | MXCSR.DAZ
    static uint64_t read() {
        return _mm_getcsr();
    }
    static void write(uint64_t r) {
        _mm_setcsr(unsigned(r));
    }
#else
#error "DenormalsOn: unsupported architecture"
#endif

    uint64_t saved_;
};

}  // namespace lexvcv
