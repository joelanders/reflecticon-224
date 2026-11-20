// dmx::Machine: the whole AMS DMX 15-80 as one object. The processor board (DMX 23.5, an
// 8085 stepped one clock state at a time) with its two 8255s, the effects extender's ROMs and
// register files, the front panel (keys, UP/DOWN, display), the LOCK IN switch and the audio
// trigger, driving AudioCore (both pitch boards, delay RAM, converters). JUCE-free, no
// globals; the CLI front end is ../dmx.cpp and tests/run.py pins its output.
//
// Memory map (schematic survey, ../../README.md):
//   0000-07FF  DMX23-3 (2716, E13)                 processor E8 Y0
//   0800-0FFF  6116 battery RAM (E10)              processor E8 Y1
//   1000-1003  8255 E6 (front panel)               processor E8 Y2, mirrored over 2K
//   1800-1803  8255 E3 (PB = option links)         processor E8 Y3, mirrored over 2K
//   8000-97FF  DMX-10-SHL-406-1/2/3 (3x 2716)      extender, NAND(A15,/A14)
//
// Two ways to drive it:
//  - offline, as dmx.cpp does: schedule key presses and switch changes by time, then run();
//  - live (plugin, web demo): power_on(), then process() blocks of 40 kHz audio, with
//    key_down()/key_up(), set_lockin(), set_trigger() between blocks; display_text() reads
//    the panel. A snapshot is a copy: `auto s = std::make_unique<Machine>(m)`.
// Diagnostics (peripheral access log, PC histogram, ROM-read map, 8086 coverage) are on by
// default for the CLI and off with `diag = false`.
//
// Include with DMX_CHIPS_IMPL defined in exactly one translation unit (it carries the
// implementations of the vendored 8085, 8255 and 8086 cores).
#pragma once
#ifdef DMX_CHIPS_IMPL
#define CHIPS_IMPL
#define I8086_IMPL
#endif
#include "../vendor/i8085/i8085.h"
#include "../vendor/i8085/i8085dasm.h"
#include "../vendor/chips/i8255.h"
#include "audio_core.hpp"
#include "sha256.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dmx {

class Machine {
public:
    static constexpr uint32_t CPU_HZ = 3072000;   // 6.144 MHz crystal / 2

    // ---- configuration (before power_on) ----
    uint8_t opts = 0xFF;          // E3 PB: option-link header (pulled up; a link to 0V reads 0)
    // The firmware (858Ch) reads the delay RAM fit here: bit 0 / 1 = channel A / B card size (1 = 16K,
    // 0 = 4K), bits 7-6 / 5-4 = cards - 1 for A / B. Bit 2 = store init (0 clears all stores),
    // bit 3 = pitch boards (1 = two, 0 = one: channel B plays from board A's word 0).
    bool audio = false;           // run the pitch boards in AudioCore (else the legacy pitch_tick model)
    bool audio_trigger = false;   // ADC3 mod 14.53: channel A's green-LED one-shot pulls E3 PC1 low
    bool panel = false;           // E6 PB4-7 as wired on the display board (UP/DOWN switches, LED read-back)
    bool diag = true;             // access log, PC histogram, ROM-read map
    bool stop_wild = false, stop_romwrite = false, trace = false;
    double clk86 = 8e6, fs = 39062.5;   // legacy pitch_tick model only
    FILE* upload_log = nullptr; FILE* trace86 = nullptr; FILE* audio_log = nullptr;

    AudioCore ac;

    // ---- the CPU and its board ----
    i8085_t cpu; uint64_t pins = 0, ticks = 0;
    uint16_t addr = 0, cur_pc = 0, prev_pc = 0;
    bool romwrite_hit = false, written = false;
    uint8_t boot[2048] = {}, ext[3][2048] = {}, ram[2048] = {};
    i8255_t e6, e3;
    uint8_t xreg[2][10] = {};     // extender register files 9D00-9D09 (A), 9C80-9C89 (B), low nibbles

    // Pitch-change boards (DMX 9.8) for the run without --audio: an 8086 with 1K x 16 of RAM,
    // stepped against the 8085's clock (TEST from a free-running sync, IN 20h = in20). Wiring from
    // ../../kicad/pitch98/ANSWERS.md. With audio = true the boards are ac.AB instead.
    struct Pitch {
        i86_t c; uint8_t ram[2048]; bool reset, ever;
        uint64_t sample_seen;
        bool test;                   // IC15 flip-flop set: TEST low
        uint32_t iphits[2048];
        uint32_t syncs, lost, in20s, last_in20_cyc, max_loop; uint16_t max_loop_ip;
        uint16_t off670[4];          // pitch-board offset registers (OUT 20/24/22/26)
        uint16_t xlatch;             // extender E23/E24 (OUT 12h)
        uint32_t xfade_edges; bool xfade;
        struct { uint32_t n; uint16_t first, last, lo, hi; } port[256];
        uint32_t inn[256];
        uint16_t in20;
        Machine* m; int ch;
    } pb[2];

    // ---- front panel ----
    uint8_t seg_latch = 0, disp[256] = {}; bool disp_seen[256] = {};
    struct DispChange { double t; char s[12]; } dlog[512]; int ndlog = 0;
    struct Press { int col, row; uint64_t down, up; } presses[64]; int npress = 0;   // scheduled
    struct Switch { int pc; uint64_t down, up; } switches[16]; int nswitch = 0;      // scheduled (PC0 = LOCK IN, PC1 = trigger)
    uint8_t live_keys[6] = {};    // live: per row, bit c = column c held; rows 4/5 = DOWN/UP (col -1)
    bool live_lockin = false, live_trigger = false;

    // ---- diagnostics ----
    uint32_t pchits[65536]; uint8_t romrd[65536];
    uint16_t ring[64] = {}; unsigned ringpos = 0;
    struct LogEnt { uint16_t pc, addr; char kind; uint8_t first, last; uint32_t count; uint64_t tick; } logs[4096]; int nlogs = 0;

    Machine() {
        std::memset(pchits, 0, sizeof pchits); std::memset(romrd, 0, sizeof romrd);
        std::memset(pb, 0, sizeof pb); std::memset(&cpu, 0, sizeof cpu);
        std::memset(&e6, 0, sizeof e6); std::memset(&e3, 0, sizeof e3);
        std::memset(presses, 0, sizeof presses); std::memset(switches, 0, sizeof switches);
        std::memset(logs, 0, sizeof logs); std::memset(dlog, 0, sizeof dlog);
        rebind();
    }
    Machine(const Machine& o) { copy_from(o); }
    Machine& operator=(const Machine& o) { if (this != &o) copy_from(o); return *this; }

    // ---- set-up ----
    // chips: DMX23-3 (boot) and DMX-10-SHL-406-1/2/3 (extender), 2048 bytes each
    void load_roms(const uint8_t* boot_rom, const uint8_t* ext1, const uint8_t* ext2, const uint8_t* ext3) {
        std::memcpy(boot, boot_rom, 2048); std::memcpy(ext[0], ext1, 2048); std::memcpy(ext[1], ext2, 2048); std::memcpy(ext[2], ext3, 2048);
    }
    // The dump repairs (firmware/patches.txt), which hold no firmware bytes. '#' comments, then
    //   sha256 ROM BEFORE AFTER   the chip the repairs fit, and the chip they must give;
    //   ROM OFFSET MASK [kind]    hex offset and the bits to flip.
    // A ROM is repaired only if its SHA-256 is BEFORE, and the result must hash to AFTER. A ROM
    // that already hashes to AFTER (a good dump) is left alone. Returns the number of bytes
    // changed, or -1 with *err set (another chip, a mask with no sha256 line, a bad line).
    int apply_patches(const char* text, std::string* err) {
        auto fail = [&](const std::string& m) { if (err) *err = m; return -1; };
        auto rom = [&](unsigned r) { return r == 0 ? boot : ext[r - 1]; };
        auto hash = [&](unsigned r) { return Sha256::of(rom(r), 2048); };
        std::string before[4], after[4];
        struct Flip { unsigned r, off, mask; };
        std::vector<Flip> flips;
        const char* p = text;
        while (*p) {
            const char* e = std::strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : std::strlen(p);
            char line[256]; size_t k = len < sizeof line - 1 ? len : sizeof line - 1; std::memcpy(line, p, k); line[k] = 0;
            p += len + (e ? 1 : 0);
            unsigned r, off, mask; char hb[80], ha[80];
            if (line[0] == '#' || line[0] == 0 || line[0] == '\r') continue;
            if (std::sscanf(line, "sha256 %u %79s %79s", &r, hb, ha) == 3) {
                if (r > 3) return fail("bad ROM number in a sha256 line");
                before[r] = hb; after[r] = ha;
            } else if (std::sscanf(line, "%u %x %x", &r, &off, &mask) == 3) {
                if (r > 3 || off > 0x7FF || mask == 0 || mask > 0xFF) return fail(std::string("bad repair line: ") + line);
                flips.push_back({r, off, mask});
            } else {
                return fail(std::string("bad repair line: ") + line);
            }
        }
        static const char* names[4] = {"DMX23-3", "DMX-10-SHL-406-1", "DMX-10-SHL-406-2", "DMX-10-SHL-406-3"};
        bool todo[4] = {};
        for (auto& f : flips) {
            if (before[f.r].empty()) return fail(std::string("repairs to ") + names[f.r] + " have no sha256 line");
            todo[f.r] = true;
        }
        for (unsigned r = 0; r < 4; r++) {
            if (!todo[r]) continue;
            std::string h = hash(r);
            if (h == after[r]) todo[r] = false;   // already the repaired chip
            else if (h != before[r]) return fail(std::string(names[r]) + " is not the dump the repairs were made for");
        }
        int n = 0;
        for (auto& f : flips) if (todo[f.r]) { rom(f.r)[f.off] ^= (uint8_t)f.mask; n++; }
        for (unsigned r = 0; r < 4; r++)
            if (todo[r] && hash(r) != after[r]) return fail(std::string(names[r]) + ": the repairs did not give the expected chip");
        return n;
    }
    // The delay RAM fit the option links describe, applied to the audio core: each channel's
    // RAM = card size x cards (858Ch decode). Returns false for fits the model cannot run
    // (three cards, or more than 64K, per channel).
    bool wrap_from_opts() {
        unsigned na = ((opts >> 6) & 3) + 1, nb = ((opts >> 4) & 3) + 1;
        unsigned wa = (opts & 1 ? 16384u : 4096u) * na, wb = (opts & 2 ? 16384u : 4096u) * nb;
        if (na == 3 || nb == 3 || wa > 65536 || wb > 65536) return false;
        ac.ac_wrap_mask = wa - 1; ac.ac_wrap_mask_b = wb == wa ? 0 : wb - 1;
        return true;
    }
    // Audio set-up (audio = true): delay RAM silent, boards held, single-pitch routing from opts.
    // Call after set_mclk / the options on ac and before power_on; n_out as AudioCore::options_apply.
    void audio_init(int64_t n_out) {
        ac.ac_single_pitch = !(opts & 8);   // option link bit 3 = 0: one pitch board (the same link as MCB4 L2)
        for (int ch = 0; ch < 2; ch++) { for (int k = 0; k < 65536; k++) ac.ac_ram[ch][k] = AudioCore::adc(0); ac.board_init(&ac.AB[ch]); }
        ac.ac_fr = 0; ac.ac_e = 0;
        ac.options_apply(n_out, true);
    }
    void power_on() {
        std::memset(ram, 0, sizeof ram);
        i8255_init(&e6); i8255_init(&e3);
        pins = i8085_init(&cpu);
        ticks = 0;
        rebind();
    }

    // ---- offline scheduling (dmx.cpp) ----
    // key code 0-F as the firmware's table at 92FBh gives it (* = F, # = D); -1 if not found
    bool key_pos(int code, int* col, int* row) const {
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
            if (ext[2][0x2FB + 4 * r + 3 - c] == code) { *col = c; *row = r; return true; }
        return false;
    }
    bool schedule_press(int col, int row, double at, double dur) {
        if (npress >= 64) return false;
        presses[npress++] = Press{col, row, (uint64_t)(at * CPU_HZ), (uint64_t)((at + dur) * CPU_HZ)};
        return true;
    }
    bool schedule_switch(int pc, double at, double dur) {
        if (nswitch >= 16) return false;
        switches[nswitch++] = Switch{pc, (uint64_t)(at * CPU_HZ), (uint64_t)((at + dur) * CPU_HZ)};
        return true;
    }

    // ---- live controls ----
    // code 0-F as printed on the keys (A B C D=# E F=*); UP = 0x10, DOWN = 0x11 (turns panel on)
    void key_down(int code) { set_key(code, true); }
    void key_up(int code) { set_key(code, false); }
    void set_lockin(bool on) { live_lockin = on; }
    void set_trigger(bool low) { live_trigger = low; }

    // ---- running ----
    // Run to `end_tick` 8085 states. Returns nullptr at the time limit, else why it stopped
    // early (only with stop_wild / stop_romwrite, or a halt / unknown opcode).
    const char* run(uint64_t end_tick) {
        while (ticks < end_tick) {
            const char* why = tick();
            if (why) return why;
            ticks++;
        }
        return nullptr;
    }

    // Live audio at the frame rate (40 kHz with set_mclk(10.24)): n input samples per channel
    // (+-1.0 = +-10 V at the sampler) in, n output samples per channel (+-1.0 = +-10 V) out.
    // The output lags the input by latency() frames: 0 on the plain path (frame fr samples input
    // fr - 1 and dumps output fr - 1), the input stage's sinc look-ahead with --analog, and with
    // the VCO a margin for the output stage, which emits frame j only at the first dump past its
    // real time (one VCO frame, up to 1.2 crystal frames, late; the input is read at real time).
    // Requires audio = true and stream_begin() after audio_init/power_on. n <= STREAM_MAX.
    static constexpr int STREAM_RING = 1 << 15, STREAM_MAX = 4096;
    int latency() const { return ac.ac_analog ? AudioCore::SINC_HALF_WIDTH + 2 + (ac.ac_vco ? 2 : 0) : ac.ac_vco ? 3 : 0; }
    void stream_begin() {
        std::memset(s_in, 0, sizeof s_in); std::memset(s_out, 0, sizeof s_out);
        s_src = Source{}; s_src.ring[0] = s_in[0]; s_src.ring[1] = s_in[1]; s_src.mask = STREAM_RING - 1;
        s_src.rate = (int)std::lround(ac.ac_fs); s_src.n = 0; s_out_n = latency(); s_out_read = 0;
        ac.io = this; ac.sample_in = stream_in; ac.frame_out = stream_frame_out; ac.emit = stream_emit;
        ac.ac_pcm_n = INT64_MAX;
        streaming = true;
    }
    void process(const float* in_a, const float* in_b, float* out_a, float* out_b, int n) {
        if (!streaming || !audio) {   // no audio core running: nothing would ever reach s_out
            for (int i = 0; i < n; i++) out_a[i] = out_b[i] = 0.f;
            return;
        }
        for (int i = 0; i < n; i++) {
            int64_t j = s_src.n + i;
            s_in[0][j & (STREAM_RING - 1)] = in_a ? in_a[i] : 0.f;
            s_in[1][j & (STREAM_RING - 1)] = in_b ? in_b[i] : (in_a ? in_a[i] : 0.f);
        }
        s_src.n += n;
        int64_t want = s_out_read + n;
        while (s_out_n < want) { for (int k = 0; k < 16; k++) { tick(); ticks++; } }
        for (int i = 0; i < n; i++) {
            int64_t j = (s_out_read + i) & (STREAM_RING - 1);
            out_a[i] = s_out[0][j]; out_b[i] = s_out[1][j];
        }
        s_out_read += n;
    }

    // ---- the panel's analog front end (streaming only) ----
    // Input level (gain before the sampler) and regeneration per channel. The regen loop as drawn
    // on MB.2 sht 2 (notes/2026-11-18-*-mb2-analog-regen.md), channel A:
    //   DAC3 AB7 (+sample/2) or AB5 (-sample/2), picked by the IN PHASE/OUT switch
    //   -> F/B FILTER, 10K log: blends that signal (one end) with its 7.96 kHz high-pass (C23 0.01u,
    //      R28 2K0, IC19; the other end); wiper into IC18's follower
    //   -> F/B GAIN, 10K log to 0V; wiper -> sampler EC2 AA1 = R2 4K7 into IC1A's virtual earth, R1 33K
    //      (x7.02 inverting, C1 470p: 10.3 kHz), then the summer (inverting) -> ADC.
    // In phase is positive feedback. Full F/B GAIN is a loop gain of 3.51 at DC. The drawn (mono)
    // 15-80 has no channel-B loop; B gets the same circuit (the stereo sampler has a x7 pair for B).
    //   set_fb_gain(ch, pos)    F/B GAIN rotation 0..1 (log law, loaded by the 4K7)
    //   set_fb_filter(ch, pos)  F/B FILTER rotation 0..1 (0 = flat, 1 = all high-pass)
    //   set_fb_phase(ch, anti)  IN PHASE/OUT at OUT: the loop (and channel A's output) inverted
    //   set_regen(ch, g)        the DC loop gain directly (signed sampler volts per sampler volt),
    //                           bypassing the pot law; what the plugin's Regen % knob sets
    // The x7 stage swings at most +-13.5 V (its rails), the ADC clips at +-10 V, so a runaway loop
    // is held by the ADC; the stage's own clip only shows when the input partly cancels the feedback.
    void set_input_gain(int ch, float g) { s_gain[ch & 1] = g; }
    void set_regen(int ch, float r) { s_regen[ch & 1] = r; }
    void set_fb_gain(int ch, float pos) { s_regen[ch & 1] = (float)fb_gain_law(pos); }
    void set_fb_filter(int ch, float pos) { s_fbt[ch & 1] = (float)log_taper(pos); }
    void set_fb_phase(int ch, bool antiphase) { s_fbanti[ch & 1] = antiphase; }
    // A "LOG" pot's track fraction from its CCW end at rotation x (0..1): exponential, 10 % at
    // half travel (the usual A taper; the sheet says only "LOG").
    static double log_taper(double x) {
        x = x < 0 ? 0 : x > 1 ? 1 : x;
        return (std::pow(81.0, x) - 1) / 80.0;
    }
    // DC loop gain at F/B GAIN rotation x: wiper fraction p, Thevenin p and 10K p (1 - p) into the
    // 4K7 at the virtual earth, times R1 33K, times DAC3's /2.
    static double fb_gain_law(double x) {
        double p = log_taper(x);
        return 0.5 * 33e3 / (4.7e3 + 10e3 * p * (1 - p)) * p;
    }
    static constexpr double X7_SWING = 13.5;   // sampler IC1A / IC2A output limit, volts
    // the largest |input| at the sampler since the last call (1.0 = 10 V = clipping), then reset
    float take_peak(int ch) { float p = s_peak[ch & 1]; s_peak[ch & 1] = 0.f; return p; }
    // The channel LEDs under the display (E6 PC6/5/4 = a/b/c, open collector: 0 = lit).
    // bit 0 = a, bit 1 = b, bit 2 = c.
    unsigned channel_leds() const { uint8_t c = e6.pc.outp; return (c & 0x40 ? 0 : 1) | (c & 0x20 ? 0 : 2) | (c & 0x10 ? 0 : 4); }

    // ---- reading the panel ----
    // The four digits left to right, '.' after a digit whose point is lit (track D's decode:
    // E1 = PA0 = FE leftmost; glyphs from the firmware's table at 92DFh).
    void display_text(char* out) const {
        static const int cols[4] = {0x0E, 0x0D, 0x0B, 0x07};
        for (int i = 0; i < 4; i++) {
            uint8_t on = disp_seen[cols[i]] ? (uint8_t)~disp[cols[i]] : 0;
            *out++ = seg2ch(on); if (on & 0x80) *out++ = '.';
        }
        *out = 0;
    }
    uint8_t digit_segments(int i) const {   // digit 0..3 left to right: a=b0 c=b1 f=b2 e=b3 d=b4 b=b5 g=b6 dp=b7, 1 = lit
        static const int cols[4] = {0x0E, 0x0D, 0x0B, 0x07};
        return disp_seen[cols[i]] ? (uint8_t)~disp[cols[i]] : 0;
    }
    double seconds() const { return ticks / (double)CPU_HZ; }
    int64_t host_ns() const { return (int64_t)((double)ticks * 1e9 / CPU_HZ); }

private:
    // streaming state
    float s_in[2][STREAM_RING], s_out[2][STREAM_RING];
    Source s_src; int64_t s_out_n = 0, s_out_read = 0; bool streaming = false;
    static uint16_t stream_in(void* io, int ch, int64_t fr) {
        Machine* m = static_cast<Machine*>(io);
        double v = m->ac.input_volts(m->s_src, ch, fr);
        double fb, hp;
        if (m->ac.ac_analog) {
            // the regen loop as wired: DAC3 out (after its hold and filters) -> panel -> the sampler's
            // x7 pair, its 10.3 kHz pole and the summer's coupling (all in dac_volts). It skips the
            // input card.
            fb = m->ac.dac_volts(ch); hp = m->ac.dac_volts_hp(ch);
        } else {
            fb = AudioCore::word2units(ch ? m->ac.ac_out_b : m->ac.ac_out_a) * (10.0 / 16384);
            // the F/B FILTER high-pass at the frame rate (one-pole bilinear, warped to 7.96 kHz)
            double k = std::tan(M_PI * AudioCore::FB_HP_HZ / m->ac.ac_fs), b0 = 1 / (1 + k), a1 = (k - 1) / (1 + k);
            hp = b0 * (fb - m->s_hpx[ch]) - a1 * m->s_hpy[ch];
            m->s_hpx[ch] = fb; m->s_hpy[ch] = hp;
        }
        if (m->s_gain[ch] != 1.0f || m->s_regen[ch] != 0.0f) {   // default: exactly the offline input
            double t = m->s_fbt[ch], x = (1 - t) * fb + t * hp;    // F/B FILTER wiper
            double r = m->s_regen[ch] * (m->s_fbanti[ch] ? -x : x);  // x7 stage out, as sampler volts
            r = r > X7_SWING ? X7_SWING : r < -X7_SWING ? -X7_SWING : r;
            v = v * m->s_gain[ch] + r;
        }
        float a = (float)std::fabs(v) * 0.1f;
        if (a > m->s_peak[ch]) m->s_peak[ch] = a;
        return AudioCore::adc(v);
    }
    float s_gain[2] = {1.f, 1.f}, s_regen[2] = {0.f, 0.f}, s_peak[2] = {0.f, 0.f}, s_fbt[2] = {0.f, 0.f};
    bool s_fbanti[2] = {false, false}; double s_hpx[2] = {0, 0}, s_hpy[2] = {0, 0};
    static void stream_frame_out(void* io, int64_t fr, uint16_t a, uint16_t b) {
        Machine* m = static_cast<Machine*>(io); (void)fr;
        int64_t j = m->s_out_n++ & (STREAM_RING - 1);
        m->s_out[0][j] = AudioCore::word2units(a) / 16384.f; m->s_out[1][j] = AudioCore::word2units(b) / 16384.f;
    }
    static void stream_emit(void* io, int64_t jj, const double* x) {
        Machine* m = static_cast<Machine*>(io); (void)jj;
        int64_t j = m->s_out_n++ & (STREAM_RING - 1);
        m->s_out[0][j] = (float)(x[0] / 32768.0); m->s_out[1][j] = (float)(x[1] / 32768.0);
    }

    void copy_from(const Machine& o) {
        // every member is plain data (AudioCore included); copy the bytes, then point the
        // internal hooks at this instance
        std::memcpy(static_cast<void*>(this), static_cast<const void*>(&o), sizeof *this);
        rebind();
        if (streaming) { s_src.ring[0] = s_in[0]; s_src.ring[1] = s_in[1]; ac.io = this; }
    }
    void rebind() {
        for (int ch = 0; ch < 2; ch++) {
            pb[ch].m = this; pb[ch].ch = ch;
            pb[ch].c.user = &pb[ch]; pb[ch].c.rd = p86_rd; pb[ch].c.wr = p86_wr; pb[ch].c.in = p86_in; pb[ch].c.out = p86_out;
        }
        ac.rebind();
    }

    void set_key(int code, bool down) {
        int col, row;
        if (code == 0x10 || code == 0x11) { panel = true; row = code == 0x10 ? 5 : 4; if (down) live_keys[row] |= 1; else live_keys[row] &= (uint8_t)~1; return; }
        if (!key_pos(code, &col, &row)) return;
        if (down) live_keys[row] |= (uint8_t)(1u << col); else live_keys[row] &= (uint8_t)~(1u << col);
    }

    // one 8085 state (the body of the CLI's loop); returns a stop reason or nullptr
    const char* tick() {
        if (i8085_opdone(&cpu)) {
            prev_pc = cur_pc; cur_pc = cpu.pc;
            if (diag) { pchits[cur_pc]++; ring[ringpos++ & 63] = cur_pc; }
            bool inrom = cur_pc < 0x0800 || (cur_pc >= 0x8000 && cur_pc < 0x9800);
            if (stop_wild && !inrom) { std::printf("left ROM: %04X -> %04X  SP=%04X\n", prev_pc, cur_pc, cpu.sp); return "left ROM"; }
        }
        pins = i8085_tick(&cpu, (pins & ~(I8085_TRAP | I8085_RST75 | I8085_RST65 | I8085_RST55)) | I8085_READY);
        if (cpu.unimplemented) return "unimplemented opcode";
        if (pins & I8085_ALE) { addr = I8085_GET_ADDR(pins); written = false; }
        if ((pins & I8085_RD) && !(pins & I8085_IOM)) I8085_SET_AD(pins, rd(addr));
        if ((pins & I8085_WR) && !written && !(pins & I8085_IOM)) { written = true; wr(addr, I8085_GET_AD(pins)); }
        if ((pins & (I8085_RD | I8085_WR)) && (pins & I8085_IOM)) logacc((pins & I8085_RD) ? 'i' : 'o', addr & 0xFF, I8085_GET_AD(pins));
        if (i8085_halted(&cpu) && !cpu.inte) return "halted with interrupts off";
        if (stop_romwrite && romwrite_hit) return "write to ROM";
        if ((ticks & 15) == 0) { if (audio) { ac.ac_lockin = lockin_now(); ac.run_until(host_ns()); } else { pitch_tick(0); pitch_tick(1); } }
        if (stop_wild && (pb[0].c.state == I86_TRAP || pb[1].c.state == I86_TRAP)) return "8086 trap";
        return nullptr;
    }

    // ---- legacy pitch boards (no audio) ----
    static uint8_t p86_rd(void* u, uint32_t a) { return static_cast<Pitch*>(u)->ram[a & 0x7FF]; }
    static void p86_wr(void* u, uint32_t a, uint8_t d) { static_cast<Pitch*>(u)->ram[a & 0x7FF] = d; }
    static uint16_t p86_in(void* u, uint16_t port, int w) {
        Pitch* p = static_cast<Pitch*>(u); p->inn[port & 0xFF]++;
        (void)w;
        if (port & 0x20) {           // IC14.11 = NAND(IOR, LA5): 25LS22 /OE and IC15 CLR
            if (p->in20s++) {
                uint32_t loop = p->c.cycles - p->last_in20_cyc;
                if (loop > p->max_loop) { p->max_loop = loop; p->max_loop_ip = p->c.ip; }
            }
            p->last_in20_cyc = p->c.cycles;
            p->test = false;
            return p->in20;
        }
        return 0xFFFF;               // nothing else drives the bus on a read
    }
    static void p86_out(void* u, uint16_t port, uint16_t v, int w) {
        Pitch* p = static_cast<Pitch*>(u); int k = port & 0xFF, ch = p->ch;
        if (!w) v &= 0xFF;
        if (!p->port[k].n) { p->port[k].first = v; p->port[k].lo = p->port[k].hi = v; }
        if (v < p->port[k].lo) p->port[k].lo = v;
        if (v > p->port[k].hi) p->port[k].hi = v;
        p->port[k].last = v; p->port[k].n++;
        if (k & 0x20) p->off670[(k >> 1 & 1) << 1 | (k >> 2 & 1)] = v;      // GW = NAND(IOW, LA5)
        else if ((k & 0x3E) == 0x12) {   // extender E17: A1=1, A4=1, A2=A3=A5=0
            p->xlatch = v;
            bool x = ch == 0 ? (v >> 1) & 1 : (v >> 9) & 1;
            if (x != p->xfade) p->xfade_edges++;
            p->xfade = x;
        }
        if (p->m->trace86) std::fprintf(p->m->trace86, "%c %.6f %02X %04X ip=%04X\n", 'A' + ch, p->m->ticks / 3072000.0, k, v, p->c.ip);
    }
    void pitch_tick(int ch) {
        Pitch* p = &pb[ch];
        if (!p->ever || p->reset) return;
        i86_t* c = &p->c;
        if (c->state == I86_HALT || c->state == I86_TRAP) return;
        uint64_t target = (uint64_t)((double)ticks * clk86 / 3072000.0);
        uint64_t sample = (uint64_t)((double)ticks * fs / 3072000.0);
        while (c->cycles < target) {
            // the /SYNC edge for each sample period the 8086 has reached
            uint64_t now = (uint64_t)((double)c->cycles * fs / clk86);
            if (now > sample) now = sample;
            if (now != p->sample_seen) {
                p->sample_seen = now; p->syncs++;
                if (p->test) p->lost++;
                p->test = true;
            }
            if (c->state == I86_WAIT && !p->test) {
                // sleep to the next sync edge (or to the end of this slice)
                uint64_t next = (uint64_t)((double)(p->sample_seen + 1) * clk86 / fs) + 1;
                c->cycles = (uint32_t)(next < target ? next : target);
                continue;
            }
            uint16_t ip0 = c->ip;
            if (c->state != I86_WAIT) p->iphits[(((uint32_t)c->s[CS] << 4) + ip0) & 0x7FF]++;
            int st = i86_step(c, p->test);
            if (st == I86_HALT || st == I86_TRAP) break;
        }
    }

    // ---- extender register files ----
    void audio_xreg(int ch, int r, uint8_t d) {
        if (r == 8) {
            unsigned hl = xreg[ch][0] | xreg[ch][1] << 4 | xreg[ch][2] << 8 | xreg[ch][3] << 12;
            unsigned de = xreg[ch][4] | xreg[ch][5] << 4 | xreg[ch][6] << 8 | xreg[ch][7] << 12;
            ac.AB[ch].ram[hl & 0x7FE] = de & 0xFF; ac.AB[ch].ram[(hl & 0x7FE) + 1] = de >> 8;
            if (audio_log && ac.AB[ch].resets) std::fprintf(audio_log, "%.6f %c W %03X %04X frame %lld\n", ticks / (double)CPU_HZ, 'A' + ch, hl & 0x7FE, de, (long long)ac.ac_fr);
        }
        if (r == 9) {
            if (!(d & 1)) { if (!ac.AB[ch].held) ac.hold(&ac.AB[ch]); }
            else if (ac.AB[ch].held) {
                ac.release(&ac.AB[ch], host_ns());
                if (audio_log) std::fprintf(audio_log, "%.6f %c RESET released, frame %lld, 1FC=%04X\n", ticks / (double)CPU_HZ, 'A' + ch, (long long)ac.ac_fr,
                                            ac.AB[ch].ram[0x1FC] | ac.AB[ch].ram[0x1FD] << 8);
            }
        }
    }
    void xreg_write(int ch, int r, uint8_t d) {
        if (audio) {                 // bring the boards up to now, then apply the write
            ac.run_until(host_ns());
            xreg[ch][r] = d & 0x0F;
            audio_xreg(ch, r, d);
        }
        xreg[ch][r] = d & 0x0F;
        if (r == 8) {
            unsigned hl = xreg[ch][0] | xreg[ch][1] << 4 | xreg[ch][2] << 8 | xreg[ch][3] << 12;
            unsigned de = xreg[ch][4] | xreg[ch][5] << 4 | xreg[ch][6] << 8 | xreg[ch][7] << 12;
            pb[ch].ram[hl & 0x7FE] = de & 0xFF; pb[ch].ram[(hl & 0x7FE) + 1] = de >> 8;
        }
        if (r == 9) {
            bool rel = (d & 1) && pb[ch].reset;
            pb[ch].reset = !(d & 1);
            if (rel || (!pb[ch].ever && (d & 1))) {
                pb[ch].ever = true; i86_reset(&pb[ch].c);
                pb[ch].c.cycles = (uint32_t)((double)ticks * clk86 / 3072000.0);
                pb[ch].sample_seen = (uint64_t)((double)ticks * fs / 3072000.0); pb[ch].test = false;
                pb[ch].in20s = 0;
            }
        }
        if (r == 8 && upload_log) {
            unsigned hl = xreg[ch][0] | xreg[ch][1] << 4 | xreg[ch][2] << 8 | xreg[ch][3] << 12;
            unsigned de = xreg[ch][4] | xreg[ch][5] << 4 | xreg[ch][6] << 8 | xreg[ch][7] << 12;
            std::fprintf(upload_log, "%c %04X %04X %llu\n", 'A' + ch, hl, de, (unsigned long long)ticks);
        }
        if (r == 9 && upload_log) std::fprintf(upload_log, "%c strobe %X %llu\n", 'A' + ch, d & 0x0F, (unsigned long long)ticks);
    }

    // ---- panel ----
    static char seg2ch(uint8_t on) {
        static const struct { uint8_t m; char c; } g[] = {
            {0x3F,'0'},{0x22,'1'},{0x79,'2'},{0x73,'3'},{0x66,'4'},{0x57,'5'},{0x5F,'6'},{0x23,'7'},{0x7F,'8'},{0x77,'9'},
            {0x5D,'E'},{0x5C,'t'},{0x48,'r'},{0x6F,'A'},{0x1C,'L'},{0x5E,'b'},{0x6E,'H'},{0x5A,'o'},{0x02,'i'},
            {0x58,'c'},{0x40,'-'},{0x1A,'u'},{0x4A,'n'},{0x15,'C'},{0x00,' '}};
        for (unsigned i = 0; i < sizeof g / sizeof g[0]; i++) if (g[i].m == (on & 0x7F)) return g[i].c;
        return '?';
    }
    void disp_logchange(double t) {  // changes within 10 ms are one change (digits refresh one at a time)
        char s[12]; display_text(s);
        if (ndlog && !std::strcmp(dlog[ndlog - 1].s, s)) return;
        if (ndlog && t - dlog[ndlog - 1].t < 0.010) { std::strcpy(dlog[ndlog - 1].s, s); if (ndlog > 1 && !std::strcmp(dlog[ndlog - 2].s, s)) ndlog--; return; }
        if (ndlog < 512) { dlog[ndlog].t = t; std::strcpy(dlog[ndlog].s, s); ndlog++; }
    }
    // Keys: E6 PA bits 4-7 strobe a column low; a pressed key pulls its row (E6 PB bit) low.
    // With panel: PB4 = DOWN, PB5 = UP (1 = pressed), PB6/PB7 = E3 PC5/PC6 read back. Without it
    // PB4-7 read 1, which the firmware takes as "UP and DOWN both held" (9159h).
    uint8_t key_rows() {
        uint8_t v = 0xFF, strobe = e6.pa.outp;
        if (panel) v = (uint8_t)(0x0F | ((e3.pc.outp >> 5 & 1) << 6) | ((e3.pc.outp >> 6 & 1) << 7));
        for (int i = 0; i < npress; i++) {
            if (ticks < presses[i].down || ticks >= presses[i].up) continue;
            if (presses[i].col < 0) v |= (uint8_t)(1u << presses[i].row);
            else if (!((strobe >> (4 + presses[i].col)) & 1)) v &= (uint8_t)~(1u << presses[i].row);
        }
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
            if ((live_keys[r] >> c & 1) && !((strobe >> (4 + c)) & 1)) v &= (uint8_t)~(1u << r);
        if (live_keys[4] & 1) v |= 1u << 4;
        if (live_keys[5] & 1) v |= 1u << 5;
        return v;
    }
    int lockin_now() const {         // the LOCK IN switch also write-inhibits both delay lines
        if (live_lockin) return 1;
        for (int i = 0; i < nswitch; i++)
            if (!switches[i].pc && ticks >= switches[i].down && ticks < switches[i].up) return 1;
        return 0;
    }
    void logacc(char kind, uint16_t a, uint8_t d) {
        if (trace) std::printf("%10llu  pc=%04X  %c %04X %02X\n", (unsigned long long)ticks, cur_pc, kind, a, d);
        if (!diag) return;
        for (int i = 0; i < nlogs; i++)
            if (logs[i].pc == cur_pc && logs[i].addr == a && logs[i].kind == kind) { logs[i].count++; logs[i].last = d; return; }
        if (nlogs < 4096) logs[nlogs++] = LogEnt{cur_pc, a, kind, d, d, 1, ticks};
    }
    uint8_t ppi(i8255_t* p, uint16_t a, bool wrt, uint8_t d) {
        uint64_t q = I8255_CS | (wrt ? I8255_WR : I8255_RD) | I8255_PORT_PINS;
        if (a & 1) q |= I8255_A0;
        if (a & 2) q |= I8255_A1;
        I8255_SET_DATA(q, d);
        if (!wrt) I8255_SET_PB(q, p == &e3 ? opts : key_rows());
        if (!wrt && p == &e3) {      // E3 PC0 / PC1 inputs (fingers B5 / B6): LOCK IN switch, audio trigger; 0 = active
            for (int i = 0; i < nswitch; i++)
                if (ticks >= switches[i].down && ticks < switches[i].up) q &= ~(switches[i].pc ? I8255_PC1 : I8255_PC0);
            if (live_lockin) q &= ~I8255_PC0;
            if (live_trigger) q &= ~I8255_PC1;
            // audio_trigger: ADC3 mod 14.53 + processor mod 23.22. Channel A's green-LED window (range code != 3)
            // fires an LS123 one-shot (68K, 1 uF: about 31 ms, retriggerable) whose output goes low on PC1.
            if (audio && audio_trigger && ac.ac_fr - ac.ac_loud_fr[0] < (int64_t)(0.0306 * ac.ac_fs)) q &= ~I8255_PC1;
        }
        q = i8255_tick(p, q);
        return I8255_GET_DATA(q);
    }
    uint8_t rd(uint16_t a) {
        if (diag) romrd[a] = 1;
        if (a < 0x0800) return boot[a];
        if (a < 0x1000) return ram[a & 0x7FF];
        if (a < 0x1800) { uint8_t d = ppi(&e6, a, false, 0); logacc('r', 0x1000 | (a & 3), d); return d; }
        if (a < 0x2000) { uint8_t d = ppi(&e3, a, false, 0); logacc('r', 0x1800 | (a & 3), d); return d; }
        if (a >= 0x8000 && a < 0x9800) return ext[(a - 0x8000) >> 11][a & 0x7FF];
        logacc('?', a, 0xFF);
        return 0xFF;
    }
    void wr(uint16_t a, uint8_t d) {
        if (a >= 0x0800 && a < 0x1000) { ram[a & 0x7FF] = d; return; }
        if (a >= 0x1000 && a < 0x1800) {
            ppi(&e6, a, true, d); logacc('w', 0x1000 | (a & 3), d);
            if ((a & 3) == 0 && d != 0xFF && cur_pc == 0x924C) { disp[d & 0x0F] = seg_latch; disp_seen[d & 0x0F] = true; if (panel) disp_logchange(ticks / (double)CPU_HZ); }   // column select latches the segments
            return;
        }
        if (a >= 0x1800 && a < 0x2000) { ppi(&e3, a, true, d); logacc('w', 0x1800 | (a & 3), d); if ((a & 3) == 0) seg_latch = d; return; }
        if (a >= 0x9D00 && a <= 0x9D09) xreg_write(0, a - 0x9D00, d);
        if (a >= 0x9C80 && a <= 0x9C89) xreg_write(1, a - 0x9C80, d);
        logacc('!', a, d);
        if (a >= 0x8000 && a < 0x9800) romwrite_hit = true;
    }
};

}  // namespace dmx
