// The analog boards between 48 kHz audio and the row machine's host: the
// AIN (filter chain, sample-and-hold, gain ranger, ADC) in front of the
// converter pins, and the AOUT (DAC sample-and-hold, filter chain) after
// the four DAC outputs. Header-only; uses the host read only.
//
//   AnalogIO io(host);                 // installs host.audio_observer
//   per 48 kHz frame:
//     io.before_frame(left, right);    // push input, set the pins for the next codes
//     host.run_until(...frame end...);
//     io.after_frame(out4);            // the four analog outputs A-D at the frame end
//   io.set_bypass(true)                // the old nearest-sample path, for A/B listening
//
// The host's model picks the boards and the time base: the 224X's
// (filters.hpp: its "FOR 224X VERSION ONLY" values, rows of 292.97 ns) or
// the Model 224's (filters_224.hpp: the values as drawn, trims at the
// drawing's nulls; rows of 488.28 ns, emulator/timing.hpp timing_224).
//
// See analog/GOAL.md and the report for the evidence.
#pragma once
#include "../emulator/host.hpp"
#include "filters.hpp"
#include "filters_224.hpp"

namespace lexicon224x::analog {

class AnalogIO {
public:
    static constexpr cpu::Tick frame = 576000000000ULL / 48000;

    // Levels: input 1.0 = 5 V at the converter (the onset of ADC clipping) at
    // low frequencies; the pre-emphasis raises treble, so a full-scale treble
    // tone clips, as on the hardware. Output 1.0 = DAC full scale.
    explicit AnalogIO(cpu::Host &host, bool gain_ranging = true)
        : host_(host), is_224_(host.model == Model::Lexicon224),
          timing_(is_224_ ? cpu::timing_224 : cpu::timing_224x),
          ain_(is_224_ ? ain_224_modal() : ain_modal()), aout_(is_224_ ? aout_224_modal() : aout_modal()),
          in_{Input(ain_, frame), Input(ain_, frame)},
          out_{Output(aout_), Output(aout_), Output(aout_), Output(aout_)}, gain_ranging_(gain_ranging) {
        t0_ = host_.now();
        frame_end_ = t0_;
        host_.audio_observer = [this](const cpu::AudioEvent &e) {
            int mantissa = int(e.dac ^ 0x800);
            if (mantissa & 0x800) {
                mantissa -= 0x1000;
            }
            int sample = mantissa * (1 << (4 - (e.gain & 3)));
            for (unsigned c = 0; c < 4; c++) {
                if (e.channels >> c & 1) {
                    // The bypass reads the holds directly, as emulator/web.cpp did.
                    raw_holds_[c] = int16_t(std::clamp(sample, -32768, 32767));
                    if (e.time >= t0_) {
                        out_[c].hold(e.time - t0_, sample / 32768.0);
                    }
                }
            }
            // run_until can overshoot a frame by more than a pass (an
            // instruction held in WAIT for its WCS grant), so the pins are
            // also re-aimed here, inside the machine's own row loop. Every
            // pass writes the DAC, so every load gets its own code.
            if (!bypass_) {
                aim_pins();
            }
        };
    }

    AnalogIO(const AnalogIO &) = delete;
    AnalogIO &operator=(const AnalogIO &) = delete;

    ~AnalogIO() {
        host_.audio_observer = {};
    }

    // Push one input frame, and set each channel's converter pins for its
    // next code. The FPC's timing ROM (lexicon224x.hpp, timing_rom) scans one
    // address per row, restarting each pass: the right input (channel 2) is
    // captured at address 39, where the gain counter is armed, and the left
    // (channel 1) at 89 (Fig. 3.5: the load at 38 ends "CONVERT CH 2"). The
    // mux reads each channel's pins only then, so they can be set a frame
    // ahead. Each is sampled 36 rows before its capture, at a CH1 edge (the
    // AIN's own hold edge is CH1L, latched at STBGN 50 rows before the
    // capture; not modeled).
    // Returns the host tick at which this frame ends.
    // Bypass: the path emulator/web.cpp used before this module, byte for byte:
    // each frame puts the frame's sample on the converter pins (gain 0) and
    // the output is the DAC holds at the frame end. The analog chains keep
    // running underneath, so switching back is seamless.
    void set_bypass(bool bypass) {
        bypass_ = bypass;
    }

    bool bypass() const {
        return bypass_;
    }

    cpu::Tick before_frame(float left, float right) {
        // If the host ran without frames (e.g. a silent boot), close the gap:
        // the chains continue as if no time had passed. An ordinary
        // run_until overshoot (up to about a pass) is not a gap: the next
        // frame simply ends sooner.
        cpu::Tick now = host_.now();
        if (now > frame_end_ + gap) {
            t0_ += now - frame_end_;
            frame_end_ = now;
        }
        in_[0].push(left);
        in_[1].push(right);
        frame_end_ += frame;
        aim_pins();
        if (bypass_) {
            host_.set_audio(adc_code(left), 0, adc_code(right), 0);
            host_.set_level_detectors(0, level_detectors(5.0 * left));
            host_.set_level_detectors(1, level_detectors(5.0 * right));
        }
        return frame_end_;
    }

    // The four analog outputs (A-D, DAC bit order) at the end of the frame.
    void after_frame(float out[4]) {
        for (int c = 0; c < 4; c++) {
            float analog = float(out_[c].sample(frame_end_ - t0_));
            out[c] = bypass_ ? raw_holds_[c] / 32768.0f : analog;
        }
    }

private:
    static constexpr cpu::Tick gap = 48 * frame;   // 1 ms

    // Put each channel's code for its next load on the pins, sampling the
    // input chain at that load's hold instant.
    void aim_pins() {
        for (int c = 0; c < 2; c++) {
            cpu::Tick load = next_clock_at_address(c == 0 ? 89 : 39);   // left, right
            if (load != last_load_[c]) {
                last_load_[c] = load;
                cpu::Tick hold = load - 36 * timing_.row;   // the CH1 edge
                cpu::Tick t_in = hold > t0_ + in_[c].latency() ? hold - t0_ - in_[c].latency() : 0;
                double volts = 5.0 * in_[c].sample(t_in);
                Converted k = convert(volts, gain_ranging_);
                code_[c] = k.code;
                iga_[c] = k.iga;
                host_.set_level_detectors(c, level_detectors(volts));
            }
        }
        host_.set_audio(code_[0], iga_[0], code_[1], iga_[1]);
    }

    // emulator/web.cpp's former input mapping, unchanged (float arithmetic kept).
    static unsigned adc_code(float x) {
        int v = int(std::lround(std::clamp(x, -1.0f, 1.0f) * 2047.0f));
        return unsigned(v) & 0xfff;  // 12-bit two's complement
    }

    // The converter clock of row k is at marker(k) + converter_offset (the
    // model's own row timing: emulator/timing.hpp); the
    // scan address advances by one per row and restarts after RESET, so a
    // pass of L rows scans addresses 0..L-1 (100 in MAX DELAY, 105 in the
    // halls). L is measured from the restarts: the clock that scanned
    // address 0 is the next clock minus the current count.
    cpu::Tick next_clock_at_address(unsigned address) {
        using cpu::Tick;
        const cpu::RowTiming &t = timing_;
        Tick now = host_.now();
        uint64_t k = now < t.first_marker + t.converter_offset ? 0 : (now - t.first_marker - t.converter_offset) / t.row + 1;
        if (t.marker(k) + t.converter_offset < now) {
            k++;
        }
        unsigned count = host_.dsp->fpc.count;
        if (count < 255 && k >= count) {   // 255: the scan saturated (no RESET)
            uint64_t restart = k - count;
            if (restart != last_restart_) {
                uint64_t length = restart - last_restart_;
                if (last_restart_ != 0 && length >= 50 && length < 255) {
                    pass_length_ = unsigned(length);
                }
                last_restart_ = restart;
            }
        }
        unsigned ahead = count <= address ? address - count : address + pass_length_ - count;
        return t.marker(k + ahead + 1) + t.converter_offset;
    }

    cpu::Host &host_;
    bool is_224_;
    cpu::RowTiming timing_;
    Modal ain_, aout_;
    Input in_[2];
    Output out_[4];
    bool gain_ranging_;
    bool bypass_ = false;
    int16_t raw_holds_[4] = {0, 0, 0, 0};
    cpu::Tick t0_ = 0, frame_end_ = 0, last_load_[2] = {0, 0};
    uint64_t last_restart_ = 0;
    unsigned pass_length_ = 100;
    unsigned code_[2] = {0, 0}, iga_[2] = {0, 0};
};

}  // namespace lexicon224x::analog
