// dmx::AudioCore: the pre-deglitch DMX 15-80 audio path, both pitch boards' 8086s plus the
// MCB frame, the delay RAM rings and the converters. JUCE-free, no globals, no allocation,
// no I/O: every piece of state is a member, so several instances can run side by side and a
// snapshot is a copy (see Machine). The C++ port of emu/audio_core.h (2026-11-10), kept
// line-for-line where it computes anything, so outputs stay byte-identical (tests/run.py).
//
// What it models (established at gate level, notes/2026-11-10-0014, -0021, -0212, -0224):
//  - one frame = 32 memory cycles = 256 MCLOCK, one sample per channel. MCLOCK is 10 MHz in
//    the gate-level testbench (25.6 us) and 10.24 MHz from ADC3's crystal on the real
//    machine (25.0 us = 40.0 kHz). All event times scale with MCLOCK; the 8086 stays 8 MHz;
//  - each channel has its own RAM ring; a frame address counter steps once per frame; the
//    channel's sample is written at the counter, taps read at counter + 670 word;
//  - pre-deglitch PROM (gen_sequencer.py --pre): output card A latches the word-2 tap
//    (OUT 22h) at e = 5, card B at e = 13; sign taps at e = 6 (word 1, OUT 24h) and e = 7
//    (word 3, OUT 26h) shift D11 into the 25LS22s IC11 / IC12 that IN 20h reads;
//  - the 670 write word = 2*LA1 + LA2: OUT 20h/22h/24h/26h -> words 0/2/1/3;
//  - one pitch board (option bit 3 = 0, MCB4 L2 out / PROM A8 = 1): channel B's output tap is
//    addressed by board A's 670 word 0 (OUT 20h), where the firmware puts B's delay (8C48h);
//  - TEST: the frame's /SYNC edge sets IC15 (TEST low), IN 20h clears it;
//  - the 8086's bus-cycle timing is the gate-level bus-functional model's
//    (tb/cpu8086_bfm.v + vpi/i86_vpi.c), so a 670 write lands at the same instant relative
//    to the memory cycles as in the gate-level runs;
//  - converters (notes/2026-11-10-0224, tb/conv_top.vh): the sampler picks the gain from
//    window comparators at 8/4/2 Vt (Vt = 15 V * 100/3100 = 0.484 V): R0 above 3.87 V,
//    R1 above 1.94 V, R2 above 0.97 V, else R3; the ADC rounds x * 2^R to a 12-bit
//    offset-binary code (LSB 20 V / 4096, +-10 V). DAC3 loads (m - 2048) * 2^(3-R) + 16384,
//    a 15-bit word; so 1 output unit = 10 V / 16384.
// Event times inside a frame were measured from the pre-deglitch gate-level top (delay8q,
// E5pre.hex) relative to the CONV A edge that advances the testbench's frame number.
//
// Times are int64_t nanoseconds from power-up and frames int64_t, so nothing overflows on
// platforms where long is 32 bits.
//
// I/O hooks (all take the `io` context pointer):
//  sample_in(io, ch, fr)        the sampler/ADC word for channel ch, frame fr (input_volts() + adc())
//  frame_out(io, fr, a, b)      the two DAC words at frame fr's dump instant (plain path)
//  emit(io, j, x)               analog / VCO path: output frame j, x[ch] scaled to +-32768 = +-10 V,
//                               already clamped (the caller rounds or scales)
// Input for the analog / VCO paths comes from a Source (see below), read at real time.
#pragma once
#include "../i8086.h"
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dmx {

// Audio input as the converters see it: volts at absolute sample j of a stream at `rate` Hz.
// Either 16-bit PCM in memory (pcm, interleaved, n frames) or a float ring the host pushes
// into (ring, mask + 1 frames per channel, +-1.0 = +-10 V, frames before `n` valid).
struct Source {
    const int16_t* pcm = nullptr; int chans = 1;
    const float* ring[2] = {nullptr, nullptr}; uint32_t mask = 0;
    int64_t n = 0; int rate = 40000;
    double volts(int ch, int64_t j) const {
        if (pcm) return pcm[j * chans + (chans > 1 ? ch : 0)] * (10.0 / 32768.0);
        return ring[ch][j & mask] * 10.0;
    }
    bool valid() const { return (pcm || ring[0]) && n > 0; }
};

struct Board {
    i86_t c; uint8_t ram[2048];
    uint8_t iphit[2048];             // coverage: RAM addresses where an instruction started
    int kind; uint16_t port, data;
    int in_probe; uint16_t in_val;
    i86_t snap; uint32_t last_cycles; int credit;
    int test_low;                    // IC15: 1 = TEST low (a sync not yet consumed)
    uint16_t w670[4];
    uint8_t ic11, ic12;              // 25LS22 serial registers (Y6 / Y7)
    int64_t t_next; int act;         // 0 = call next(), 1 = 670 write, 2 = IN sample
    int pend_kind; uint16_t pend_port, pend_data; int64_t t_after;
    int held, halted;                // held in reset by the 8085 / stopped at HALT or TRAP
    uint32_t outs, ins, lost, resets;
};

class AudioCore {
public:
    static constexpr int64_t T86 = 125;                    // 8086 clock, ns (8 MHz)
    static constexpr int64_t NEVER = int64_t(1) << 62;
    enum { Y7A, Y7B, CNT, TAPB, SYNC, WRA, WRB, DUMP, TAPA, Y6A, Y6B, NEV };

    // ---- configuration (set before running; see dmx.cpp / dmxaudio.cpp for the CLI names) ----
    unsigned ac_wrap_mask = 16383;   // delay RAM words per channel - 1 (channel A, and B unless set below)
    unsigned ac_wrap_mask_b = 0;     // channel B's own fit (0 = same as A); the firmware allows A/B fits to differ
    int ac_single_pitch = 0;         // one pitch board (option bit 3 = 0)
    int ac_lockin = 0;               // front-panel LOCK IN switch: inhibits delay-RAM writes (user manual 2.4.5)
    // optional models (notes/2026-11-10-0345-analog-models.md), all off by default; with all off,
    // every code path below is the original one:
    //  analog       input-card filters (DMX 1.3), DAC3 track/hold and output filters (DMX 77.4),
    //               as continuous-time filters on an 8x fine grid; with it: load (differential load,
    //               ohms, 0 = none), p5 (input card IC8 gain preset, ohms), phase_out (both inverted)
    //  vco          XTAL/VCO switch at VCO: MCLOCK = mhz * (1 + depth * sin(2 pi speed t))
    //  ram_random   delay RAM (and the boards' 2114s) power up random
    //  interp       pitch-tap interpolation (a guess at what the deglitch card's multiply/add path did in
    //               later units; notes/2026-11-11-*-interpolated-pitch.md): the audio taps read at a
    //               fractional offset that ramps through the 8086's whole-sample steps instead of jumping
    //               by them. The 8086's sign taps (IN 20h) stay exact, so its program runs unchanged.
    //  vco_shape    0 = a sine LFO; 1 = the ADC3 circuit (notes/2026-11-11-0250-vco-circuit.md): a triangle
    //               from IC12 (+-4.56 V: the Schmitt R17/R18 at +-13.5 V) through the 8038's sine shaper
    //               (+-5 V window at +-15 V supplies, so driven to 91.2 %), inverted on its way to the
    //               74S124 (IC13A), so MCLOCK falls while the triangle rises (the rate LED's half);
    //               normalised so depth = peak deviation
    int ac_analog = 0, ac_vco = 0, ac_ram_random = 0, ac_interp = 0, ac_vco_shape = 0;
    double ac_load = 0, ac_p5 = 18000, ac_gbw_in = 5e6; int ac_phase_out = 0;
    double ac_vco_speed = 0, ac_vco_depth = 0, ac_vco_mhz = 0; unsigned ac_ram_seed = 0;

    // ---- hooks ----
    void* io = nullptr;
    uint16_t (*sample_in)(void* io, int ch, int64_t fr) = nullptr;
    void (*frame_out)(void* io, int64_t fr, uint16_t a, uint16_t b) = nullptr;
    void (*emit)(void* io, int64_t j, const double* x) = nullptr;
    int64_t ac_pcm_n = 0;            // analog / VCO path: output frames 1..ac_pcm_n are emitted

    // ---- state ----
    Board AB[2];
    uint16_t ac_ram[2][65536];
    uint16_t ac_last_in[2] = {0, 0}; // last sampler/ADC word per channel (taken even in lock-in: the sampler runs on)
    int64_t ac_loud_fr[2] = {-1000000, -1000000};   // frame of the last sample with range code != 3 (|x| > 2 Vt):
                                     // the sampler's green-LED window, which ADC3's one-shot turns into the audio trigger
    int64_t ac_ev[NEV] = {}, ac_frame_ns = 25600, ac_t0 = 9635;   // scaled by set_mclk
    double ac_fs = 39062.5;
    int64_t ac_fr = 0; int ac_e = 0; // next event: frame, index
    unsigned ac_cnt = 0; uint16_t ac_out_a = 0, ac_out_b = 0;
    struct Interp { double pos, rate; unsigned last; int since, init; } ac_ip[2] = {};   // ac_interp state

    AudioCore() { std::memset(AB, 0, sizeof AB); std::memset(ac_ram, 0, sizeof ac_ram); for (auto& b : AB) b.t_next = NEVER; set_mclk(10.0); }
    AudioCore(const AudioCore& o) { *this = o; }
    AudioCore& operator=(const AudioCore& o) {
        if (this != &o) { std::memcpy(static_cast<void*>(this), static_cast<const void*>(&o), sizeof *this); rebind(); }
        return *this;
    }
    // after a copy: the boards' bus hooks point at this instance's boards
    void rebind() { for (auto& b : AB) { b.c.user = &b; b.c.rd = mrd; b.c.wr = mwr; b.c.in = pin; b.c.out = pout; } }

    unsigned wrap(int ch) const { return ch && ac_wrap_mask_b ? ac_wrap_mask_b : ac_wrap_mask; }

    void set_mclk(double mhz) {      // 10 = gate-level testbench, 10.24 = ADC3 crystal
        for (int e = 0; e < NEV; e++) ac_ev[e] = std::llround(ev10[e] * 10.0 / mhz);
        ac_frame_ns = std::llround(25600 * 10.0 / mhz); ac_t0 = std::llround(9635 * 10.0 / mhz);
        ac_fs = mhz * 1e6 / 256;
    }

    // power-up: RAM as given (the caller fills b->ram), CPU held until release
    void board_init(Board* b) {
        b->c.user = b; b->c.rd = mrd; b->c.wr = mwr; b->c.in = pin; b->c.out = pout;
        b->ic11 = b->ic12 = 0x01;    // power-up state the gate-level 25LS22s show
        b->test_low = 1;             // IC15 powers up set in the gate-level model
        b->held = 1; b->t_next = NEVER;
    }
    // 8284 RES released at time t (ns): the 8086 starts from FFFF0h four clocks later
    void release(Board* b, int64_t t) {
        i86_reset(&b->c);
        b->last_cycles = b->c.cycles; b->credit = 0; b->act = 0; b->in_probe = 0; b->kind = 0;
        b->held = 0; b->halted = 0; b->resets++;
        b->t_next = t + 4 * T86;
    }
    void hold(Board* b) { b->held = 1; b->t_next = NEVER; }

    // process every event before time t, then run the boards up to t
    void run_until(int64_t t) {
        while (event_time() < t) step_event();
        run_boards(t);
    }

    // process the next frame event (running the boards up to it first)
    void step_event() {
        int64_t te = event_time();
        if (te >= 0) {
            run_boards(te);
            switch (ac_e) {
            case Y7A: AB[0].ic12 = (uint8_t)(AB[0].ic12 << 1 | d11(ac_ram[0][(ac_cnt + AB[0].w670[3]) & ac_wrap_mask])); break;
            case Y7B: AB[1].ic12 = (uint8_t)(AB[1].ic12 << 1 | d11(ac_ram[1][(ac_cnt + AB[1].w670[3]) & wrap(1)])); break;
            case CNT: ac_cnt++; break;
            case TAPB: { uint16_t off = ac_single_pitch ? AB[0].w670[0] : AB[1].w670[2];
                ac_out_b = ac_interp ? interp_tap(1, off, wrap(1)) : ac_ram[1][(ac_cnt + off) & wrap(1)]; break; }
            case SYNC:
                for (int k = 0; k < 2; k++) { if (AB[k].test_low && ac_fr > 4 && !AB[k].held && !AB[k].halted) AB[k].lost++; AB[k].test_low = 1; }
                break;
            case WRA: case WRB: { int ch = ac_e == WRB; uint16_t v = sample_in(io, ch, ac_fr); ac_last_in[ch] = v;
                if (((v >> 12) & 3) != 3) ac_loud_fr[ch] = ac_fr;
                if (!ac_lockin) ac_ram[ch][ac_cnt & wrap(ch)] = v; break; }
            case DUMP: if (ac_fr >= 1) { if (ac_analog || ac_vco) out_stage((double)te, ac_out_a, ac_out_b); else frame_out(io, ac_fr, ac_out_a, ac_out_b); } break;
            case TAPA: ac_out_a = ac_interp ? interp_tap(0, AB[0].w670[2], ac_wrap_mask) : ac_ram[0][(ac_cnt + AB[0].w670[2]) & ac_wrap_mask]; break;
            case Y6A: AB[0].ic11 = (uint8_t)(AB[0].ic11 << 1 | d11(ac_ram[0][(ac_cnt + AB[0].w670[1]) & ac_wrap_mask])); break;
            case Y6B: AB[1].ic11 = (uint8_t)(AB[1].ic11 << 1 | d11(ac_ram[1][(ac_cnt + AB[1].w670[1]) & wrap(1)])); break;
            }
        }
        if (++ac_e == NEV) {
            ac_e = 0; ac_fr++;
            if (ac_vco) { ac_vf_start += 25600 * 10.0 / ac_vf_mhz; ac_vf_mhz = vco_at(ac_vf_start); }
        }
    }

    int64_t event_time() {
        if (!ac_vco) return ac_t0 + (ac_fr - 1) * ac_frame_ns + ac_ev[ac_e];
        if (!ac_vf_init) {           // frame ac_fr (normally 0) starts one nominal frame before ac_t0
            ac_vf_init = 1; ac_vf_mhz = vco_at((double)ac_t0);
            ac_vf_start = ac_t0 + (ac_fr - 1) * (25600 * 10.0 / ac_vf_mhz);
        }
        return std::llround(ac_vf_start + ev10[ac_e] * 10.0 / ac_vf_mhz);
    }

    // ---- sample format ----
    // RAM / bus word: D13 D12 = R1 R0 (R = log2 of the sampler gain), D11..D0 offset binary
    static int word2units(uint16_t w) {      // DAC3: (m - 2048) * 2^(3-R), units of 10 V / 16384
        int r = (w >> 12) & 3, m = (int)(w & 0xFFF) - 2048;
        return m * (1 << (3 - r));
    }
    // units back to a word with the largest R0R1 that holds them (the finest step), as a renormaliser would
    static uint16_t units2word(double u) {
        for (int r = 3; r >= 0; r--) {
            long m = std::lround(u / (1 << (3 - r)));
            if (m >= -2048 && m <= 2047) return (uint16_t)(r << 12 | (m + 2048));
        }
        return (uint16_t)(u < 0 ? 0 : 0xFFF);
    }
    // ac_interp: the 8086 moves a pitch tap's offset by +-1 every few frames (the rate it wants) and by
    // a period at a splice. Follow it with a ramp: the step rate, smoothed, advances a fractional position
    // that is pulled toward the 8086's offset and never more than 1.5 samples from it; jumps (|step| > 2)
    // move the position by the same amount. The output is the linear interpolation of the two words
    // around the position. A held offset gives back the plain tap word exactly.
    uint16_t interp_tap(int ch, unsigned off, unsigned mask) {
        Interp& s = ac_ip[ch];
        const double n = mask + 1.0;
        auto wrapd = [n](double d) { d = std::fmod(d, n); if (d >= n / 2) d -= n; if (d < -n / 2) d += n; return d; };
        off &= mask;
        if (!s.init) { s.init = 1; s.pos = off; s.rate = 0; s.last = off; s.since = 1 << 20; }
        int d = (int)((off - s.last) & mask); if (d > (int)(mask / 2)) d -= (int)mask + 1;
        if (d) {
            if (d >= -2 && d <= 2 && s.since < 256) s.rate = 0.7 * s.rate + 0.3 * ((double)d / s.since);
            else if (d < -2 || d > 2) s.pos += d;
            s.since = 0; s.last = off;
        }
        s.since++;
        if (s.since > 256) s.rate = 0;
        else if (std::fabs(s.rate) * s.since > 1.0) s.rate = (s.rate > 0 ? 1.0 : -1.0) / s.since;   // no step for a while: slower
        s.pos += s.rate;
        double e = wrapd(off - s.pos);
        if (e > 1.5) e = 1.5; if (e < -1.5) e = -1.5;
        s.pos = off - e + 0.05 * e;
        if (s.rate == 0 && std::fabs(e) < 1e-3) s.pos = off;
        s.pos = std::fmod(s.pos, n); if (s.pos < 0) s.pos += n;
        double b = std::floor(s.pos), f = s.pos - b;
        unsigned i0 = (ac_cnt + (unsigned)b) & mask;
        uint16_t w0 = ac_ram[ch][i0];
        if (f < 1e-9) return w0;
        uint16_t w1 = ac_ram[ch][(i0 + 1) & mask];
        return units2word((1 - f) * word2units(w0) + f * word2units(w1));
    }
    static uint16_t adc(double v) {          // sampler + ADC3, v in volts
        const double vt = 15.0 * 100.0 / 3100.0;
        int r = (v > 8 * vt || v < -8 * vt) ? 0 : (v > 4 * vt || v < -4 * vt) ? 1 : (v > 2 * vt || v < -2 * vt) ? 2 : 3;
        double ain = v * (1 << r);
        if (ain > 9.999) ain = 9.999;
        if (ain < -10.0) ain = -10.0;
        int c = (int)std::floor(ain / (20.0 / 4096.0) + 2048.5);
        if (c > 4095) c = 4095;
        if (c < 0) c = 0;
        return (uint16_t)(r << 12 | c);
    }
    static int16_t units2pcm(int u) { int x = u * 2; return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

    // input in volts for channel ch at frame fr: linear resampling of the Source at the frame
    // rate (frame 1 = sample 0), or with analog / VCO the input stage at this event's real time
    double input_volts(const Source& w, int ch, int64_t fr) {
        if (!w.valid()) return 0;
        if (ac_analog || ac_vco)
            return in_stage(w, ch, (event_time() - (ac_t0 + ac_ev[ac_e])) * 1e-9);
        double pos = (fr - 1) * (double)w.rate / ac_fs;
        int64_t i = (int64_t)pos; double a = pos - i;
        if (i < 0) return 0;
        if (i >= w.n - 1) return i == w.n - 1 ? w.volts(ch, i) : 0;
        return w.volts(ch, i) * (1 - a) + w.volts(ch, i + 1) * a;
    }

    // after set_mclk and the RAM / board set-up: n_out = output frames the analog / VCO stage
    // emits; boards = also randomise the pitch boards' 2114s
    void options_apply(int64_t n_out, bool boards) {
        if (ac_ram_random) {
            uint32_t x = ac_ram_seed * 2654435761u + 0x9E3779B9u;
            auto rnd = [&x] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
            for (int ch = 0; ch < 2; ch++) for (int k = 0; k < 65536; k++) ac_ram[ch][k] = (uint16_t)(rnd() & 0x3FFF);
            if (boards) for (int ch = 0; ch < 2; ch++) for (int k = 0; k < 2048; k++) AB[ch].ram[k] = (uint8_t)rnd();
        }
        if (ac_vco && ac_vco_mhz <= 0) ac_vco_mhz = ac_fs * 256 / 1e6;   // centre = the crystal (set_mclk)
        if (ac_analog) filters_init();
        ac_pcm_n = n_out;
    }

private:
    static constexpr int ev10[NEV] = {710, 1510, 2310, 11110, 11213, 11890, 12690, 12800, 23110, 24710, 25510};
    double ac_vf_start = 0, ac_vf_mhz = 0; int ac_vf_init = 0;   // VCO frame timebase (ns, MHz)

    // ---- 8086 bus hooks ----
    static uint8_t mrd(void* u, uint32_t a) { return static_cast<Board*>(u)->ram[a & 0x7FF]; }
    static void mwr(void* u, uint32_t a, uint8_t d) { static_cast<Board*>(u)->ram[a & 0x7FF] = d; }
    static uint16_t pin(void* u, uint16_t p, int w) {
        Board* b = static_cast<Board*>(u); b->kind = w ? 4 : 3; b->port = p;
        return b->in_probe == 2 ? (w ? b->in_val : (b->in_val & 0xFF)) : 0;
    }
    static void pout(void* u, uint16_t p, uint16_t v, int w) {
        Board* b = static_cast<Board*>(u); b->kind = w ? 2 : 1; b->port = p; b->data = w ? v : (v & 0xFF);
    }

    // i86_vpi.c's $i86_next: run to the next I/O instruction or WAIT stall.
    static void next86(Board* b, int test_low, int* kind, uint16_t* port, uint16_t* data, int* clocks) {
        i86_t* c = &b->c; *kind = 0; *port = 0; *data = 0;
        uint32_t start = b->last_cycles;
        for (int n = 0; n < 100000; n++) {
            if (c->state == I86_HALT || c->state == I86_TRAP) { *kind = 5; break; }
            b->snap = *c; b->kind = 0; b->in_probe = 1;
            uint16_t ip0 = c->ip;
            int st = i86_step(c, test_low);
            if (st != I86_WAIT) b->iphit[(((uint32_t)c->s[CS] << 4) + ip0) & 0x7FF] = 1;
            if (st == I86_WAIT && c->cycles == b->snap.cycles) { *kind = 0; break; }
            if (b->kind == 3 || b->kind == 4) { *c = b->snap; *kind = b->kind; *port = b->port; break; }
            if (b->kind == 1 || b->kind == 2) { *kind = b->kind; *port = b->port; *data = b->data; b->outs++; break; }
            if (st == I86_WAIT) { *kind = 0; break; }
        }
        *clocks = (int)(c->cycles - start); b->last_cycles = c->cycles;
    }

    // one bus-functional-model step at b->t_next (cpu8086_bfm.v's loop, split at the instants
    // the board sees: the 670 write strobe and the IN data sample / TEST clear)
    static void bfm_step(Board* b) {
        int64_t t = b->t_next;
        if (b->act == 1) {           // OUT strobe (WR rising)
            int p = b->pend_port & 0xFF;
            if ((p & 0xF9) == 0x20) b->w670[((p >> 1) & 1) * 2 + ((p >> 2) & 1)] = b->pend_data;
            b->act = 0; b->t_next = b->t_after; return;
        }
        if (b->act == 2) {           // IN: data sampled, instruction completes
            uint16_t v = 0xFFFF;
            if (b->pend_port & 0x20) { v = (uint16_t)(b->ic12 << 8 | b->ic11); b->test_low = 0; b->ins++; }
            b->in_probe = 2; b->in_val = b->pend_kind == 4 ? v : (v & 0xFF); b->kind = 0;
            i86_step(&b->c, 0);
            b->in_probe = 0; b->credit = 10;
            b->act = 0; b->t_next = b->t_after; return;
        }
        int kind, clocks; uint16_t port, data;
        next86(b, b->test_low, &kind, &port, &data, &clocks);
        clocks -= b->credit; b->credit = 0; if (clocks < 0) clocks = 0;
        switch (kind) {
        case 0: b->t_next = t + (int64_t)(clocks > 0 ? clocks : 5) * T86; break;
        case 1: case 2: {
            int64_t pre = t + (int64_t)(clocks > 4 ? clocks - 4 : 0) * T86;
            b->pend_port = port; b->pend_data = kind == 2 ? data : (uint16_t)(data << 8 | (data & 0xFF));
            b->act = 1; b->t_next = pre + 3 * T86; b->t_after = pre + 4 * T86; break;
        }
        case 3: case 4: {
            int64_t pre = t + (int64_t)(clocks + 6) * T86;
            b->pend_kind = kind; b->pend_port = port;
            b->act = 2; b->t_next = pre + 3 * T86; b->t_after = pre + 4 * T86; break;
        }
        default: b->halted = 1; b->t_next = NEVER; break;
        }
    }

    // run both boards up to (not including) time t
    void run_boards(int64_t t) {
        for (;;) {
            Board* b = AB[0].t_next <= AB[1].t_next ? &AB[0] : &AB[1];
            if (b->t_next >= t) break;
            bfm_step(b);
        }
    }

    static int d11(uint16_t w) { return (w >> 11) & 1; }
    // the LFO's phase is ac_vco_ph0 at ac_vco_t0 (both 0 unless set_vco_speed moved them, so a
    // fixed-speed run computes exactly sin(2 pi speed t))
    double vco_at(double t_ns) const { return ac_vco_mhz * (1 + ac_vco_depth * lfo(ac_vco_ph0 + 2 * M_PI * ac_vco_speed * (t_ns - ac_vco_t0) * 1e-9)); }
    double lfo(double ph) const {
        if (!ac_vco_shape) return std::sin(ph);
        const double drive = (13.5 * 5.1 / 15.1) / 5.0;           // 0.912
        double tri = (2 / M_PI) * std::asin(std::sin(ph));         // rising for cos(ph) > 0
        return -std::sin(drive * M_PI / 2 * tri) / std::sin(drive * M_PI / 2);
    }
public:
    // Live control (plugin): a new LFO speed from the current frame on, phase-continuous; the
    // depth (ac_vco_depth) can be written directly at any time. vco_phase() = the LFO phase now,
    // 0..2 pi (the panel's yellow LED lights once per cycle).
    void set_vco_speed(double hz) {
        if (hz == ac_vco_speed) return;
        ac_vco_ph0 = vco_phase(); ac_vco_t0 = ac_vf_start; ac_vco_speed = hz;
    }
    // the yellow rate LED: TR2 follows IC12A's square, high while the triangle rises (half of each
    // cycle); with the plain sine, the first quarter
    bool vco_led() const { double ph = vco_phase(); return ac_vco_shape ? std::cos(ph) > 0 : ph < 0.5 * M_PI; }
    double vco_phase() const {
        double ph = std::fmod(ac_vco_ph0 + 2 * M_PI * ac_vco_speed * (ac_vf_start - ac_vco_t0) * 1e-9, 2 * M_PI);
        return ph < 0 ? ph + 2 * M_PI : ph;
    }
private:
    double ac_vco_ph0 = 0, ac_vco_t0 = 0;

    // ---- optional analog / clock models (off by default). Filters from the netlists:
    //  input card DMX 1.3 (kicad/shared/input): three unity-gain Sallen-Key low-passes,
    //    IC5: R11+R12 5K84, R13+R14 21K4, C6 2n0 (feedback), C5 1n0 (ground) -> 10.07 kHz, Q 0.580
    //    IC6: R17+R18 17K83, R19+R20 59K56, C9 1n0, C8 100p              -> 15.44 kHz, Q 1.332
    //    IC7: R23+R24 5K45, R25+R26 12K0, C12 10n0, C11 100p             -> 19.68 kHz, Q 4.634
    //  DAC3 DMX 77.4 (kicad/shared/dac3): E10 -> DG303 E12 -> E9 track/hold (R39 = R36 = 2K2,
    //    C14 220p: tau 0.48 us, held while the switch is open) -> three Sallen-Keys
    //    E1: 6K62, 22K0, C2 2n0, C1 1n0 -> 9.33 kHz Q 0.596; E2: 18K32, 61K21, C4 1n0, C3 100p
    //    -> 15.03 kHz Q 1.332; E3: 5K62, 11K82, C6 10n0, C5 100p -> 19.53 kHz Q 4.673;
    //    then R14/R13 3K9 + C7 1n5 (54.4 kHz pole, the /2 made up by the level presets) and the
    //    balanced driver's E7B Sallen-Key R28 = R29 3K3, C12 2n0, C13 1n0 -> 34.1 kHz Q 0.707.
    //  Analog prototypes are bilinear-transformed (pre-warped at each pole) at 8x the nominal
    //  frame rate. The track/hold settles in 0.48 us << 25 us, so it is a zero-order hold whose
    //  steps fall at the output card's dump instant (each fine step sees the held value's average).
    static constexpr int K = 8;
    struct Bq { double b0, b1, b2, a1, a2, z1, z2; };
    static void bq_lp2(Bq* q, double f0, double Q, double fs) {
        double k = std::tan(M_PI * f0 / fs), n = 1 / (1 + k / Q + k * k);
        q->b0 = k * k * n; q->b1 = 2 * q->b0; q->b2 = q->b0;
        q->a1 = 2 * (k * k - 1) * n; q->a2 = (1 - k / Q + k * k) * n; q->z1 = q->z2 = 0;
    }
    // fw: the frequency the bilinear map is exact at (default fc; poles above the audio band use 15 kHz)
    static void bq_lp1(Bq* q, double fc, double fs, double fw = 0) {
        double k = fw > 0 ? std::tan(M_PI * fw / fs) * fc / fw : std::tan(M_PI * fc / fs);
        q->b0 = q->b1 = k / (1 + k); q->b2 = 0; q->a1 = (k - 1) / (1 + k); q->a2 = 0; q->z1 = q->z2 = 0;
    }
    static void bq_hp1(Bq* q, double fc, double fs) {
        double k = std::tan(M_PI * fc / fs);
        q->b0 = 1 / (1 + k); q->b1 = -q->b0; q->b2 = 0; q->a1 = (k - 1) / (1 + k); q->a2 = 0; q->z1 = q->z2 = 0;
    }
    // first-order H(s) = (n0 + n1 s) / (d0 + d1 s), normalised to H(0) = 1, plain bilinear at fs
    static void bq_1st(Bq* q, double n0, double n1, double d0, double d1, double fs) {
        double Kb = 2 * fs, g = d0 / n0, a0 = d0 + d1 * Kb;
        q->b0 = g * (n0 + n1 * Kb) / a0; q->b1 = g * (n0 - n1 * Kb) / a0; q->b2 = 0;
        q->a1 = (d0 - d1 * Kb) / a0; q->a2 = 0; q->z1 = q->z2 = 0;
    }
    static double bq(Bq* q, double x) {
        double y = q->b0 * x + q->z1;
        q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y;
        return y;
    }
    static double sk(double r1, double r2, double cf, double cg, double* Q) {   // f0, Q of a unity SK
        double m = std::sqrt(r1 * r2 * cf * cg); *Q = m / (cg * (r1 + r2)); return 1 / (2 * M_PI * m);
    }
    // A unity-gain Sallen-Key low-pass whose follower has a finite gain-bandwidth gbw (Hz; 0 = ideal):
    //   k(s) = 1 / (1 + s / wt),  H(s) = k / (1 + s (Cg (R1 + R2) + R1 Cf (1 - k)) + s^2 R1 R2 Cf Cg).
    // The biquad is fitted to that response on the fine grid (Steiglitz-McBride, 0 .. 0.4 fs), which
    // removes the bilinear transform's warping as well (checked against ngspice runs of the
    // netlists in sim/analog: notes/2026-11-11-0430-analog-spice.md). DC gain is exactly 1.
    static void bq_sk_fit(Bq* q, double r1, double r2, double cf, double cg, double gbw, double fs) {
        const int N = 400; double x[5] = {0, 0, 0, 0, 0}, a1 = 0, a2 = 0;
        { double Q, f0 = sk(r1, r2, cf, cg, &Q); bq_lp2(q, f0, Q, fs); a1 = q->a1; a2 = q->a2; }
        for (int it = 0; it < 6; it++) {
            double M[5][6] = {};
            for (int k = 0; k < N; k++) {
                double f = 0.4 * fs * (k + 0.5) / N, w = 2 * M_PI * f / fs;
                std::complex<double> s(0, 2 * M_PI * f), z1 = std::exp(std::complex<double>(0, -w)), z2 = z1 * z1;
                std::complex<double> kk = gbw > 0 ? 1.0 / (1.0 + s / (2 * M_PI * gbw)) : 1.0;
                std::complex<double> H = kk / (1.0 + s * (cg * (r1 + r2) + r1 * cf * (1.0 - kk)) + s * s * r1 * r2 * cf * cg);
                double wt = 1 / std::norm(1.0 + a1 * z1 + a2 * z2);          // Steiglitz-McBride weight
                if (f > 24000) wt *= 0.1;                                       // audio band first
                std::complex<double> row[5] = {1.0, z1, z2, -H * z1, -H * z2};
                for (int part = 0; part < 2; part++) {
                    double r[5], rhs = part ? H.imag() : H.real();
                    for (int j = 0; j < 5; j++) r[j] = part ? row[j].imag() : row[j].real();
                    for (int i = 0; i < 5; i++) { for (int j = 0; j < 5; j++) M[i][j] += wt * r[i] * r[j]; M[i][5] += wt * r[i] * rhs; }
                }
            }
            for (int c = 0; c < 5; c++) {                                       // Gauss-Jordan, partial pivot
                int pv = c; for (int i = c + 1; i < 5; i++) if (std::fabs(M[i][c]) > std::fabs(M[pv][c])) pv = i;
                for (int j = 0; j < 6; j++) std::swap(M[c][j], M[pv][j]);
                for (int i = 0; i < 5; i++) if (i != c) { double m = M[i][c] / M[c][c]; for (int j = c; j < 6; j++) M[i][j] -= m * M[c][j]; }
            }
            for (int i = 0; i < 5; i++) x[i] = M[i][5] / M[i][i];
            a1 = x[3]; a2 = x[4];
        }
        double g = (1 + a1 + a2) / (x[0] + x[1] + x[2]);
        q->b0 = x[0] * g; q->b1 = x[1] * g; q->b2 = x[2] * g; q->a1 = a1; q->a2 = a2; q->z1 = q->z2 = 0;
    }
    static constexpr int NIN = 7, NOUT = 6, NRG = 3;
    Bq ac_fin[2][NIN] = {}, ac_fout[2][NOUT] = {}, ac_frg[2][NRG] = {}, ac_ffb[2] = {};
    void filters_init() {
        double fi = K * ac_fs;
        for (int ch = 0; ch < 2; ch++) {
            // op-amp gain-bandwidth: input card IC5-7 probably NE5534 (47p between pins 5 and 8, about
            // 5 MHz); DAC3 E1-E3 LM310 followers (20 MHz); E7 NE5532 (10 MHz)
            bq_sk_fit(&ac_fin[ch][0], 5840, 21400, 2e-9, 1e-9, ac_gbw_in, fi);
            bq_sk_fit(&ac_fin[ch][1], 17830, 59560, 1e-9, 100e-12, ac_gbw_in, fi);
            bq_sk_fit(&ac_fin[ch][2], 5450, 12000, 10e-9, 100e-12, ac_gbw_in, fi);
            bq_sk_fit(&ac_fout[ch][0], 6620, 22000, 2e-9, 1e-9, 20e6, fi);
            bq_sk_fit(&ac_fout[ch][1], 18320, 61210, 1e-9, 100e-12, 20e6, fi);
            bq_sk_fit(&ac_fout[ch][2], 5620, 11820, 10e-9, 100e-12, 20e6, fi);
            bq_lp1(&ac_fout[ch][3], 1 / (2 * M_PI * 1950.0 * 1.5e-9), fi, 15e3);
            bq_sk_fit(&ac_fout[ch][4], 3300, 3300, 2e-9, 1e-9, 10e6, fi);
            // input coupling: input card C14 2u2 into R29 100K (0.72 Hz); sampler C2/C3 3u3 into 10K || 100K (5.30 Hz)
            bq_hp1(&ac_fin[ch][3], 1 / (2 * M_PI * 2.2e-6 * 100e3), fi);
            bq_hp1(&ac_fin[ch][4], 1 / (2 * M_PI * 3.3e-6 * (1 / (1 / 10e3 + 1 / 100e3))), fi);   // R13/R14 100K too: 5.30 Hz
            // input card IC8, non-inverting: G = 1 + P5 / (R31 + R30 || C15), R30 = R31 = 1K, C15 = 10n:
            // a treble shelf (+0.9 dB at 10 kHz, +2.1 dB at 18 kHz with P5 = 18K), gain normalised to 1
            { double r30 = 1e3, r31 = 1e3, t = r30 * 10e-9;
              bq_1st(&ac_fin[ch][5], r31 + r30 + ac_p5, t * (r31 + ac_p5), r31 + r30, t * r31, fi); }
            // sampler summer IC1C / IC2C (mod 15.22): C41 / C42 220p across R9 / R23 10K, a 72.3 kHz pole
            bq_lp1(&ac_fin[ch][6], 1 / (2 * M_PI * 10e3 * 220e-12), fi, 15e3);
            // the regen loop's way back in: the sampler's x7.02 pair (R2/R3 4K7, R1/R4 33K) has C1 / C4
            // 470p across R1 (10.3 kHz), then the same summer pole and the C2/C3 coupling (5.30 Hz).
            // The input card must be on the x1 pair (that pole would break the 18 kHz spec), so this
            // pair is the feedback's (notes/2026-11-11-0439-sampler.md). Gain normalised to 1 at DC.
            bq_lp1(&ac_frg[ch][0], 1 / (2 * M_PI * 33e3 * 470e-12), fi);
            bq_lp1(&ac_frg[ch][1], 1 / (2 * M_PI * 10e3 * 220e-12), fi, 15e3);
            bq_hp1(&ac_frg[ch][2], 1 / (2 * M_PI * 3.3e-6 * (1 / (1 / 10e3 + 1 / 100e3))), fi);
            // MB.2 sht 2's F/B FILTER high-pass (C23 0.01u into R28 2K0, buffered by IC19): 7.96 kHz.
            // The panel pot blends the plain and the high-passed loop signal (Machine::stream_in).
            bq_hp1(&ac_ffb[ch], FB_HP_HZ, fi);
            // output coupling: C8+C9 / C10+C11, 47u back to back = 23.5 uF, into R25/R27 20K || load/2
            { double r = 20e3; if (ac_load > 0) r = 1 / (1 / r + 2 / ac_load);
              bq_hp1(&ac_fout[ch][5], 1 / (2 * M_PI * 23.5e-6 * r), fi); }
        }
    }

    // ---- input: band-limited source (windowed sinc) on the fine grid -> input-card filter ----
    static constexpr int SW = 16;    // sinc half-width, source samples
    static constexpr int SP = 512;   // table phases
    struct SincTab { float t[SP + 1][2 * SW]; };
    static const SincTab& sinc_tab() {
        static const SincTab tab = [] {
            SincTab s{};
            const double fc = 0.47;  // cutoff, fraction of the source rate (just under Nyquist)
            for (int p = 0; p <= SP; p++) for (int k = 0; k < 2 * SW; k++) {
                double x = (k - SW + 1) - (double)p / SP;   // tap distance in source samples
                double v = x == 0 ? 2 * fc : std::sin(2 * M_PI * fc * x) / (M_PI * x);
                double u = x / SW, wdw = std::fabs(u) >= 1 ? 0 : 0.42 + 0.5 * std::cos(M_PI * u) + 0.08 * std::cos(2 * M_PI * u);
                s.t[p][k] = (float)(v * wdw);
            }
            return s;
        }();
        return tab;
    }
public:
    // With --analog: DAC3's output after its track/hold and filters (E1-E3 and the R13/R14 pole,
    // i.e. what leaves the card on AB7 / AB1), then the sampler's x7 feedback pair (10.3 kHz), summer
    // pole (72.3 kHz) and coupling (5.30 Hz), in sampler volts at the latest fine step, gain 1 at DC.
    // The panel's regeneration is taken from here (MB.2 sht 2: the IN PHASE/OUT switch picks AB7
    // or AB5 = -AB7, E5's inverter; notes/2026-11-18-*-mb2-analog-regen.md). Real volts on AB7 are
    // half of this (R14/R13); Machine::stream_in applies the panel and the x7 pair's gain.
    double dac_volts(int ch) const { return ac_o_ab7[ch & 1]; }
    // the same through the F/B FILTER's 7.96 kHz high-pass (C23 / R28, IC19)
    double dac_volts_hp(int ch) const { return ac_o_ab7hp[ch & 1]; }
    static constexpr double FB_HP_HZ = 1 / (2 * M_PI * 2000.0 * 0.01e-6);
    static constexpr int SINC_HALF_WIDTH = SW;   // look-ahead the analog input stage needs, source samples
private:
    static double wav_bl(const Source& w, int ch, double pos) {   // volts at source position pos
        const SincTab& st = sinc_tab();
        int64_t i = (int64_t)std::floor(pos); double fr = pos - i; int p = (int)(fr * SP); double a = fr * SP - p;
        double acc = 0;
        for (int k = 0; k < 2 * SW; k++) {
            int64_t j = i + k - SW + 1; if (j < 0 || j >= w.n) continue;
            double h = st.t[p][k] * (1 - a) + st.t[p + 1][k] * a;
            acc += h * w.volts(ch, j);
        }
        return acc;
    }
    int64_t ac_in_n[2] = {-1, -1}; double ac_in_y[2][2] = {};   // fine index done, y[n-1], y[n]
    double in_stage(const Source& w, int ch, double t) {   // volts at time t (s)
        if (!ac_analog) {            // VCO only: the original linear interpolation, at time t
            double pos = t * w.rate; int64_t i = (int64_t)std::floor(pos); double a = pos - i;
            if (i < 0 || i >= w.n) return 0;
            if (i == w.n - 1) return w.volts(ch, i);
            return w.volts(ch, i) * (1 - a) + w.volts(ch, i + 1) * a;
        }
        double fi = K * ac_fs, x = t * fi; int64_t need = (int64_t)std::ceil(x);
        if (need < 1) need = 1;
        while (ac_in_n[ch] < need) {
            int64_t n = ++ac_in_n[ch];
            double v = wav_bl(w, ch, n / fi * w.rate);
            for (int k = 0; k < NIN; k++) v = bq(&ac_fin[ch][k], v);
            ac_in_y[ch][0] = ac_in_y[ch][1]; ac_in_y[ch][1] = v;
        }
        double a = x - (need - 1); if (a < 0) a = 0;
        return ac_in_y[ch][0] * (1 - a) + ac_in_y[ch][1] * a;
    }

    // ---- output: words at the dump instants -> (track/hold + filters) -> uniform frame-rate samples ----
    double ac_o_ab7[2] = {};   // DAC3's filtered output (AB7, x2 for the R13/R14 divider) as it re-enters
                               // through the sampler's x7 pair, volts, gain 1 at DC
    double ac_o_ab7hp[2] = {}; // the same through the F/B FILTER high-pass
    double ac_o_h[2] = {}, ac_o_acc[2] = {}, ac_o_mark = 0, ac_o_tau0 = 0, ac_o_y[2][2] = {}, ac_o_prev_t = 0;
    int64_t ac_o_n = 0, ac_o_j = 1; int ac_o_started = 0;
    double o_T(int64_t j) const { return ac_t0 + (j - 1) * (double)ac_frame_ns + ac_ev[DUMP]; }   // nominal output instants
    void o_emit(int64_t j, const double* v) {
        if (j < 1 || j > ac_pcm_n || !emit) return;
        double x[2];
        for (int ch = 0; ch < 2; ch++) {
            double y = v[ch] * (ac_phase_out && ac_analog ? -32768.0 / 10.0 : 32768.0 / 10.0);
            x[ch] = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
        }
        emit(io, j, x);
    }
    void out_stage(double te, uint16_t a, uint16_t b) {
        double nv[2] = {word2units(a) * (10.0 / 16384), word2units(b) * (10.0 / 16384)};
        if (!ac_o_started) {         // first dump: start the clocks here
            ac_o_started = 1; ac_o_mark = ac_o_tau0 = ac_o_prev_t = te; ac_o_n = 0;
            for (int ch = 0; ch < 2; ch++) { ac_o_h[ch] = nv[ch]; ac_o_y[ch][0] = ac_o_y[ch][1] = 0; }
            if (!ac_analog) { while (ac_o_j <= ac_pcm_n && o_T(ac_o_j) <= te) o_emit(ac_o_j++, nv); }
            return;
        }
        if (!ac_analog) {            // VCO only: linear interpolation between dump instants
            while (ac_o_j <= ac_pcm_n && o_T(ac_o_j) <= te) {
                double f = (o_T(ac_o_j) - ac_o_prev_t) / (te - ac_o_prev_t), v[2];
                for (int ch = 0; ch < 2; ch++) v[ch] = ac_o_h[ch] + (nv[ch] - ac_o_h[ch]) * f;
                o_emit(ac_o_j++, v);
            }
            ac_o_prev_t = te; ac_o_h[0] = nv[0]; ac_o_h[1] = nv[1]; return;
        }
        double dt = 1e9 / (K * ac_fs);                       // fine step, ns
        for (;;) {
            double tau = ac_o_tau0 + (ac_o_n + 1) * dt;
            if (tau > te) break;
            for (int ch = 0; ch < 2; ch++) {
                double u = ac_o_acc[ch] + ac_o_h[ch] * (tau - ac_o_mark) / dt;
                for (int k = 0; k < NOUT; k++) {
                    u = bq(&ac_fout[ch][k], u);
                    if (k == 3) { double r = u; for (int j = 0; j < NRG; j++) r = bq(&ac_frg[ch][j], r); ac_o_ab7[ch] = r; ac_o_ab7hp[ch] = bq(&ac_ffb[ch], r); }
                }
                ac_o_y[ch][0] = ac_o_y[ch][1]; ac_o_y[ch][1] = u; ac_o_acc[ch] = 0;
            }
            ac_o_mark = tau; ac_o_n++;
            while (ac_o_j <= ac_pcm_n && o_T(ac_o_j) <= tau) {
                double f = 1 - (tau - o_T(ac_o_j)) / dt, v[2];
                if (f < 0) f = 0;
                for (int ch = 0; ch < 2; ch++) v[ch] = ac_o_y[ch][0] + (ac_o_y[ch][1] - ac_o_y[ch][0]) * f;
                o_emit(ac_o_j++, v);
            }
        }
        for (int ch = 0; ch < 2; ch++) { ac_o_acc[ch] += ac_o_h[ch] * (te - ac_o_mark) / dt; ac_o_h[ch] = nv[ch]; }
        ac_o_mark = te;
    }
};

}  // namespace dmx
