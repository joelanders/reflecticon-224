// RMX16 DSP: the MCB 5A/5B sequencer, RAM 4 delay memory and three
// multiplier cards, at the level of the microcode (one call per sample).
// This is emu/model/ucmodel.py in C; that model matches the gate-level
// simulation (emu/hdl) RAM write for RAM write. See
// notes/opus-5.5/2026-10-18-0800-reverb-working.md.
//
// Per step s (0..15), words 4s..4s+3 of the 64 x 4-byte microcode store
// (lanes: 0/1 = 16-bit delay offset, 2 = control, 3 = coefficient):
//   - a write pending from step s-1 lands at B + offset[4s-1] (phase 1); one from
//     step 15 lands at step 0 of the next sample with the old B (address latched first)
//   - card c (1..3) adds coef[4s+c-1] * mem[B + offset[4s+c-1]] (phases 3/5/7)
//   - control lo3 = 3+c latches card c's sum (>> 6, saturated) as its output;
//     that op's product starts the next sum. lo3 = 3 selects the input.
//   - control bit 5: write the selected value next step; bit 4: DAC A
//     strobe, bit 6: DAC B strobe (also next step).
// B, the base address, counts up once per sample. Samples are signed 16-bit
// (the hardware bus is offset binary).
#ifndef RMX16_DSP_H
#define RMX16_DSP_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define DSP_RATE 40000.0    // 5.12 MHz master clock / 128: the firmware's 40 samples/ms, and the
                            // only rate at which a real unit's recording matches (notes/opus-5.5/
                            // 2026-11-11-2016; until then 39062.5 from a misread 5 MHz label)

typedef struct {
    int16_t mem[65536];
    int32_t acc[4];
    int16_t out[4];
    uint16_t base;
    // the write/strobe decided in one step, done at the start of the next
    bool pend, pend_write, pend_a, pend_b;
    int16_t pend_val;
    uint8_t pend_word;
    int16_t dac_a, dac_b;   // the DACs hold their last strobed value
} dsp_t;

static inline void dsp_init(dsp_t* d) { memset(d, 0, sizeof *d); }

static inline int16_t dsp_sat(int32_t v) {
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

// One sample period: in = the exponent card's output this sample.
static inline void dsp_sample(dsp_t* d, const uint8_t uc[64][4], int16_t in) {
    d->base++;
    for (int s = 0; s < 16; s++) {
        if (d->pend) {
            if (d->pend_write) {
                const uint8_t* w = uc[d->pend_word];
                // a write flagged at step 15 lands in the next sample, but MCB 5A's 373s latch its
                // address before the base counter ticks: it uses the old B (gate sim; 2026-11-12 note)
                uint16_t b = d->pend_word == 63 ? (uint16_t)(d->base - 1) : d->base;
                d->mem[(uint16_t)(b + (w[0] | w[1] << 8))] = d->pend_val;
            }
            if (d->pend_a) d->dac_a = d->pend_val;
            if (d->pend_b) d->dac_b = d->pend_val;
            d->pend = false;
        }
        uint8_t ctl = uc[4 * s][2], lo = ctl & 7;
        for (int c = 1; c <= 3; c++) {
            const uint8_t* w = uc[4 * s + c - 1];
            int32_t p = (int8_t)w[3] * (int32_t)d->mem[(uint16_t)(d->base + (w[0] | w[1] << 8))];
            if (lo == 3 + c) {
                d->out[c] = dsp_sat(d->acc[c] >> 6);
                d->acc[c] = p;
            } else {
                d->acc[c] += p;
            }
        }
        bool sel = lo >= 3 && lo <= 6;
        if (sel || (ctl & 0x50)) {
            d->pend = true;
            d->pend_val = lo == 3 ? in : sel ? d->out[lo - 3] : 0;
            d->pend_write = ctl & 0x20;   // also for the input (gate sim)
            d->pend_word = (uint8_t)(4 * s + 3);
            d->pend_a = ctl & 0x10;
            d->pend_b = ctl & 0x40;
        }
    }
}
#endif
