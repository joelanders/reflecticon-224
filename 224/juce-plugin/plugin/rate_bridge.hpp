// The host's rate on one side, the machine's 48 kHz frames on the other: the
// block logic of bench/speed_gate.cpp, with every buffer allocated in
// setup(). No JUCE here, so tests can drive it with any render function.
//
// At 48 kHz it is a straight wire: host sample n is internal frame n, and a
// block is rendered in place (chunked to the capacity). At any other rate:
//
//   host in --up--> [internal FIFO] --render--> --down--> [host FIFO] --> host out
//
// The internal FIFO starts with `prime_` frames of silence so it never runs
// dry, whatever the block sizes; each block renders exactly the frames the
// down-sampler needs to complete the block. Every stage is a fixed rational
// ratio, so the delay is a constant: the up-sampler's (16 host samples), the
// prime and the down-sampler's (16 internal frames), which is
// 16 + (prime + 16) * rate / 48000 host samples. The prime is chosen (from 64
// up) to make that a whole number of samples when it can.
#pragma once
#include "../source/resampler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace lexplug {

class RateBridge {
public:
    static constexpr int internal_rate = 48000;
    // The delay of one Resampler stage, in its input samples: the peak of
    // phase 0 sits at history index taps/2 - 1, and the history starts with
    // taps - 1 samples of silence.
    static constexpr int stage_delay = Resampler::taps / 2;

    // max_host_block: the most host samples one chunk will carry (bigger
    // blocks are split). Allocates; not on the audio thread.
    void setup(int host_rate, int max_host_block) {
        host_rate_ = host_rate;
        max_host_ = std::max(1, max_host_block);
        passthrough_ = host_rate == internal_rate;
        max_internal_ = int((int64_t(max_host_) * internal_rate + host_rate - 1) / host_rate) + 2 * Resampler::taps + 64;
        prime_ = 64;
        latency_ = 0;
        if (!passthrough_) {
            int step = internal_rate / std::gcd(host_rate, internal_rate);
            for (int p = 64; p < 64 + step && p <= 1024; p++) {
                if ((int64_t(p) + stage_delay) * host_rate % internal_rate == 0) {
                    prime_ = p;
                    break;
                }
            }
            latency_ = int(std::lround(stage_delay + double(prime_ + stage_delay) * host_rate / internal_rate));
        }
        in_l_.assign(size_t(max_internal_), 0.0f);
        in_r_.assign(size_t(max_internal_), 0.0f);
        out_l_.assign(size_t(max_internal_), 0.0f);
        out_r_.assign(size_t(max_internal_), 0.0f);
        fifo_size_ = size_t(2 * max_internal_ + prime_ + 256);
        fifo_l_.assign(fifo_size_, 0.0f);
        fifo_r_.assign(fifo_size_, 0.0f);
        host_fifo_size_ = size_t(2 * max_host_ + 64);
        host_fifo_l_.assign(host_fifo_size_, 0.0f);
        host_fifo_r_.assign(host_fifo_size_, 0.0f);
        reset();
    }

    // Back to silence: empty FIFOs, fresh filters (the latency is unchanged).
    void reset() {
        fifo_n_ = 0;
        host_fifo_n_ = 0;
        underruns_ = 0;
        if (!passthrough_) {
            up_l_.setup(host_rate_, internal_rate, max_host_);
            up_r_.setup(host_rate_, internal_rate, max_host_);
            down_l_.setup(internal_rate, host_rate_, max_internal_);
            down_r_.setup(internal_rate, host_rate_, max_internal_);
            std::fill(fifo_l_.begin(), fifo_l_.begin() + prime_, 0.0f);
            std::fill(fifo_r_.begin(), fifo_r_.begin() + prime_, 0.0f);
            fifo_n_ = size_t(prime_);
        }
    }

    bool passthrough() const {
        return passthrough_;
    }

    // The delay from host input to host output, in host samples (0 at 48 kHz).
    int latency() const {
        return latency_;
    }

    // The most internal frames one render call is asked for.
    int max_internal() const {
        return max_internal_;
    }

    // Times the internal FIFO would have run dry (padded with silence instead).
    uint64_t underruns() const {
        return underruns_;
    }

    // Process `n` host samples (any n: split into chunks of max_host_block).
    // render(left, right, out_left, out_right, frames) renders `frames`
    // internal frames and writes the two output channels. `out_*` may alias
    // `in_*` (a host's in-place buffer). Never allocates.
    template <class Render>
    void process(const float *in_l, const float *in_r, float *out_l, float *out_r, int n, Render &&render) {
        int done = 0;
        while (done < n) {
            int count = std::min(n - done, max_host_);
            if (passthrough_) {
                std::copy(in_l + done, in_l + done + count, in_l_.begin());
                std::copy(in_r + done, in_r + done + count, in_r_.begin());
                render(in_l_.data(), in_r_.data(), out_l + done, out_r + done, count);
            } else {
                chunk(in_l + done, in_r + done, out_l + done, out_r + done, count, render);
            }
            done += count;
        }
    }

private:
    template <class Render>
    void chunk(const float *in_l, const float *in_r, float *out_l, float *out_r, int n, Render &render) {
        // Host input, up to 48 kHz, onto the internal FIFO.
        size_t made = size_t(up_l_.process(in_l, n, fifo_l_.data() + fifo_n_));
        up_r_.process(in_r, n, fifo_r_.data() + fifo_n_);
        fifo_n_ += made;
        // Render what the down-sampler needs to finish this block.
        int need = down_l_.inputs_needed(n - int(host_fifo_n_));
        if (need > max_internal_) {
            need = max_internal_;
        }
        if (size_t(need) > fifo_n_) {
            std::fill(fifo_l_.begin() + long(fifo_n_), fifo_l_.begin() + need, 0.0f);
            std::fill(fifo_r_.begin() + long(fifo_n_), fifo_r_.begin() + need, 0.0f);
            fifo_n_ = size_t(need);
            underruns_++;
        }
        render(fifo_l_.data(), fifo_r_.data(), out_l_.data(), out_r_.data(), need);
        std::copy(fifo_l_.begin() + need, fifo_l_.begin() + long(fifo_n_), fifo_l_.begin());
        std::copy(fifo_r_.begin() + need, fifo_r_.begin() + long(fifo_n_), fifo_r_.begin());
        fifo_n_ -= size_t(need);
        // Down to the host's rate, then out of the host FIFO.
        size_t got = size_t(down_l_.process(out_l_.data(), need, host_fifo_l_.data() + host_fifo_n_));
        down_r_.process(out_r_.data(), need, host_fifo_r_.data() + host_fifo_n_);
        host_fifo_n_ += got;
        int give = int(std::min(size_t(n), host_fifo_n_));
        std::copy(host_fifo_l_.begin(), host_fifo_l_.begin() + give, out_l);
        std::copy(host_fifo_r_.begin(), host_fifo_r_.begin() + give, out_r);
        std::fill(out_l + give, out_l + n, 0.0f);
        std::fill(out_r + give, out_r + n, 0.0f);
        std::copy(host_fifo_l_.begin() + give, host_fifo_l_.begin() + long(host_fifo_n_), host_fifo_l_.begin());
        std::copy(host_fifo_r_.begin() + give, host_fifo_r_.begin() + long(host_fifo_n_), host_fifo_r_.begin());
        host_fifo_n_ -= size_t(give);
    }

    int host_rate_ = internal_rate, max_host_ = 1, max_internal_ = 1, prime_ = 64, latency_ = 0;
    bool passthrough_ = true;
    Resampler up_l_, up_r_, down_l_, down_r_;
    std::vector<float> in_l_, in_r_, out_l_, out_r_, fifo_l_, fifo_r_, host_fifo_l_, host_fifo_r_;
    size_t fifo_size_ = 0, host_fifo_size_ = 0, fifo_n_ = 0, host_fifo_n_ = 0;
    uint64_t underruns_ = 0;
};

}  // namespace lexplug
