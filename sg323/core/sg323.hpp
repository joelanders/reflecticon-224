// The StarGate 323 in C++, header-only, real time: a port of the Python model (sg323/) for plugins.
//
// The digital core is the Python core.run() one frame at a time: the 15-bit SAR converter, the
// tap tables decoded from the PROM images (the user's files: nothing here holds PROM data), the
// modulation counter fed by the rate-level detector, and the feedback path through LPF1 sampled
// exactly at the ADC hold (one numerator per tap over shared poles).
//
// The analog stages run at 192 kHz (6 x the 32 kHz frame rate), as machine.py does, but as
// parallel complex one-pole modes instead of long FIRs:
//   - each chain's partial fractions come from the stages' own poles and residues (the residue of
//     a product at a simple pole p of stage A is r_A * prod H_B(p)); the HF EQ pole is nudged 0.1%
//     off any pole it would meet (Python nudges it off R76/C51 the same way);
//   - impulse-invariant at 192 kHz, with the half-sample correction at t = 0. machine.py uses the
//     band-limited exact response (chain_fir) instead; the two differ by the aliasing of the
//     continuous response above 96 kHz, which the 15 kHz elliptic keeps small;
//   - the DAC side is exact: each frame's tap sum is a pulse into the output chain, the R side
//     0.75 sample (32 bit times) after the L side, as machine.py.
// The input/output stages (C10, U8 clip, C59, U16 pole and clip) run at 192 kHz too (machine.py
// runs them at the WAV rate). Not modelled here: the clock offset (clock_ppm; the plugin runs the
// design crystal), the XLR input stage overload (source_vpk).
//
// Units as machine.py: input and output in WAV full-scale units; input_gain = the fraction of
// converter full scale a full-scale input reaches; one full-scale gain-254 tap = 1.0 at the output
// at REVERB MIX 1.
#pragma once
#include "sg323_consts.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace sg323 {

using cd = std::complex<double>;
constexpr double FS = 32000.0, T = 1.0 / FS, FS_HI = k::FS_HI, T_HI = 1.0 / FS_HI;
constexpr int HI = 6, N_F = 15, FIRST_F = 8, FIRST_LR = 23, MC_ORIGIN = 7;

// ---------------------------------------------------------------- PROMs (the user's images)
struct Proms {
    // 0803 (256, or the 128-byte half), 0804, 0805, 0807 (4096), 0806 (2048), 0808 (8192)
    std::vector<uint8_t> p0803, p0804, p0805, p0806, p0807, p0808;

    bool complete() const {
        return (p0803.size() == 256 || p0803.size() == 128) && p0804.size() == 4096 && p0805.size() == 4096 &&
               p0806.size() == 2048 && p0807.size() == 4096 && p0808.size() == 8192;
    }
    static int knob_code(int pos) { return (pos ^ (pos >> 1)) ^ 3; }
    int a0808(int slot, int half, int prog, int pd) const {
        auto b = [](int v, int i) { return (v >> i) & 1; };
        return (half | (slot << 1) | b(pd, 0) << 6 | b(pd, 1) << 7 | b(pd, 2) << 8 | b(prog, 0) << 9 | b(prog, 1) << 10 |
                b(prog, 2) << 11 | b(pd, 3) << 12) % 8192;
    }
    // write-to-read delay (frames) of the read in `slot`; low < 0 = what the bus carries
    int delay(int slot, int prog, int pd, int low = -1) const {
        int hi = p0808[size_t(a0808(slot, 0, prog, pd))];
        if (low < 0) {
            bool mod_prev = FIRST_F - 1 <= slot - 1 && slot - 1 <= FIRST_F + N_F - 2;
            low = mod_prev ? 0xFF : p0808[size_t(a0808(slot - 1, 1, prog, pd))];
        }
        return (low + 256 * hi) % 16384;
    }
    // (ceiling 0..254, sign)
    void gain(int slot, int prog, int rd, int& ceil, int& sign) const {
        auto b = [](int v, int i) { return (v >> i) & 1; };
        int a = (slot | b(rd, 0) << 5 | b(rd, 1) << 6 | b(rd, 2) << 7 | b(prog, 0) << 8 | b(prog, 1) << 9 | b(prog, 2) << 10 |
                 b(prog, 3) << 11 | b(rd, 3) << 12) % 2048;
        int q = p0806[size_t(a)];
        ceil = (q & 0x7F) << 1;
        sign = (q & 0x80) ? 1 : -1;
    }
    int mod_period(int prog, int lvl) const {
        return 16 - (p0803[size_t((((prog & 7) << 4) | lvl) % int(p0803.size()))] & 15);
    }
    int mod_low(int k, int mc) const { return p0807[size_t(((mc >> 6) << 5) | (FIRST_F - 1 + k))]; }
    int mod_env(int k, int mc) const {
        int q = p0804[size_t(((mc >> 6) << 5) | (FIRST_F + k))];
        if (q & 8) return 255;
        return p0805[size_t((q & 7) | ((mc & 0x1FF) << 3))];
    }
    // predelay codes in knob order (0 .. 320 ms), and the ms the front panel shows for a code
    double predelay_ms(int code) const {
        int m = 1 << 30;
        for (int k = 0; k < 8; k++) m = std::min(m, delay(FIRST_LR + k, 0, code));
        return m / 32.0 - 1.0;
    }
    void predelay_codes(int out[16]) const {
        int c[16];
        for (int i = 0; i < 16; i++) c[i] = i;
        std::stable_sort(c, c + 16, [&](int a, int b) { return predelay_ms(a) < predelay_ms(b); });
        std::memcpy(out, c, sizeof c);
    }
};

// ---------------------------------------------------------------- the converter (converter.py)
inline double quantize(double x) {
    x += 0.25;
    const bool pos = x > 0;
    const double m = pos ? x : -x;
    auto above = [&](double trial) { return pos ? m > trial : m >= trial; };
    long c = 0;
    for (int i = 6; i >= 0; i--) {
        double trial = double(c | ((1L << i) - 1)) * k::KC + 191.0;
        if (above(trial)) c |= 1L << i;
    }
    long f = 0;
    for (int i = 7; i >= 0; i--) {
        double trial = double(c) * k::KC + double(f | ((1L << i) - 1));
        if (above(trial)) f |= 1L << i;
    }
    double v = double(c) * k::KC + double(f);
    return pos ? v : -v;
}

// ---------------------------------------------------------------- continuous stages
struct Stage {
    static constexpr int MAXP = 8;
    int n = 0;
    cd p[MAXP], r[MAXP];
    double D = 0;
    cd at(cd s) const {
        cd h = D;
        for (int i = 0; i < n; i++) h += r[i] / (s - p[i]);
        return h;
    }
    static Stage table(const cd* pp, const cd* rr, int n, double D) {
        Stage s;
        s.n = n;
        for (int i = 0; i < n; i++) s.p[i] = pp[i], s.r[i] = rr[i];
        s.D = D;
        return s;
    }
    static Stage lowpass1(double tau) {   // 1 / (1 + s tau)
        Stage s;
        s.n = 1;
        s.p[0] = -1.0 / tau;
        s.r[0] = 1.0 / tau;
        return s;
    }
    static Stage highpass1(double tau) {  // s tau / (1 + s tau)
        Stage s;
        s.n = 1;
        s.p[0] = -1.0 / tau;
        s.r[0] = -1.0 / tau;
        s.D = 1.0;
        return s;
    }
    static Stage shelf(double gain, double w) {   // gain (s + w) / (s + w gain): unity at DC, gain at HF
        Stage s;
        s.n = 1;
        s.p[0] = -w * gain;
        s.r[0] = gain * w * (1.0 - gain);
        s.D = gain;
        return s;
    }
};
inline Stage lpf() { return Stage::table(k::lpf_p, k::lpf_r, 7, k::lpf_D); }
inline Stage pre() { return Stage::table(k::pre_p, k::pre_r, 1, k::pre_D); }
inline Stage de() { return Stage::table(k::de_p, k::de_r, 1, k::de_D); }
inline Stage dip() { return Stage::table(k::dip_p, k::dip_r, 3, k::dip_D); }

// Move stage `v`'s single real pole at least 0.1% away from every pole of the other stages,
// keeping its DC gain (a product's partial fractions need simple poles).
inline void nudge_apart(Stage& v, const Stage* others, int n_others) {
    for (int pass = 0; pass < 4; pass++) {
        bool moved = false;
        for (int s = 0; s < n_others; s++)
            for (int i = 0; i < others[s].n; i++) {
                const cd q = others[s].p[i];
                if (std::abs(v.p[0] - q) >= 1e-3 * std::abs(q)) continue;
                const double g = v.r[0].real() / -v.p[0].real();
                const double p_new = q.real() * (v.p[0].real() <= q.real() ? 1.0 + 1.001e-3 : 1.0 - 1.001e-3);
                v.p[0] = p_new;
                v.r[0] = -p_new * g;
                moved = true;
            }
        if (!moved) break;
    }
}

// A chain as modes (at most 16 poles).
struct Chain {
    static constexpr int MAXM = 16;
    int n = 0;
    cd p[MAXM], r[MAXM];
    double D = 0;
    static Chain of(const Stage* st, int ns) {
        Chain c;
        c.D = 1.0;
        for (int a = 0; a < ns; a++) c.D *= st[a].D;
        for (int a = 0; a < ns; a++)
            for (int i = 0; i < st[a].n && c.n < MAXM; i++) {
                cd rr = st[a].r[i];
                for (int b = 0; b < ns; b++)
                    if (b != a) rr *= st[b].at(st[a].p[i]);
                c.p[c.n] = st[a].p[i];
                c.r[c.n] = rr;
                c.n++;
            }
        return c;
    }
};

// Impulse-invariant filter of a chain at T_HI: y[n] = D x + Re sum q_i - h(0+) T x / 2,
// q_i[n] = z_i q_i[n-1] + r_i T x[n].
struct ModeCoefs {
    int n = 0;
    cd z[Chain::MAXM], g[Chain::MAXM];
    double D = 0, half = 0;
    static ModeCoefs of(const Chain& c, double Ts) {
        ModeCoefs m;
        m.n = c.n;
        m.D = c.D;
        cd h0 = 0;
        for (int i = 0; i < c.n; i++) {
            m.z[i] = std::exp(c.p[i] * Ts);
            m.g[i] = c.r[i] * Ts;
            h0 += c.r[i];
        }
        m.half = h0.real() * Ts / 2;
        return m;
    }
};
struct ModeState {
    cd q[Chain::MAXM] = {};
    double step(const ModeCoefs& c, double x) {
        double y = (c.D - c.half) * x;
        for (int i = 0; i < c.n; i++) {
            q[i] = c.z[i] * q[i] + c.g[i] * x;
            y += q[i].real();
        }
        return y;
    }
};

// One-pole/three-pole IIR in scipy's lfilter form (direct form II transposed, a[0] = 1).
template <int N>
struct Iir {
    double b[N + 1] = {}, a[N + 1] = {}, z[N] = {};
    double step(double x) {
        double y = b[0] * x + z[0];
        for (int i = 0; i < N - 1; i++) z[i] = b[i + 1] * x + z[i + 1] - a[i + 1] * y;
        z[N - 1] = b[N] * x - a[N] * y;
        return y;
    }
};

// ---------------------------------------------------------------- settings
struct Controls {
    int room = 3;              // 1..8
    int decay = 5;             // knob 0..7 (0 = no reverb)
    int predelay = 0;          // knob 0..15
    double kfb = -0.279;
    bool modulation = true;
    double lf_eq = 1.0;        // RV2 0..1 (1 = lowest cut, 14 Hz)
    double hf_eq = 0.0;        // RV3 0..1 (0 = widest, 48 kHz; 1 = 3 kHz)
    double reverb_mix = 1.0, direct_mix = 0.0;
    bool dry_only = false, reverb_clear = false;
    bool input_mute = false;   // INPUT MUTE: sheet 8 U2 (4053) grounds the RV1 wiper's 10K instead of feeding U8
    double input_gain = 0.5;
    double deemph_trim = 1.0, hf_shelf_db = 0.0;
    double hiss_nv = 212.0;    // 0 = off
    uint64_t seed = 0;
    int mc_start = 0;
};

// What changes with the analog-facing controls (HF EQ, the shelf, the trim): built off the
// audio thread, handed over whole.
struct Coefs {
    ModeCoefs in;              // pre-emphasis, HF EQ, LPF1 (+ shelf) at 192 kHz
    ModeCoefs out;             // U23/U24, LPF2, de-emphasis (+ trim), C18, U16 pole at 192 kHz
    cd out_r_lag[Chain::MAXM]; // the R side's injection: r_i T exp(p_i (1 - 0.75) T_HI)
    int fo_n = 0;              // FO path: pulse-sampled per tap
    cd fo_z[Chain::MAXM], fo_g[N_F][Chain::MAXM];
    double hp2_a = 0;          // the LF EQ high-pass in the loop (RV2)

    static Coefs make(const Controls& c) {
        Coefs o;
        const double rv3 = c.hf_eq * 50000.0, rv2 = c.lf_eq * 50000.0;
        const bool shelf_on = c.hf_shelf_db != 0.0;
        const Stage sh = Stage::shelf(std::pow(10.0, c.hf_shelf_db / 20.0), 2 * M_PI * 3000.0);
        // the input chain (machine._in_chain)
        {
            Stage st[4] = {pre(), Stage::lowpass1((3300.0 + rv3) * 1e-9), lpf(), sh};
            Stage others[3] = {st[0], st[2], st[3]};
            nudge_apart(st[1], others, shelf_on ? 3 : 2);
            o.in = ModeCoefs::of(Chain::of(st, shelf_on ? 4 : 3), T_HI);
        }
        // the output chain (machine._out_chain with analog_io)
        {
            Stage st[6];
            int n = 0;
            st[n++] = Stage::lowpass1(16.2e3 * 220e-12);
            st[n++] = lpf();
            st[n++] = de();
            if (c.deemph_trim != 1.0) st[n++] = Stage::shelf(c.deemph_trim, 2 * M_PI * 2120.0);
            st[n++] = Stage::highpass1(1.0 / (2 * M_PI * k::F_REV_HP));
            st[n++] = Stage::lowpass1(k::TAU_U16);
            Chain ch = Chain::of(st, n);
            o.out = ModeCoefs::of(ch, T_HI);
            o.out.half = 0;   // strictly proper (U23 x LPF2): h(0+) = 0
            for (int i = 0; i < ch.n; i++) o.out_r_lag[i] = ch.r[i] * T_HI * std::exp(ch.p[i] * (0.25 * T_HI));
        }
        // the FO path (core._exact_paths): LPF1, HF EQ (nudged 0.1%), shelf, dip, U23
        {
            Stage hf = Stage::lowpass1((3300.0 + rv3) * 1e-9 * (1 + 1e-3));
            Stage st[5];
            int n = 0;
            st[n++] = lpf();
            int ihf = n;
            st[n++] = hf;
            if (shelf_on) st[n++] = sh;
            st[n++] = dip();
            st[n++] = Stage::lowpass1(16.2e3 * 220e-12);
            Stage others[4];
            int no = 0;
            for (int i = 0; i < n; i++)
                if (i != ihf) others[no++] = st[i];
            nudge_apart(st[ihf], others, no);
            Chain ch = Chain::of(st, n);
            const double TBT = 1.0 / 8.192e6, w = 4 * TBT;
            o.fo_n = ch.n;
            for (int i = 0; i < ch.n; i++) {
                o.fo_z[i] = std::exp(ch.p[i] * T);
                for (int kk = 0; kk < N_F; kk++) {
                    int slot = FIRST_F + kk;
                    double a = (210 - (8 * slot + 8)) * TBT;
                    o.fo_g[kk][i] = ch.r[i] * std::exp(ch.p[i] * a) * (std::exp(ch.p[i] * w) - 1.0) / ch.p[i] * (T / w);
                }
            }
        }
        o.hp2_a = 0.22e-6 * (1500.0 + rv2) / (0.22e-6 * (1500.0 + rv2) + T);
        return o;
    }
};

// What changes with the switches (room, decay, predelay, modulation, S5/S6): decoded off the
// audio thread from the PROMs.
struct Taps {
    int prog = 0;
    int f_hi[N_F] = {}, f_delay[N_F] = {};
    double f_sign[N_F] = {}, f_ceil[N_F] = {};
    int lr_d[8] = {};
    double lr_g[8] = {};
    int period[16] = {};
    bool modulation = true, reverb_clear = false;

    static Taps make(const Proms& pr, const Controls& c) {
        Taps t;
        int pdc[16];
        pr.predelay_codes(pdc);
        const int prog = Proms::knob_code(std::clamp(c.room, 1, 8) - 1);
        const int rd = Proms::knob_code(std::clamp(c.decay, 0, 7));
        const int pd = pdc[std::clamp(c.predelay, 0, 15)];
        t.prog = prog;
        for (int kk = 0; kk < N_F; kk++) {
            t.f_delay[kk] = pr.delay(FIRST_F + kk, prog, pd);
            t.f_hi[kk] = (t.f_delay[kk] - 255) / 256;   // the 0808 high byte (delay low byte 0xFF)
            int ce, sg;
            pr.gain(FIRST_F + kk, prog, rd, ce, sg);
            t.f_ceil[kk] = ce;
            t.f_sign[kk] = sg;
        }
        for (int kk = 0; kk < 8; kk++) {
            t.lr_d[kk] = pr.delay(FIRST_LR + kk, prog, pd);
            int ce, sg;
            pr.gain(FIRST_LR + kk, prog, rd, ce, sg);
            t.lr_g[kk] = c.dry_only ? 0.0 : double(ce * sg) / 254.0;
        }
        for (int l = 0; l < 16; l++) t.period[l] = pr.mod_period(prog, l);
        t.modulation = c.modulation;
        t.reverb_clear = c.reverb_clear;
        return t;
    }
};

// ---------------------------------------------------------------- the digital core (core.run)
class Core {
public:
    void init(const Proms& pr, int mc_start) {
        lo_.assign(8192 * N_F, 0);
        env_.assign(8192 * N_F, 0);
        for (int m = 0; m < 8192; m++)
            for (int kk = 0; kk < N_F; kk++) {
                lo_[size_t(m * N_F + kk)] = uint8_t(pr.mod_low(kk, m));
                env_[size_t(m * N_F + kk)] = uint8_t(pr.mod_env(kk, m));
            }
        std::fill(std::begin(W_), std::end(W_), 0.0);
        n_ = 0;
        mc_start_ = mc_start;
        m_ = mc_start;
        pos_ = 0;
        std::fill(std::begin(sf_), std::end(sf_), cd(0));
        hp1_x_ = hp1_y_ = hp2_x_ = hp2_y_ = 0;
        hp1_z_ = hp2_z_ = 0;
    }

    // One frame: the input at the ADC hold (fine LSB, already through the input chain) and the
    // rate-level code. Returns the L and R tap sums.
    void frame(double in, int lvl, const Taps& t, const Coefs& c, double kfb, double& lo, double& ro) {
        const int64_t n = n_;
        // the modulation count (core.mc_schedule, streaming)
        int mc;
        if (n == 0) p0_ = t.period[lvl];
        if (n < MC_ORIGIN) {
            mc = mc_start_ + floordiv(int(n) - MC_ORIGIN, p0_);
        } else {
            if (n == MC_ORIGIN) {
                m_ = mc_start_;
                pos_ = 0;
                per_ = t.period[lvl];
            } else if (++pos_ == per_) {
                m_++;
                pos_ = 0;
                per_ = t.period[lvl];
            }
            mc = m_;
        }
        mc &= 0x1FFF;
        // the feedback taps
        double u[N_F];
        for (int kk = 0; kk < N_F; kk++) {
            int d;
            double g;
            if (t.modulation) {
                d = (256 * t.f_hi[kk] + lo_[size_t(mc * N_F + kk)]) % 16384;
                g = t.f_sign[kk] * std::min(double(env_[size_t(mc * N_F + kk)]), t.f_ceil[kk]) / 254.0;
            } else {
                d = t.f_delay[kk];
                g = t.f_sign[kk] * t.f_ceil[kk] / 254.0;
            }
            if (t.reverb_clear) g = 0.0;
            u[kk] = word(n - d) * g;
        }
        // FO through the exact path, sampled at the hold
        double yfo = 0;
        for (int i = 0; i < c.fo_n; i++) {
            cd v = 0;
            for (int kk = 0; kk < N_F; kk++) v += u[kk] * c.fo_g[kk][i];
            sf_[i] = v + c.fo_z[i] * sf_[i];
            yfo += sf_[i].real();
        }
        // the 22 Hz high-pass (R..C), the U7 summer, the LF EQ high-pass (lfilter form)
        const double a1 = 0.22e-6 * 33.0e3 / (0.22e-6 * 33.0e3 + T);
        double y1 = a1 * yfo + hp1_z_;
        hp1_z_ = -a1 * yfo + a1 * y1;
        const double a2 = c.hp2_a, x2 = in + kfb * y1;
        double y3 = a2 * x2 + hp2_z_;
        hp2_z_ = -a2 * x2 + a2 * y3;
        W_[size_t((n + 1) & MASK)] = quantize(y3);
        last_ain_ = y3;
        // the audition taps
        lo = ro = 0;
        for (int kk = 0; kk < 4; kk++) lo += word(n - t.lr_d[kk]) * t.lr_g[kk];
        for (int kk = 4; kk < 8; kk++) ro += word(n - t.lr_d[kk]) * t.lr_g[kk];
        n_++;
    }

    double last_ain() const { return last_ain_; }      // AIN of the last frame (tests)
    double last_word() const { return W_[size_t(n_ & MASK)]; }   // the word it stored

private:
    static constexpr int64_t MASK = 32767;
    double last_ain_ = 0;
    static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    double word(int64_t i) const { return i < 0 ? 0.0 : W_[size_t(i & MASK)]; }

    std::vector<uint8_t> lo_, env_;
    double W_[MASK + 1] = {};
    int64_t n_ = 0;
    int mc_start_ = 0, m_ = 0, pos_ = 0, per_ = 1, p0_ = 1;
    cd sf_[Chain::MAXM] = {};
    double hp1_x_ = 0, hp1_y_ = 0, hp2_x_ = 0, hp2_y_ = 0, hp1_z_ = 0, hp2_z_ = 0;
};

// ---------------------------------------------------------------- the machine at 192 kHz
class Machine {
public:
    bool init(const Proms& pr, const Controls& c) {
        if (!pr.complete()) return false;
        core_.init(pr, c.mc_start);
        rng_.seed(c.seed);
        for (int ch = 0; ch < 2; ch++) {
            c10_[ch].b[0] = k::c10_b[0], c10_[ch].b[1] = k::c10_b[1], c10_[ch].a[0] = 1, c10_[ch].a[1] = k::c10_a[1];
            c59_[ch].b[0] = k::c59_b[0], c59_[ch].b[1] = k::c59_b[1], c59_[ch].a[0] = 1, c59_[ch].a[1] = k::c59_a[1];
            u16_[ch].b[0] = k::u16_b[0], u16_[ch].b[1] = k::u16_b[1], u16_[ch].a[0] = 1, u16_[ch].a[1] = k::u16_a[1];
        }
        for (int i = 0; i < 4; i++) rate_.b[i] = k::rate_b[i], rate_.a[i] = k::rate_a[i];
        env_k_ = std::exp(-1.0 / (FS_HI * k::TAU_RATE));
        phase_ = 0;
        pending_r_ = 0;
        return true;
    }
    // Set from the control side (or once before running); see Coefs / Taps.
    void set(const Coefs& c, const Taps& t, const Controls& ctl) {
        coefs_ = c;
        taps_ = t;
        ctl_ = ctl;
        hiss_sd_ = ctl.hiss_nv * 1e-9 * std::sqrt(FS_HI / 2) / k::V_FS;
    }
    void set_coefs(const Coefs& c) { coefs_ = c; }
    void set_taps(const Taps& t) { taps_ = t; }
    void set_levels(const Controls& ctl) {   // mixes, input gain, kfb, hiss: no rebuild
        ctl_ = ctl;
        hiss_sd_ = ctl.hiss_nv * 1e-9 * std::sqrt(FS_HI / 2) / k::V_FS;
    }

    // One 192 kHz sample.
    void step(double xl, double xr, double& yl, double& yr) {
        const Controls& c = ctl_;
        const double lim = k::V_RAIL / (k::V_FS / 2);
        double li[2] = {xl * c.input_gain, xr * c.input_gain};
        for (int ch = 0; ch < 2; ch++) {   // C10 (same load either way), U2 INPUT MUTE, U8
            const double v = c10_[ch].step(li[ch]);
            li[ch] = c.input_mute ? 0.0 : std::clamp(v, -lim, lim);
        }
        const double mono = 0.5 * (li[0] + li[1]);
        // the rate-level detector, on U7.7 before the pre-emphasis
        double v = rate_.step(k::V_FS * mono);
        env_ = (1 - env_k_) * std::fabs(v) + env_k_ * env_;
        int code = 0;
        for (int i = 0; i < 4; i++) code |= (env_ > k::THRESH[i] ? 1 : 0) << i;
        double h = mono;
        if (hiss_sd_ > 0) h += hiss_sd_ * nd_(rng_);
        h = in_.step(coefs_.in, h);
        // the output chain advances one sample, then takes this sample's pulses
        const ModeCoefs& oc = coefs_.out;
        for (int i = 0; i < oc.n; i++) {
            ql_[i] *= oc.z[i];
            qr_[i] *= oc.z[i];
        }
        if (pending_r_ != 0) {   // the R pulse of the last frame, 0.75 sample after it
            for (int i = 0; i < oc.n; i++) qr_[i] += pending_r_ * coefs_.out_r_lag[i];
            pending_r_ = 0;
        }
        if (phase_ == 0) {   // the ADC hold: one frame
            double lo, ro;
            core_.frame(h * k::FULL_SCALE, code, taps_, coefs_, c.kfb, lo, ro);
            last_code_ = code;
            double al = HI * lo / k::FULL_SCALE;
            for (int i = 0; i < oc.n; i++) ql_[i] += al * oc.g[i];
            pending_r_ = HI * ro / k::FULL_SCALE;
        }
        phase_ = (phase_ + 1) % HI;
        double wet[2] = {0, 0};
        for (int i = 0; i < oc.n; i++) {
            wet[0] += ql_[i].real();
            wet[1] += qr_[i].real();
        }
        // U16: reverb + direct (LI via C59), in volts, clipped at the rails
        const double gw = k::G_REV * k::WET_V;
        double out[2];
        for (int ch = 0; ch < 2; ch++) {
            double d = u16_[ch].step(c59_[ch].step(li[ch]) * (k::V_FS / 2) * k::G_DIR);
            double volts = c.reverb_mix * wet[ch] * gw + c.direct_mix * d;
            out[ch] = std::clamp(volts, -k::V_RAIL, k::V_RAIL) / gw;
        }
        yl = out[0];
        yr = out[1];
    }
    int level_code() const { return last_code_; }

private:
    Core core_;
    Coefs coefs_;
    Taps taps_;
    Controls ctl_;
    Iir<1> c10_[2], c59_[2], u16_[2];
    Iir<3> rate_;
    double env_ = 0, env_k_ = 0, hiss_sd_ = 0, pending_r_ = 0;
    ModeState in_;
    cd ql_[Chain::MAXM] = {}, qr_[Chain::MAXM] = {};
    int phase_ = 0, last_code_ = 0;
    std::mt19937_64 rng_;
    std::normal_distribution<double> nd_{0.0, 1.0};
};

}  // namespace sg323
