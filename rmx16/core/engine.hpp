// rmx16::Engine: the whole RMX16 (8085 firmware, front panel, DSP; core/rmx16_core.h) as a
// streaming effect at the host's rate. JUCE-free, so tests drive it the way the plugin does.
//
// Unlike the DN780, the machine runs whole on the audio thread: the RMX16's CPU keeps writing the
// DSP microcode while it runs (CHORUS moves its taps continuously; ECHO, DELAY and FREEZE change
// coefficients on edits; a program change mutes and reloads), so there is no "compute the program,
// then play it" split. Settings go in the way a user's do, as key presses, and come back from the
// firmware's own display:
//
//   control thread   set_target() posts the wanted settings (program, predelay, decay, LF, HF),
//                    already clamped to what the program accepts (rmx16plug's table).
//   audio thread     process() runs the machine; between key sequences its driver compares the
//                    newest target with the firmware's settings and types only what differs (key
//                    held 40 ms, 40 ms apart, 400 ms after a program change: measured minima are
//                    30 / 30 / 300 ms). After a sequence it reads the display back; a field that did
//                    not take is retyped (three tries, then it waits for the next target).
//   either           panel(): the display as decoded text, published by the audio side through a
//                    seqlock (the audio side never waits).
//
// Before audio starts, run_until_idle() types on the calling thread instead (rmx16plug decides
// when that is safe), so a session's settings are in place before its first block.
//
// Hardware mode: panel_key() queues a raw key (S1-S10, keypad) for the thread running the machine,
// held at least 120 ms. A panel key stops the driver at once (a key it holds is released) and it
// forgets its target, so it never retypes over the user; it waits until the keys have been quiet
// for kQuiet and a new target arrives, then rereads the display for its starting point. Once the
// keys are quiet and the last key finished an entry (not a digit or *), the display's settings are
// published as Panel::adopt (adopt_serial counts changes) for the host's parameters.
//
// The battery RAM (stores and the last settings) is copied out every half second for the session
// (battery()), and boot() can restore it.
//
// The input goes to the DSP as a 16-bit sample (the real gain-ranging 12-bit ADC is not modelled);
// the outputs are DAC A / DAC B through a ~5 Hz DC blocker, standing in for the analog stage.
#pragma once
#include "rmx16_core.h"
#include "../../shared/core/mailbox.hpp"
#include "../../shared/core/panel.hpp"
#include "../../shared/core/rate_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace rmx16 {

constexpr int kRate = RMXC_RATE;

struct Target {
    int prog = 1;     // 1-17
    int pre = 0;      // ms
    int dec = 40;     // tenths (0-99)
    int lf = 0;       // -9..9 (typed only if the program has LF)
    int hf = -4;      // -9..0 (typed only if the program has HF)
    bool has_lf = true, has_hf = true;
    bool operator==(const Target&) const = default;
};

// The display decoded: one char per digit (' ' when dark), a '.' flag per digit, the two sign
// digits, the name, the key LEDs, and whether the driver is still typing.
struct Panel {
    char dig[RMXC_NDIG] = {};
    bool dp[RMXC_NDIG] = {};
    int lf_sign = 0, hf_sign = 0;   // -1 minus, +1 plus, 0 dark
    char name[9] = {};
    uint16_t leds = 0;
    bool busy = false, stopped = false, valid = false;
    uint8_t seg[RMXC_NDIG] = {};   // raw patterns of the digits lit now (bit0 a .. bit6 g, bit7 dp); 0 = dark
    uint8_t signs = 0;             // E23 sign bits (0x40/0x20 E11 minus/vertical, 0x08/0x04 E13)
    int driver_key = 0;            // the key the driver holds now, 0 = none
    bool powered = true;           // the power switch
    Target adopt;                  // the display's settings after the panel was used directly
    uint32_t adopt_serial = 0;     // counts new adopt values
};

class Engine {
public:
    Engine() = default;
    ~Engine() { rmxc_destroy(m_); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- control thread ----
    // roms: boot, IC1-IC4. Allocates. Runs the power-up (2.6 s of machine time) here.
    // battery: a saved 6116 image (battery()) or nullptr for a cleared one.
    bool boot(const uint8_t* const roms[5], const int sizes[5], const uint8_t* battery = nullptr, int battery_len = 0) {
        if (!m_) m_ = rmxc_create();
        if (!m_) return false;
        for (int i = 0; i < 5; i++)
            if (!rmxc_load(m_, i, roms[i], sizes[i])) return false;
        if (battery && battery_len == kBattery) std::memcpy(rmxc_battery(m_), battery, kBattery);
        rmxc_power(m_);
        t_ = 0;
        for (int i = 0; i < int(2.6 * kRate); i++)
            if (!step(0)) return false;
        // the firmware's power-up leaves program 1 at its factory settings
        have_cur_ = false;
        snapshot_battery();
        publish();
        return !stopped_;
    }

    static constexpr int kBattery = 2048;
    // The newest copy of the battery RAM (control thread; the copy is made by the machine's thread).
    std::string battery() const {
        std::string out(kBattery, '\0');
        for (;;) {
            uint32_t s0 = bseq_.load(std::memory_order_acquire);
            if (s0 & 1) continue;
            std::memcpy(out.data(), bat_, kBattery);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (bseq_.load(std::memory_order_relaxed) == s0) return out;
        }
    }

    // A raw panel key (rmxc_key codes), from one control thread at a time.
    void panel_key(int code, bool down) { keyq_.push(code, down); }
    // Pots: 0 DELAY (the panel's "pot"), 1 INPUT, 2 OUTPUT; 0-255. Read by the firmware only.
    void set_pot(int which, int value) {
        if (which >= 0 && which < 3) pots_[which].store(value, std::memory_order_relaxed);
    }
    // The power switch: off silences the unit and darkens the panel; on is a power-up (the battery
    // RAM kept, the firmware boots again in real time). Takes effect at the next block.
    void set_power(bool on) { power_.store(on, std::memory_order_relaxed); }

    void prepare(int host_rate, int max_block) {
        // no converter filter modelled: band-limit at the narrower Nyquist
        bridge_.setup(host_rate, max_block, kRate, 1, 0.0, 1);
    }
    long latency() const { return bridge_.latency(); }

    void set_target(const Target& t) { mailbox_.post(t); }
    void set_swap(bool on) { swap_.store(on, std::memory_order_relaxed); }

    // Not concurrent with process(): types the newest target on this thread (silent input, output
    // dropped) until the driver is idle, at most `max_seconds` of machine time.
    void run_until_idle(double max_seconds = 8.0) {
        long n = long(max_seconds * kRate);
        if (mailbox_.take(next_)) have_next_ = true;
        for (long i = 0; i < n; i++) {
            if (!step(0)) break;
            if (idle()) break;
        }
        publish();
    }

    Panel panel() const {
        Panel p;
        for (;;) {
            uint32_t s0 = seq_.load(std::memory_order_acquire);
            if (s0 & 1) continue;
            std::memcpy(&p, &pub_, sizeof p);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s0) return p;
        }
    }

    // ---- audio thread ----
    // mono in; in, out_l, out_r may alias
    void process(const float* in, float* out_l, float* out_r, int n) {
        if (!bridge_.ready() || !m_) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        if (mailbox_.take(next_)) have_next_ = true;
        if (const bool pw = power_.load(std::memory_order_relaxed); pw != powered_) {
            powered_ = pw;
            if (pw) {   // a power-up: the driver starts over and waits for a new target
                rmxc_power(m_);
                stopped_ = false;
                nkeys_ = 0;
                verify_pending_ = give_up_ = have_cur_ = have_next_ = reread_ = false;
                tries_ = 0;
                adopt_due_ = true;
                std::memset(lit_, 0, sizeof lit_);
            }
        }
        if (!powered_) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            publish();
            return;
        }
        const float* ins[1] = {in};
        bridge_.process(ins, out_l, out_r, n, [this](const float* const* x, float* l, float* r, int count) {
            const bool sw = swap_.load(std::memory_order_relaxed);
            for (int i = 0; i < count; i++) {
                double v = double(x[0][i]) * 32767.0;
                int16_t s = int16_t(v >= 32767.0 ? 32767 : v <= -32768.0 ? -32768 : std::lrint(v));
                if (!step(s)) {
                    l[i] = r[i] = 0.0f;
                    continue;
                }
                float a = float(dc_[0]) / 32768.0f, b = float(dc_[1]) / 32768.0f;
                l[i] = sw ? b : a;
                r[i] = sw ? a : b;
            }
        });
        publish();
    }

private:
    // ---- the machine, one DSP sample at a time (whichever thread owns it) ----
    bool step(int16_t in) {
        if (stopped_) return false;
        if ((t_ & 63) == 0) panel_tick();
        drive();
        int16_t a, b;
        if (!rmxc_run_sample(m_, in, &a, &b)) {
            stopped_ = true;
            return false;
        }
        t_++;
        int16_t d[2] = {a, b};
        for (int c = 0; c < 2; c++) {   // DC blocker: y = x - x1 + a y1
            double y = d[c] - hp_x_[c] + kHp * hp_y_[c];
            hp_x_[c] = d[c];
            hp_y_[c] = y;
            dc_[c] = y;
        }
        // digit freshness: a digit not lit for 60 ms is dark (the firmware multiplexes at ~50 Hz)
        if ((t_ & 63) == 0) {
            rmxc_panel(m_, &raw_);
            for (int k = 0; k < RMXC_NDIG; k++)
                if (raw_.lit[k] != lit_[k]) lit_[k] = raw_.lit[k], lit_at_[k] = t_;
        }
        return true;
    }

    // ---- the key driver ----
    enum : int { kHold = kRate * 40 / 1000, kGap = kRate * 40 / 1000, kLoad = kRate * 400 / 1000,
                 kSettle = kRate * 120 / 1000 };
    struct Key {
        int code;
        bool load;   // a program change: wait kLoad after it
    };

    // ---- the panel's own keys (hardware mode) ----
    enum : int { kMinHold = kRate * 120 / 1000, kQuiet = kRate * 400 / 1000 };
    bool manual_quiet() const { return !keyq_.any_held() && long(t_ - keyq_.last_at()) >= kQuiet; }
    static bool entry_open(int code) {   // a digit or * leaves an entry unfinished
        return (code >= 101 && code <= 104) || (code >= 111 && code <= 114) || (code >= 121 && code <= 123);
    }
    void panel_tick() {
        for (int i = 0; i < 3; i++) {
            int v = pots_[i].load(std::memory_order_relaxed);
            if (v >= 0 && v != pot_set_[i]) rmxc_pot(m_, i, v), pot_set_[i] = v;
        }
        bool any = keyq_.poll(t_, kMinHold, [this](int code, bool down) { rmxc_key(m_, code, down ? 1 : 0); });
        if (any) {
            // the user has the panel: stop typing and forget the target
            if (nkeys_ && phase_ == 1) rmxc_key(m_, keys_[key_i_].code, 0);
            nkeys_ = 0;
            verify_pending_ = false;
            have_next_ = false;
            give_up_ = false;
            tries_ = 0;
            reread_ = adopt_due_ = true;
        }
        if (adopt_due_ && (t_ & 1023) == 0 && manual_quiet() && !entry_open(keyq_.last_key())) {
            Target got;
            if (read_settings(got) && !(adopted_ && got == adopt_)) {
                adopt_ = got;
                adopted_ = true;
                adopt_serial_++;
            }
        }
        if ((t_ & 32767) == 0) snapshot_battery();
    }
    void snapshot_battery() {
        uint32_t s = bseq_.load(std::memory_order_relaxed);
        bseq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(bat_, rmxc_battery(m_), kBattery);
        bseq_.store(s + 2, std::memory_order_release);
    }

    bool idle() const {
        return nkeys_ == 0 && !verify_pending_ &&
               (!have_next_ || (have_cur_ && next_ == cur_) || (give_up_ && next_ == gave_up_));
    }

    static int digit_code(char c) {
        switch (c) {
            case '1': return 101; case '2': return 111; case '3': return 121;
            case '4': return 102; case '5': return 112; case '6': return 122;
            case '7': return 103; case '8': return 113; case '9': return 123;
            case '0': return 114; case '*': return 104; case '#': return 124;
            default: return 0;
        }
    }
    void add(int code, bool load = false) {
        if (nkeys_ < kMaxKeys) keys_[nkeys_++] = {code, load};
    }
    void add_text(const char* s, bool load = false) {
        for (; *s; s++) add(digit_code(*s), load && s[1] == 0);
    }
    void add_number(int key, int v) {   // S-key, then the number, then #
        char b[8];
        int k = 0;
        unsigned u = unsigned(std::abs(v));
        char tmp[8];
        int nt = 0;
        do tmp[nt++] = char('0' + u % 10), u /= 10; while (u && nt < 7);
        if (v < 0) b[k++] = '*';
        while (nt) b[k++] = tmp[--nt];
        b[k++] = '#';
        b[k] = 0;
        add(key);
        add_text(b);
    }

    // Plan the keys for target T from the firmware's settings cur_.
    void plan(const Target& T) {
        nkeys_ = 0;
        key_i_ = 0;
        typing_ = T;
        phase_ = 0;
        phase_t_ = t_;
        prog_only_ = !have_cur_ || cur_.prog != T.prog;
        if (prog_only_) {
            // the program alone: its load sets the program's factory values, which the display
            // then shows; the next pass types only the fields that differ from those
            char b[8];
            std::snprintf(b, sizeof b, "%d#", T.prog);
            add(2);
            add_text(b, true);
            return;
        }
        const Target& c = cur_;
        if (c.pre != T.pre) add_number(3, T.pre);
        if (c.dec != T.dec) {
            char b[8];
            std::snprintf(b, sizeof b, "%d*%d#", T.dec / 10, T.dec % 10);
            add(4);
            add_text(b);
        }
        if (T.has_lf && c.lf != T.lf) add_number(5, T.lf);
        if (T.has_hf && c.hf != T.hf) add_number(6, T.hf);
    }

    void drive() {
        if (nkeys_) {   // typing: down kHold, up, then kGap (kLoad after a program's #)
            const Key& k = keys_[key_i_];
            long dt = long(t_ - phase_t_);
            if (phase_ == 0) {
                rmxc_key(m_, k.code, 1);
                phase_ = 1;
                phase_t_ = t_;
            } else if (phase_ == 1 && dt >= kHold) {
                rmxc_key(m_, k.code, 0);
                phase_ = 2;
                phase_t_ = t_;
            } else if (phase_ == 2 && dt >= (k.load ? kLoad : kGap)) {
                phase_ = 0;
                if (++key_i_ >= nkeys_) {
                    nkeys_ = 0;
                    verify_pending_ = true;
                    phase_t_ = t_;
                }
            }
            return;
        }
        if (verify_pending_) {
            if (long(t_ - phase_t_) < kSettle) return;
            verify_pending_ = false;
            Target got;
            bool ok = read_settings(got);
            got.has_lf = typing_.has_lf;
            got.has_hf = typing_.has_hf;
            if (prog_only_ && ok && got.prog == typing_.prog) {
                cur_ = got;
                have_cur_ = true;
                tries_ = 0;
            } else if (!prog_only_ && ok && got.prog == typing_.prog && got.pre == typing_.pre && got.dec == typing_.dec &&
                (!typing_.has_lf || got.lf == typing_.lf) && (!typing_.has_hf || got.hf == typing_.hf)) {
                cur_ = typing_;
                have_cur_ = true;
                tries_ = 0;
            } else {
                // resync from what the display shows, and let the next pass retype the rest
                if (ok) {
                    cur_ = got;
                    have_cur_ = true;
                } else {
                    have_cur_ = false;
                }
                if (++tries_ >= 3) {
                    gave_up_ = typing_;
                    give_up_ = true;
                }
            }
            return;
        }
        if (!have_next_) return;
        if (reread_) {   // after the panel was used: start from what the display shows
            if (!manual_quiet()) return;
            Target got;
            if (read_settings(got)) {
                got.has_lf = next_.has_lf;
                got.has_hf = next_.has_hf;
                cur_ = got;
                have_cur_ = true;
            } else {
                have_cur_ = false;
            }
            reread_ = false;
        }
        if (have_cur_ && next_ == cur_) return;
        if (give_up_) {
            if (next_ == gave_up_) return;
            give_up_ = false;
            tries_ = 0;
        }
        plan(next_);
        if (!nkeys_) {   // nothing differs after all
            cur_ = next_;
            have_cur_ = true;
        }
    }

    // ---- reading the display ----
    static char seg_char(uint8_t s) {
        switch (s & 0x7F) {
            case 0x3F: return '0'; case 0x06: return '1'; case 0x5B: return '2'; case 0x4F: return '3';
            case 0x66: return '4'; case 0x6D: return '5'; case 0x7D: return '6'; case 0x07: case 0x27: return '7';
            case 0x7F: return '8'; case 0x6F: return '9'; case 0x79: return 'E'; case 0x00: return ' ';
            case 0x40: return '-'; case 0x78: return 't'; case 0x73: return 'P'; case 0x77: return 'A';
            case 0x7C: return 'b'; case 0x39: return 'C'; case 0x5E: return 'd'; case 0x71: return 'F';
            case 0x38: return 'L'; case 0x76: return 'H'; case 0x50: return 'r';
            default: return '?';
        }
    }
    bool fresh(int k) const { return lit_[k] && long(t_ - lit_at_[k]) < kRate * 60 / 1000; }
    char dchar(int k) const { return fresh(k) ? seg_char(raw_.seg[k]) : ' '; }
    // digits [from, to] as a number (blanks ignored); false if any other character
    bool number(int from, int to, int& out) const {
        int v = 0;
        bool any = false;
        for (int k = from; k <= to; k++) {
            char c = dchar(k);
            if (c == ' ') continue;
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
            any = true;
        }
        out = v;
        return any;
    }
    bool read_settings(Target& o) const {
        // E39 E5 program, E6-E8 predelay, E9.E10 decay, E11 sign + E12 LF, E13 sign + E14 HF
        // a filter the program lacks is dark (not 0): that is the firmware's own availability flag
        int lf = 0, hf = 0;
        if (!number(0, 1, o.prog) || !number(2, 4, o.pre) || !number(5, 6, o.dec)) return false;
        if (dchar(8) != ' ' && !number(8, 8, lf)) return false;
        if (dchar(10) != ' ' && !number(10, 10, hf)) return false;
        int ls = (raw_.signs & 0x40) ? ((raw_.signs & 0x20) ? 1 : -1) : 1;
        int hs = (raw_.signs & 0x08) ? ((raw_.signs & 0x04) ? 1 : -1) : 1;
        o.lf = ls * lf;
        o.hf = hs * hf;
        return true;
    }

    void publish() {
        Panel p;
        for (int k = 0; k < RMXC_NDIG; k++) {
            p.dig[k] = dchar(k);
            p.dp[k] = fresh(k) && (raw_.seg[k] & 0x80);
        }
        p.lf_sign = (raw_.signs & 0x40) ? ((raw_.signs & 0x20) ? 1 : -1) : 0;
        p.hf_sign = (raw_.signs & 0x08) ? ((raw_.signs & 0x04) ? 1 : -1) : 0;
        std::memcpy(p.name, raw_.alpha, 8);
        p.leds = raw_.leds;
        p.busy = !idle() && !(give_up_ && have_next_ && next_ == gave_up_);
        for (int k = 0; k < RMXC_NDIG; k++) p.seg[k] = powered_ && fresh(k) ? raw_.seg[k] : 0;
        p.signs = powered_ ? raw_.signs : 0;
        p.powered = powered_;
        if (!powered_) std::memset(p.name, ' ', 8), p.leds = 0;
        p.driver_key = nkeys_ && phase_ == 1 ? keys_[key_i_].code : 0;
        p.adopt = adopt_;
        p.adopt_serial = adopt_serial_;
        p.stopped = stopped_;
        p.valid = true;
        uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&pub_, &p, sizeof p);
        seq_.store(s + 2, std::memory_order_release);
    }

    static constexpr int kMaxKeys = 48;
    static constexpr double kHp = 0.99921496;   // exp(-2 pi 5 / 40000)

    rmxc_t* m_ = nullptr;
    uint64_t t_ = 0;
    bool stopped_ = false;
    double hp_x_[2] = {}, hp_y_[2] = {}, dc_[2] = {};
    rmxc_panel_t raw_{};
    uint32_t lit_[RMXC_NDIG] = {};
    uint64_t lit_at_[RMXC_NDIG] = {};

    kit::Mailbox<Target> mailbox_;
    Target next_, cur_, typing_, gave_up_;
    bool have_next_ = false, have_cur_ = false, verify_pending_ = false, give_up_ = false, prog_only_ = false;
    int tries_ = 0;
    Key keys_[kMaxKeys] = {};
    int nkeys_ = 0, key_i_ = 0, phase_ = 0;
    uint64_t phase_t_ = 0;

    std::atomic<bool> swap_{false};
    kit::PanelKeyQueue<64> keyq_;
    std::atomic<bool> power_{true};
    bool powered_ = true;
    std::atomic<int> pots_[3] = {-1, -1, -1};   // -1 = never set (the core keeps its power-up value)
    int pot_set_[3] = {-1, -1, -1};
    bool reread_ = false, adopt_due_ = false, adopted_ = false;
    Target adopt_;
    uint32_t adopt_serial_ = 0;
    uint8_t bat_[kBattery] = {};
    std::atomic<uint32_t> bseq_{0};
    kit::RateBridgeT<32> bridge_;
    std::atomic<uint32_t> seq_{0};
    Panel pub_;
};

}  // namespace rmx16
