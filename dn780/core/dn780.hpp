// Klark Teknik DN780: the emulator core as C++, with no global state (several instances per
// process). Copied from the dn780 project's emu/core/dn780.hpp (SOURCE.md); there it is checked
// bit for bit against the C reference (dn780_dsp.c, dn780_fw.c). Added here: the 8279 display
// writes (Firmware::display()), which change no microcode or coefficient.
//
//   dn780::Dsp       slot-level model of the DSP board (BRD 2638/4), driven by the 32-byte IC44
//                    timing-PROM image (inferred, ../fx/ic44_hypothesis.bin)
//   dn780::Firmware  the real Z80 firmware (v1.5 or v2.2 EPROM, supplied by the user), run only to
//                    compute DSP microcode and coefficients for a program and its parameters
//
// Header-only, except that exactly one translation unit must define DN780_IMPLEMENTATION before
// including this header (it compiles floooh's z80.h implementation there, zlib licence).
//
// See the dn780 project's emu/fx/README.md for what is modelled, and its notes/ for the evidence.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef DN780_IMPLEMENTATION
#define CHIPS_IMPL
#endif
#include "third_party/z80.h"

namespace dn780 {

constexpr double kFs = 29296.875;  // 15 MHz / 512, the design sample rate

// ---- The DSP board ------------------------------------------------------------------------------
// 128 instructions x 4 slots per sample; the IC44 image drives MEM/R X0 /XOE MEM/W D/A A/D SP/S
// SP/W; one AD bus; TDC1010 registers X/Y/ACC on X0 rising, CLK P at the next K2; output
// P31,P29..P15 saturated; truncating arithmetic (RND grounded).
class Dsp {
public:
    void init(const uint8_t prom[32]) {
        *this = Dsp();
        for (int i = 0; i < 32; i++) sig_[i] = prom[i] ^ (MEMR | XOE | MEMW | SPS | SPW);
    }
    // Load a program: microcode and coefficients take effect at once. Delay memory and registers
    // keep their contents (as the hardware's DRAM does).
    void load(const uint8_t ucode[256], const uint8_t coef[256]) {
        std::memcpy(U_, ucode, 256);
        std::memcpy(D_, coef, 256);
        dnext_ = false;
    }
    // New coefficients, applied at the start of the next sample (the CPU's UP window).
    void set_coef(const uint8_t coef[256]) {
        std::memcpy(Dnext_, coef, 256);
        dnext_ = true;
    }
    // Opt-in calibration to recorded units (not hardware behaviour): scale record rec's gain by f
    // at 16-bit precision. f = 1 restores the exact 8-bit gain.
    void gain_scale(int rec, double f) {
        if (rec >= 0 && rec < 64) gscale_[rec] = (f == 1.0) ? 0.f : float(f);
    }
    void clear_memory() {
        std::memset(mem_, 0, sizeof mem_);
        std::memset(reg_, 0, sizeof reg_);
    }
    const uint8_t* ucode() const { return U_; }
    long contention() const { return contention_; }

    // One sample: the A/D word in, the two D/A words out (left = instructions 0-63's write).
    void sample(int16_t adc, int16_t& outL, int16_t& outR) {
        static constexpr int rowof[4] = {0, 3, 2, 1};
        if (dnext_) {
            std::memcpy(D_, Dnext_, 256);
            dnext_ = false;
        }
        for (int n = 0; n < 128; n++) {
            uint8_t E = U_[2 * n], O = U_[2 * n + 1], En1 = U_[(2 * n + 2) & 255];
            int key = ((O >> 7) & 1) | ((O >> 6) & 1) << 1 | ((O >> 5) & 1) << 2;
            int rec = E & 63, sp = ((E >> 7) << 4) | ((O >> 1) & 15);
            uint16_t tap = uint16_t(D_[4 * rec + 1] | D_[4 * rec + 2] << 8);
            int8_t g = int8_t(D_[4 * rec + 3]);
            dramUntil_ = -1;
            for (int k = 0; k < 4; k++, gs_++) {
                if (pendFire_ >= 0 && gs_ >= pendFire_) {
                    P_ = (pendAcc_ ? P_ : 0) + pendProd_;
                    pendFire_ = -1;
                }
                int sig = sig_[key * 4 + rowof[k]];
                int rise = sig & ~prevSig_, fall = prevSig_ & ~sig;
                if (rise & MEMR) {
                    dramAddr_ = uint16_t(ctr_ + tap);
                    if (!(sig & MEMW)) {
                        dramFrom_ = k + 1;
                        dramUntil_ = kDramHold;
                    }
                }
                int nd = 0;
                int16_t v = bus_;
                if (dramUntil_ >= 0 && k >= dramFrom_ && k <= dramUntil_ && !(sig & MEMW)) { v = mem_[dramAddr_]; nd++; }
                if ((sig & SPS) && !(sig & SPW)) { v = reg_[sp]; nd++; }
                if (sig & XOE) { v = satP(P_); nd++; }
                if (sig & AD) { v = adc; nd++; }
                if (nd > 1) contention_++;
                bus_ = v;
                if ((sig & MEMW) && (sig & MEMR) && ((rise & MEMR) || (rise & MEMW))) mem_[uint16_t(ctr_ + tap)] = bus_;
                if (rise & X0) {
                    int16_t Y = int16_t(uint16_t(uint8_t(g)) << 8);
                    if (gscale_[rec] != 0.f) {  // calibration only
                        long y = std::lrint(double(Y) * gscale_[rec]);
                        Y = int16_t(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
                    }
                    pendProd_ = int64_t(bus_) * Y;
                    pendAcc_ = (k == 0) ? ((E >> 6) & 1) : ((En1 >> 6) & 1);
                    long g1 = gs_;
                    while ((g1 & 3) != 1) g1++;
                    pendFire_ = g1 + 1;
                }
                if ((sig & SPS) && (sig & SPW)) reg_[sp] = bus_;
                if (rise & DA) {
                    if (n < 64) dacL_ = bus_;
                    else dacR_ = bus_;
                }
                if (fall & AD) ctr_++;
                prevSig_ = sig;
            }
        }
        outL = dacL_;
        outR = dacR_;
    }

private:
    enum { MEMR = 1, X0 = 2, XOE = 4, MEMW = 8, DA = 16, AD = 32, SPS = 64, SPW = 128 };
    static constexpr int kDramHold = 3;
    static int16_t satP(int64_t P) {
        int p31 = (P >> 31) & 1, p30 = (P >> 30) & 1;
        if (p31 != p30) return p31 ? int16_t(0x8000) : int16_t(0x7FFF);
        return int16_t(((P >> 15) & 0x7FFF) | (p31 << 15));
    }

    uint8_t U_[256] = {}, D_[256] = {}, Dnext_[256] = {};
    bool dnext_ = false;
    uint8_t sig_[32] = {};
    int16_t mem_[65536] = {}, reg_[32] = {};
    uint16_t ctr_ = 0;
    int16_t bus_ = 0, dacL_ = 0, dacR_ = 0;
    int64_t P_ = 0, pendProd_ = 0;
    long pendFire_ = -1;
    int pendAcc_ = 0;
    int prevSig_ = 0;
    long gs_ = 0;
    int dramFrom_ = 0, dramUntil_ = -1;
    uint16_t dramAddr_ = 0;
    long contention_ = 0;
    float gscale_[64] = {};
};

// ---- The firmware -------------------------------------------------------------------------------
// Boot once; then each load() starts from the booted snapshot, writes a preset's parameter bytes
// (E030-E03B) and any pokes, and calls the firmware's own program loader, which computes the
// microcode (C000) and coefficients (D000) exactly as the unit does.
// Memory map as ../dn780_run.c: ROM 0000-7FFF, 8279 A000/1, B block, C000 microcode,
// D000 coefficients, E000 2K battery RAM, F000 latch / F001 input; FFF0+ reads HALT.
class Firmware {
public:
    struct Poke {
        uint16_t addr;
        uint8_t value;
    };
    enum Result { Ok = 0, EmptySlot = -2, NoReturn = -3, NotBooted = -4 };

    // 0 on success; -1 if the firmware does not reach its idle loop (wrong ROM).
    int boot(const uint8_t* r, size_t n) {
        if (n > 0x8000) n = 0x8000;
        rom_.assign(r, r + n);
        version_ = n > 0x4000 ? 22 : 15;
        M_ = Machine();
        M_.pins = z80_init(&M_.cpu);
        run(version_ == 22 ? 30000000 : 20000000, false);  // ROM check, C/D RAM self-test, idle loop
        if (M_.cpu.pc < 0x0060 || M_.cpu.pc > 0x00A0) {   // idle loop: v1.5 0065-0089, v2.2 0092-0098
            booted_ok_ = false;
            return -1;
        }
        booted_ = M_;
        booted_ok_ = true;
        return 0;
    }
    bool booted() const { return booted_ok_; }
    int version() const { return version_; }  // 15 or 22, from the ROM size
    // The 8279 display RAM (segment bytes, digit numbers as notes/2026-11-10-0244 §2b) as the
    // firmware left it after the last successful load().
    const uint8_t* display() const { return display_; }
    const std::vector<uint8_t>& rom() const { return rom_; }

    // The 12 parameter bytes (E030-E03B) of factory preset 1-39, or nullptr for an empty slot.
    const uint8_t* preset_bytes(int preset) const {
        if (preset < 1 || preset > 39) return nullptr;
        size_t at = (version_ == 22 ? 0x3701 : 0x1EF1) + 12 * size_t(preset - 1);
        if (at + 12 > rom_.size() || (rom_[at] & 0x80)) return nullptr;
        return &rom_[at];
    }

    // preset 1-39 or 0 = none (zeroed parameters, everything from pokes).
    int load(int preset, const Poke* pokes, int npoke, uint8_t ucode[256], uint8_t coef[256], int* progp = nullptr) {
        if (!booted_ok_) return NotBooted;
        M_ = booted_;
        if (preset >= 1 && preset <= 39) {
            const uint8_t* b = preset_bytes(preset);
            if (!b) return EmptySlot;
            for (int i = 0; i < 12; i++) M_.ram[(0x30 + i) & 0x7FF] = b[i];
        }
        for (int i = 0; i < npoke; i++) M_.ram[pokes[i].addr & 0x7FF] = pokes[i].value;
        int prog = M_.ram[0x39];
        if (version_ == 15) {
            // force PROG_LOAD (0EB2) to reload the microcode: E026 must be in another program group
            M_.ram[0x26] = (prog <= 1) ? 0xFF : 0x00;
            if (prog == 6) M_.ram[0x26] = 0x00;
            if (!call(0x0EB2)) return NoReturn;
        } else {
            M_.ram[0x26] = 0xFF;  // never a valid program: L0DA5 always reloads (the unmute L10CB runs inside)
            if (!call(0x0DA5)) return NoReturn;
        }
        std::memcpy(ucode, M_.c000, 256);
        std::memcpy(coef, M_.d000, 256);
        std::memcpy(display_, M_.disp, 16);
        if (progp) *progp = prog;
        return Ok;
    }

private:
    static constexpr uint16_t kSentinel = 0xFFF0;
    struct Machine {
        uint8_t ram[0x800] = {}, c000[0x1000] = {}, d000[0x1000] = {}, f000 = 0, b[16] = {};
        uint8_t disp[16] = {};          // the 8279's display RAM (writes only; see wr())
        uint8_t disp_addr = 0;
        bool disp_autoinc = false;
        z80_t cpu{};
        uint64_t pins = 0;
    };

    uint8_t rd(uint16_t a) const {
        if (a < 0x8000) return a < rom_.size() ? rom_[a] : 0xFF;
        switch (a >> 12) {
        case 0xA: return (a & 1) ? 0x00 : 0xFF;
        case 0xB: return M_.b[a & 15];
        case 0xC: return M_.c000[a & 0xFFF];
        case 0xD: return M_.d000[a & 0xFFF];
        case 0xE: return M_.ram[a & 0x7FF];
        case 0xF: return a >= kSentinel ? 0x76 : ((a & 1) ? 0xFF : M_.f000);
        }
        return 0xFF;
    }
    void wr(uint16_t a, uint8_t v) {
        if (a < 0x8000) return;
        switch (a >> 12) {
        case 0xA:   // 8279: A001 command, A000 data. Only display writes are modelled; reads stay
                    // as before (A001 0, A000 FF) so the microcode and coefficients are unchanged.
            if (a & 1) {
                if ((v & 0xE0) == 0x80) { M_.disp_addr = v & 15; M_.disp_autoinc = (v >> 4) & 1; }   // write display RAM
                else if ((v & 0xE0) == 0xC0 && (v & 0x10)) std::memset(M_.disp, 0, 16);             // clear
            } else {
                M_.disp[M_.disp_addr & 15] = v;
                if (M_.disp_autoinc) M_.disp_addr = (M_.disp_addr + 1) & 15;
            }
            break;
        case 0xB: M_.b[a & 15] = v; break;
        case 0xC: M_.c000[a & 0xFFF] = v; break;
        case 0xD: M_.d000[a & 0xFFF] = v; break;
        case 0xE: M_.ram[a & 0x7FF] = v; break;
        case 0xF: if (!(a & 1)) M_.f000 = v; break;
        }
    }
    bool run(long cycles, bool stop_at_halt) {
        for (long i = 0; i < cycles; i++) {
            M_.pins = z80_tick(&M_.cpu, M_.pins);
            if (M_.pins & Z80_MREQ) {
                uint16_t a = Z80_GET_ADDR(M_.pins);
                if (M_.pins & Z80_RD) { Z80_SET_DATA(M_.pins, rd(a)); }
                else if (M_.pins & Z80_WR) { wr(a, Z80_GET_DATA(M_.pins)); }
            } else if ((M_.pins & Z80_IORQ) && (M_.pins & Z80_RD)) {
                Z80_SET_DATA(M_.pins, 0xFF);
            }
            if (stop_at_halt && (M_.pins & Z80_HALT)) return true;
        }
        return false;
    }
    bool call(uint16_t addr) {
        M_.cpu.sp -= 2;
        wr(M_.cpu.sp, kSentinel & 0xFF);
        wr(uint16_t(M_.cpu.sp + 1), kSentinel >> 8);
        M_.pins = z80_prefetch(&M_.cpu, addr);
        bool ok = run(50000000, true);
        M_.cpu.sp += 2;  // the sentinel frame (never popped by the HALT)
        return ok;
    }

    std::vector<uint8_t> rom_;
    int version_ = 15;
    Machine M_, booted_;
    bool booted_ok_ = false;
    uint8_t display_[16] = {};
};

}  // namespace dn780
