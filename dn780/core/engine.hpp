// dn780::Engine: the DN780 as a streaming effect at the host's rate, with live parameter changes.
// JUCE-free, so tests and a WASM build can drive it the same way a plugin does. From the dn780
// project's emu/core/engine.hpp (SOURCE.md), on the kit's rate bridge and mailbox.
//
// Threads:
// - control thread (UI / message thread): boot(), prepare(), set_program(). set_program() runs
//   the real firmware's program loader (a few ms of Z80) and posts the resulting microcode and
//   coefficients through a lock-free mailbox. Never call it from the audio thread.
// - audio thread: process(). It takes the newest posted program at the start of a block and
//   never blocks, allocates or prints.
//
// What a change does:
// - same microcode, new coefficients (a parameter within a program): the coefficients take effect
//   at the next sample, as the firmware's live patching of D000 does on the unit. The steps are the
//   hardware's 8-bit ones.
// - new microcode (another program): the output fades out over ~10 ms, the program is loaded and
//   the delay memory cleared, then the output fades in. The unit itself mutes on a program load
//   (the firmware's L128C/L12A8 ramps); the exact ramp is not modelled.
//
// Not supported yet: the v1.5 gated programs (p1 infinite room, p5 sound-on-sound) and v2.2
// Sound-on-sound / Infinite, which need the Level key's hold input (notes/2026-11-11-0105).
//
// Output mapping: instruction 127 = left, 63 = right, the default of ../fx/dn780fx since
// 2026-11-11 (3 of 4 recordings; the hardware truth is in the unread converter PROM IC10).
#pragma once
#include "dn780.hpp"
#include "../../shared/core/mailbox.hpp"
#include "../../shared/core/rate_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>

namespace dn780 {

class Engine {
public:
    // ---- control thread ----
    // 0 on success; -1 if the ROM does not boot.
    int boot(const uint8_t* rom, size_t n, const uint8_t prom[32]) {
        std::memcpy(prom_, prom, 32);
        if (fw_.boot(rom, n)) return -1;
        dsp_.init(prom_);
        loaded_ = false;
        return 0;
    }
    const Firmware& firmware() const { return fw_; }

    // Allocates. Call before audio starts or with audio stopped.
    void prepare(int host_rate, int max_block) {
        // rates in eighths of a hertz (15 MHz / 512 = 234375 / 8); both directions band-limit at
        // 12 kHz, standing in for the converter board's KTR009 filters
        bridge_.setup(host_rate, max_block, 234375, 8, 12000.0, 1);
        fade_len_ = std::max(1, int(0.010 * kFs));
    }
    long latency() const { return bridge_.latency(); }
    double latency_exact() const { return bridge_.latency_exact(); }

    // Load factory preset 1-39 (or 0) with pokes over it (e.g. {0xE036, decay}).
    // Returns Firmware::Ok, or why not. The audio thread picks the result up at its next block.
    int set_program(int preset, const Firmware::Poke* pokes, int npoke) {
        Program p;
        int rc = fw_.load(preset, pokes, npoke, p.ucode, p.coef, &p.prog);
        if (rc != Firmware::Ok) return rc;
        if (unsupported(p.prog)) return Unsupported;
        p.calibrated = calibrate_.load();
        mailbox_.post(p);
        return Firmware::Ok;
    }
    static constexpr int Unsupported = -10;
    bool unsupported(int prog) const {
        if (fw_.version() == 15) return prog == 1 || prog == 5;
        return prog == 0x14 || prog == 0x15;  // v2.2 sound-on-sound, infinite
    }

    // Opt-in calibration to the recorded units (../fx/README.md, "-U"): comb feed and loop gain on
    // the v2.2 reverbs. Takes effect at the next program posted. Not the design.
    void set_recorded_calibration(bool on) { calibrate_.store(on); }

    // ---- audio thread ----
    // Mono input (the unit sums to mono; pass the average of the host's channels), stereo wet out.
    // in, out_l, out_r may alias.
    void process(const float* in, float* out_l, float* out_r, int n) {
        if (!bridge_.ready()) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        if (mailbox_.take(next_)) have_next_ = true;
        const float* ins[1] = {in};
        bridge_.process(ins, out_l, out_r, n,
                        [this](const float* const* x, float* l, float* r, int count) { render(x[0], l, r, count); });
    }

private:
    struct Program {
        uint8_t ucode[256], coef[256];
        int prog = 0;
        bool calibrated = false;
    };

    void apply_next() {
        bool same_program = loaded_ && std::memcmp(next_.ucode, dsp_.ucode(), 256) == 0;
        if (same_program) {
            dsp_.set_coef(next_.coef);
        } else {
            dsp_.clear_memory();
            dsp_.load(next_.ucode, next_.coef);
        }
        bool cal = next_.calibrated && fw_.version() == 22 && next_.prog >= 0x0A && next_.prog <= 0x0D;
        for (int k = 0; k < 64; k++) dsp_.gain_scale(k, 1.0);
        if (cal) {
            dsp_.gain_scale(19, 1.18);
            for (int k = 22; k <= 25; k++) dsp_.gain_scale(k, 1.004);
        }
        loaded_ = true;
        have_next_ = false;
    }

    void render(const float* x, float* l, float* r, int count) {
        for (int i = 0; i < count; i++) {
            if (have_next_) {
                bool new_program = !loaded_ || std::memcmp(next_.ucode, dsp_.ucode(), 256) != 0;
                if (!new_program) {
                    apply_next();
                } else if (fade_ <= 0) {   // faded out (or never playing): swap and fade in
                    apply_next();
                    fading_in_ = true;
                } else {
                    fading_in_ = false;    // fade out first
                }
            }
            if (!loaded_) {
                l[i] = r[i] = 0.0f;
                continue;
            }
            double v = double(x[i]) * 32768.0;
            int16_t adc = int16_t(v >= 32767.0 ? 32767 : v <= -32768.0 ? -32768 : std::lrint(v));
            int16_t dl, dr;
            dsp_.sample(adc, dl, dr);
            if (fading_in_) {
                if (fade_ < fade_len_) fade_++;
            } else if (have_next_) {
                if (fade_ > 0) fade_--;
            }
            float g = float(fade_) / float(fade_len_);
            l[i] = g * float(dr) / 32768.0f;  // instruction 127 = left
            r[i] = g * float(dl) / 32768.0f;
        }
    }

    Firmware fw_;
    Dsp dsp_;
    kit::RateBridgeT<64> bridge_;   // 64 taps per phase: closer to dn780fx's offline filter
    uint8_t prom_[32] = {};
    kit::Mailbox<Program> mailbox_;
    std::atomic<bool> calibrate_{false};
    Program next_;
    bool have_next_ = false, loaded_ = false, fading_in_ = true;
    int fade_ = 0, fade_len_ = 293;
};

}  // namespace dn780
