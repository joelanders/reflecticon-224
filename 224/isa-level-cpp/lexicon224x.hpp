// Lexicon 224X DSP (T&C + ARU + DMEM + XREG + FPC) as a row machine, in C++.
//
// A companion to ../isa-level-verilog/dsp.sv and ../isa-level-verilog/lexicon224x.sv: step_row() runs one WCS
// row, split into fetch / converter clock / execute so a CPU host can
// interleave with them. The hardware spreads each microinstruction over three
// 293 ns rows, and so does this: the ARU is its own pipeline, clocked three
// times a row (aruck(), below), so a multiply finishes in the row after the
// one that loads it. The two SystemVerilog files do each multiply at once;
// their outputs are the same (run.py check).
#pragma once
#include <algorithm>
#include <cstdint>

namespace lexicon224x {

// ---- Bit fields -----------------------------------------------------------
// bits(w, hi, lo) is SystemVerilog's w[hi:lo]; bit(w, n) is w[n].
constexpr unsigned bits(uint32_t w, unsigned hi, unsigned lo) {
    return w >> lo & ((1u << (hi - lo + 1)) - 1);
}

constexpr bool bit(uint32_t w, unsigned n) {
    return w >> n & 1;
}

// ---- The microinstruction (the polarity of the T&C's microinstruction register) ----
enum Operation { NOP = 0, OPER = 1, MEMW = 2, MEMR = 3 };
// OPER bus sources (OFST13:12, the T&C read-select decoder U47B). Source 0
// selects nothing: decoder output 2Y0/ is unwired on both T&C drawings
// (224X 060-02475 sheet 2 at U47 pin 12; 224 060-01317 at U27 2Y0).
enum Source { SelectsNothing = 0, FromRR = 1, FromXREG = 2, FromADC = 3 };

struct Microinstruction {
    unsigned coefficient;   // c, meaning c/32 (0 .. 63/32)
    bool zero, xfer;        // ZERO clears ACC after XFER; XFER: RR := ACC
    bool negative;          // subtract this row's product
    unsigned ra, wa;        // multiplicand register; register the bus is written to
    Operation op;
    uint16_t low;           // MEMR/MEMW: OFST/ (complemented offset)
    // OPER's use of the low half:
    Source source;          // what drives the data bus
    unsigned channels;      // DAC selects, bit 0 = A
    bool wr_da, wr_xreg;
    bool keep_shifting;     // multiplicand := previous one / 64
    bool reset;             // end of pass
    bool protect;           // the SBC may not access the WCS at this row (../emulator/wcs_access.hpp)
};

// The two T&C boards pack the microinstruction differently. The 224X
// (060-02475) is below; the original 224 (060-01317) differs only in the
// fields decode_224() lists. Everything after the decode is the same machine.
enum class Model { Lexicon224X, Lexicon224 };

inline Microinstruction decode_224x(uint32_t w) {
    bool oper = bits(w, 17, 16) == OPER;
    return {
        .coefficient   = bits(w, 31, 26),
        .zero          = bit(w, 25),
        .xfer          = bit(w, 24),
        .negative      = bit(w, 23),
        .ra            = bits(w, 21, 20),
        .wa            = bits(w, 19, 18),
        .op            = Operation(bits(w, 17, 16)),
        .low           = uint16_t(bits(w, 15, 0)),
        .source        = Source(bits(w, 13, 12)),
        .channels      = unsigned(bit(w, 11) | bit(w, 10) << 1 | bit(w, 9) << 2 | bit(w, 8) << 3),  // A..D
        .wr_da         = oper && bit(w, 7),
        .wr_xreg       = oper && bit(w, 6),
        .keep_shifting = oper && bit(w, 4),
        .reset         = oper && bit(w, 3),
        .protect       = bit(w, 22),
    };
}

// The original 224: its microinstruction register has no OFST14/15, so RA
// sits in byte 1 where the 224X put those two address bits, and WA, protect
// and RESET move within byte 2 (224 T&C drawing 060-01317 and the 224's own
// diagnostics). The offset is 14
// bits (16K words of delay memory). RESET is b2[6] on any row: whether the
// T&C also gates it with OPER is not settled, and the stock programs cannot
// tell (their one RESET row is an OPER row).
inline Microinstruction decode_224(uint32_t w) {
    Microinstruction mi = decode_224x(w);
    mi.ra      = bits(w, 15, 14);
    mi.wa      = bits(w, 20, 19);
    mi.low     = uint16_t(bits(w, 13, 0));
    mi.protect = bit(w, 21);
    mi.reset   = bit(w, 22);
    return mi;
}

inline Microinstruction decode(uint32_t w, Model model = Model::Lexicon224X) {
    if (model == Model::Lexicon224) {
        return decode_224(w);
    }
    return decode_224x(w);
}

// ---- The arithmetic -------------------------------------------------------
// Values are sample * 8 (three guard bits). ACC saturates to the 19-bit
// range at every partial-product add; RR takes ACC >> 3. A coefficient is c/32.
constexpr int32_t AccMax = 262143, AccMin = -262144;

// Two coefficient bits per partial product: the higher selects x, the lower
// x/2. |x| <= 2^18, so a partial product never wraps the 20-bit adder.
inline int32_t partial(int32_t x, bool high_bit, bool low_bit) {
    int32_t p = 0;
    if (high_bit) {
        p += x;
    }
    if (low_bit) {
        p += x >> 1;
    }
    return p;
}

// The product as the adder sees it: negated when the row subtracts.
inline int32_t signed_product(int32_t p, bool negative) {
    if (negative) {
        return -p;
    }
    return p;
}

// With |acc| <= 2^18 and |p| < 1.5 * 2^18, the adder's two-top-bits overflow
// test and its clamp toward the addend's sign are exactly this clamp.
inline int32_t accumulate(int32_t acc, int32_t p, bool negative) {
    return std::clamp(acc + signed_product(p, negative), AccMin, AccMax);
}

// The ARU's SAT pin for this add.
inline bool overflows(int32_t acc, int32_t p, bool negative) {
    int32_t sum = acc + signed_product(p, negative);
    return sum > AccMax || sum < AccMin;
}

// ---- The floating-point converter (FPC), one clock per row --------------
// Input: a timing ROM scans the two ADC channels in 50-row halves, loading a
// 12-bit code and shifting it left by the gain range. Output: WR_DA fills a
// waiting latch; the previous conversion must finish before it moves in and
// is normalized (mantissa + gain); 9 rows later the DAC holds see it.
struct Converter {
    unsigned count = 1;         // position in the ROM scan; RESET restarts it
    // Timing-ROM outputs as last published (pins CNVCLK, CH1, STBGN):
    bool conversion_clock = false, ch1 = false, gain_strobe = false, previous_gain_strobe = false;
    unsigned input_gain = 0;    // gain counter: 12 | IGA, counts up; bit 2 enables shifting
    uint16_t input_sample = 0;  // what RD_AD puts on the bus
    uint16_t waiting = 0;       // WR_DA latch
    unsigned waiting_channels = 0;
    bool new_data = false;      // a word is waiting
    uint16_t converting = 0;    // being normalized for the DAC
    unsigned channels = 0, output_gain = 12, age = 0;
    bool busy = true;

    // The DAC holds are selected from row 9 to row 22 of a conversion.
    unsigned output_selects() const {
        if (age >= 9 && age < 23) {
            return channels;
        }
        return 0;
    }

    // Offset binary: inverted sign, then mantissa bits 14:4.
    unsigned dac_code() const {
        return !bit(converting, 15) << 11 | bits(converting, 14, 4);
    }
};

// The FPC timing ROM (U6), one entry per converter clock.
struct TimingRom {
    bool load, conversion_clock, ch1, gain_strobe;
};

constexpr TimingRom timing_rom_entry(unsigned address) {
    if (address >= 100) {
        return {.gain_strobe = true};
    }
    unsigned position = (address + 1) % 100;
    unsigned half = position % 50;
    return {
        .load             = address % 50 >= 38 && address % 50 < 40,
        .conversion_clock = half >= 5 && half <= 41 && (half - 5) % 3 == 0,
        .ch1              = position >= 4 && position < 54,
        .gain_strobe      = (half + 12) % 50 < 13,
    };
}

// The ROM's 256 entries (the count saturates at 255), looked up per row:
// timing_rom_entry's five divisions were 2% of the plugin's time.
struct TimingRomTable {
    TimingRom entry[256];
    constexpr TimingRomTable() : entry{} {
        for (unsigned a = 0; a < 256; a++) {
            entry[a] = timing_rom_entry(a);
        }
    }
};
inline constexpr TimingRomTable timing_rom_table{};

inline TimingRom timing_rom(unsigned address) {
    return timing_rom_table.entry[address & 255];
}

// The input LS194s. The ROM's load strobe and the gain counter's bit 2 pick
// the mode: load the ADC code, shift left (undo the gain range), hold, or --
// the load strobe while the counter is idle -- a nibble-wise right shift.
inline uint16_t move_input(uint16_t s, unsigned adc, bool load, bool shifting) {
    if (load && shifting) {
        return uint16_t(int16_t(adc << 4) >> 4);  // sign-extend 12 bits
    }
    if (shifting) {
        return uint16_t(s << 1);
    }
    if (load) {
        return uint16_t(0x8888 | (s >> 1 & 0x7777));
    }
    return s;
}

inline void clock_converter(Converter &c, unsigned adc_left, unsigned adc_right, unsigned gain_left,
                            unsigned gain_right, bool restart, bool write_dac, unsigned channels,
                            uint16_t bus) {
    Converter n = c;

    // Input: the ADC mux follows CH1 as it was before this clock. The load
    // while CH1 is high (address 38) ends "CONVERT CH 2", and the one while
    // it is low (88) ends "CONVERT CH 1" (Fig. 3.5). Channel 1 is the left
    // input (IN1, level pot R1): the self-test sends it to A and B, the delay
    // lines to A and D.
    TimingRom rom = timing_rom(c.count);
    unsigned adc;
    unsigned adc_gain;
    if (c.ch1) {
        adc = adc_right;
        adc_gain = gain_right;
    } else {
        adc = adc_left;
        adc_gain = gain_left;
    }
    bool shifting = bit(c.input_gain, 2);

    if (restart) {
        n.count = 0;
    } else {
        n.count = std::min(c.count + 1, 255u);
    }

    n.input_sample = move_input(c.input_sample, adc, rom.load, shifting);
    if (rom.load) {
        n.input_gain = 12 | adc_gain;
    } else if (shifting) {
        n.input_gain = (c.input_gain + 1) & 15;
    }
    n.conversion_clock = rom.conversion_clock;
    n.ch1 = rom.ch1;
    n.gain_strobe = rom.gain_strobe;
    n.previous_gain_strobe = c.gain_strobe;

    // Output: start a waiting word, or keep normalizing the current one.
    if (!c.busy && c.new_data) {
        n.converting = c.waiting;
        n.channels = c.waiting_channels;
        n.output_gain = 12;
        n.age = 0;
        n.busy = true;
        n.new_data = false;
    } else if (c.busy) {
        n.age = c.age + 1;
        if (c.output_gain < 15 && bit(c.converting, 15) == bit(c.converting, 14)) {
            n.converting = uint16_t(c.converting << 1 | 1);
            n.output_gain = c.output_gain + 1;
        }
        if (n.age == 22) {
            n.busy = false;
        }
    } else if (c.age == 22) {
        n.age = 23;
    }

    // WR_DA refills the waiting latch after the old word was taken.
    if (write_dac) {
        n.waiting = bus;
        n.waiting_channels = channels;
        n.new_data = true;
    }

    c = n;
}

// ---- The machine -------------------------------------------------------------
// What the T&C drives on one ARU clock edge (see aruck(), below).
enum class OperandClock { Hold, Shift, Load };   // S1 S0 on the operand register's LS194s
struct Aruck {
    OperandClock operand;
    int32_t load_value;     // R[RA] * 8, when loading
    unsigned pair;          // two coefficient bits: M1 (x) and M0 (x/2)
    bool negative;          // the sign that goes with this partial product
    bool zero;              // ZERO: the accumulator takes 0 instead of the sum
};

struct Machine {
    Model model = Model::Lexicon224X;   // set before the first fetch (or call redecode_wcs after)
    uint32_t wcs[128] = {};
    // wcs[] decoded, filled as rows are fetched: decoded[a] is decode(decoded_word[a]).
    // A fetch compares the word, so wcs[] may be written directly (load_wcs,
    // the CPU's commits, tests) and a changed row is decoded once, not every pass.
    Microinstruction decoded[128] = {};
    uint32_t decoded_word[128] = {};
    unsigned pc = 0;
    uint32_t microinstruction = 0;  // the microinstruction register: the word this row executes
    Microinstruction mi = decode(0);  // ... decoded: the MI lines' fields
    uint16_t R[4] = {};             // register file: the only multiply operands
    Microinstruction finishing = decode(0);  // the one before: its multiply finishes this row

    // The ARU's pipeline, one register per stage (see aruck()):
    int32_t operand = 0;            // the multiplicand shift register (x = R[RA] * 8)
    int32_t partial = 0;            // the partial product register
    bool partial_negative = false;  // ... and its sign (CSIGN), kept with it
    int32_t ACC = -1;               // the accumulator (the reference's cleared, inverted partial adds -1)
    int16_t RR = 0;                 // result register: ACC >> 3, saved by XFER
    uint16_t memory[65536] = {};
    uint16_t cpc = 1;               // current position counter: +1 per pass
    // The pass boundary: RESET travels through three flip-flops, a row apart.
    bool counter_clear = false;     // T&C: the last fetch was a RESET row, so the next one clears the PC
    bool restart = false;           // RESET/ on the backplane this row: the FPC restarts its scan
    bool reset_pulse = false;       // DMEM: RESET/ as it sampled it; the position counter steps
    Converter fpc;
    uint16_t xreg_to_cpu = 0;
    unsigned saturated = 0;         // SAT at each of the last row's three ARU clocks (bit 0 first)
    Aruck clocked[3] = {};          // what the T&C drove at those clocks (DPORT3 watches it)

    // Pins: set before each row.
    bool run = true;                // RUN (HALT/ high); low holds the program counter
    unsigned adc_left = 0, adc_right = 0, gain_left = 0, gain_right = 0;  // left = IN1 (channel 1), right = IN2
    uint16_t xreg_from_cpu = 0;
    bool operand_held[3] = {};      // a CPU WCS access holds the operand register at these clocks

    // Output of the last converter clock: the DAC holds took a sample.
    unsigned dac_channels = 0, dac_code = 0, dac_gain = 0;
};

// After the model changes: decode every word again under the new layout.
inline void redecode_wcs(Machine &m) {
    for (unsigned a = 0; a < 128; a++) {
        m.decoded_word[a] = m.wcs[a];
        m.decoded[a] = decode(m.wcs[a], m.model);
    }
}

// The delay memory address of a MEMR or MEMW: CPC - offset. The field holds
// the offset's complement, so this is CPC + field + 1: 16 bits on the 224X,
// 14 on the 224 (16K words).
inline uint16_t memory_address(uint16_t cpc, const Microinstruction &mi, Model model = Model::Lexicon224X) {
    uint16_t address = uint16_t(cpc + mi.low + 1);
    if (model == Model::Lexicon224) {
        return address & 0x3fff;
    }
    return address;
}

// The value this row's source drives onto the bus (before any memory read).
inline uint16_t source_value(const Machine &m, const Microinstruction &mi) {
    if (mi.op == MEMW) {
        return uint16_t(m.RR);
    }
    if (mi.op != OPER) {
        return 0;  // NOP, and MEMR until the memory answers
    }
    if (mi.source == FromXREG) {
        return m.xreg_from_cpu;
    }
    if (mi.source == FromADC) {
        return m.fpc.input_sample;
    }
    if (mi.source == FromRR) {
        return uint16_t(m.RR);
    }
    // Source 0 drives nothing. Like a NOP row, the register file reads the
    // undriven bus as 0: a convention, as the power-on state is, not a
    // measurement. No stock program uses source 0 where the bus is consumed.
    return 0;
}

// A row happens in three parts, at their hardware times. step_row() runs all
// three; a host with a CPU calls them separately (see ../emulator/).

// 1. SEQUENCE (+102 ns). Latch the microinstruction. A RESET row sets the
//    T&C's counter clear, so the next fetch (the pass's last row) also clears
//    the program counter; the T&C then drives RESET/ on the backplane for a
//    row from the latched word. A CPU WCS access can displace the fetch: the
//    register then takes an all-zero word, and pc still advances.
inline void fetch(Machine &m, bool displaced = false) {
    if (displaced) {
        m.microinstruction = 0;
        m.mi = Microinstruction{};     // decode(0): every field zero
    } else {
        uint32_t word = m.wcs[m.pc];
        if (word != m.decoded_word[m.pc]) {
            m.decoded_word[m.pc] = word;
            m.decoded[m.pc] = decode(word, m.model);
        }
        m.microinstruction = word;
        m.mi = m.decoded[m.pc];
    }

    // The 224X clears the PC at the fetch after the RESET row's, so one more
    // row (the flush row) runs before the pass wraps. The 224's pass ends
    // with the RESET row itself: its PC clears at the RESET row's own fetch
    // (the stock programs run 127..28 = 100 rows with RESET at 28; the
    // diagnostics load only the rows up to their RESET row).
    bool clear_pc = m.counter_clear;
    if (m.model == Model::Lexicon224) {
        clear_pc = m.mi.reset;
    }
    if (clear_pc) {
        m.pc = 0;
    } else if (m.run) {
        m.pc = (m.pc + 1) & 127;
    }

    m.restart = m.counter_clear;
    m.counter_clear = m.mi.reset;
}

// 2. CONVERTER CLOCK (+250 ns). WR_DA offers the bus word to the DAC; the
//    ADC side takes one step (restarting its scan in the row after a RESET).
inline void converter_clock(Machine &m) {
    const Microinstruction &mi = m.mi;
    unsigned before = m.fpc.output_selects();
    clock_converter(m.fpc, m.adc_left, m.adc_right, m.gain_left, m.gain_right, m.restart,
                    mi.wr_da, mi.channels, source_value(m, mi));
    m.dac_channels = m.fpc.output_selects() & ~before;
    m.dac_code = m.fpc.dac_code();
    m.dac_gain = m.fpc.output_gain;
}

// ---- The ARU: a pipeline clocked three times a row --------------------------
// The arithmetic unit (060-01318) is three registers in a line, all clocked
// by the same ARUCK edge, three edges a row:
//
//   operand   the multiplicand shift register: loads R[RA] * 8, shifts two
//             places right, or holds;
//   partial   the partial product register: the operand as it was before the
//             edge, times two of the six coefficient bits;
//   ACC       the accumulator: adds the partial product from the edge before
//             (saturating), or takes 0 (ZERO).
//
// So one multiply, x * c/32 = c5 c4 . c3 c2 . c1 c0, takes five edges:
//
//                 ---------- its row ----------   ----------- the next row -----------
//   ARUCK edge     0        1          2           0           1           XFER   2
//   operand                 load x     x/4         x/16        (next load)
//   partial                            x*c5c4      x/4*c3c2    x/16*c1c0
//   ACC                                            +           +                  +
//
// and on every edge two microinstructions share the ARU: the one that loaded
// this row, and the one before it, whose last partial products are added.
// This is why XFER saves "the sum through the previous row" (between edges 1
// and 2, ACC + partial is exactly the previous product's full sum) and why
// ZERO "starts a new sum with this row's product" (at edge 2 it replaces the
// add that would fold the old sum in; this row's first partial comes next).

// ACC + partial, as the adder settles it between edges (XFER samples this).
inline int32_t settled_sum(const Machine &m) {
    return accumulate(m.ACC, m.partial, m.partial_negative);
}

// One edge: every register takes its new value from the others' old ones.
// Returns the SAT pin for this edge's add.
inline bool aruck(Machine &m, const Aruck &c) {
    // One add for the SAT pin and the clamped sum (what overflows() and settled_sum() compute).
    int32_t raw = m.ACC + signed_product(m.partial, m.partial_negative);
    bool sat = raw > AccMax || raw < AccMin;
    int32_t sum = std::clamp(raw, AccMin, AccMax);
    m.partial = partial(m.operand, bit(c.pair, 1), bit(c.pair, 0));
    m.partial_negative = c.negative;
    if (c.operand == OperandClock::Load) {
        m.operand = c.load_value;
    } else if (c.operand == OperandClock::Shift) {
        m.operand >>= 2;
    }
    if (c.zero) {
        m.ACC = 0;
    } else {
        m.ACC = sum;
    }
    return sat;
}

// Load, shift, or hold if a CPU WCS access holds the register at this edge.
inline OperandClock operand_clock(const Machine &m, unsigned edge, OperandClock wanted) {
    if (m.operand_held[edge]) {
        return OperandClock::Hold;
    }
    return wanted;
}

// 3. EXECUTE (early in the next row). The row's three ARU edges, around the
//    bus and register write of the microinstruction fetched last row (`m.mi`,
//    loading) while the one before it (`m.finishing`) completes its multiply.
inline void execute(Machine &m) {
    const Microinstruction &mi = m.mi;
    const Microinstruction &old = m.finishing;
    unsigned sat = 0;

    // ARUCK 0 (at the row marker): the old multiply's second shift and
    // partial product (c3 c2); its first partial is added.
    m.clocked[0] = {operand_clock(m, 0, OperandClock::Shift), 0, bits(old.coefficient, 3, 2), old.negative, false};
    sat |= aruck(m, m.clocked[0]) << 0;

    // DATA BUS: one value per row. MEMW drives RR, MEMR the memory word, NOP
    // nothing (the register file reads an undriven bus as 0). An RD_AD row
    // carries the converter's new word: the clock came before this write.
    uint16_t bus = source_value(m, mi);

    // DELAY MEMORY at CPC - offset (the field holds the complement). DMEM
    // samples RESET/ at the end of each row; a row after it has seen it, the
    // position counter steps: once per pass, before the new pass's first access.
    if (m.reset_pulse) {
        m.cpc++;
    }
    m.reset_pulse = m.restart;
    uint16_t address = memory_address(m.cpc, mi, m.model);
    if (mi.op == MEMW) {
        m.memory[address] = bus;
    }
    if (mi.op == MEMR) {
        bus = m.memory[address];
    }

    // REGISTER WRITE, before ARUCK 1 loads the operand: R[RA] sees it.
    m.R[mi.wa] = bus;
    if (mi.wr_xreg) {
        m.xreg_to_cpu = bus;
    }

    // ARUCK 1: this row's operand loads (keep-shifting shifts the old one on
    // instead); the old multiply's last partial product (c1 c0); its second
    // partial is added.
    OperandClock load = OperandClock::Load;
    if (mi.keep_shifting) {
        load = OperandClock::Shift;
    }
    m.clocked[1] = {operand_clock(m, 1, load), int16_t(m.R[mi.ra]) * 8, bits(old.coefficient, 1, 0), old.negative,
                    false};
    sat |= aruck(m, m.clocked[1]) << 1;

    // XFER: RR takes the settled sum, the old multiply's full product included.
    if (mi.xfer) {
        m.RR = int16_t(settled_sum(m) >> 3);
    }

    // ARUCK 2: the first shift and first partial product (c5 c4) of this
    // row's multiply; the old one's last partial is added, unless ZERO
    // clears the accumulator instead.
    m.clocked[2] = {operand_clock(m, 2, OperandClock::Shift), 0, bits(mi.coefficient, 5, 4), mi.negative, mi.zero};
    sat |= aruck(m, m.clocked[2]) << 2;

    m.saturated = sat;
    m.finishing = mi;
}

inline void step_row(Machine &m) {
    fetch(m);
    converter_clock(m);
    execute(m);
}

// A 512-byte CPU-order WCS image: row a at (a ^ 127) * 4; the bus inverts bytes.
inline void load_wcs(Machine &m, const uint8_t image[512]) {
    for (unsigned a = 0; a < 128; a++) {
        m.wcs[a] = 0;
        for (unsigned lane = 0; lane < 4; lane++) {
            m.wcs[a] |= uint32_t(image[(a ^ 127) * 4 + lane] ^ 0xff) << (8 * lane);
        }
    }
}

}  // namespace lexicon224x
