// The plugin's resampler (source/resampler.hpp) and rate bridge
// (plugin/rate_bridge.hpp, machine replaced by a wire), measured:
//
//   - frequency response of each stage and of the round trip (gain at spot
//     frequencies, ripple 20 Hz-10 kHz, the -0.1 / -1 / -3 dB points),
//   - stopband: tones swept near and above the lower Nyquist frequency; the
//     worst alias / image level in the output, in dBFS for a 0 dBFS input
//     sine (for a downsampler above the output Nyquist, all output is alias;
//     otherwise the output minus the fitted input tone),
//   - DC gain,
//   - round-trip THD+N of a 1 kHz sine,
//   - delay, measured from the phase of a 50 Hz tone (a linear-phase filter's
//     delay at any frequency), against Resampler::latency() and
//     RateBridge::latency(),
//   - for comparison, the 224X and 224 analog boards' own response
//     (../analog/filters*.hpp: AIN, the input filter before the ADC,
//     and AOUT, the output filter after the DAC), from their modal form.
//
// Exit status 0 when every stage's latency() matches the measured delay
// (within 0.01 output samples) and the bridge's measured delay equals its
// latency().
//
//   sh tests/resampler/run.sh
#include "../../plugin/rate_bridge.hpp"
#include "../../../analog/filters.hpp"
#include "../../../analog/filters_224.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using Path = std::function<std::vector<float>(const std::vector<float> &)>;

struct Fit {
    double amplitude = 0;
    double phase = 0;       // of cos(w t + phase)
    double residual = 0;    // RMS of what is left
    double rms = 0;         // RMS of the whole window
};

// Least-squares fit of a cos(w k) + b sin(w k) + c over y[from, to).
Fit fitTone(const std::vector<float> &y, double w, size_t from, size_t to) {
    double m[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double v[3] = {0, 0, 0};
    for (size_t k = from; k < to; k++) {
        double b[3] = {std::cos(w * double(k)), std::sin(w * double(k)), 1.0};
        for (int i = 0; i < 3; i++) {
            v[i] += b[i] * double(y[k]);
            for (int j = 0; j < 3; j++) {
                m[i][j] += b[i] * b[j];
            }
        }
    }
    // Gaussian elimination, 3x3.
    for (int i = 0; i < 3; i++) {
        int pivot = i;
        for (int r = i + 1; r < 3; r++) {
            if (std::fabs(m[r][i]) > std::fabs(m[pivot][i])) {
                pivot = r;
            }
        }
        for (int j = 0; j < 3; j++) {
            std::swap(m[i][j], m[pivot][j]);
        }
        std::swap(v[i], v[pivot]);
        for (int r = i + 1; r < 3; r++) {
            double f = m[r][i] / m[i][i];
            for (int j = i; j < 3; j++) {
                m[r][j] -= f * m[i][j];
            }
            v[r] -= f * v[i];
        }
    }
    double x[3] = {0, 0, 0};
    for (int i = 2; i >= 0; i--) {
        double s = v[i];
        for (int j = i + 1; j < 3; j++) {
            s -= m[i][j] * x[j];
        }
        x[i] = s / m[i][i];
    }
    Fit fit;
    fit.amplitude = std::hypot(x[0], x[1]);
    fit.phase = std::atan2(-x[1], x[0]);
    double r2 = 0, y2 = 0;
    for (size_t k = from; k < to; k++) {
        double model = x[0] * std::cos(w * double(k)) + x[1] * std::sin(w * double(k)) + x[2];
        double e = double(y[k]) - model;
        r2 += e * e;
        y2 += double(y[k]) * double(y[k]);
    }
    fit.residual = std::sqrt(r2 / double(to - from));
    fit.rms = std::sqrt(y2 / double(to - from));
    return fit;
}

double db(double x) {
    if (x <= 0) {
        return -400.0;
    }
    return 20.0 * std::log10(x);
}

std::vector<float> sine(double f, double rate, int n, double amplitude) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        x[size_t(i)] = float(amplitude * std::sin(2 * M_PI * f * double(i) / rate));
    }
    return x;
}

// One Resampler stage, in blocks of 512.
Path stage(int in_rate, int out_rate) {
    return [in_rate, out_rate](const std::vector<float> &in) {
        lexplug::Resampler r;
        r.setup(in_rate, out_rate, 512);
        std::vector<float> out(in.size() * size_t(out_rate) / size_t(in_rate) + 1024);
        size_t made = 0;
        for (size_t i = 0; i < in.size(); i += 512) {
            int n = int(std::min<size_t>(512, in.size() - i));
            made += size_t(r.process(in.data() + i, n, out.data() + made));
        }
        out.resize(made);
        return out;
    };
}

// The bridge at a host rate with a wire for the machine, in blocks of 512.
Path bridge(int host_rate) {
    return [host_rate](const std::vector<float> &in) {
        lexplug::RateBridge b;
        b.setup(host_rate, 512);
        std::vector<float> out(in.size()), l(512), r(512);
        for (size_t i = 0; i < in.size(); i += 512) {
            int n = int(std::min<size_t>(512, in.size() - i));
            std::copy(in.begin() + long(i), in.begin() + long(i) + n, l.begin());
            std::copy(in.begin() + long(i), in.begin() + long(i) + n, r.begin());
            b.process(l.data(), r.data(), l.data(), r.data(), n,
                      [](const float *il, const float *ir, float *ol, float *orr, int frames) {
                          for (int f = 0; f < frames; f++) {
                              ol[f] = il[f];
                              orr[f] = ir[f];
                          }
                      });
            std::copy(l.begin(), l.begin() + n, out.begin() + long(i));
        }
        return out;
    };
}

struct Measured {
    Fit fit;
    size_t length = 0;
};

// A tone of frequency f (input rate `in_rate`) through `path`; fit at f on
// the output (rate `out_rate`), skipping the first 0.1 s.
Measured tone(const Path &path, double f, int in_rate, int out_rate, double amplitude, double seconds = 0.5) {
    std::vector<float> y = path(sine(f, in_rate, int(seconds * in_rate), amplitude));
    Measured m;
    m.length = y.size();
    size_t from = size_t(out_rate / 10);
    size_t to = y.size() - 64;
    m.fit = fitTone(y, 2 * M_PI * f / out_rate, from, to);
    return m;
}

double gainDb(const Path &path, double f, int in_rate, int out_rate) {
    return db(tone(path, f, in_rate, out_rate, 1.0).fit.amplitude);
}

// The frequency where the gain falls to `level` dB (bisection between lo and hi).
double crossing(const Path &path, int in_rate, int out_rate, double level, double lo, double hi) {
    for (int i = 0; i < 18; i++) {
        double mid = 0.5 * (lo + hi);
        if (gainDb(path, mid, in_rate, out_rate) > level) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// Delay of the path in output samples, from the phase of a 50 Hz tone
// (period 882+ samples, longer than any delay here; a linear-phase filter
// delays every frequency alike): output k ~ sin(w (k - delay)).
double delayOutputSamples(const Path &path, int in_rate, int out_rate) {
    const double f = 50.0;
    Measured m = tone(path, f, in_rate, out_rate, 0.5, 1.5);
    // sin(w t) = cos(w t - pi/2): a delay d gives phase -pi/2 - w d.
    double w_out = 2 * M_PI * f / out_rate;
    double lag = -(m.fit.phase + M_PI / 2);
    while (lag < -0.01) {
        lag += 2 * M_PI;
    }
    while (lag >= 2 * M_PI - 0.01) {
        lag -= 2 * M_PI;
    }
    return lag / w_out;
}

int failures = 0;

void stageReport(int in_rate, int out_rate) {
    Path p = stage(in_rate, out_rate);
    lexplug::Resampler r;
    r.setup(in_rate, out_rate, 512);
    double lower = 0.5 * std::min(in_rate, out_rate);
    std::printf("\n== stage %d -> %d (lower Nyquist %.0f Hz)\n", in_rate, out_rate, lower);
    std::printf("  gain dB:");
    for (double f : {20.0, 1000.0, 5000.0, 10000.0, 12000.0, 14000.0, 15000.0, 16000.0, 17000.0, 18000.0, 19000.0,
                     20000.0}) {
        if (f < lower) {
            std::printf("  %gk %+.3f", f / 1000, gainDb(p, f, in_rate, out_rate));
        }
    }
    double lo = 1e9, hi = -1e9;
    for (double f = 20; f <= 10000; f *= 1.1) {
        double g = gainDb(p, f, in_rate, out_rate);
        lo = std::min(lo, g);
        hi = std::max(hi, g);
    }
    std::printf("\n  ripple 20 Hz-10 kHz: %.4f dB\n", hi - lo);
    std::printf("  -0.1 dB at %.0f Hz, -1 dB at %.0f Hz, -3 dB at %.0f Hz\n",
                crossing(p, in_rate, out_rate, -0.1, 1000, lower), crossing(p, in_rate, out_rate, -1, 1000, lower),
                crossing(p, in_rate, out_rate, -3, 1000, lower));
    // Stopband: tones from 0.8 x lower Nyquist to the input Nyquist.
    double worst = -400, worstF = 0, worstAudible = -400, worstAudibleF = 0;
    double outNyquist = 0.5 * out_rate;
    double inNyquist = 0.5 * in_rate;
    for (double f = 0.8 * lower; f < inNyquist - 10; f += 50) {
        Measured m = tone(p, f, in_rate, out_rate, 1.0);
        double level;
        double lands;   // where the unwanted component lands in the output
        if (f >= outNyquist) {
            level = db(m.fit.rms * std::sqrt(2.0));
            lands = std::fabs(out_rate * std::round(f / out_rate) - f);
        } else {
            level = db(m.fit.residual * std::sqrt(2.0));
            // Upsampler image nearest in the output band: in_rate - f.
            lands = in_rate - f;
            if (in_rate > out_rate) {
                lands = f;
            }
        }
        if (level > worst) {
            worst = level;
            worstF = f;
        }
        if (lands <= 20000 && level > worstAudible) {
            worstAudible = level;
            worstAudibleF = f;
        }
    }
    std::printf("  worst alias/image (tones %.0f-%.0f Hz): %.1f dBFS for a tone at %.0f Hz\n", 0.8 * lower,
                inNyquist, worst, worstF);
    if (worstAudible > -400) {
        std::printf("  worst landing at or below 20 kHz: %.1f dBFS (tone %.0f Hz)\n", worstAudible, worstAudibleF);
    } else {
        std::printf("  nothing lands at or below 20 kHz\n");
    }
    std::vector<float> dc(size_t(in_rate / 4), 0.5f);
    std::vector<float> y = p(dc);
    std::printf("  DC gain: %.7f\n", double(y[y.size() - 100]) / 0.5);
    double measured = delayOutputSamples(p, in_rate, out_rate);
    double inSamples = measured * double(in_rate) / double(out_rate);
    std::printf("  delay: measured %.4f output samples (%.4f input samples); latency() %.4f", measured, inSamples,
                r.latency());
    if (std::fabs(measured - r.latency()) > 0.01) {
        std::printf("  MISMATCH");
        failures++;
    }
    std::printf("\n");
}

void bridgeReport(int host_rate) {
    Path p = bridge(host_rate);
    lexplug::RateBridge b;
    b.setup(host_rate, 512);
    std::printf("\n== round trip through the bridge at %d Hz (wire for the machine)\n", host_rate);
    std::printf("  gain dB:");
    for (double f : {20.0, 1000.0, 10000.0, 15000.0, 16000.0, 17000.0, 18000.0, 19000.0, 20000.0}) {
        std::printf("  %gk %+.3f", f / 1000, gainDb(p, f, host_rate, host_rate));
    }
    double lower = 0.5 * std::min(host_rate, 48000);
    std::printf("\n  -0.1 dB at %.0f Hz, -1 dB at %.0f Hz, -3 dB at %.0f Hz\n",
                crossing(p, host_rate, host_rate, -0.1, 1000, lower), crossing(p, host_rate, host_rate, -1, 1000, lower),
                crossing(p, host_rate, host_rate, -3, 1000, lower));
    for (double amplitude : {0.5, 1.0}) {
        Measured m = tone(p, 1000.0, host_rate, host_rate, amplitude, 2.0);
        std::printf("  THD+N at 1 kHz, %.0f dBFS: %.1f dB (re the tone)\n", db(amplitude),
                    db(m.fit.residual / (m.fit.amplitude / std::sqrt(2.0))));
    }
    std::vector<float> dc(size_t(host_rate / 2), 0.5f);
    std::vector<float> y = p(dc);
    std::printf("  DC gain: %.7f\n", double(y[y.size() - 100]) / 0.5);
    double measured = delayOutputSamples(p, host_rate, host_rate);
    std::printf("  delay: measured %.4f samples; RateBridge::latency() %d", measured, b.latency());
    if (std::fabs(measured - double(b.latency())) > 0.01) {
        std::printf("  MISMATCH");
        failures++;
    }
    std::printf("\n");
}

double modalDb(const lexicon224x::analog::Modal &m, double f) {
    std::complex<double> s(0, 2 * M_PI * f);
    std::complex<double> h = m.direct;
    for (size_t k = 0; k < m.poles.size(); k++) {
        h += m.residues[k] / (s - m.poles[k]);
    }
    return db(std::abs(h * m.scale));
}

void analogReport() {
    using namespace lexicon224x::analog;
    std::printf("\n== the analog boards (../analog), gain re 1 kHz, dB\n");
    std::printf("  %-10s", "Hz");
    const double fs[] = {10000, 12000, 15000, 16000, 17000, 18000, 20000, 22050, 24000, 30000};
    for (double f : fs) {
        std::printf(" %8.0f", f);
    }
    std::printf("\n");
    struct Row {
        const char *name;
        Modal m;
    };
    Row rows[] = {{"224X AIN", ain_modal()}, {"224X AOUT", aout_modal()}, {"224 AIN", ain_224_modal()},
                  {"224 AOUT", aout_224_modal()}};
    for (const Row &row : rows) {
        std::printf("  %-10s", row.name);
        double ref = modalDb(row.m, 1000);
        for (double f : fs) {
            std::printf(" %8.2f", modalDb(row.m, f) - ref);
        }
        std::printf("\n");
    }
    // In through out: the pre-emphasis and de-emphasis cancel; what is left
    // is the two elliptic lowpasses (the machine adds its DAC hold's droop).
    for (int pair = 0; pair < 2; pair++) {
        const Row &in = rows[2 * pair];
        const Row &out = rows[2 * pair + 1];
        const char *name = "224 in+out";
        if (pair == 0) {
            name = "224X in+out";
        }
        std::printf("  %-10s", name);
        double ref = modalDb(in.m, 1000) + modalDb(out.m, 1000);
        for (double f : fs) {
            std::printf(" %8.2f", modalDb(in.m, f) + modalDb(out.m, f) - ref);
        }
        std::printf("\n");
    }
    std::printf("  in+out, lower band:");
    const double low[] = {2000, 4000, 5000, 6000, 7000, 8000, 9000, 12000, 14000, 15000, 15500};
    for (double f : low) {
        std::printf(" %gk", f / 1000);
    }
    std::printf("\n");
    for (int pair = 0; pair < 2; pair++) {
        const Row &in = rows[2 * pair];
        const Row &out = rows[2 * pair + 1];
        const char *name = "224";
        if (pair == 0) {
            name = "224X";
        }
        std::printf("  %-18s", name);
        double ref = modalDb(in.m, 1000) + modalDb(out.m, 1000);
        for (double f : low) {
            std::printf(" %.2f", modalDb(in.m, f) + modalDb(out.m, f) - ref);
        }
        std::printf("\n");
    }
    for (int rate : {44100, 96000}) {
        Path p = bridge(rate);
        std::printf("  bridge %-3d ", rate / 1000);
        for (double f : fs) {
            if (f < 0.5 * std::min(rate, 48000) - 10) {
                std::printf(" %8.2f", gainDb(p, f, rate, rate));
            } else {
                std::printf(" %8s", "-");
            }
        }
        std::printf("\n");
    }
}

}  // namespace

int main() {
    std::printf("Resampler: %d taps per phase, Kaiser beta 8, cutoff 0.9 x lower Nyquist\n", lexplug::Resampler::taps);
    stageReport(44100, 48000);
    stageReport(48000, 44100);
    stageReport(96000, 48000);
    stageReport(48000, 96000);
    stageReport(88200, 48000);
    stageReport(48000, 88200);
    bridgeReport(44100);
    bridgeReport(88200);
    bridgeReport(96000);
    analogReport();
    std::printf("\n%d latency mismatches\n", failures);
    if (failures) {
        return 1;
    }
    return 0;
}
