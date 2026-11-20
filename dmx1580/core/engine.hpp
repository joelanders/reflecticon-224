// The DMX 15-80 for the kit, JUCE-free: the whole machine (8085 firmware, both pitch boards' 8086s,
// the audio core) running live at 40 kHz behind kit::RateBridgeT, with
//   - the front panel's knobs and switches applied at every block (no typing: they are analog),
//   - delay and pitch per channel typed on the unit's keypad by a driver and read back from its
//     display, the way the RMX16 plugin types its settings.
//
// A Unit is one booted machine and its driver's state. The control thread builds a unit (boot,
// repairs, start-up), types the current target into it before anyone hears it, then hands it to
// the audio thread (pending -> current -> retired, as kit::Host does with machines). A change of
// the machine options (RAM fit, pitch boards, analog stages, smooth pitch) builds a new unit.
//
// Keypad (RMX16-style probes on this core, 2026-11-12):
//   A/B digits #     delay in whole ms; the display shows it right-aligned (" 250"). No range check:
//                    past the RAM's length it wraps (64K words: 1638 ms, 1639 -> 0.7 ms)
//   A/B C r #        pitch ratio r, 0.500-2.000 in thousandths (the point is key F); the display
//                    shows "2.000"; outside the range "Err." and no change. 1.000 = no shift
//   A delay entry leaves the pitch alone and vice versa. While a channel shifts pitch, its display
//   flashes "H" in the first digit.
//
// Hardware mode: Engine::panel_key() queues raw keys (0-F as printed, D = 0xE store, # = 0xD,
// * = 0xF; UP 0x10, DOWN 0x11) for whichever thread runs the unit, held at least 120 ms. A panel key
// stops the driver (a key it holds is released) and it forgets its target until a new one arrives.
// Once the keys are quiet and the last one closed an entry (A, B, C, #, UP, DOWN), the display is
// read back with the a/b/c LEDs saying which field it shows: that field becomes the driver's
// confirmed value and is published as Shown::adopt for the host's parameters.
#pragma once
#include "emu/core/machine.hpp"
#include "../../shared/core/mailbox.hpp"
#include "../../shared/core/panel.hpp"
#include "../../shared/core/rate_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace dmxkit {

constexpr int kRate = 40000;

// What the driver types: per channel (0 = A, 1 = B).
struct Target {
    int delay[2] = {0, 0};          // ms
    int pitch[2] = {1000, 1000};    // ratio x 1000; 1000 = no shift
    bool pitch_on[2] = {true, true};// false: not typed (one pitch board: B has none)
    bool operator==(const Target& o) const {
        for (int c = 0; c < 2; c++)
            if (delay[c] != o.delay[c] || pitch_on[c] != o.pitch_on[c] || (pitch_on[c] && pitch[c] != o.pitch[c]))
                return false;
        return true;
    }
    bool operator!=(const Target& o) const { return !(*this == o); }
};

// The knobs and switches, read at every block.
struct Knobs {
    bool lockin = false, trigger = false;
    bool vco = false;
    float vco_speed = 1.f, vco_depth = 0.f;   // Hz, 0..0.2
    float in_gain[2] = {1.f, 1.f}, out_gain = 1.f;
    // MB.2 sht 2's regen pots, rotation 0..1: F/B GAIN (10K log, loaded by the sampler's 4K7; loop
    // gain 1 at about 81 %, 3.51 at full) and F/B FILTER (blend with a 7.96 kHz high-pass, 0 = flat).
    // The drawn 15-80 has them on A only; B gets the same as a 15-80S stand-in.
    float fb_gain[2] = {0.f, 0.f}, fb_filter[2] = {0.f, 0.f};
    bool phase_a = false;   // IN PHASE/OUT: before both channel A's output and its regen loop
    bool mono_in = false, mix_out = false;
};

struct Options {
    uint8_t opts = 0xFF;   // E3 PB option links (see opts_for)
    bool analog = false, interp = false;
    bool operator==(const Options& o) const { return opts == o.opts && analog == o.analog && interp == o.interp; }
};

// The option-link byte for a RAM fit per channel (words: 4096, 16384, 32768 or 65536) and the
// number of pitch boards; bit 2 (keep stores) always set. As the DMX plugin's opts_for.
inline uint8_t opts_for(unsigned words, bool two_boards) {
    uint8_t size = words == 4096 ? 0 : 3, cards = words == 32768 ? 1 : words == 65536 ? 3 : 0;
    return uint8_t(cards << 6 | cards << 4 | (two_boards ? 8 : 0) | 4 | size);
}

// What the panel shows, published by whichever thread runs the unit.
struct Shown {
    bool valid = false, stopped = false, busy = false;
    char display[12] = {};   // Machine::display_text ("1.000", " 250", "Err. ")
    unsigned leds = 0;       // a/b/c under the display (bits 0-2)
    bool vco_led = false;
    Target cur;              // what the driver last confirmed on the display
    bool have_cur = false, gave_up = false;
    uint8_t seg[4] = {};     // the digits, kit order: bit 0 a .. bit 6 g, bit 7 dp
    int driver_key = -1;     // the key the driver holds now, -1 = none
    float peak[2] = {};      // input level at the samplers, 1.0 = clipping, with a ~300 ms fall
    float adopt[4] = {};     // delay A, delay B (ms), pitch A, pitch B (ratio) read back; NaN = never
    uint32_t adopt_serial = 0;
};

using PanelKeys = kit::PanelKeyQueue<64>;

class Unit {
public:
    std::unique_ptr<dmx::Machine> m;
    PanelKeys* keys = nullptr;   // the engine's panel keys, once this unit is the one heard

    // Boot: ROMs (boot, ext1-3, 2048 bytes each), the repairs, the options, then the start-up.
    bool boot(const uint8_t* const roms[4], const char* patches, const Options& o, std::string* err) {
        m = std::make_unique<dmx::Machine>();
        m->load_roms(roms[0], roms[1], roms[2], roms[3]);
        std::string perr;
        if (m->apply_patches(patches, &perr) < 0) {
            *err = "these chips are not the dump the repairs were made for (" + perr + ")";
            return false;
        }
        m->opts = o.opts;
        m->audio = true;
        m->diag = false;
        if (!m->wrap_from_opts()) {
            *err = "option links not modelled";
            return false;
        }
        m->ac.set_mclk(10.24);
        m->ac.ac_analog = o.analog ? 1 : 0;
        m->ac.ac_interp = o.interp ? 1 : 0;
        m->ac.ac_vco = 1;   // always on the VCO timebase, so the XTAL/VCO switch can move live
        m->ac.ac_vco_speed = 1.0;
        m->ac.ac_vco_depth = 0.0;
        m->ac.ac_vco_shape = 1;
        m->audio_init(INT64_MAX);
        m->power_on();
        m->stream_begin();
        run_silent(1.5);   // the firmware uploads the pitch boards
        // a power-up with the battery RAM clear: both channels at 0 ms, no pitch shift
        cur_ = Target{};
        have_cur_ = true;
        return true;
    }

    void set_knobs(const Knobs& k) {
        m->set_lockin(k.lockin);
        m->set_trigger(k.trigger);
        for (int c = 0; c < 2; c++) {
            m->set_input_gain(c, k.in_gain[c]);
            m->set_fb_gain(c, k.fb_gain[c]);
            m->set_fb_filter(c, k.fb_filter[c]);
        }
        m->set_fb_phase(0, k.phase_a);
        m->ac.set_vco_speed(std::clamp(double(k.vco_speed), 0.01, 50.0));
        m->ac.ac_vco_depth = k.vco ? std::clamp(double(k.vco_depth), 0.0, 0.5) : 0.0;
        vco_ = k.vco;
    }
    void set_target(const Target& t) {
        next_ = t;
        have_next_ = true;
    }
    bool idle() const {
        return nkeys_ == 0 && !verifying_ &&
               (!have_next_ || (have_cur_ && next_ == cur_) || (give_up_ && next_ == gave_up_));
    }
    bool stopped() const { return stopped_; }

    // Render n <= dmx::Machine::STREAM_MAX samples (A/B in, A/B out). The keys are driven every 64
    // samples of machine time, on a fixed grid, so the result does not depend on how the host
    // splits its blocks.
    void render(const float* a, const float* b, float* x, float* y, int n) {
        for (int done = 0; done < n;) {
            if ((pos_ & 63) == 0) drive();
            int k = std::min(int(64 - (pos_ & 63)), n - done);
            m->process(a + done, b + done, x + done, y + done, k);
            done += k;
            pos_ += uint64_t(k);
        }
    }
    // Machine time with silence in, output dropped (the start-up, typing before audio).
    void run_silent(double seconds) {
        static thread_local float z[256], o1[256], o2[256];
        for (long left = long(seconds * kRate); left > 0; left -= 256) render(z, z, o1, o2, int(std::min(256L, left)));
    }
    // Type the newest target until the driver is idle, at most max_seconds of machine time.
    void run_until_idle(double max_seconds = 30.0) {
        static thread_local float z[256], o1[256], o2[256];
        for (long left = long(max_seconds * kRate); left > 0 && !idle(); left -= 256) render(z, z, o1, o2, 256);
    }

    void shown(Shown& s) const {
        s.valid = true;
        s.stopped = stopped_;
        s.busy = !idle();
        m->display_text(s.display);
        s.leds = m->channel_leds();
        s.vco_led = vco_ && m->ac.vco_led();
        s.cur = cur_;
        s.have_cur = have_cur_;
        s.gave_up = give_up_;
        for (int i = 0; i < 4; i++) {   // a=b0 c=b1 f=b2 e=b3 d=b4 b=b5 g=b6 dp=b7 -> a b c d e f g dp
            uint8_t d = m->digit_segments(i);
            s.seg[i] = uint8_t((d & 1) | (d >> 5 & 1) << 1 | (d >> 1 & 1) << 2 | (d >> 4 & 1) << 3 | (d >> 3 & 1) << 4 |
                               (d >> 2 & 1) << 5 | (d & 0xC0));
        }
        s.driver_key = nkeys_ && phase_ == 1 ? keys_[key_i_] : -1;
        for (int i = 0; i < 4; i++) s.adopt[i] = adopt_[i];
        s.adopt_serial = adopt_serial_;
    }

private:
    // ---- the panel's own keys (hardware mode) ----
    static constexpr uint64_t kMinHold = dmx::Machine::CPU_HZ * 12 / 100, kQuiet = dmx::Machine::CPU_HZ * 4 / 10;
    void panel_keys(uint64_t now) {
        if (!keys) return;
        bool any = keys->poll(now, kMinHold, [this](int code, bool down) {
            if (down) m->key_down(code);
            else m->key_up(code);
        });
        if (any) {   // the user has the panel: stop typing, forget the target
            if (nkeys_ && phase_ == 1) m->key_up(keys_[key_i_]);
            nkeys_ = 0;
            verifying_ = give_up_ = have_next_ = false;
            tries_ = 0;
            readback_ = true;
        }
        if (!readback_ || keys->any_held() || now - keys->last_at() < kQuiet) return;
        const int k = keys->last_key();
        if (!(k == 0xA || k == 0xB || k == 0xC || k == 0xD || k == 0x10 || k == 0x11)) return;
        readback_ = false;
        // the a/b LEDs say which channel the display shows, c that it is the pitch
        const unsigned l = m->channel_leds();
        const int c = (l & 3) == 1 ? 0 : (l & 3) == 2 ? 1 : -1;
        if (c < 0) return;
        char d[12], t[12];
        m->display_text(d);
        int n = 0;
        for (const char* q = d; *q; q++)
            if (*q != ' ' && *q != 'H') t[n++] = *q;
        t[n] = 0;
        if (!n) return;
        char* end = nullptr;
        double v = std::strtod(t, &end);
        if (*end) return;   // "Err." or anything else
        int f = (l & 4) ? 2 + c : c;
        if (f >= 2) {
            int r = int(std::lround(v * 1000.0));
            if (r < 500 || r > 2000) return;
            cur_.pitch[c] = r;
            v = r / 1000.0;
        } else {
            cur_.delay[c] = int(std::lround(v));
        }
        if (!(adopt_[f] == float(v))) adopt_[f] = float(v), adopt_serial_++;
    }

    // keys: hold 100 ms, 60 ms apart (the firmware's scan debounces; tested on every field)
    static constexpr uint64_t kHold = dmx::Machine::CPU_HZ / 10, kGap = dmx::Machine::CPU_HZ * 6 / 100,
                              kSettle = dmx::Machine::CPU_HZ / 20, kWindow = dmx::Machine::CPU_HZ * 4 / 10;
    enum Field { DelayA, DelayB, PitchA, PitchB, None };

    void add(int code) {
        if (nkeys_ < int(sizeof keys_)) keys_[nkeys_++] = uint8_t(code);
    }
    void add_digits(const char* s) {
        for (; *s; s++) add(*s == '.' ? 0xF : *s - '0');
    }
    // The next field that differs (pitch before delay, A before B), or None.
    Field next_field() const {
        const Target& t = next_;
        for (int c = 0; c < 2; c++) {
            if (t.pitch_on[c] && (!have_cur_ || t.pitch[c] != cur_.pitch[c])) return Field(PitchA + c);
            if (!have_cur_ || t.delay[c] != cur_.delay[c]) return Field(DelayA + c);
        }
        return None;
    }
    void plan(Field f) {
        nkeys_ = key_i_ = 0;
        phase_ = 0;
        field_ = f;
        typing_ = next_;
        int c = (f == DelayB || f == PitchB) ? 1 : 0;
        add(c == 0 ? 0xA : 0xB);
        char b[16];
        if (f == PitchA || f == PitchB) {
            add(0xC);
            std::snprintf(b, sizeof b, "%d.%03d", next_.pitch[c] / 1000, next_.pitch[c] % 1000);
            add_digits(b);
            std::snprintf(want_, sizeof want_, "%d.%03d", next_.pitch[c] / 1000, next_.pitch[c] % 1000);
        } else {
            std::snprintf(b, sizeof b, "%d", next_.delay[c]);
            add_digits(b);
            std::snprintf(want_, sizeof want_, "%4d", next_.delay[c]);
        }
        add(0xD);
        phase_t_ = m->ticks;
    }
    void drive() {
        if (stopped_) return;
        const uint64_t now = m->ticks;
        panel_keys(now);
        if (nkeys_) {   // typing: down kHold, up, kGap
            if (phase_ == 0) {
                m->key_down(keys_[key_i_]);
                phase_ = 1;
                phase_t_ = now;
            } else if (phase_ == 1 && now - phase_t_ >= kHold) {
                m->key_up(keys_[key_i_]);
                phase_ = 2;
                phase_t_ = now;
            } else if (phase_ == 2 && now - phase_t_ >= kGap) {
                phase_ = 0;
                if (++key_i_ >= nkeys_) {
                    nkeys_ = 0;
                    verifying_ = true;
                    phase_t_ = now;
                }
            }
            return;
        }
        if (verifying_) {   // the display must show the value within kWindow
            if (now - phase_t_ < kSettle) return;
            char d[12];
            m->display_text(d);
            bool ok = std::strcmp(d, want_) == 0;
            if (!ok && now - phase_t_ < kWindow) return;
            verifying_ = false;
            int c = (field_ == DelayB || field_ == PitchB) ? 1 : 0;
            if (ok) {
                if (field_ == PitchA || field_ == PitchB) cur_.pitch[c] = typing_.pitch[c];
                else cur_.delay[c] = typing_.delay[c];
                cur_.pitch_on[c] = typing_.pitch_on[c];
                tries_ = 0;
            } else if (++tries_ >= 3) {
                gave_up_ = next_;
                give_up_ = true;
            }
            return;
        }
        if (!have_next_) return;
        if (give_up_) {
            if (next_ == gave_up_) return;
            give_up_ = false;
            tries_ = 0;
        }
        cur_.pitch_on[0] = next_.pitch_on[0];
        cur_.pitch_on[1] = next_.pitch_on[1];
        Field f = next_field();
        if (f != None) plan(f);
    }

    Target cur_, next_, typing_, gave_up_;
    bool have_cur_ = false, have_next_ = false, give_up_ = false, verifying_ = false, stopped_ = false, vco_ = false;
    int tries_ = 0;
    uint8_t keys_[16] = {};
    int nkeys_ = 0, key_i_ = 0, phase_ = 0;
    uint64_t phase_t_ = 0, pos_ = 0;
    Field field_ = None;
    char want_[12] = {};
    bool readback_ = false;
    float adopt_[4] = {NAN, NAN, NAN, NAN};
    uint32_t adopt_serial_ = 0;
};

class Engine {
public:
    Engine() = default;
    ~Engine() {
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
        delete current_;
    }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- control thread ----
    // Hand a booted unit to the audio side. Before audio has started it becomes current here.
    void install(std::unique_ptr<Unit> u) {
        collect();
        latency_frames_.store(u->m->latency());
        u->keys = &keys_;
        if (!audio_started_.load(std::memory_order_acquire) && !current_) {
            current_ = u.release();
            publish(*current_);
            return;
        }
        delete pending_.exchange(u.release());
    }
    // Free a unit the audio thread has replaced.
    void collect() { delete retired_.exchange(nullptr); }

    void prepare(int host_rate, int max_block) {
        bridge_.setup(host_rate, max_block, kRate, 1, 0.0, 2);
        host_rate_ = host_rate;
        take_pending();
    }
    long latency() const {
        return bridge_.latency() + std::lround(latency_frames_.load() * double(host_rate_) / kRate);
    }

    void set_target(const Target& t) { targets_.post(t); }
    void set_knobs(const Knobs& k) { knobs_.post(k); }
    // A raw panel key (one control thread at a time); the unit being heard takes it.
    void panel_key(int code, bool down) { keys_.push(code, down); }
    bool audio_started() const { return audio_started_.load(std::memory_order_acquire); }

    // Before audio starts: type the newest target on this thread, with the audio side kept off the
    // unit by the owner flag. Returns false if the audio side holds it (it then types itself).
    bool type_now() {
        int free = 0;
        if (!owner_.compare_exchange_strong(free, 1, std::memory_order_acq_rel)) return false;
        if (!audio_started() && current_) {
            Target t;
            if (targets_.take(t)) current_->set_target(t);
            Knobs k;
            if (knobs_.take(k)) knobs_now_ = k;
            current_->set_knobs(knobs_now_);
            current_->run_until_idle();
            publish(*current_);
        }
        owner_.store(0, std::memory_order_release);
        return true;
    }
    // Type the target into a unit nobody else holds yet (a fresh build).
    static void type_into(Unit& u, const Target& t, const Knobs& k) {
        u.set_knobs(k);
        u.set_target(t);
        u.run_until_idle();
    }

    Shown shown() const {
        Shown s;
        for (;;) {
            uint32_t s0 = seq_.load(std::memory_order_acquire);
            if (s0 & 1) continue;
            std::memcpy(&s, &pub_, sizeof s);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s0) return s;
        }
    }

    // ---- audio thread ----
    void process(const float* in_a, const float* in_b, float* out_l, float* out_r, int n) {
        int free = 0;
        if (!owner_.compare_exchange_strong(free, 2, std::memory_order_acq_rel)) {   // control is typing
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        audio_started_.store(true, std::memory_order_release);
        take_pending();
        Unit* u = current_;
        if (!u || !bridge_.ready()) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            owner_.store(0, std::memory_order_release);
            return;
        }
        Target t;
        if (targets_.take(t)) u->set_target(t);
        Knobs k;
        if (knobs_.take(k)) knobs_now_ = k;
        const Knobs& p = knobs_now_;
        u->set_knobs(p);
        const float* ins[2] = {in_a, p.mono_in ? in_a : in_b};
        bridge_.process(ins, out_l, out_r, n, [u](const float* const* x, float* a, float* b, int count) {
            for (int done = 0; done < count;) {
                int c = std::min(count - done, int(dmx::Machine::STREAM_MAX));
                u->render(x[0] + done, x[1] + done, a + done, b + done, c);
                done += c;
            }
        });
        // the output level ramps over the block; phase A flips at once, like the switch
        const float g0 = out_gain_, g1 = p.out_gain, sa = p.phase_a ? -1.f : 1.f;
        out_gain_ = g1;
        if (p.mix_out || g0 != 1.f || g1 != 1.f || p.phase_a)
            for (int i = 0; i < n; i++) {
                float g = g0 + (g1 - g0) * float(i + 1) / float(n), a = sa * g * out_l[i], b = g * out_r[i];
                if (p.mix_out) out_l[i] = out_r[i] = 0.5f * (a + b);
                else {
                    out_l[i] = a;
                    out_r[i] = b;
                }
            }
        // the input LEDs: the samplers' peak, falling ~300 ms
        for (int c = 0; c < 2; c++) peak_[c] = std::max(peak_[c] * std::exp(-float(n) / (0.3f * float(host_rate_))), u->m->take_peak(c));
        publish(*u);
        owner_.store(0, std::memory_order_release);
    }

private:
    void take_pending() {
        if (retired_.load(std::memory_order_acquire)) return;
        Unit* p = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (!p) return;
        retired_.store(current_, std::memory_order_release);
        current_ = p;
    }
    void publish(const Unit& u) {
        Shown s;
        u.shown(s);
        s.peak[0] = peak_[0];
        s.peak[1] = peak_[1];
        uint32_t q = seq_.load(std::memory_order_relaxed);
        seq_.store(q + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&pub_, &s, sizeof s);
        seq_.store(q + 2, std::memory_order_release);
    }

    kit::RateBridgeT<32> bridge_;
    int host_rate_ = kRate;
    std::atomic<int> latency_frames_{0};
    std::atomic<Unit*> pending_{nullptr}, retired_{nullptr};
    Unit* current_ = nullptr;   // the side holding owner_ (audio once started)
    kit::Mailbox<Target> targets_;
    kit::Mailbox<Knobs> knobs_;
    Knobs knobs_now_;
    float out_gain_ = 1.f;
    float peak_[2] = {};
    PanelKeys keys_;
    std::atomic<int> owner_{0};   // 0 free, 1 control (typing before audio), 2 audio
    std::atomic<bool> audio_started_{false};
    std::atomic<uint32_t> seq_{0};
    Shown pub_;
};

}  // namespace dmxkit
