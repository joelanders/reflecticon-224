// A fixed-ratio polyphase windowed-sinc resampler for one channel: the
// plugin's 48 kHz machine to and from the host's rate. The ratio is rational
// (out/in = up/down in lowest terms), so the phases repeat exactly and the
// output depends only on the input: no drift, no floating-point clock.
//
// Every buffer is allocated in setup(); process() never allocates.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace lexplug {

class Resampler {
public:
    static constexpr int taps = 32;   // per phase

    // max_input: the most input samples one process() call will be given.
    void setup(int in_rate, int out_rate, int max_input) {
        int g = std::gcd(in_rate, out_rate);
        up_ = out_rate / g;
        down_ = in_rate / g;
        // The passband edge sits at 90% of the lower Nyquist frequency.
        double cutoff = 0.9 * std::min(1.0, double(up_) / double(down_));
        filter_.assign(size_t(up_) * taps, 0.0f);
        for (int p = 0; p < up_; p++) {
            double sum = 0;
            for (int j = 0; j < taps; j++) {
                double d = double(j) - double(p) / double(up_) - double(taps / 2 - 1);
                double x = cutoff * d;
                double sinc = 1.0;
                if (x != 0.0) {
                    sinc = std::sin(M_PI * x) / (M_PI * x);
                }
                double w = d / (taps / 2);
                double window = 0.0;
                if (std::fabs(w) < 1.0) {
                    window = bessel_i0(beta * std::sqrt(1.0 - w * w)) / bessel_i0(beta);
                }
                double h = cutoff * sinc * window;
                filter_[size_t(p) * taps + size_t(j)] = float(h);
                sum += h;
            }
            // Unity gain at DC in every phase.
            for (int j = 0; j < taps; j++) {
                filter_[size_t(p) * taps + size_t(j)] = float(filter_[size_t(p) * taps + size_t(j)] / sum);
            }
        }
        history_.assign(size_t(taps + max_input), 0.0f);
        filled_ = taps - 1;   // start from silence
        position_ = 0;
    }

    bool identity() const {
        return up_ == down_;
    }

    // The delay through this stage, in output samples: taps/2 input samples.
    // Output k is centred on input time k*down/up; phase 0's peak sits at
    // history index taps/2 - 1 and input sample n at history index
    // n + taps - 1, so the output lags by (taps - 1) - (taps/2 - 1) = taps/2
    // input samples (checked by tests/resampler/resampler_quality.cpp).
    double latency() const {
        return double(taps / 2) * double(up_) / double(down_);
    }

    // The input samples needed before `outputs` more samples can be made.
    int inputs_needed(int outputs) const {
        if (outputs <= 0) {
            return 0;
        }
        long long last = position_ + (long long)(outputs - 1) * down_;
        long long needed = last / up_ + taps - filled_;
        return int(std::max(0LL, needed));
    }

    // Consume `n` inputs and write every output they complete; returns the count.
    int process(const float *in, int n, float *out) {
        std::copy(in, in + n, history_.begin() + filled_);
        filled_ += n;
        int made = 0;
        for (;;) {
            long long index = position_ / up_;
            if (index + taps > filled_) {
                break;
            }
            const float *h = filter_.data() + size_t(position_ % up_) * taps;
            const float *x = history_.data() + index;
            float y = 0.0f;
            for (int j = 0; j < taps; j++) {
                y += h[j] * x[j];
            }
            out[made++] = y;
            position_ += down_;
        }
        // Keep what the next outputs still need.
        long long drop = position_ / up_;
        std::copy(history_.begin() + drop, history_.begin() + filled_, history_.begin());
        filled_ -= int(drop);
        position_ -= drop * up_;
        return made;
    }

private:
    static constexpr double beta = 8.0;

    static double bessel_i0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; k++) {
            term *= (x / (2 * k)) * (x / (2 * k));
            sum += term;
        }
        return sum;
    }

    int up_ = 1, down_ = 1;
    std::vector<float> filter_, history_;
    int filled_ = 0;
    long long position_ = 0;   // the next output's input position, in 1/up_ input samples
};

}  // namespace lexplug
