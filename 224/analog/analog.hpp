// The 224X analog boundary: AIN (input filter chain, sample-and-hold, gain
// ranger, ADC) and AOUT (DAC sample-and-hold, output filter chain), for
// driving the bit-exact digital machine from 48 kHz audio and back.
//
// Each analog chain is a linear circuit, reduced to modal form (poles p_k,
// residues r_k, direct term d) by tools/mna.py from its ngspice netlist.
// A mode obeys z' = p z + u; the chain's output is y = Re(sum r_k z_k) + d u.
// Between two instants the input is either constant (the DAC hold) or a
// straight line (the upsampled audio input), and for both the modes advance
// exactly:
//   z(t+T) = e^{pT} z + u0 (e^{pT} - 1)/p + slope (e^{pT} - 1 - pT)/p^2
// Step lengths are integer ticks (1/576 ns, the machine's time base). Only
// a few distinct lengths occur (48 kHz and the machine's pass rate are in a
// 45:32 ratio), so their coefficients are cached.
//
// This models the analog boards; it makes no claim about the digital
// machine, which is exact without it.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace lexicon224x::analog {

using Tick = uint64_t;
constexpr double seconds_per_tick = 1e-9 / 576.0;

struct Modal {
    std::vector<std::complex<double>> poles;     // rad/s
    std::vector<std::complex<double>> residues;
    double direct = 0;
    double scale = 1;                            // level normalization applied to the output
};

// One chain's state, advanced through piecewise-linear input.
class Chain {
public:
    explicit Chain(const Modal &m)
        : m_(m), z_(m.poles.size()), keys_(cache_slots, 0), next_(cache_sets, 0),
          decay_(cache_slots * m.poles.size()), step_(cache_slots * m.poles.size()),
          ramp_(cache_slots * m.poles.size()) {}

    // Advance by `ticks` with input u0 at the start and slope (per second).
    void advance(Tick ticks, double u0, double slope) {
        if (ticks == 0) {
            return;
        }
        size_t base = coefficients(ticks);
        // z = decay * z + u0 * step + slope * ramp, written out in the order
        // libc++'s complex operators compute it (so the same bits under
        // -ffp-contract=off), without their call and NaN/inf recovery, which
        // only acts when both parts of a product are NaN.
        for (size_t k = 0; k < z_.size(); k++) {
            double dr = decay_[base + k].real(), di = decay_[base + k].imag();
            double zr = z_[k].real(), zi = z_[k].imag();
            double re = dr * zr - di * zi + u0 * step_[base + k].real() + slope * ramp_[base + k].real();
            double im = dr * zi + di * zr + u0 * step_[base + k].imag() + slope * ramp_[base + k].imag();
            z_[k] = {re, im};
        }
    }

    double output(double u) const {
        // The real part of sum(residue * z), as libc++ computes it (see advance()).
        double y = 0;
        for (size_t k = 0; k < z_.size(); k++) {
            y += m_.residues[k].real() * z_[k].real() - m_.residues[k].imag() * z_[k].imag();
        }
        return (y + m_.direct * u) * m_.scale;
    }

private:
    // The coefficients for a step of `ticks`, cached: a set-associative table
    // allocated with the chain, so a new step length (a program with another
    // pass length) costs a computation, never an allocation. The values
    // depend only on `ticks`, so the table's size and eviction cannot change
    // the output. Returns the first mode's index into decay_/step_/ramp_.
    // Four ways: the 224X and 224XL step through ~1,000 lengths per chain in
    // steady state, and a direct-mapped table misses on pairs that share a
    // slot (17% of steps at 256 slots, still 1% at 4096); 4 x 256 misses
    // 0.01%, about the first use of each length.
    static constexpr size_t cache_ways = 4;
    static constexpr size_t cache_sets = 256;
    static constexpr size_t cache_slots = cache_ways * cache_sets;

    size_t coefficients(Tick ticks) {
        size_t first = size_t((ticks * 0x9E3779B97F4A7C15ull) >> 56) % cache_sets * cache_ways;
        for (size_t slot = first; slot < first + cache_ways; slot++) {
            if (keys_[slot] == ticks) {
                return slot * m_.poles.size();
            }
        }
        uint8_t &victim = next_[first / cache_ways];  // round robin within the set
        size_t slot = first + victim;
        victim = uint8_t((victim + 1) % cache_ways);
        size_t base = slot * m_.poles.size();
        keys_[slot] = ticks;
        const double T = double(ticks) * seconds_per_tick;
        for (size_t k = 0; k < m_.poles.size(); k++) {
            std::complex<double> p = m_.poles[k];
            std::complex<double> x = p * T;
            std::complex<double> e = std::exp(x);
            std::complex<double> em1, em1x;
            if (std::abs(x) < 1e-3) {   // series: avoid cancellation for short steps
                em1 = x * (1.0 + x / 2.0 + x * x / 6.0 + x * x * x / 24.0);
                em1x = x * x * (0.5 + x / 6.0 + x * x / 24.0 + x * x * x / 120.0);
            } else {
                em1 = e - 1.0;
                em1x = e - 1.0 - x;
            }
            decay_[base + k] = e;
            step_[base + k] = em1 / p;
            ramp_[base + k] = em1x / (p * p);
        }
        return base;
    }

    const Modal &m_;
    std::vector<std::complex<double>> z_;
    std::vector<Tick> keys_;                    // the step length in each slot (0: empty; advance() never asks for 0)
    std::vector<uint8_t> next_;                 // per set: the way the next miss replaces
    std::vector<std::complex<double>> decay_, step_, ramp_;   // cache_slots x modes
};

// ---- AOUT: DAC sample-and-hold -> output chain ---------------------------
// The hold is piecewise constant: it changes at each DAC capture. Captures
// may be reported ahead of the output being read (the machine can run past
// a frame boundary), so they are queued and applied in time order.
class Output {
public:
    explicit Output(const Modal &m) : chain_(m) {
        pending_.reserve(queue_room);
    }

    // A DAC capture at time t (ticks) with value v (full scale = 1.0).
    void hold(Tick t, double v) {
        pending_.push_back({t, v});
    }

    // The analog output at time t (ticks). Captures at or before t take effect.
    double sample(Tick t) {
        size_t used = 0;
        for (; used < pending_.size() && pending_[used].first <= t; used++) {
            advance_to(pending_[used].first);
            held_ = pending_[used].second;
        }
        pending_.erase(pending_.begin(), pending_.begin() + long(used));
        advance_to(t);
        return chain_.output(held_);
    }

private:
    void advance_to(Tick t) {
        if (t > now_) {
            chain_.advance(t - now_, held_, 0.0);
            now_ = t;
        }
    }

    // Room for the captures queued ahead of a frame read: a few per frame
    // (the pass rate is below 48 kHz; run_until overshoots by about a pass).
    static constexpr size_t queue_room = 256;

    Chain chain_;
    Tick now_ = 0;
    double held_ = 0;
    std::vector<std::pair<Tick, double>> pending_;
};

// ---- AIN: 48 kHz audio -> x4 band-limited upsampling -> input chain -------
// The upsampled sequence is played as straight lines between its points
// (a first-order hold at 192 kHz: -0.18 dB at 15 kHz, images above 177 kHz).
// The AIN chain then filters it like the real board.
class Input {
public:
    static constexpr int factor = 4;
    static constexpr int taps_per_phase = 32;   // windowed sinc, 128 taps at 192 kHz

    Input(const Modal &m, Tick frame_ticks) : chain_(m), step_(frame_ticks / factor) {
        // Kaiser-windowed sinc, cutoff 22 kHz at 192 kHz; gain `factor` per phase sum.
        const int n = factor * taps_per_phase;
        const double fc = 22000.0 / (48000.0 * factor);
        const double beta = 8.0;
        auto bessel0 = [](double x) {
            double sum = 1, term = 1;
            for (int k = 1; k < 30; k++) {
                term *= (x / (2 * k)) * (x / (2 * k));
                sum += term;
            }
            return sum;
        };
        for (int i = 0; i < n; i++) {
            double t = i - (n - 1) / 2.0;
            double sinc = t == 0 ? 2 * fc : std::sin(2 * M_PI * fc * t) / (M_PI * t);
            double r = 2.0 * i / (n - 1) - 1.0;
            double w = bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / bessel0(beta);
            h_.push_back(sinc * w * factor);
        }
        // Four points per frame, consumed as the converter samples: a few
        // frames' worth at most, plus up to compact_after consumed ones.
        points_.reserve(256);
    }

    // Push one 48 kHz input sample; its upsampled points follow the previous
    // ones at 192 kHz. Latency: the filter's delay (taps_per_phase/2 frames).
    void push(double x) {
        // The history ring keeps each sample twice, so the last
        // taps_per_phase samples are contiguous: window[0] the oldest,
        // window[taps_per_phase - 1] this one.
        newest_ = (newest_ + 1) % taps_per_phase;
        history_[newest_] = history_[newest_ + taps_per_phase] = x;
        const double *window = &history_[newest_ + 1];
        if (head_ >= compact_after) {
            points_.erase(points_.begin(), points_.begin() + long(head_));
            head_ = 0;
        }
        for (int phase = 0; phase < factor; phase++) {
            double y = 0;
            for (int k = 0; k < taps_per_phase; k++) {
                y += window[taps_per_phase - 1 - k] * h_[k * factor + phase];
            }
            points_.push_back(y);
        }
    }

    // The chain's output at time t (ticks, on the input's own clock:
    // point j of the upsampled stream is at j * step). Consumes points.
    double sample(Tick t) {
        // Never step backward: a late correction of the sampling instant
        // reads the chain where it stands.
        t = std::max(t, now_);
        while (true) {
            if (points_.size() - head_ < 2) {
                break;                          // no more input: hold the last line
            }
            const double p0 = points_[head_], p1 = points_[head_ + 1];
            Tick segment_end = start_ + step_;
            double slope = (p1 - p0) / (double(step_) * seconds_per_tick);
            if (t < segment_end) {
                double u = p0 + slope * double(t - now_) * seconds_per_tick;
                chain_.advance(t - now_, p0 + slope * double(now_ - start_) * seconds_per_tick, slope);
                now_ = t;
                return chain_.output(u);
            }
            chain_.advance(segment_end - now_, p0 + slope * double(now_ - start_) * seconds_per_tick,
                           slope);
            now_ = start_ = segment_end;
            head_++;                            // consumed (push() compacts)
        }
        return chain_.output(points_.size() == head_ ? 0.0 : points_[head_]);
    }

    Tick latency() const {
        return step_ * factor * taps_per_phase / 2;
    }

private:
    // Consumed points stay in points_ until this many, then push() drops them.
    static constexpr size_t compact_after = 64;

    Chain chain_;
    Tick step_;
    std::vector<double> h_;
    std::vector<double> history_ = std::vector<double>(2 * taps_per_phase, 0.0);  // ring, each sample twice
    std::vector<double> points_;
    size_t newest_ = taps_per_phase - 1;    // history_ index of the newest sample
    size_t head_ = 0;                       // points_[head_] is the current line's start
    Tick start_ = 0, now_ = 0;
};

// ---- The gain ranger and ADC (service manual Table 3.3) -------------------
// v is the voltage at the converter input, with 5 V = the onset of ADC
// clipping. The ranger picks the largest gain that keeps the sample below
// clipping (6 dB steps), and the FPC shifts the code by 4 - IGA to undo it.
struct Converted {
    unsigned code;   // 12-bit two's complement
    unsigned iga;    // IGA1:IGA0
};

inline Converted convert(double volts, bool gain_ranging = true) {
    double a = std::abs(volts);
    unsigned iga = 0;
    if (gain_ranging) {
        if (a < 0.56) {
            iga = 3;
        } else if (a < 1.12) {
            iga = 2;
        } else if (a < 2.24) {
            iga = 1;
        }
    }
    double x = volts * double(1u << iga) / 5.0 * 2047.0;
    int code = int(std::lround(std::clamp(x, -2048.0, 2047.0)));
    return {unsigned(code) & 0xfff, iga};
}

// The same comparators as seen by the headroom display: five thresholds at
// 0.28, 0.56, 1.12, 2.24 and 5.0 V (service manual 3.9; the 5 V and 0.28 V
// ones are for the display only). Bit k set = comparator k exceeded.
inline unsigned level_detectors(double volts) {
    static constexpr double thresholds[5] = {0.28, 0.56, 1.12, 2.24, 5.0};
    double a = std::abs(volts);
    unsigned asserted = 0;
    for (unsigned k = 0; k < 5; k++) {
        if (a >= thresholds[k]) {
            asserted |= 1u << k;
        }
    }
    return asserted;
}

}  // namespace lexicon224x::analog
