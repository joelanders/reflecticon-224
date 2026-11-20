// The rate bridge on its own (plugin/rate_bridge.hpp), with the machine
// replaced by a wire: at every host rate and block pattern, an impulse must
// come out exactly latency() samples later, with no underrun, and a steady
// tone must keep its level. At 48 kHz the bridge must be the identity.
//
//   bridge_check
#include "rate_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Blocks of `pattern` (0 = irregular 1..2000 with a 20000 every 50 blocks).
std::vector<int> blocks(int pattern, int total) {
    std::vector<int> sizes;
    uint32_t seed = 7;
    int done = 0;
    while (done < total) {
        int n = pattern;
        if (pattern == 0) {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + int((seed >> 8) % 2000);
            if (sizes.size() % 50 == 49) {
                n = 20000;
            }
        }
        if (n > total - done) {
            n = total - done;
        }
        sizes.push_back(n);
        done += n;
    }
    return sizes;
}

bool check(int rate, int pattern) {
    lexplug::RateBridge bridge;
    bridge.setup(rate, 16384);
    const int total = rate * 3;
    std::vector<float> in(size_t(total), 0.0f), out(size_t(total), 0.0f);
    // Impulses at 0.5 s and 1.0 s; a 1 kHz tone from 1.5 s.
    const int impulse = rate / 2;
    in[size_t(impulse)] = 1.0f;
    in[size_t(rate)] = 1.0f;
    for (int i = rate * 3 / 2; i < total; i++) {
        in[size_t(i)] = float(0.5 * std::sin(2 * M_PI * 1000.0 * double(i) / double(rate)));
    }
    int done = 0;
    for (int n : blocks(pattern, total)) {
        std::vector<float> l(in.begin() + done, in.begin() + done + n), r = l;
        bridge.process(l.data(), r.data(), l.data(), r.data(), n,
                       [](const float *il, const float *ir, float *ol, float *orr, int frames) {
                           for (int f = 0; f < frames; f++) {
                               ol[f] = il[f];
                               orr[f] = ir[f];
                           }
                       });
        std::copy(l.begin(), l.end(), out.begin() + done);
        done += n;
    }
    int peak_at = 0;
    float peak = 0;
    for (int i = 0; i < rate * 3 / 4; i++) {
        if (std::fabs(out[size_t(i)]) > peak) {
            peak = std::fabs(out[size_t(i)]);
            peak_at = i;
        }
    }
    int measured = peak_at - impulse;
    double tone = 0;
    int from = rate * 2, to = total - bridge.latency() - 1;
    for (int i = from; i < to; i++) {
        double e = double(out[size_t(i + bridge.latency())]) - double(in[size_t(i)]);
        tone = std::max(tone, std::fabs(e));
    }
    bool ok = measured == bridge.latency() && bridge.underruns() == 0 && tone < 2e-3;
    if (rate == 48000) {
        ok = ok && bridge.latency() == 0 && out == in;
    }
    std::string label = "irregular", verdict = "FAIL";
    if (ok) {
        verdict = "ok";
    }
    if (pattern != 0) {
        label = std::to_string(pattern);
    }
    std::printf("  rate %6d blocks %-9s latency %4d measured %4d (impulse peak %.4f) underruns %llu, "
                "tone error after latency %.2e  %s\n",
                rate, label.c_str(), bridge.latency(), measured, double(peak), (unsigned long long)bridge.underruns(),
                tone, verdict.c_str());
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    for (int rate : {48000, 44100, 88200, 96000, 32000, 22050, 176400, 192000}) {
        for (int pattern : {1, 64, 128, 511, 4096, 0}) {
            ok = check(rate, pattern) && ok;
        }
    }
    if (ok) {
        std::printf("bridge_check: PASS\n");
        return 0;
    }
    std::printf("bridge_check: FAIL\n");
    return 1;
}
