// The host's rate on one side, a machine's own rate on the other, every buffer allocated in
// setup(). Generalised from dn780/emu/core/rate_bridge.hpp and dmx15-80/emu/core/rate_bridge.hpp
// (both from reflecticon-224, same author, AGPLv3):
// - the internal rate is a rational number: `internal` rate units, `unit` units per hertz
//   (the DN780's 15 MHz / 512 = 29,296.875 Hz is 234375 with unit 8);
// - one or two input channels, two outputs;
// - an optional cutoff in both directions (0 = the narrower Nyquist), which can stand in for a
//   machine's converter filters.
//
//   host in --up--> [internal FIFO] --render--> --down--> [host FIFO] --> host out
//
// The internal FIFO starts with `prime_` samples of silence so it never runs dry, whatever the
// block sizes; each block renders exactly the samples the down-sampler needs to complete it. Every
// stage is a fixed rational ratio, so the delay is a constant (latency_exact()).
#pragma once
#include "resampler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kit {

template <int Taps = 32>
class RateBridgeT {
public:
    using Resampler = ResamplerT<Taps>;
    static constexpr int kStageDelay = Taps / 2;

    // host_rate in whole hertz (as hosts use). Allocates: not on the audio thread.
    void setup(int host_rate, int max_host_block, long internal, long unit = 1, double cutoff_hz = 0,
               int inputs = 2) {
        unit_ = std::max(1L, unit);
        internal_ = internal;
        host_ = long(host_rate) * unit_;
        host_rate_ = host_rate;
        cutoff_ = cutoff_hz;
        inputs_ = std::clamp(inputs, 1, 2);
        max_host_ = std::max(1, max_host_block);
        max_internal_ = int(double(max_host_) * internal_ / host_) + 2 * Taps + 64;
        prime_ = 64;
        for (auto& f : fifo_) f.assign(size_t(2 * max_internal_ + prime_ + 256), 0.0f);
        out_l_.assign(size_t(max_internal_), 0.0f);
        out_r_.assign(size_t(max_internal_), 0.0f);
        host_fifo_l_.assign(size_t(2 * max_host_ + 64), 0.0f);
        host_fifo_r_.assign(size_t(2 * max_host_ + 64), 0.0f);
        reset();
    }

    // Back to silence: empty FIFOs, fresh filters (the latency is unchanged). Allocates.
    void reset() {
        for (auto& u : up_) u.setup(host_, internal_, max_host_, cutoff_, double(unit_));
        down_l_.setup(internal_, host_, max_internal_, cutoff_, double(unit_));
        down_r_.setup(internal_, host_, max_internal_, cutoff_, double(unit_));
        for (auto& f : fifo_) std::fill(f.begin(), f.begin() + prime_, 0.0f);
        fifo_n_ = size_t(prime_);
        host_fifo_n_ = 0;
        underruns_ = 0;
    }

    bool ready() const { return host_rate_ > 0; }
    // Host input to host output in host samples: rounded, and exact (fractional; a whole number
    // would need a prime of hundreds of samples, which an effect does not need).
    long latency() const { return std::lround(latency_exact()); }
    double latency_exact() const { return kStageDelay + double(prime_ + kStageDelay) * host_ / internal_; }
    int host_rate() const { return host_rate_; }
    int max_internal() const { return max_internal_; }
    uint64_t underruns() const { return underruns_; }

    // Process `n` host samples (any n: split into chunks of max_host_block). `in` holds `inputs`
    // channel pointers. render(in, out_l, out_r, count) renders `count` internal samples, with
    // in[0..inputs-1] valid. Never allocates.
    template <class Render>
    void process(const float* const* in, float* out_l, float* out_r, int n, Render&& render) {
        int done = 0;
        while (done < n) {
            int count = std::min(n - done, max_host_);
            const float* chunk_in[2] = {in[0] + done, inputs_ > 1 ? in[1] + done : nullptr};
            chunk(chunk_in, out_l + done, out_r + done, count, render);
            done += count;
        }
    }

private:
    template <class Render>
    void chunk(const float* const* in, float* out_l, float* out_r, int n, Render& render) {
        size_t got_in = 0;
        for (int c = 0; c < inputs_; c++) got_in = size_t(up_[c].process(in[c], n, fifo_[c].data() + fifo_n_));
        fifo_n_ += got_in;
        int need = down_l_.inputs_needed(n - int(host_fifo_n_));
        need = std::min(need, max_internal_);
        if (size_t(need) > fifo_n_) {
            for (int c = 0; c < inputs_; c++) std::fill(fifo_[c].begin() + long(fifo_n_), fifo_[c].begin() + need, 0.0f);
            fifo_n_ = size_t(need);
            underruns_++;
        }
        const float* rin[2] = {fifo_[0].data(), inputs_ > 1 ? fifo_[1].data() : nullptr};
        render(rin, out_l_.data(), out_r_.data(), need);
        for (int c = 0; c < inputs_; c++) std::copy(fifo_[c].begin() + need, fifo_[c].begin() + long(fifo_n_), fifo_[c].begin());
        fifo_n_ -= size_t(need);
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

    long unit_ = 1, internal_ = 48000, host_ = 0;
    int host_rate_ = 0, max_host_ = 1, max_internal_ = 1, prime_ = 64, inputs_ = 2;
    double cutoff_ = 0;
    Resampler up_[2], down_l_, down_r_;
    std::vector<float> fifo_[2], out_l_, out_r_, host_fifo_l_, host_fifo_r_;
    size_t fifo_n_ = 0, host_fifo_n_ = 0;
    uint64_t underruns_ = 0;
};

using RateBridge = RateBridgeT<32>;

}  // namespace kit
