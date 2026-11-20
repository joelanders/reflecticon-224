#pragma once
/*#
    # i8085.h

    A cycle-stepped Intel 8085A in a C header, in the style of floooh's
    `chips/z80.h` (https://github.com/floooh/chips, zlib license; see
    LICENSE-floooh-chips). One call to `i8085_tick()` runs one clock state
    (T1..T6, TWAIT or THALT) and returns the CPU's pins.

    Derived from the Lexicon project's cycle-stepped 8080 (same generator
    design). The ALU arithmetic is ported from Pedro Minicz's m8080 (2019,
    MIT license; see LICENSE-m8080).

    Parts of this file between `// <% name` and `// %>` are written by
    `codegen/i8085_gen.py` from `codegen/i8085_desc.yml`. Edit those two
    files, not the generated parts.

    Source for the timing: Intel MCS-80/85 Family User's Manual (Jan 1983),
    chapter 2 (Figures 2-9 to 2-20) and Table 5-1.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the
    implementation (or define `I8085_API` as `static` and include it with
    CHIPS_IMPL in every file that uses it).

    Define `I8085_8080_FLAGS` before including it to get the 8080's flag
    rules instead of the 8085's (for running 8080 test programs): ANA/ANI set
    AC to the OR of bit 3 of the operands (8085: always 1), and flag bit 1
    reads 1 (8085: bits 1, 3 and 5 are undefined; here they read 0).

    ## Pins

    ***************************************
    *            +-----------+            *
    *  ALE   <---|           |---> A8     *
    *  RD    <---|           |---> ..     *
    *  WR    <---|           |---> A15    *
    *  IO/M  <---|           |            *
    *  S0    <---|           |<--> AD0    *
    *  S1    <---|   8085    |<--> ..     *
    *  SOD   <---|           |<--> AD7    *
    *  READY --->|           |            *
    *  TRAP  --->|           |<--- SID    *
    *  RST7.5--->|           |            *
    *  RST6.5--->|           |            *
    *  RST5.5--->|           |            *
    *            +-----------+            *
    ***************************************

    All pins are positive logic here: I8085_RD set means the chip's RD/ pin
    is low (reading). I8085_WAIT is not a pin: it marks a TWAIT state for
    the host's convenience. Not modeled: INTR/INTA (the RMX16 processor board
    ties INTR to ground), HOLD/HLDA (also grounded), CLK out, RESET OUT.

    ## What the pins mean after each tick

    | state  | pins                                                         |
    |--------|--------------------------------------------------------------|
    | T1     | ALE; A8-A15 and AD0-AD7 = address; IO/M S1 S0 = cycle type    |
    | T2     | a read: RD (put the byte on AD). A write: WR, data on AD     |
    | TWAIT  | as T2, plus I8085_WAIT                                       |
    | T3     | a read has taken AD; a write still has WR and its data       |
    | T4-T6  | opcode fetch only: internal                                  |
    | THALT  | halted: status S1 S0 = 00                                    |

    IO/M, S1 and S0 stay valid for the whole machine cycle. A8-A15 hold the
    high address from T1 on; the low address is on AD0-AD7 only in T1, so a
    host latches it on ALE (as the 74LS373 on the RMX16 processor board does).
    `I8085_GET_ADDR(pins)` gives the full address while ALE is set.

    Inputs the CPU looks at:
    - **READY** is looked at by the tick after T2 or TWAIT (except in bus
      idle cycles). If it is low, that tick is another TWAIT.
    - **AD** for a read is taken by the tick that runs T3. So when a returned
      state has RD, put the byte on AD before the next call.
    - **TRAP, RST7.5, RST6.5, RST5.5** are sampled every tick. RST7.5 and
      TRAP rising edges set internal flip-flops. The decision to take an
      interrupt uses the levels sampled two states before the next opcode
      fetch (and every THALT state), as the manual says.
    - **SID** is read by RIM.

    ## Usage

    ~~~C
        i8085_t cpu;
        uint64_t pins = i8085_init(&cpu);
        uint8_t mem[1 << 16];
        uint16_t addr = 0;
        for (;;) {
            pins = i8085_tick(&cpu, pins | I8085_READY);
            if (pins & I8085_ALE) addr = I8085_GET_ADDR(pins);
            if (pins & I8085_RD) I8085_SET_AD(pins, mem[addr]);
            if (pins & I8085_WR) mem[addr] = I8085_GET_AD(pins);
        }
    ~~~
#*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// multiplexed address/data and high address pins
#define I8085_PIN_AD0   (0)
#define I8085_PIN_AD7   (7)
#define I8085_PIN_A8    (8)
#define I8085_PIN_A15   (15)
// outputs
#define I8085_PIN_ALE   (16)    // address latch enable (T1)
#define I8085_PIN_RD    (17)    // RD/ low: put the read byte on AD
#define I8085_PIN_WR    (18)    // WR/ low: AD holds the byte being written
#define I8085_PIN_IOM   (19)    // IO/M: 1 = I/O cycle (and interrupt cycle)
#define I8085_PIN_S0    (20)
#define I8085_PIN_S1    (21)
#define I8085_PIN_SOD   (22)    // serial output data (SIM)
#define I8085_PIN_WAIT  (23)    // not a pin: this tick was a TWAIT state
// inputs
#define I8085_PIN_READY (32)
#define I8085_PIN_TRAP  (33)
#define I8085_PIN_RST75 (34)
#define I8085_PIN_RST65 (35)
#define I8085_PIN_RST55 (36)
#define I8085_PIN_SID   (37)

#define I8085_ALE   (1ULL<<I8085_PIN_ALE)
#define I8085_RD    (1ULL<<I8085_PIN_RD)
#define I8085_WR    (1ULL<<I8085_PIN_WR)
#define I8085_IOM   (1ULL<<I8085_PIN_IOM)
#define I8085_S0    (1ULL<<I8085_PIN_S0)
#define I8085_S1    (1ULL<<I8085_PIN_S1)
#define I8085_SOD   (1ULL<<I8085_PIN_SOD)
#define I8085_WAIT  (1ULL<<I8085_PIN_WAIT)
#define I8085_READY (1ULL<<I8085_PIN_READY)
#define I8085_TRAP  (1ULL<<I8085_PIN_TRAP)
#define I8085_RST75 (1ULL<<I8085_PIN_RST75)
#define I8085_RST65 (1ULL<<I8085_PIN_RST65)
#define I8085_RST55 (1ULL<<I8085_PIN_RST55)
#define I8085_SID   (1ULL<<I8085_PIN_SID)

// the outputs that are recomputed on every tick
#define I8085_CTRL_PIN_MASK (I8085_ALE|I8085_RD|I8085_WR|I8085_IOM|I8085_S0|I8085_S1|I8085_SOD|I8085_WAIT)

#define I8085_GET_ADDR(p) ((uint16_t)(p))
#define I8085_GET_AD(p) ((uint8_t)(p))
#define I8085_SET_AD(p,d) {p=((p)&~0xFFULL)|((d)&0xFFULL);}
#define I8085_SET_ADDR(p,a) {p=((p)&~0xFFFFULL)|((a)&0xFFFFULL);}

// Machine-cycle status: IO/M, S1, S0 (Figure 2-10).
#define I8085_STATUS_MASK (I8085_IOM|I8085_S1|I8085_S0)
#define I8085_STATUS_OF   (I8085_S1|I8085_S0)            // opcode fetch
#define I8085_STATUS_MR   (I8085_S1)                     // memory read
#define I8085_STATUS_MW   (I8085_S0)                     // memory write
#define I8085_STATUS_IOR  (I8085_IOM|I8085_S1)           // I/O read
#define I8085_STATUS_IOW  (I8085_IOM|I8085_S0)           // I/O write
#define I8085_STATUS_BI   (I8085_S1)                     // bus idle (DAD): as MR, no RD, no ALE
#define I8085_STATUS_INT  (I8085_IOM|I8085_S1|I8085_S0)  // bus idle (RST/TRAP restart)
#define I8085_STATUS_HALT (0)                            // IO/M floats; shown as 0

// Flag bits in F: S Z x AC x P x CY.
#define I8085_CF (1<<0)
#define I8085_PF (1<<2)
#define I8085_HF (1<<4)     // auxiliary carry
#define I8085_ZF (1<<6)
#define I8085_SF (1<<7)

enum {
    I8085_ALU_ADD, I8085_ALU_ADC, I8085_ALU_SUB, I8085_ALU_SBB,
    I8085_ALU_ANA, I8085_ALU_XRA, I8085_ALU_ORA, I8085_ALU_CMP,
};
enum { I8085_ROT_RLC, I8085_ROT_RRC, I8085_ROT_RAL, I8085_ROT_RAR };

// interrupt sample bits (raw, before masks and INTE)
#define I8085_IRQ_TRAP  (1<<0)
#define I8085_IRQ_RST75 (1<<1)
#define I8085_IRQ_RST65 (1<<2)
#define I8085_IRQ_RST55 (1<<3)

typedef struct {
    uint16_t step;          // the next decoder step (which state of which instruction)
    uint8_t opcode;         // instruction register
    bool inte;              // interrupt enable flip-flop
    bool unimplemented;     // hit an opcode the description file does not cover
    uint8_t tmp;            // operand temporary
    uint64_t status;        // IO/M S1 S0 of the current machine cycle
    // interrupts (Figures 2-4 to 2-6)
    uint8_t mask;           // RST mask bits: 0 = 5.5, 1 = 6.5, 2 = 7.5 (1 = masked)
    bool rst75_ff;          // RST 7.5 edge flip-flop
    bool trap_ff;           // TRAP edge flip-flop
    bool rst75_prev, trap_prev;
    bool trap_ie_valid;     // RIM right after TRAP reports the IE from before it
    bool trap_ie;
    bool sod;               // serial output flip-flop
    uint8_t irq_sample[3];  // raw interrupt samples of this tick and the two before
    uint16_t int_vector;    // the restart address of the interrupt being taken
    union { struct { uint8_t f; uint8_t a; }; uint16_t psw; };
    union { struct { uint8_t c; uint8_t b; }; uint16_t bc; };
    union { struct { uint8_t e; uint8_t d; }; uint16_t de; };
    union { struct { uint8_t l; uint8_t h; }; uint16_t hl; };
    union { struct { uint8_t z; uint8_t w; }; uint16_t wz; };
    union { struct { uint8_t spl; uint8_t sph; }; uint16_t sp; };
    union { struct { uint8_t pcl; uint8_t pch; }; uint16_t pc; };
} i8085_t;

#ifndef I8085_API
#define I8085_API
#endif

// initialize a new 8085 and return the initial pins
I8085_API uint64_t i8085_init(i8085_t* cpu);
// RESET IN: PC, INTE, the RST 7.5 and TRAP flip-flops and SOD cleared, all
// RST masks set (Section 2.3.7); next state is T1 of a fetch
I8085_API uint64_t i8085_reset(i8085_t* cpu);
// run one clock state, return the pins at its end
I8085_API uint64_t i8085_tick(i8085_t* cpu, uint64_t pins);
// true when the next tick starts a new instruction (T1 of a fetch)
I8085_API bool i8085_opdone(const i8085_t* cpu);
// true when halted (in THALT)
I8085_API bool i8085_halted(const i8085_t* cpu);

#ifdef __cplusplus
} // extern "C"
#endif

//-- IMPLEMENTATION ------------------------------------------------------------
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_ASSERT
#include <assert.h>
#define CHIPS_ASSERT(c) assert(c)
#endif

// shared decoder steps (after the generated ones)
// <% extra_step_defines
#define I8085_M1_T1 902
#define I8085_M1_T2 903
#define I8085_M1_T3 904
#define I8085_HALT 905
#define I8085_INT_T1 906
#define I8085_INT_T2 907
#define I8085_INT_T3 908
#define I8085_INT_T4 909
#define I8085_INT_T5 910
#define I8085_INT_T6 911
#define I8085_INT_M2_T1 912
#define I8085_INT_M2_T2 913
#define I8085_INT_M2_T3 914
#define I8085_INT_M3_T1 915
#define I8085_INT_M3_T2 916
#define I8085_INT_M3_T3 917
// %>

uint64_t i8085_init(i8085_t* cpu) {
    CHIPS_ASSERT(cpu);
    memset(cpu, 0, sizeof(i8085_t));
#ifdef I8085_8080_FLAGS
    cpu->f = 0x02;
#endif
    return i8085_reset(cpu);
}

uint64_t i8085_reset(i8085_t* cpu) {
    cpu->pc = 0;
    cpu->inte = false;
    cpu->mask = 7;
    cpu->rst75_ff = false;
    cpu->trap_ff = false;
    cpu->trap_ie_valid = false;
    cpu->sod = false;
    cpu->unimplemented = false;
    cpu->step = I8085_M1_T1;
    return 0;
}

bool i8085_opdone(const i8085_t* cpu) {
    return cpu->step == I8085_M1_T1;
}

bool i8085_halted(const i8085_t* cpu) {
    return cpu->step == I8085_HALT;
}

// ---- ALU ------------------------------------------------------------------
// Ported from m8080 (MIT). Flags: S Z x AC x P x CY.

#ifdef I8085_8080_FLAGS
#define _I8085_FBASE 0x02
#else
#define _I8085_FBASE 0x00
#endif

static inline uint8_t _i8085_szp(uint8_t v) {
    uint8_t p = v;
    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    uint8_t flags = v & I8085_SF;
    if (v == 0) {
        flags |= I8085_ZF;
    }
    if (!(p & 1)) {
        flags |= I8085_PF;
    }
    return flags;
}

// the flag byte as POP PSW loads it: the undefined bits read as above
static inline uint8_t _i8085_fixf(uint8_t f) {
    return (uint8_t)((f & 0xD5) | _I8085_FBASE);
}

// a + b + carry: AC from bit 3, CY from bit 7
static inline uint8_t _i8085_add(i8085_t* cpu, uint8_t a, uint8_t b, unsigned carry_in) {
    unsigned res = (unsigned)a + b + carry_in;
    uint8_t flags = _I8085_FBASE | _i8085_szp((uint8_t)res);
    if (res >> 8) {
        flags |= I8085_CF;
    }
    if (((a & 0x0F) + (b & 0x0F) + carry_in) >> 4) {
        flags |= I8085_HF;
    }
    cpu->f = flags;
    return (uint8_t)res;
}

// a - b - borrow, as the adder does it (a + ~b + 1): AC is set when there is
// no borrow out of bit 3, CY when there is a borrow out of bit 7. Assumed the
// same on the 8085 as on the 8080; the 8085 manual does not say otherwise.
static inline uint8_t _i8085_sub(i8085_t* cpu, uint8_t a, uint8_t b, unsigned borrow_in) {
    int res = (int)a - (int)b - (int)borrow_in;
    int low = (int)(a & 0x0F) - (int)(b & 0x0F) - (int)borrow_in;
    uint8_t flags = _I8085_FBASE | _i8085_szp((uint8_t)res);
    if (res < 0) {
        flags |= I8085_CF;
    }
    if (low >= 0) {
        flags |= I8085_HF;
    }
    cpu->f = flags;
    return (uint8_t)res;
}

static inline void _i8085_alu(i8085_t* cpu, int op, uint8_t t) {
    uint8_t a = cpu->a;
    unsigned cy = cpu->f & I8085_CF;
    switch (op) {
        case I8085_ALU_ADD: cpu->a = _i8085_add(cpu, a, t, 0); break;
        case I8085_ALU_ADC: cpu->a = _i8085_add(cpu, a, t, cy); break;
        case I8085_ALU_SUB: cpu->a = _i8085_sub(cpu, a, t, 0); break;
        case I8085_ALU_SBB: cpu->a = _i8085_sub(cpu, a, t, cy); break;
        case I8085_ALU_CMP: (void)_i8085_sub(cpu, a, t, 0); break;
        case I8085_ALU_ANA:
            cpu->a = a & t;
            cpu->f = _I8085_FBASE | _i8085_szp(cpu->a);
#ifdef I8085_8080_FLAGS
            // 8080: AC is the OR of bit 3 of the two operands
            if ((a | t) & 0x08) {
                cpu->f |= I8085_HF;
            }
#else
            // 8085: "logical AND instructions always set the auxiliary flag
            // ON" (8080/8085 Assembly Language Programming Manual, p. 1-24)
            cpu->f |= I8085_HF;
#endif
            break;
        case I8085_ALU_XRA:
            cpu->a = a ^ t;
            cpu->f = _I8085_FBASE | _i8085_szp(cpu->a);
            break;
        case I8085_ALU_ORA:
            cpu->a = a | t;
            cpu->f = _I8085_FBASE | _i8085_szp(cpu->a);
            break;
        default:
            break;
    }
}

// rotates change only CY
static inline void _i8085_rot(i8085_t* cpu, int op) {
    uint8_t a = cpu->a;
    unsigned cy = cpu->f & I8085_CF;
    switch (op) {
        case I8085_ROT_RLC:
            cpu->a = (uint8_t)(a << 1 | a >> 7);
            cpu->f = (uint8_t)((cpu->f & ~I8085_CF) | (a >> 7));
            break;
        case I8085_ROT_RRC:
            cpu->a = (uint8_t)(a >> 1 | a << 7);
            cpu->f = (uint8_t)((cpu->f & ~I8085_CF) | (a & 1));
            break;
        case I8085_ROT_RAL:
            cpu->a = (uint8_t)(a << 1 | cy);
            cpu->f = (uint8_t)((cpu->f & ~I8085_CF) | (a >> 7));
            break;
        case I8085_ROT_RAR:
            cpu->a = (uint8_t)(a >> 1 | cy << 7);
            cpu->f = (uint8_t)((cpu->f & ~I8085_CF) | (a & 1));
            break;
        default:
            break;
    }
}

// INR/DCR: S Z P from the result, AC as for an add/subtract of 1; CY unchanged.
static inline uint8_t _i8085_inr(i8085_t* cpu, uint8_t v) {
    uint8_t res = (uint8_t)(v + 1);
    cpu->f = (uint8_t)((cpu->f & I8085_CF) | _I8085_FBASE | _i8085_szp(res));
    if ((res & 0x0F) == 0) {
        cpu->f |= I8085_HF;
    }
    return res;
}

static inline uint8_t _i8085_dcr(i8085_t* cpu, uint8_t v) {
    uint8_t res = (uint8_t)(v - 1);
    cpu->f = (uint8_t)((cpu->f & I8085_CF) | _I8085_FBASE | _i8085_szp(res));
    if ((res & 0x0F) != 0x0F) {
        cpu->f |= I8085_HF;
    }
    return res;
}

// One half of DAD: *dst + v (+ CY for the high half) into *dst; CY only.
static inline void _i8085_dad(i8085_t* cpu, uint8_t* dst, uint8_t v, bool with_carry) {
    unsigned carry_in = with_carry ? (cpu->f & I8085_CF) : 0;
    unsigned res = (unsigned)*dst + v + carry_in;
    *dst = (uint8_t)res;
    cpu->f = (uint8_t)((cpu->f & ~I8085_CF) | (res >> 8));
}

// DAA (as m8080)
static inline void _i8085_daa(i8085_t* cpu) {
    uint8_t a = cpu->a;
    uint8_t cy = cpu->f & I8085_CF;
    uint8_t ac = cpu->f & I8085_HF;
    if (ac || (a & 0x0F) > 0x09) {
        cy |= (uint8_t)((a + 0x06) >> 8);
        ac = (((a & 0x0F) + 0x06) >> 4) ? I8085_HF : 0;
        a = (uint8_t)(a + 0x06);
    }
    if (cy || (a & 0xF0) > 0x90) {
        cy |= (uint8_t)((a + 0x60) >> 8);
        a = (uint8_t)(a + 0x60);
    }
    cpu->a = a;
    cpu->f = (uint8_t)(_I8085_FBASE | _i8085_szp(a) | ac | cy);
}

// RIM (Figure 2-5): SID, pending 7.5 6.5 5.5, IE, masks 7.5 6.5 5.5.
// Pending 6.5/5.5 are the input levels; right after TRAP, IE is the value
// from before the TRAP.
static inline void _i8085_rim(i8085_t* cpu, uint64_t in) {
    bool ie = cpu->trap_ie_valid ? cpu->trap_ie : cpu->inte;
    cpu->trap_ie_valid = false;
    cpu->a = (uint8_t)(((in & I8085_SID) ? 0x80 : 0)
        | (cpu->rst75_ff ? 0x40 : 0)
        | ((in & I8085_RST65) ? 0x20 : 0)
        | ((in & I8085_RST55) ? 0x10 : 0)
        | (ie ? 0x08 : 0)
        | (cpu->mask & 7));
}

// SIM (Figures 2-4, 2-7): bit 3 MSE loads the masks from bits 2-0; bit 4
// resets the RST 7.5 flip-flop; bit 6 SDE loads SOD from bit 7.
static inline void _i8085_sim(i8085_t* cpu) {
    uint8_t a = cpu->a;
    if (a & 0x08) {
        cpu->mask = a & 7;
    }
    if (a & 0x10) {
        cpu->rst75_ff = false;
    }
    if (a & 0x40) {
        cpu->sod = (a & 0x80) != 0;
    }
}

// The interrupt to take, by priority (TRAP, 7.5, 6.5, 5.5), from a sample
// of the TRAP and RST 7.5 flip-flops and the 6.5/5.5 levels, with the masks
// and INTE as they are now (an edge flip-flop must also still be set: SIM or
// a restart may have cleared it since). Returns the restart address, or 0.
static inline uint16_t _i8085_select(const i8085_t* cpu, uint8_t sample) {
    if ((sample & I8085_IRQ_TRAP) && cpu->trap_ff) {
        return 0x24;
    }
    if (!cpu->inte) {
        return 0;
    }
    if ((sample & I8085_IRQ_RST75) && cpu->rst75_ff && !(cpu->mask & 4)) {
        return 0x3C;
    }
    if ((sample & I8085_IRQ_RST65) && !(cpu->mask & 2)) {
        return 0x34;
    }
    if ((sample & I8085_IRQ_RST55) && !(cpu->mask & 1)) {
        return 0x2C;
    }
    return 0;
}

// Take an interrupt: INTE off, the 7.5 or TRAP flip-flop cleared.
static inline void _i8085_take(i8085_t* cpu, uint16_t vector) {
    if (vector == 0x24) {
        cpu->trap_ff = false;
        cpu->trap_ie = cpu->inte;
        cpu->trap_ie_valid = true;
    } else if (vector == 0x3C) {
        cpu->rst75_ff = false;
    }
    cpu->inte = false;
    cpu->int_vector = vector;
}

// ---- state helpers ----------------------------------------------------------
// Each generated `case` is one clock state. `in` is the pins passed in; `pins`
// is what this state returns.
#define _goto(n)        cpu->step=(n);goto step_to
#define _fetch()        _goto(I8085_M1_T1)
#define _unimplemented() cpu->unimplemented=true;goto step_to
#define _gd()           I8085_GET_AD(in)
// T1: ALE, address on A8-A15 and AD0-AD7, the cycle's status
#define _t1(ab,st)      I8085_SET_ADDR(pins,ab);cpu->status=(st);pins|=I8085_ALE
// T2 of a read: RD
#define _rd()           pins|=I8085_RD
// T2 of a write: WR and the data on AD
#define _wr(d)          pins|=I8085_WR;I8085_SET_AD(pins,d)
// T3 of a read: if READY was low, this tick is a TWAIT instead (same step next time)
#define _ready_rd()     if(!(in&I8085_READY)){pins|=I8085_WAIT|I8085_RD;goto step_to;}pins|=I8085_RD
// T3 of a write: WR stays low through TWAIT and T3
#define _ready_wr()     pins|=I8085_WR;if(!(in&I8085_READY)){pins|=I8085_WAIT;goto step_to;}
// a bus idle state: the status only
#define _bi(st)         cpu->status=(st)

uint64_t i8085_tick(i8085_t* cpu, uint64_t pins) {
    const uint64_t in = pins;
    pins &= ~I8085_CTRL_PIN_MASK;
    // Interrupt inputs, every state: the edge flip-flops, and the raw levels
    // (kept for three ticks: the decision uses the one two states back).
    {
        bool r75 = (in & I8085_RST75) != 0;
        if (r75 && !cpu->rst75_prev) {
            cpu->rst75_ff = true;
        }
        cpu->rst75_prev = r75;
        bool trap = (in & I8085_TRAP) != 0;
        if (trap && !cpu->trap_prev) {
            cpu->trap_ff = true;
        }
        cpu->trap_prev = trap;
        cpu->irq_sample[2] = cpu->irq_sample[1];
        cpu->irq_sample[1] = cpu->irq_sample[0];
        cpu->irq_sample[0] = (uint8_t)((trap && cpu->trap_ff ? I8085_IRQ_TRAP : 0)
            | (cpu->rst75_ff ? I8085_IRQ_RST75 : 0)
            | ((in & I8085_RST65) ? I8085_IRQ_RST65 : 0)
            | ((in & I8085_RST55) ? I8085_IRQ_RST55 : 0));
    }
    switch (cpu->step) {
        // <% decoder
        case    0: _fetch(); // NOP M1 T4
        case    1: _goto(256); // LXI B,d16 M1 T4
        case    2: _goto(262); // STAX B M1 T4
        case    3: cpu->bc++;_goto(265); // INX B M1 T4
        case    4: cpu->b=_i8085_inr(cpu,cpu->b);_fetch(); // INR B M1 T4
        case    5: cpu->b=_i8085_dcr(cpu,cpu->b);_fetch(); // DCR B M1 T4
        case    6: _goto(267); // MVI B,d8 M1 T4
        case    7: _i8085_rot(cpu,I8085_ROT_RLC);_fetch(); // RLC M1 T4
        case    8: _unimplemented(); // unimplemented 08 M1 T4
        case    9: _goto(270); // DAD B M1 T4
        case   10: _goto(276); // LDAX B M1 T4
        case   11: cpu->bc--;_goto(279); // DCX B M1 T4
        case   12: cpu->c=_i8085_inr(cpu,cpu->c);_fetch(); // INR C M1 T4
        case   13: cpu->c=_i8085_dcr(cpu,cpu->c);_fetch(); // DCR C M1 T4
        case   14: _goto(281); // MVI C,d8 M1 T4
        case   15: _i8085_rot(cpu,I8085_ROT_RRC);_fetch(); // RRC M1 T4
        case   16: _unimplemented(); // unimplemented 10 M1 T4
        case   17: _goto(284); // LXI D,d16 M1 T4
        case   18: _goto(290); // STAX D M1 T4
        case   19: cpu->de++;_goto(293); // INX D M1 T4
        case   20: cpu->d=_i8085_inr(cpu,cpu->d);_fetch(); // INR D M1 T4
        case   21: cpu->d=_i8085_dcr(cpu,cpu->d);_fetch(); // DCR D M1 T4
        case   22: _goto(295); // MVI D,d8 M1 T4
        case   23: _i8085_rot(cpu,I8085_ROT_RAL);_fetch(); // RAL M1 T4
        case   24: _unimplemented(); // unimplemented 18 M1 T4
        case   25: _goto(298); // DAD D M1 T4
        case   26: _goto(304); // LDAX D M1 T4
        case   27: cpu->de--;_goto(307); // DCX D M1 T4
        case   28: cpu->e=_i8085_inr(cpu,cpu->e);_fetch(); // INR E M1 T4
        case   29: cpu->e=_i8085_dcr(cpu,cpu->e);_fetch(); // DCR E M1 T4
        case   30: _goto(309); // MVI E,d8 M1 T4
        case   31: _i8085_rot(cpu,I8085_ROT_RAR);_fetch(); // RAR M1 T4
        case   32: _i8085_rim(cpu,in);_fetch(); // RIM M1 T4
        case   33: _goto(312); // LXI H,d16 M1 T4
        case   34: _goto(318); // SHLD a16 M1 T4
        case   35: cpu->hl++;_goto(330); // INX H M1 T4
        case   36: cpu->h=_i8085_inr(cpu,cpu->h);_fetch(); // INR H M1 T4
        case   37: cpu->h=_i8085_dcr(cpu,cpu->h);_fetch(); // DCR H M1 T4
        case   38: _goto(332); // MVI H,d8 M1 T4
        case   39: _i8085_daa(cpu);_fetch(); // DAA M1 T4
        case   40: _unimplemented(); // unimplemented 28 M1 T4
        case   41: _goto(335); // DAD H M1 T4
        case   42: _goto(341); // LHLD a16 M1 T4
        case   43: cpu->hl--;_goto(353); // DCX H M1 T4
        case   44: cpu->l=_i8085_inr(cpu,cpu->l);_fetch(); // INR L M1 T4
        case   45: cpu->l=_i8085_dcr(cpu,cpu->l);_fetch(); // DCR L M1 T4
        case   46: _goto(355); // MVI L,d8 M1 T4
        case   47: cpu->a=~cpu->a;_fetch(); // CMA M1 T4
        case   48: _i8085_sim(cpu);_fetch(); // SIM M1 T4
        case   49: _goto(358); // LXI SP,d16 M1 T4
        case   50: _goto(364); // STA a16 M1 T4
        case   51: cpu->sp++;_goto(373); // INX SP M1 T4
        case   52: _goto(375); // INR M M1 T4
        case   53: _goto(381); // DCR M M1 T4
        case   54: _goto(387); // MVI M,d8 M1 T4
        case   55: cpu->f|=I8085_CF;_fetch(); // STC M1 T4
        case   56: _unimplemented(); // unimplemented 38 M1 T4
        case   57: _goto(393); // DAD SP M1 T4
        case   58: _goto(399); // LDA a16 M1 T4
        case   59: cpu->sp--;_goto(408); // DCX SP M1 T4
        case   60: cpu->a=_i8085_inr(cpu,cpu->a);_fetch(); // INR A M1 T4
        case   61: cpu->a=_i8085_dcr(cpu,cpu->a);_fetch(); // DCR A M1 T4
        case   62: _goto(410); // MVI A,d8 M1 T4
        case   63: cpu->f^=I8085_CF;_fetch(); // CMC M1 T4
        case   64: cpu->b=cpu->b;_fetch(); // MOV B,B M1 T4
        case   65: cpu->b=cpu->c;_fetch(); // MOV B,C M1 T4
        case   66: cpu->b=cpu->d;_fetch(); // MOV B,D M1 T4
        case   67: cpu->b=cpu->e;_fetch(); // MOV B,E M1 T4
        case   68: cpu->b=cpu->h;_fetch(); // MOV B,H M1 T4
        case   69: cpu->b=cpu->l;_fetch(); // MOV B,L M1 T4
        case   70: _goto(413); // MOV B,M M1 T4
        case   71: cpu->b=cpu->a;_fetch(); // MOV B,A M1 T4
        case   72: cpu->c=cpu->b;_fetch(); // MOV C,B M1 T4
        case   73: cpu->c=cpu->c;_fetch(); // MOV C,C M1 T4
        case   74: cpu->c=cpu->d;_fetch(); // MOV C,D M1 T4
        case   75: cpu->c=cpu->e;_fetch(); // MOV C,E M1 T4
        case   76: cpu->c=cpu->h;_fetch(); // MOV C,H M1 T4
        case   77: cpu->c=cpu->l;_fetch(); // MOV C,L M1 T4
        case   78: _goto(416); // MOV C,M M1 T4
        case   79: cpu->c=cpu->a;_fetch(); // MOV C,A M1 T4
        case   80: cpu->d=cpu->b;_fetch(); // MOV D,B M1 T4
        case   81: cpu->d=cpu->c;_fetch(); // MOV D,C M1 T4
        case   82: cpu->d=cpu->d;_fetch(); // MOV D,D M1 T4
        case   83: cpu->d=cpu->e;_fetch(); // MOV D,E M1 T4
        case   84: cpu->d=cpu->h;_fetch(); // MOV D,H M1 T4
        case   85: cpu->d=cpu->l;_fetch(); // MOV D,L M1 T4
        case   86: _goto(419); // MOV D,M M1 T4
        case   87: cpu->d=cpu->a;_fetch(); // MOV D,A M1 T4
        case   88: cpu->e=cpu->b;_fetch(); // MOV E,B M1 T4
        case   89: cpu->e=cpu->c;_fetch(); // MOV E,C M1 T4
        case   90: cpu->e=cpu->d;_fetch(); // MOV E,D M1 T4
        case   91: cpu->e=cpu->e;_fetch(); // MOV E,E M1 T4
        case   92: cpu->e=cpu->h;_fetch(); // MOV E,H M1 T4
        case   93: cpu->e=cpu->l;_fetch(); // MOV E,L M1 T4
        case   94: _goto(422); // MOV E,M M1 T4
        case   95: cpu->e=cpu->a;_fetch(); // MOV E,A M1 T4
        case   96: cpu->h=cpu->b;_fetch(); // MOV H,B M1 T4
        case   97: cpu->h=cpu->c;_fetch(); // MOV H,C M1 T4
        case   98: cpu->h=cpu->d;_fetch(); // MOV H,D M1 T4
        case   99: cpu->h=cpu->e;_fetch(); // MOV H,E M1 T4
        case  100: cpu->h=cpu->h;_fetch(); // MOV H,H M1 T4
        case  101: cpu->h=cpu->l;_fetch(); // MOV H,L M1 T4
        case  102: _goto(425); // MOV H,M M1 T4
        case  103: cpu->h=cpu->a;_fetch(); // MOV H,A M1 T4
        case  104: cpu->l=cpu->b;_fetch(); // MOV L,B M1 T4
        case  105: cpu->l=cpu->c;_fetch(); // MOV L,C M1 T4
        case  106: cpu->l=cpu->d;_fetch(); // MOV L,D M1 T4
        case  107: cpu->l=cpu->e;_fetch(); // MOV L,E M1 T4
        case  108: cpu->l=cpu->h;_fetch(); // MOV L,H M1 T4
        case  109: cpu->l=cpu->l;_fetch(); // MOV L,L M1 T4
        case  110: _goto(428); // MOV L,M M1 T4
        case  111: cpu->l=cpu->a;_fetch(); // MOV L,A M1 T4
        case  112: _goto(431); // MOV M,B M1 T4
        case  113: _goto(434); // MOV M,C M1 T4
        case  114: _goto(437); // MOV M,D M1 T4
        case  115: _goto(440); // MOV M,E M1 T4
        case  116: _goto(443); // MOV M,H M1 T4
        case  117: _goto(446); // MOV M,L M1 T4
        case  118: _goto(449); // HLT M1 T4
        case  119: _goto(450); // MOV M,A M1 T4
        case  120: cpu->a=cpu->b;_fetch(); // MOV A,B M1 T4
        case  121: cpu->a=cpu->c;_fetch(); // MOV A,C M1 T4
        case  122: cpu->a=cpu->d;_fetch(); // MOV A,D M1 T4
        case  123: cpu->a=cpu->e;_fetch(); // MOV A,E M1 T4
        case  124: cpu->a=cpu->h;_fetch(); // MOV A,H M1 T4
        case  125: cpu->a=cpu->l;_fetch(); // MOV A,L M1 T4
        case  126: _goto(453); // MOV A,M M1 T4
        case  127: cpu->a=cpu->a;_fetch(); // MOV A,A M1 T4
        case  128: _i8085_alu(cpu,I8085_ALU_ADD,cpu->b);_fetch(); // ADD B M1 T4
        case  129: _i8085_alu(cpu,I8085_ALU_ADD,cpu->c);_fetch(); // ADD C M1 T4
        case  130: _i8085_alu(cpu,I8085_ALU_ADD,cpu->d);_fetch(); // ADD D M1 T4
        case  131: _i8085_alu(cpu,I8085_ALU_ADD,cpu->e);_fetch(); // ADD E M1 T4
        case  132: _i8085_alu(cpu,I8085_ALU_ADD,cpu->h);_fetch(); // ADD H M1 T4
        case  133: _i8085_alu(cpu,I8085_ALU_ADD,cpu->l);_fetch(); // ADD L M1 T4
        case  134: _goto(456); // ADD M M1 T4
        case  135: _i8085_alu(cpu,I8085_ALU_ADD,cpu->a);_fetch(); // ADD A M1 T4
        case  136: _i8085_alu(cpu,I8085_ALU_ADC,cpu->b);_fetch(); // ADC B M1 T4
        case  137: _i8085_alu(cpu,I8085_ALU_ADC,cpu->c);_fetch(); // ADC C M1 T4
        case  138: _i8085_alu(cpu,I8085_ALU_ADC,cpu->d);_fetch(); // ADC D M1 T4
        case  139: _i8085_alu(cpu,I8085_ALU_ADC,cpu->e);_fetch(); // ADC E M1 T4
        case  140: _i8085_alu(cpu,I8085_ALU_ADC,cpu->h);_fetch(); // ADC H M1 T4
        case  141: _i8085_alu(cpu,I8085_ALU_ADC,cpu->l);_fetch(); // ADC L M1 T4
        case  142: _goto(459); // ADC M M1 T4
        case  143: _i8085_alu(cpu,I8085_ALU_ADC,cpu->a);_fetch(); // ADC A M1 T4
        case  144: _i8085_alu(cpu,I8085_ALU_SUB,cpu->b);_fetch(); // SUB B M1 T4
        case  145: _i8085_alu(cpu,I8085_ALU_SUB,cpu->c);_fetch(); // SUB C M1 T4
        case  146: _i8085_alu(cpu,I8085_ALU_SUB,cpu->d);_fetch(); // SUB D M1 T4
        case  147: _i8085_alu(cpu,I8085_ALU_SUB,cpu->e);_fetch(); // SUB E M1 T4
        case  148: _i8085_alu(cpu,I8085_ALU_SUB,cpu->h);_fetch(); // SUB H M1 T4
        case  149: _i8085_alu(cpu,I8085_ALU_SUB,cpu->l);_fetch(); // SUB L M1 T4
        case  150: _goto(462); // SUB M M1 T4
        case  151: _i8085_alu(cpu,I8085_ALU_SUB,cpu->a);_fetch(); // SUB A M1 T4
        case  152: _i8085_alu(cpu,I8085_ALU_SBB,cpu->b);_fetch(); // SBB B M1 T4
        case  153: _i8085_alu(cpu,I8085_ALU_SBB,cpu->c);_fetch(); // SBB C M1 T4
        case  154: _i8085_alu(cpu,I8085_ALU_SBB,cpu->d);_fetch(); // SBB D M1 T4
        case  155: _i8085_alu(cpu,I8085_ALU_SBB,cpu->e);_fetch(); // SBB E M1 T4
        case  156: _i8085_alu(cpu,I8085_ALU_SBB,cpu->h);_fetch(); // SBB H M1 T4
        case  157: _i8085_alu(cpu,I8085_ALU_SBB,cpu->l);_fetch(); // SBB L M1 T4
        case  158: _goto(465); // SBB M M1 T4
        case  159: _i8085_alu(cpu,I8085_ALU_SBB,cpu->a);_fetch(); // SBB A M1 T4
        case  160: _i8085_alu(cpu,I8085_ALU_ANA,cpu->b);_fetch(); // ANA B M1 T4
        case  161: _i8085_alu(cpu,I8085_ALU_ANA,cpu->c);_fetch(); // ANA C M1 T4
        case  162: _i8085_alu(cpu,I8085_ALU_ANA,cpu->d);_fetch(); // ANA D M1 T4
        case  163: _i8085_alu(cpu,I8085_ALU_ANA,cpu->e);_fetch(); // ANA E M1 T4
        case  164: _i8085_alu(cpu,I8085_ALU_ANA,cpu->h);_fetch(); // ANA H M1 T4
        case  165: _i8085_alu(cpu,I8085_ALU_ANA,cpu->l);_fetch(); // ANA L M1 T4
        case  166: _goto(468); // ANA M M1 T4
        case  167: _i8085_alu(cpu,I8085_ALU_ANA,cpu->a);_fetch(); // ANA A M1 T4
        case  168: _i8085_alu(cpu,I8085_ALU_XRA,cpu->b);_fetch(); // XRA B M1 T4
        case  169: _i8085_alu(cpu,I8085_ALU_XRA,cpu->c);_fetch(); // XRA C M1 T4
        case  170: _i8085_alu(cpu,I8085_ALU_XRA,cpu->d);_fetch(); // XRA D M1 T4
        case  171: _i8085_alu(cpu,I8085_ALU_XRA,cpu->e);_fetch(); // XRA E M1 T4
        case  172: _i8085_alu(cpu,I8085_ALU_XRA,cpu->h);_fetch(); // XRA H M1 T4
        case  173: _i8085_alu(cpu,I8085_ALU_XRA,cpu->l);_fetch(); // XRA L M1 T4
        case  174: _goto(471); // XRA M M1 T4
        case  175: _i8085_alu(cpu,I8085_ALU_XRA,cpu->a);_fetch(); // XRA A M1 T4
        case  176: _i8085_alu(cpu,I8085_ALU_ORA,cpu->b);_fetch(); // ORA B M1 T4
        case  177: _i8085_alu(cpu,I8085_ALU_ORA,cpu->c);_fetch(); // ORA C M1 T4
        case  178: _i8085_alu(cpu,I8085_ALU_ORA,cpu->d);_fetch(); // ORA D M1 T4
        case  179: _i8085_alu(cpu,I8085_ALU_ORA,cpu->e);_fetch(); // ORA E M1 T4
        case  180: _i8085_alu(cpu,I8085_ALU_ORA,cpu->h);_fetch(); // ORA H M1 T4
        case  181: _i8085_alu(cpu,I8085_ALU_ORA,cpu->l);_fetch(); // ORA L M1 T4
        case  182: _goto(474); // ORA M M1 T4
        case  183: _i8085_alu(cpu,I8085_ALU_ORA,cpu->a);_fetch(); // ORA A M1 T4
        case  184: _i8085_alu(cpu,I8085_ALU_CMP,cpu->b);_fetch(); // CMP B M1 T4
        case  185: _i8085_alu(cpu,I8085_ALU_CMP,cpu->c);_fetch(); // CMP C M1 T4
        case  186: _i8085_alu(cpu,I8085_ALU_CMP,cpu->d);_fetch(); // CMP D M1 T4
        case  187: _i8085_alu(cpu,I8085_ALU_CMP,cpu->e);_fetch(); // CMP E M1 T4
        case  188: _i8085_alu(cpu,I8085_ALU_CMP,cpu->h);_fetch(); // CMP H M1 T4
        case  189: _i8085_alu(cpu,I8085_ALU_CMP,cpu->l);_fetch(); // CMP L M1 T4
        case  190: _goto(477); // CMP M M1 T4
        case  191: _i8085_alu(cpu,I8085_ALU_CMP,cpu->a);_fetch(); // CMP A M1 T4
        case  192: _goto(480); // RNZ M1 T4
        case  193: _goto(488); // POP B M1 T4
        case  194: _goto(494); // JNZ a16 M1 T4
        case  195: _goto(500); // JMP a16 M1 T4
        case  196: _goto(506); // CNZ a16 M1 T4
        case  197: _goto(520); // PUSH B M1 T4
        case  198: _goto(528); // ADI d8 M1 T4
        case  199: _goto(531); // RST 0 M1 T4
        case  200: _goto(539); // RZ M1 T4
        case  201: _goto(547); // RET M1 T4
        case  202: _goto(553); // JZ a16 M1 T4
        case  203: _unimplemented(); // unimplemented CB M1 T4
        case  204: _goto(559); // CZ a16 M1 T4
        case  205: _goto(573); // CALL a16 M1 T4
        case  206: _goto(587); // ACI d8 M1 T4
        case  207: _goto(590); // RST 1 M1 T4
        case  208: _goto(598); // RNC M1 T4
        case  209: _goto(606); // POP D M1 T4
        case  210: _goto(612); // JNC a16 M1 T4
        case  211: _goto(618); // OUT d8 M1 T4
        case  212: _goto(624); // CNC a16 M1 T4
        case  213: _goto(638); // PUSH D M1 T4
        case  214: _goto(646); // SUI d8 M1 T4
        case  215: _goto(649); // RST 2 M1 T4
        case  216: _goto(657); // RC M1 T4
        case  217: _unimplemented(); // unimplemented D9 M1 T4
        case  218: _goto(665); // JC a16 M1 T4
        case  219: _goto(671); // IN d8 M1 T4
        case  220: _goto(677); // CC a16 M1 T4
        case  221: _unimplemented(); // unimplemented DD M1 T4
        case  222: _goto(691); // SBI d8 M1 T4
        case  223: _goto(694); // RST 3 M1 T4
        case  224: _goto(702); // RPO M1 T4
        case  225: _goto(710); // POP H M1 T4
        case  226: _goto(716); // JPO a16 M1 T4
        case  227: _goto(722); // XTHL M1 T4
        case  228: _goto(734); // CPO a16 M1 T4
        case  229: _goto(748); // PUSH H M1 T4
        case  230: _goto(756); // ANI d8 M1 T4
        case  231: _goto(759); // RST 4 M1 T4
        case  232: _goto(767); // RPE M1 T4
        case  233: _goto(775); // PCHL M1 T4
        case  234: _goto(777); // JPE a16 M1 T4
        case  235: {uint16_t t=cpu->hl;cpu->hl=cpu->de;cpu->de=t;}_fetch(); // XCHG M1 T4
        case  236: _goto(783); // CPE a16 M1 T4
        case  237: _unimplemented(); // unimplemented ED M1 T4
        case  238: _goto(797); // XRI d8 M1 T4
        case  239: _goto(800); // RST 5 M1 T4
        case  240: _goto(808); // RP M1 T4
        case  241: _goto(816); // POP PSW M1 T4
        case  242: _goto(822); // JP a16 M1 T4
        case  243: cpu->inte=false;cpu->trap_ie_valid=false;_fetch(); // DI M1 T4
        case  244: _goto(828); // CP a16 M1 T4
        case  245: _goto(842); // PUSH PSW M1 T4
        case  246: _goto(850); // ORI d8 M1 T4
        case  247: _goto(853); // RST 6 M1 T4
        case  248: _goto(861); // RM M1 T4
        case  249: _goto(869); // SPHL M1 T4
        case  250: _goto(871); // JM a16 M1 T4
        case  251: cpu->inte=true;cpu->trap_ie_valid=false;_fetch(); // EI M1 T4
        case  252: _goto(877); // CM a16 M1 T4
        case  253: _unimplemented(); // unimplemented FD M1 T4
        case  254: _goto(891); // CPI d8 M1 T4
        case  255: _goto(894); // RST 7 M1 T4
        case  256: _t1(cpu->pc,I8085_STATUS_MR);_goto(257); // LXI B,d16 M2 T1
        case  257: _rd();cpu->pc++;_goto(258); // LXI B,d16 M2 T2
        case  258: _ready_rd();cpu->c=_gd();_goto(259); // LXI B,d16 M2 T3
        case  259: _t1(cpu->pc,I8085_STATUS_MR);_goto(260); // LXI B,d16 M3 T1
        case  260: _rd();cpu->pc++;_goto(261); // LXI B,d16 M3 T2
        case  261: _ready_rd();cpu->b=_gd();_fetch(); // LXI B,d16 M3 T3
        case  262: _t1(cpu->bc,I8085_STATUS_MW);_goto(263); // STAX B M2 T1
        case  263: _wr(cpu->a);_goto(264); // STAX B M2 T2
        case  264: _ready_wr();_fetch(); // STAX B M2 T3
        case  265: _goto(266); // INX B M1 T5
        case  266: _fetch(); // INX B M1 T6
        case  267: _t1(cpu->pc,I8085_STATUS_MR);_goto(268); // MVI B,d8 M2 T1
        case  268: _rd();cpu->pc++;_goto(269); // MVI B,d8 M2 T2
        case  269: _ready_rd();cpu->b=_gd();_fetch(); // MVI B,d8 M2 T3
        case  270: _bi(I8085_STATUS_BI);_goto(271); // DAD B M2 T1
        case  271: _bi(I8085_STATUS_BI);_goto(272); // DAD B M2 T2
        case  272: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->l,cpu->c,false);_goto(273); // DAD B M2 T3
        case  273: _bi(I8085_STATUS_BI);_goto(274); // DAD B M3 T1
        case  274: _bi(I8085_STATUS_BI);_goto(275); // DAD B M3 T2
        case  275: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->h,cpu->b,true);_fetch(); // DAD B M3 T3
        case  276: _t1(cpu->bc,I8085_STATUS_MR);_goto(277); // LDAX B M2 T1
        case  277: _rd();_goto(278); // LDAX B M2 T2
        case  278: _ready_rd();cpu->a=_gd();_fetch(); // LDAX B M2 T3
        case  279: _goto(280); // DCX B M1 T5
        case  280: _fetch(); // DCX B M1 T6
        case  281: _t1(cpu->pc,I8085_STATUS_MR);_goto(282); // MVI C,d8 M2 T1
        case  282: _rd();cpu->pc++;_goto(283); // MVI C,d8 M2 T2
        case  283: _ready_rd();cpu->c=_gd();_fetch(); // MVI C,d8 M2 T3
        case  284: _t1(cpu->pc,I8085_STATUS_MR);_goto(285); // LXI D,d16 M2 T1
        case  285: _rd();cpu->pc++;_goto(286); // LXI D,d16 M2 T2
        case  286: _ready_rd();cpu->e=_gd();_goto(287); // LXI D,d16 M2 T3
        case  287: _t1(cpu->pc,I8085_STATUS_MR);_goto(288); // LXI D,d16 M3 T1
        case  288: _rd();cpu->pc++;_goto(289); // LXI D,d16 M3 T2
        case  289: _ready_rd();cpu->d=_gd();_fetch(); // LXI D,d16 M3 T3
        case  290: _t1(cpu->de,I8085_STATUS_MW);_goto(291); // STAX D M2 T1
        case  291: _wr(cpu->a);_goto(292); // STAX D M2 T2
        case  292: _ready_wr();_fetch(); // STAX D M2 T3
        case  293: _goto(294); // INX D M1 T5
        case  294: _fetch(); // INX D M1 T6
        case  295: _t1(cpu->pc,I8085_STATUS_MR);_goto(296); // MVI D,d8 M2 T1
        case  296: _rd();cpu->pc++;_goto(297); // MVI D,d8 M2 T2
        case  297: _ready_rd();cpu->d=_gd();_fetch(); // MVI D,d8 M2 T3
        case  298: _bi(I8085_STATUS_BI);_goto(299); // DAD D M2 T1
        case  299: _bi(I8085_STATUS_BI);_goto(300); // DAD D M2 T2
        case  300: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->l,cpu->e,false);_goto(301); // DAD D M2 T3
        case  301: _bi(I8085_STATUS_BI);_goto(302); // DAD D M3 T1
        case  302: _bi(I8085_STATUS_BI);_goto(303); // DAD D M3 T2
        case  303: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->h,cpu->d,true);_fetch(); // DAD D M3 T3
        case  304: _t1(cpu->de,I8085_STATUS_MR);_goto(305); // LDAX D M2 T1
        case  305: _rd();_goto(306); // LDAX D M2 T2
        case  306: _ready_rd();cpu->a=_gd();_fetch(); // LDAX D M2 T3
        case  307: _goto(308); // DCX D M1 T5
        case  308: _fetch(); // DCX D M1 T6
        case  309: _t1(cpu->pc,I8085_STATUS_MR);_goto(310); // MVI E,d8 M2 T1
        case  310: _rd();cpu->pc++;_goto(311); // MVI E,d8 M2 T2
        case  311: _ready_rd();cpu->e=_gd();_fetch(); // MVI E,d8 M2 T3
        case  312: _t1(cpu->pc,I8085_STATUS_MR);_goto(313); // LXI H,d16 M2 T1
        case  313: _rd();cpu->pc++;_goto(314); // LXI H,d16 M2 T2
        case  314: _ready_rd();cpu->l=_gd();_goto(315); // LXI H,d16 M2 T3
        case  315: _t1(cpu->pc,I8085_STATUS_MR);_goto(316); // LXI H,d16 M3 T1
        case  316: _rd();cpu->pc++;_goto(317); // LXI H,d16 M3 T2
        case  317: _ready_rd();cpu->h=_gd();_fetch(); // LXI H,d16 M3 T3
        case  318: _t1(cpu->pc,I8085_STATUS_MR);_goto(319); // SHLD a16 M2 T1
        case  319: _rd();cpu->pc++;_goto(320); // SHLD a16 M2 T2
        case  320: _ready_rd();cpu->z=_gd();_goto(321); // SHLD a16 M2 T3
        case  321: _t1(cpu->pc,I8085_STATUS_MR);_goto(322); // SHLD a16 M3 T1
        case  322: _rd();cpu->pc++;_goto(323); // SHLD a16 M3 T2
        case  323: _ready_rd();cpu->w=_gd();_goto(324); // SHLD a16 M3 T3
        case  324: _t1(cpu->wz,I8085_STATUS_MW);_goto(325); // SHLD a16 M4 T1
        case  325: _wr(cpu->l);cpu->wz++;_goto(326); // SHLD a16 M4 T2
        case  326: _ready_wr();_goto(327); // SHLD a16 M4 T3
        case  327: _t1(cpu->wz,I8085_STATUS_MW);_goto(328); // SHLD a16 M5 T1
        case  328: _wr(cpu->h);_goto(329); // SHLD a16 M5 T2
        case  329: _ready_wr();_fetch(); // SHLD a16 M5 T3
        case  330: _goto(331); // INX H M1 T5
        case  331: _fetch(); // INX H M1 T6
        case  332: _t1(cpu->pc,I8085_STATUS_MR);_goto(333); // MVI H,d8 M2 T1
        case  333: _rd();cpu->pc++;_goto(334); // MVI H,d8 M2 T2
        case  334: _ready_rd();cpu->h=_gd();_fetch(); // MVI H,d8 M2 T3
        case  335: _bi(I8085_STATUS_BI);_goto(336); // DAD H M2 T1
        case  336: _bi(I8085_STATUS_BI);_goto(337); // DAD H M2 T2
        case  337: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->l,cpu->l,false);_goto(338); // DAD H M2 T3
        case  338: _bi(I8085_STATUS_BI);_goto(339); // DAD H M3 T1
        case  339: _bi(I8085_STATUS_BI);_goto(340); // DAD H M3 T2
        case  340: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->h,cpu->h,true);_fetch(); // DAD H M3 T3
        case  341: _t1(cpu->pc,I8085_STATUS_MR);_goto(342); // LHLD a16 M2 T1
        case  342: _rd();cpu->pc++;_goto(343); // LHLD a16 M2 T2
        case  343: _ready_rd();cpu->z=_gd();_goto(344); // LHLD a16 M2 T3
        case  344: _t1(cpu->pc,I8085_STATUS_MR);_goto(345); // LHLD a16 M3 T1
        case  345: _rd();cpu->pc++;_goto(346); // LHLD a16 M3 T2
        case  346: _ready_rd();cpu->w=_gd();_goto(347); // LHLD a16 M3 T3
        case  347: _t1(cpu->wz,I8085_STATUS_MR);_goto(348); // LHLD a16 M4 T1
        case  348: _rd();cpu->wz++;_goto(349); // LHLD a16 M4 T2
        case  349: _ready_rd();cpu->l=_gd();_goto(350); // LHLD a16 M4 T3
        case  350: _t1(cpu->wz,I8085_STATUS_MR);_goto(351); // LHLD a16 M5 T1
        case  351: _rd();_goto(352); // LHLD a16 M5 T2
        case  352: _ready_rd();cpu->h=_gd();_fetch(); // LHLD a16 M5 T3
        case  353: _goto(354); // DCX H M1 T5
        case  354: _fetch(); // DCX H M1 T6
        case  355: _t1(cpu->pc,I8085_STATUS_MR);_goto(356); // MVI L,d8 M2 T1
        case  356: _rd();cpu->pc++;_goto(357); // MVI L,d8 M2 T2
        case  357: _ready_rd();cpu->l=_gd();_fetch(); // MVI L,d8 M2 T3
        case  358: _t1(cpu->pc,I8085_STATUS_MR);_goto(359); // LXI SP,d16 M2 T1
        case  359: _rd();cpu->pc++;_goto(360); // LXI SP,d16 M2 T2
        case  360: _ready_rd();cpu->spl=_gd();_goto(361); // LXI SP,d16 M2 T3
        case  361: _t1(cpu->pc,I8085_STATUS_MR);_goto(362); // LXI SP,d16 M3 T1
        case  362: _rd();cpu->pc++;_goto(363); // LXI SP,d16 M3 T2
        case  363: _ready_rd();cpu->sph=_gd();_fetch(); // LXI SP,d16 M3 T3
        case  364: _t1(cpu->pc,I8085_STATUS_MR);_goto(365); // STA a16 M2 T1
        case  365: _rd();cpu->pc++;_goto(366); // STA a16 M2 T2
        case  366: _ready_rd();cpu->z=_gd();_goto(367); // STA a16 M2 T3
        case  367: _t1(cpu->pc,I8085_STATUS_MR);_goto(368); // STA a16 M3 T1
        case  368: _rd();cpu->pc++;_goto(369); // STA a16 M3 T2
        case  369: _ready_rd();cpu->w=_gd();_goto(370); // STA a16 M3 T3
        case  370: _t1(cpu->wz,I8085_STATUS_MW);_goto(371); // STA a16 M4 T1
        case  371: _wr(cpu->a);_goto(372); // STA a16 M4 T2
        case  372: _ready_wr();_fetch(); // STA a16 M4 T3
        case  373: _goto(374); // INX SP M1 T5
        case  374: _fetch(); // INX SP M1 T6
        case  375: _t1(cpu->hl,I8085_STATUS_MR);_goto(376); // INR M M2 T1
        case  376: _rd();_goto(377); // INR M M2 T2
        case  377: _ready_rd();cpu->tmp=_gd();cpu->tmp=_i8085_inr(cpu,cpu->tmp);_goto(378); // INR M M2 T3
        case  378: _t1(cpu->hl,I8085_STATUS_MW);_goto(379); // INR M M3 T1
        case  379: _wr(cpu->tmp);_goto(380); // INR M M3 T2
        case  380: _ready_wr();_fetch(); // INR M M3 T3
        case  381: _t1(cpu->hl,I8085_STATUS_MR);_goto(382); // DCR M M2 T1
        case  382: _rd();_goto(383); // DCR M M2 T2
        case  383: _ready_rd();cpu->tmp=_gd();cpu->tmp=_i8085_dcr(cpu,cpu->tmp);_goto(384); // DCR M M2 T3
        case  384: _t1(cpu->hl,I8085_STATUS_MW);_goto(385); // DCR M M3 T1
        case  385: _wr(cpu->tmp);_goto(386); // DCR M M3 T2
        case  386: _ready_wr();_fetch(); // DCR M M3 T3
        case  387: _t1(cpu->pc,I8085_STATUS_MR);_goto(388); // MVI M,d8 M2 T1
        case  388: _rd();cpu->pc++;_goto(389); // MVI M,d8 M2 T2
        case  389: _ready_rd();cpu->tmp=_gd();_goto(390); // MVI M,d8 M2 T3
        case  390: _t1(cpu->hl,I8085_STATUS_MW);_goto(391); // MVI M,d8 M3 T1
        case  391: _wr(cpu->tmp);_goto(392); // MVI M,d8 M3 T2
        case  392: _ready_wr();_fetch(); // MVI M,d8 M3 T3
        case  393: _bi(I8085_STATUS_BI);_goto(394); // DAD SP M2 T1
        case  394: _bi(I8085_STATUS_BI);_goto(395); // DAD SP M2 T2
        case  395: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->l,cpu->spl,false);_goto(396); // DAD SP M2 T3
        case  396: _bi(I8085_STATUS_BI);_goto(397); // DAD SP M3 T1
        case  397: _bi(I8085_STATUS_BI);_goto(398); // DAD SP M3 T2
        case  398: _bi(I8085_STATUS_BI);_i8085_dad(cpu,&cpu->h,cpu->sph,true);_fetch(); // DAD SP M3 T3
        case  399: _t1(cpu->pc,I8085_STATUS_MR);_goto(400); // LDA a16 M2 T1
        case  400: _rd();cpu->pc++;_goto(401); // LDA a16 M2 T2
        case  401: _ready_rd();cpu->z=_gd();_goto(402); // LDA a16 M2 T3
        case  402: _t1(cpu->pc,I8085_STATUS_MR);_goto(403); // LDA a16 M3 T1
        case  403: _rd();cpu->pc++;_goto(404); // LDA a16 M3 T2
        case  404: _ready_rd();cpu->w=_gd();_goto(405); // LDA a16 M3 T3
        case  405: _t1(cpu->wz,I8085_STATUS_MR);_goto(406); // LDA a16 M4 T1
        case  406: _rd();_goto(407); // LDA a16 M4 T2
        case  407: _ready_rd();cpu->a=_gd();_fetch(); // LDA a16 M4 T3
        case  408: _goto(409); // DCX SP M1 T5
        case  409: _fetch(); // DCX SP M1 T6
        case  410: _t1(cpu->pc,I8085_STATUS_MR);_goto(411); // MVI A,d8 M2 T1
        case  411: _rd();cpu->pc++;_goto(412); // MVI A,d8 M2 T2
        case  412: _ready_rd();cpu->a=_gd();_fetch(); // MVI A,d8 M2 T3
        case  413: _t1(cpu->hl,I8085_STATUS_MR);_goto(414); // MOV B,M M2 T1
        case  414: _rd();_goto(415); // MOV B,M M2 T2
        case  415: _ready_rd();cpu->b=_gd();_fetch(); // MOV B,M M2 T3
        case  416: _t1(cpu->hl,I8085_STATUS_MR);_goto(417); // MOV C,M M2 T1
        case  417: _rd();_goto(418); // MOV C,M M2 T2
        case  418: _ready_rd();cpu->c=_gd();_fetch(); // MOV C,M M2 T3
        case  419: _t1(cpu->hl,I8085_STATUS_MR);_goto(420); // MOV D,M M2 T1
        case  420: _rd();_goto(421); // MOV D,M M2 T2
        case  421: _ready_rd();cpu->d=_gd();_fetch(); // MOV D,M M2 T3
        case  422: _t1(cpu->hl,I8085_STATUS_MR);_goto(423); // MOV E,M M2 T1
        case  423: _rd();_goto(424); // MOV E,M M2 T2
        case  424: _ready_rd();cpu->e=_gd();_fetch(); // MOV E,M M2 T3
        case  425: _t1(cpu->hl,I8085_STATUS_MR);_goto(426); // MOV H,M M2 T1
        case  426: _rd();_goto(427); // MOV H,M M2 T2
        case  427: _ready_rd();cpu->h=_gd();_fetch(); // MOV H,M M2 T3
        case  428: _t1(cpu->hl,I8085_STATUS_MR);_goto(429); // MOV L,M M2 T1
        case  429: _rd();_goto(430); // MOV L,M M2 T2
        case  430: _ready_rd();cpu->l=_gd();_fetch(); // MOV L,M M2 T3
        case  431: _t1(cpu->hl,I8085_STATUS_MW);_goto(432); // MOV M,B M2 T1
        case  432: _wr(cpu->b);_goto(433); // MOV M,B M2 T2
        case  433: _ready_wr();_fetch(); // MOV M,B M2 T3
        case  434: _t1(cpu->hl,I8085_STATUS_MW);_goto(435); // MOV M,C M2 T1
        case  435: _wr(cpu->c);_goto(436); // MOV M,C M2 T2
        case  436: _ready_wr();_fetch(); // MOV M,C M2 T3
        case  437: _t1(cpu->hl,I8085_STATUS_MW);_goto(438); // MOV M,D M2 T1
        case  438: _wr(cpu->d);_goto(439); // MOV M,D M2 T2
        case  439: _ready_wr();_fetch(); // MOV M,D M2 T3
        case  440: _t1(cpu->hl,I8085_STATUS_MW);_goto(441); // MOV M,E M2 T1
        case  441: _wr(cpu->e);_goto(442); // MOV M,E M2 T2
        case  442: _ready_wr();_fetch(); // MOV M,E M2 T3
        case  443: _t1(cpu->hl,I8085_STATUS_MW);_goto(444); // MOV M,H M2 T1
        case  444: _wr(cpu->h);_goto(445); // MOV M,H M2 T2
        case  445: _ready_wr();_fetch(); // MOV M,H M2 T3
        case  446: _t1(cpu->hl,I8085_STATUS_MW);_goto(447); // MOV M,L M2 T1
        case  447: _wr(cpu->l);_goto(448); // MOV M,L M2 T2
        case  448: _ready_wr();_fetch(); // MOV M,L M2 T3
        case  449: _t1(cpu->pc,I8085_STATUS_HALT);_goto(I8085_HALT); // HLT M2 T1
        case  450: _t1(cpu->hl,I8085_STATUS_MW);_goto(451); // MOV M,A M2 T1
        case  451: _wr(cpu->a);_goto(452); // MOV M,A M2 T2
        case  452: _ready_wr();_fetch(); // MOV M,A M2 T3
        case  453: _t1(cpu->hl,I8085_STATUS_MR);_goto(454); // MOV A,M M2 T1
        case  454: _rd();_goto(455); // MOV A,M M2 T2
        case  455: _ready_rd();cpu->a=_gd();_fetch(); // MOV A,M M2 T3
        case  456: _t1(cpu->hl,I8085_STATUS_MR);_goto(457); // ADD M M2 T1
        case  457: _rd();_goto(458); // ADD M M2 T2
        case  458: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ADD,cpu->tmp);_fetch(); // ADD M M2 T3
        case  459: _t1(cpu->hl,I8085_STATUS_MR);_goto(460); // ADC M M2 T1
        case  460: _rd();_goto(461); // ADC M M2 T2
        case  461: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ADC,cpu->tmp);_fetch(); // ADC M M2 T3
        case  462: _t1(cpu->hl,I8085_STATUS_MR);_goto(463); // SUB M M2 T1
        case  463: _rd();_goto(464); // SUB M M2 T2
        case  464: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_SUB,cpu->tmp);_fetch(); // SUB M M2 T3
        case  465: _t1(cpu->hl,I8085_STATUS_MR);_goto(466); // SBB M M2 T1
        case  466: _rd();_goto(467); // SBB M M2 T2
        case  467: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_SBB,cpu->tmp);_fetch(); // SBB M M2 T3
        case  468: _t1(cpu->hl,I8085_STATUS_MR);_goto(469); // ANA M M2 T1
        case  469: _rd();_goto(470); // ANA M M2 T2
        case  470: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ANA,cpu->tmp);_fetch(); // ANA M M2 T3
        case  471: _t1(cpu->hl,I8085_STATUS_MR);_goto(472); // XRA M M2 T1
        case  472: _rd();_goto(473); // XRA M M2 T2
        case  473: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_XRA,cpu->tmp);_fetch(); // XRA M M2 T3
        case  474: _t1(cpu->hl,I8085_STATUS_MR);_goto(475); // ORA M M2 T1
        case  475: _rd();_goto(476); // ORA M M2 T2
        case  476: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ORA,cpu->tmp);_fetch(); // ORA M M2 T3
        case  477: _t1(cpu->hl,I8085_STATUS_MR);_goto(478); // CMP M M2 T1
        case  478: _rd();_goto(479); // CMP M M2 T2
        case  479: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_CMP,cpu->tmp);_fetch(); // CMP M M2 T3
        case  480: _goto(481); // RNZ M1 T5
        case  481: if(!(!(cpu->f&I8085_ZF))){_fetch();}_goto(482); // RNZ M1 T6
        case  482: _t1(cpu->sp,I8085_STATUS_MR);_goto(483); // RNZ M2 T1
        case  483: _rd();cpu->sp++;_goto(484); // RNZ M2 T2
        case  484: _ready_rd();cpu->z=_gd();_goto(485); // RNZ M2 T3
        case  485: _t1(cpu->sp,I8085_STATUS_MR);_goto(486); // RNZ M3 T1
        case  486: _rd();cpu->sp++;_goto(487); // RNZ M3 T2
        case  487: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RNZ M3 T3
        case  488: _t1(cpu->sp,I8085_STATUS_MR);_goto(489); // POP B M2 T1
        case  489: _rd();cpu->sp++;_goto(490); // POP B M2 T2
        case  490: _ready_rd();cpu->c=_gd();_goto(491); // POP B M2 T3
        case  491: _t1(cpu->sp,I8085_STATUS_MR);_goto(492); // POP B M3 T1
        case  492: _rd();cpu->sp++;_goto(493); // POP B M3 T2
        case  493: _ready_rd();cpu->b=_gd();_fetch(); // POP B M3 T3
        case  494: _t1(cpu->pc,I8085_STATUS_MR);_goto(495); // JNZ a16 M2 T1
        case  495: _rd();cpu->pc++;_goto(496); // JNZ a16 M2 T2
        case  496: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_ZF))){cpu->pc++;_fetch();}_goto(497); // JNZ a16 M2 T3
        case  497: _t1(cpu->pc,I8085_STATUS_MR);_goto(498); // JNZ a16 M3 T1
        case  498: _rd();cpu->pc++;_goto(499); // JNZ a16 M3 T2
        case  499: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JNZ a16 M3 T3
        case  500: _t1(cpu->pc,I8085_STATUS_MR);_goto(501); // JMP a16 M2 T1
        case  501: _rd();cpu->pc++;_goto(502); // JMP a16 M2 T2
        case  502: _ready_rd();cpu->z=_gd();_goto(503); // JMP a16 M2 T3
        case  503: _t1(cpu->pc,I8085_STATUS_MR);_goto(504); // JMP a16 M3 T1
        case  504: _rd();cpu->pc++;_goto(505); // JMP a16 M3 T2
        case  505: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JMP a16 M3 T3
        case  506: if((!(cpu->f&I8085_ZF))){cpu->sp--;}_goto(507); // CNZ a16 M1 T5
        case  507: _goto(508); // CNZ a16 M1 T6
        case  508: _t1(cpu->pc,I8085_STATUS_MR);_goto(509); // CNZ a16 M2 T1
        case  509: _rd();cpu->pc++;_goto(510); // CNZ a16 M2 T2
        case  510: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_ZF))){cpu->pc++;_fetch();}_goto(511); // CNZ a16 M2 T3
        case  511: _t1(cpu->pc,I8085_STATUS_MR);_goto(512); // CNZ a16 M3 T1
        case  512: _rd();cpu->pc++;_goto(513); // CNZ a16 M3 T2
        case  513: _ready_rd();cpu->w=_gd();_goto(514); // CNZ a16 M3 T3
        case  514: _t1(cpu->sp,I8085_STATUS_MW);_goto(515); // CNZ a16 M4 T1
        case  515: _wr(cpu->pch);cpu->sp--;_goto(516); // CNZ a16 M4 T2
        case  516: _ready_wr();_goto(517); // CNZ a16 M4 T3
        case  517: _t1(cpu->sp,I8085_STATUS_MW);_goto(518); // CNZ a16 M5 T1
        case  518: _wr(cpu->pcl);_goto(519); // CNZ a16 M5 T2
        case  519: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CNZ a16 M5 T3
        case  520: cpu->sp--;_goto(521); // PUSH B M1 T5
        case  521: _goto(522); // PUSH B M1 T6
        case  522: _t1(cpu->sp,I8085_STATUS_MW);_goto(523); // PUSH B M2 T1
        case  523: _wr(cpu->b);cpu->sp--;_goto(524); // PUSH B M2 T2
        case  524: _ready_wr();_goto(525); // PUSH B M2 T3
        case  525: _t1(cpu->sp,I8085_STATUS_MW);_goto(526); // PUSH B M3 T1
        case  526: _wr(cpu->c);_goto(527); // PUSH B M3 T2
        case  527: _ready_wr();_fetch(); // PUSH B M3 T3
        case  528: _t1(cpu->pc,I8085_STATUS_MR);_goto(529); // ADI d8 M2 T1
        case  529: _rd();cpu->pc++;_goto(530); // ADI d8 M2 T2
        case  530: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ADD,cpu->tmp);_fetch(); // ADI d8 M2 T3
        case  531: cpu->sp--;_goto(532); // RST 0 M1 T5
        case  532: _goto(533); // RST 0 M1 T6
        case  533: _t1(cpu->sp,I8085_STATUS_MW);_goto(534); // RST 0 M2 T1
        case  534: _wr(cpu->pch);cpu->sp--;_goto(535); // RST 0 M2 T2
        case  535: _ready_wr();_goto(536); // RST 0 M2 T3
        case  536: _t1(cpu->sp,I8085_STATUS_MW);_goto(537); // RST 0 M3 T1
        case  537: _wr(cpu->pcl);_goto(538); // RST 0 M3 T2
        case  538: _ready_wr();cpu->pc=0x00;_fetch(); // RST 0 M3 T3
        case  539: _goto(540); // RZ M1 T5
        case  540: if(!(cpu->f&I8085_ZF)){_fetch();}_goto(541); // RZ M1 T6
        case  541: _t1(cpu->sp,I8085_STATUS_MR);_goto(542); // RZ M2 T1
        case  542: _rd();cpu->sp++;_goto(543); // RZ M2 T2
        case  543: _ready_rd();cpu->z=_gd();_goto(544); // RZ M2 T3
        case  544: _t1(cpu->sp,I8085_STATUS_MR);_goto(545); // RZ M3 T1
        case  545: _rd();cpu->sp++;_goto(546); // RZ M3 T2
        case  546: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RZ M3 T3
        case  547: _t1(cpu->sp,I8085_STATUS_MR);_goto(548); // RET M2 T1
        case  548: _rd();cpu->sp++;_goto(549); // RET M2 T2
        case  549: _ready_rd();cpu->z=_gd();_goto(550); // RET M2 T3
        case  550: _t1(cpu->sp,I8085_STATUS_MR);_goto(551); // RET M3 T1
        case  551: _rd();cpu->sp++;_goto(552); // RET M3 T2
        case  552: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RET M3 T3
        case  553: _t1(cpu->pc,I8085_STATUS_MR);_goto(554); // JZ a16 M2 T1
        case  554: _rd();cpu->pc++;_goto(555); // JZ a16 M2 T2
        case  555: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_ZF)){cpu->pc++;_fetch();}_goto(556); // JZ a16 M2 T3
        case  556: _t1(cpu->pc,I8085_STATUS_MR);_goto(557); // JZ a16 M3 T1
        case  557: _rd();cpu->pc++;_goto(558); // JZ a16 M3 T2
        case  558: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JZ a16 M3 T3
        case  559: if((cpu->f&I8085_ZF)){cpu->sp--;}_goto(560); // CZ a16 M1 T5
        case  560: _goto(561); // CZ a16 M1 T6
        case  561: _t1(cpu->pc,I8085_STATUS_MR);_goto(562); // CZ a16 M2 T1
        case  562: _rd();cpu->pc++;_goto(563); // CZ a16 M2 T2
        case  563: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_ZF)){cpu->pc++;_fetch();}_goto(564); // CZ a16 M2 T3
        case  564: _t1(cpu->pc,I8085_STATUS_MR);_goto(565); // CZ a16 M3 T1
        case  565: _rd();cpu->pc++;_goto(566); // CZ a16 M3 T2
        case  566: _ready_rd();cpu->w=_gd();_goto(567); // CZ a16 M3 T3
        case  567: _t1(cpu->sp,I8085_STATUS_MW);_goto(568); // CZ a16 M4 T1
        case  568: _wr(cpu->pch);cpu->sp--;_goto(569); // CZ a16 M4 T2
        case  569: _ready_wr();_goto(570); // CZ a16 M4 T3
        case  570: _t1(cpu->sp,I8085_STATUS_MW);_goto(571); // CZ a16 M5 T1
        case  571: _wr(cpu->pcl);_goto(572); // CZ a16 M5 T2
        case  572: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CZ a16 M5 T3
        case  573: cpu->sp--;_goto(574); // CALL a16 M1 T5
        case  574: _goto(575); // CALL a16 M1 T6
        case  575: _t1(cpu->pc,I8085_STATUS_MR);_goto(576); // CALL a16 M2 T1
        case  576: _rd();cpu->pc++;_goto(577); // CALL a16 M2 T2
        case  577: _ready_rd();cpu->z=_gd();_goto(578); // CALL a16 M2 T3
        case  578: _t1(cpu->pc,I8085_STATUS_MR);_goto(579); // CALL a16 M3 T1
        case  579: _rd();cpu->pc++;_goto(580); // CALL a16 M3 T2
        case  580: _ready_rd();cpu->w=_gd();_goto(581); // CALL a16 M3 T3
        case  581: _t1(cpu->sp,I8085_STATUS_MW);_goto(582); // CALL a16 M4 T1
        case  582: _wr(cpu->pch);cpu->sp--;_goto(583); // CALL a16 M4 T2
        case  583: _ready_wr();_goto(584); // CALL a16 M4 T3
        case  584: _t1(cpu->sp,I8085_STATUS_MW);_goto(585); // CALL a16 M5 T1
        case  585: _wr(cpu->pcl);_goto(586); // CALL a16 M5 T2
        case  586: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CALL a16 M5 T3
        case  587: _t1(cpu->pc,I8085_STATUS_MR);_goto(588); // ACI d8 M2 T1
        case  588: _rd();cpu->pc++;_goto(589); // ACI d8 M2 T2
        case  589: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ADC,cpu->tmp);_fetch(); // ACI d8 M2 T3
        case  590: cpu->sp--;_goto(591); // RST 1 M1 T5
        case  591: _goto(592); // RST 1 M1 T6
        case  592: _t1(cpu->sp,I8085_STATUS_MW);_goto(593); // RST 1 M2 T1
        case  593: _wr(cpu->pch);cpu->sp--;_goto(594); // RST 1 M2 T2
        case  594: _ready_wr();_goto(595); // RST 1 M2 T3
        case  595: _t1(cpu->sp,I8085_STATUS_MW);_goto(596); // RST 1 M3 T1
        case  596: _wr(cpu->pcl);_goto(597); // RST 1 M3 T2
        case  597: _ready_wr();cpu->pc=0x08;_fetch(); // RST 1 M3 T3
        case  598: _goto(599); // RNC M1 T5
        case  599: if(!(!(cpu->f&I8085_CF))){_fetch();}_goto(600); // RNC M1 T6
        case  600: _t1(cpu->sp,I8085_STATUS_MR);_goto(601); // RNC M2 T1
        case  601: _rd();cpu->sp++;_goto(602); // RNC M2 T2
        case  602: _ready_rd();cpu->z=_gd();_goto(603); // RNC M2 T3
        case  603: _t1(cpu->sp,I8085_STATUS_MR);_goto(604); // RNC M3 T1
        case  604: _rd();cpu->sp++;_goto(605); // RNC M3 T2
        case  605: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RNC M3 T3
        case  606: _t1(cpu->sp,I8085_STATUS_MR);_goto(607); // POP D M2 T1
        case  607: _rd();cpu->sp++;_goto(608); // POP D M2 T2
        case  608: _ready_rd();cpu->e=_gd();_goto(609); // POP D M2 T3
        case  609: _t1(cpu->sp,I8085_STATUS_MR);_goto(610); // POP D M3 T1
        case  610: _rd();cpu->sp++;_goto(611); // POP D M3 T2
        case  611: _ready_rd();cpu->d=_gd();_fetch(); // POP D M3 T3
        case  612: _t1(cpu->pc,I8085_STATUS_MR);_goto(613); // JNC a16 M2 T1
        case  613: _rd();cpu->pc++;_goto(614); // JNC a16 M2 T2
        case  614: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_CF))){cpu->pc++;_fetch();}_goto(615); // JNC a16 M2 T3
        case  615: _t1(cpu->pc,I8085_STATUS_MR);_goto(616); // JNC a16 M3 T1
        case  616: _rd();cpu->pc++;_goto(617); // JNC a16 M3 T2
        case  617: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JNC a16 M3 T3
        case  618: _t1(cpu->pc,I8085_STATUS_MR);_goto(619); // OUT d8 M2 T1
        case  619: _rd();cpu->pc++;_goto(620); // OUT d8 M2 T2
        case  620: _ready_rd();cpu->z=_gd();cpu->w=cpu->z;_goto(621); // OUT d8 M2 T3
        case  621: _t1(cpu->wz,I8085_STATUS_IOW);_goto(622); // OUT d8 M3 T1
        case  622: _wr(cpu->a);_goto(623); // OUT d8 M3 T2
        case  623: _ready_wr();_fetch(); // OUT d8 M3 T3
        case  624: if((!(cpu->f&I8085_CF))){cpu->sp--;}_goto(625); // CNC a16 M1 T5
        case  625: _goto(626); // CNC a16 M1 T6
        case  626: _t1(cpu->pc,I8085_STATUS_MR);_goto(627); // CNC a16 M2 T1
        case  627: _rd();cpu->pc++;_goto(628); // CNC a16 M2 T2
        case  628: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_CF))){cpu->pc++;_fetch();}_goto(629); // CNC a16 M2 T3
        case  629: _t1(cpu->pc,I8085_STATUS_MR);_goto(630); // CNC a16 M3 T1
        case  630: _rd();cpu->pc++;_goto(631); // CNC a16 M3 T2
        case  631: _ready_rd();cpu->w=_gd();_goto(632); // CNC a16 M3 T3
        case  632: _t1(cpu->sp,I8085_STATUS_MW);_goto(633); // CNC a16 M4 T1
        case  633: _wr(cpu->pch);cpu->sp--;_goto(634); // CNC a16 M4 T2
        case  634: _ready_wr();_goto(635); // CNC a16 M4 T3
        case  635: _t1(cpu->sp,I8085_STATUS_MW);_goto(636); // CNC a16 M5 T1
        case  636: _wr(cpu->pcl);_goto(637); // CNC a16 M5 T2
        case  637: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CNC a16 M5 T3
        case  638: cpu->sp--;_goto(639); // PUSH D M1 T5
        case  639: _goto(640); // PUSH D M1 T6
        case  640: _t1(cpu->sp,I8085_STATUS_MW);_goto(641); // PUSH D M2 T1
        case  641: _wr(cpu->d);cpu->sp--;_goto(642); // PUSH D M2 T2
        case  642: _ready_wr();_goto(643); // PUSH D M2 T3
        case  643: _t1(cpu->sp,I8085_STATUS_MW);_goto(644); // PUSH D M3 T1
        case  644: _wr(cpu->e);_goto(645); // PUSH D M3 T2
        case  645: _ready_wr();_fetch(); // PUSH D M3 T3
        case  646: _t1(cpu->pc,I8085_STATUS_MR);_goto(647); // SUI d8 M2 T1
        case  647: _rd();cpu->pc++;_goto(648); // SUI d8 M2 T2
        case  648: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_SUB,cpu->tmp);_fetch(); // SUI d8 M2 T3
        case  649: cpu->sp--;_goto(650); // RST 2 M1 T5
        case  650: _goto(651); // RST 2 M1 T6
        case  651: _t1(cpu->sp,I8085_STATUS_MW);_goto(652); // RST 2 M2 T1
        case  652: _wr(cpu->pch);cpu->sp--;_goto(653); // RST 2 M2 T2
        case  653: _ready_wr();_goto(654); // RST 2 M2 T3
        case  654: _t1(cpu->sp,I8085_STATUS_MW);_goto(655); // RST 2 M3 T1
        case  655: _wr(cpu->pcl);_goto(656); // RST 2 M3 T2
        case  656: _ready_wr();cpu->pc=0x10;_fetch(); // RST 2 M3 T3
        case  657: _goto(658); // RC M1 T5
        case  658: if(!(cpu->f&I8085_CF)){_fetch();}_goto(659); // RC M1 T6
        case  659: _t1(cpu->sp,I8085_STATUS_MR);_goto(660); // RC M2 T1
        case  660: _rd();cpu->sp++;_goto(661); // RC M2 T2
        case  661: _ready_rd();cpu->z=_gd();_goto(662); // RC M2 T3
        case  662: _t1(cpu->sp,I8085_STATUS_MR);_goto(663); // RC M3 T1
        case  663: _rd();cpu->sp++;_goto(664); // RC M3 T2
        case  664: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RC M3 T3
        case  665: _t1(cpu->pc,I8085_STATUS_MR);_goto(666); // JC a16 M2 T1
        case  666: _rd();cpu->pc++;_goto(667); // JC a16 M2 T2
        case  667: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_CF)){cpu->pc++;_fetch();}_goto(668); // JC a16 M2 T3
        case  668: _t1(cpu->pc,I8085_STATUS_MR);_goto(669); // JC a16 M3 T1
        case  669: _rd();cpu->pc++;_goto(670); // JC a16 M3 T2
        case  670: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JC a16 M3 T3
        case  671: _t1(cpu->pc,I8085_STATUS_MR);_goto(672); // IN d8 M2 T1
        case  672: _rd();cpu->pc++;_goto(673); // IN d8 M2 T2
        case  673: _ready_rd();cpu->z=_gd();cpu->w=cpu->z;_goto(674); // IN d8 M2 T3
        case  674: _t1(cpu->wz,I8085_STATUS_IOR);_goto(675); // IN d8 M3 T1
        case  675: _rd();_goto(676); // IN d8 M3 T2
        case  676: _ready_rd();cpu->a=_gd();_fetch(); // IN d8 M3 T3
        case  677: if((cpu->f&I8085_CF)){cpu->sp--;}_goto(678); // CC a16 M1 T5
        case  678: _goto(679); // CC a16 M1 T6
        case  679: _t1(cpu->pc,I8085_STATUS_MR);_goto(680); // CC a16 M2 T1
        case  680: _rd();cpu->pc++;_goto(681); // CC a16 M2 T2
        case  681: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_CF)){cpu->pc++;_fetch();}_goto(682); // CC a16 M2 T3
        case  682: _t1(cpu->pc,I8085_STATUS_MR);_goto(683); // CC a16 M3 T1
        case  683: _rd();cpu->pc++;_goto(684); // CC a16 M3 T2
        case  684: _ready_rd();cpu->w=_gd();_goto(685); // CC a16 M3 T3
        case  685: _t1(cpu->sp,I8085_STATUS_MW);_goto(686); // CC a16 M4 T1
        case  686: _wr(cpu->pch);cpu->sp--;_goto(687); // CC a16 M4 T2
        case  687: _ready_wr();_goto(688); // CC a16 M4 T3
        case  688: _t1(cpu->sp,I8085_STATUS_MW);_goto(689); // CC a16 M5 T1
        case  689: _wr(cpu->pcl);_goto(690); // CC a16 M5 T2
        case  690: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CC a16 M5 T3
        case  691: _t1(cpu->pc,I8085_STATUS_MR);_goto(692); // SBI d8 M2 T1
        case  692: _rd();cpu->pc++;_goto(693); // SBI d8 M2 T2
        case  693: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_SBB,cpu->tmp);_fetch(); // SBI d8 M2 T3
        case  694: cpu->sp--;_goto(695); // RST 3 M1 T5
        case  695: _goto(696); // RST 3 M1 T6
        case  696: _t1(cpu->sp,I8085_STATUS_MW);_goto(697); // RST 3 M2 T1
        case  697: _wr(cpu->pch);cpu->sp--;_goto(698); // RST 3 M2 T2
        case  698: _ready_wr();_goto(699); // RST 3 M2 T3
        case  699: _t1(cpu->sp,I8085_STATUS_MW);_goto(700); // RST 3 M3 T1
        case  700: _wr(cpu->pcl);_goto(701); // RST 3 M3 T2
        case  701: _ready_wr();cpu->pc=0x18;_fetch(); // RST 3 M3 T3
        case  702: _goto(703); // RPO M1 T5
        case  703: if(!(!(cpu->f&I8085_PF))){_fetch();}_goto(704); // RPO M1 T6
        case  704: _t1(cpu->sp,I8085_STATUS_MR);_goto(705); // RPO M2 T1
        case  705: _rd();cpu->sp++;_goto(706); // RPO M2 T2
        case  706: _ready_rd();cpu->z=_gd();_goto(707); // RPO M2 T3
        case  707: _t1(cpu->sp,I8085_STATUS_MR);_goto(708); // RPO M3 T1
        case  708: _rd();cpu->sp++;_goto(709); // RPO M3 T2
        case  709: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RPO M3 T3
        case  710: _t1(cpu->sp,I8085_STATUS_MR);_goto(711); // POP H M2 T1
        case  711: _rd();cpu->sp++;_goto(712); // POP H M2 T2
        case  712: _ready_rd();cpu->l=_gd();_goto(713); // POP H M2 T3
        case  713: _t1(cpu->sp,I8085_STATUS_MR);_goto(714); // POP H M3 T1
        case  714: _rd();cpu->sp++;_goto(715); // POP H M3 T2
        case  715: _ready_rd();cpu->h=_gd();_fetch(); // POP H M3 T3
        case  716: _t1(cpu->pc,I8085_STATUS_MR);_goto(717); // JPO a16 M2 T1
        case  717: _rd();cpu->pc++;_goto(718); // JPO a16 M2 T2
        case  718: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_PF))){cpu->pc++;_fetch();}_goto(719); // JPO a16 M2 T3
        case  719: _t1(cpu->pc,I8085_STATUS_MR);_goto(720); // JPO a16 M3 T1
        case  720: _rd();cpu->pc++;_goto(721); // JPO a16 M3 T2
        case  721: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JPO a16 M3 T3
        case  722: _t1(cpu->sp,I8085_STATUS_MR);_goto(723); // XTHL M2 T1
        case  723: _rd();cpu->sp++;_goto(724); // XTHL M2 T2
        case  724: _ready_rd();cpu->z=_gd();_goto(725); // XTHL M2 T3
        case  725: _t1(cpu->sp,I8085_STATUS_MR);_goto(726); // XTHL M3 T1
        case  726: _rd();_goto(727); // XTHL M3 T2
        case  727: _ready_rd();cpu->w=_gd();_goto(728); // XTHL M3 T3
        case  728: _t1(cpu->sp,I8085_STATUS_MW);_goto(729); // XTHL M4 T1
        case  729: _wr(cpu->h);cpu->sp--;_goto(730); // XTHL M4 T2
        case  730: _ready_wr();_goto(731); // XTHL M4 T3
        case  731: _t1(cpu->sp,I8085_STATUS_MW);_goto(732); // XTHL M5 T1
        case  732: _wr(cpu->l);_goto(733); // XTHL M5 T2
        case  733: _ready_wr();cpu->hl=cpu->wz;_fetch(); // XTHL M5 T3
        case  734: if((!(cpu->f&I8085_PF))){cpu->sp--;}_goto(735); // CPO a16 M1 T5
        case  735: _goto(736); // CPO a16 M1 T6
        case  736: _t1(cpu->pc,I8085_STATUS_MR);_goto(737); // CPO a16 M2 T1
        case  737: _rd();cpu->pc++;_goto(738); // CPO a16 M2 T2
        case  738: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_PF))){cpu->pc++;_fetch();}_goto(739); // CPO a16 M2 T3
        case  739: _t1(cpu->pc,I8085_STATUS_MR);_goto(740); // CPO a16 M3 T1
        case  740: _rd();cpu->pc++;_goto(741); // CPO a16 M3 T2
        case  741: _ready_rd();cpu->w=_gd();_goto(742); // CPO a16 M3 T3
        case  742: _t1(cpu->sp,I8085_STATUS_MW);_goto(743); // CPO a16 M4 T1
        case  743: _wr(cpu->pch);cpu->sp--;_goto(744); // CPO a16 M4 T2
        case  744: _ready_wr();_goto(745); // CPO a16 M4 T3
        case  745: _t1(cpu->sp,I8085_STATUS_MW);_goto(746); // CPO a16 M5 T1
        case  746: _wr(cpu->pcl);_goto(747); // CPO a16 M5 T2
        case  747: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CPO a16 M5 T3
        case  748: cpu->sp--;_goto(749); // PUSH H M1 T5
        case  749: _goto(750); // PUSH H M1 T6
        case  750: _t1(cpu->sp,I8085_STATUS_MW);_goto(751); // PUSH H M2 T1
        case  751: _wr(cpu->h);cpu->sp--;_goto(752); // PUSH H M2 T2
        case  752: _ready_wr();_goto(753); // PUSH H M2 T3
        case  753: _t1(cpu->sp,I8085_STATUS_MW);_goto(754); // PUSH H M3 T1
        case  754: _wr(cpu->l);_goto(755); // PUSH H M3 T2
        case  755: _ready_wr();_fetch(); // PUSH H M3 T3
        case  756: _t1(cpu->pc,I8085_STATUS_MR);_goto(757); // ANI d8 M2 T1
        case  757: _rd();cpu->pc++;_goto(758); // ANI d8 M2 T2
        case  758: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ANA,cpu->tmp);_fetch(); // ANI d8 M2 T3
        case  759: cpu->sp--;_goto(760); // RST 4 M1 T5
        case  760: _goto(761); // RST 4 M1 T6
        case  761: _t1(cpu->sp,I8085_STATUS_MW);_goto(762); // RST 4 M2 T1
        case  762: _wr(cpu->pch);cpu->sp--;_goto(763); // RST 4 M2 T2
        case  763: _ready_wr();_goto(764); // RST 4 M2 T3
        case  764: _t1(cpu->sp,I8085_STATUS_MW);_goto(765); // RST 4 M3 T1
        case  765: _wr(cpu->pcl);_goto(766); // RST 4 M3 T2
        case  766: _ready_wr();cpu->pc=0x20;_fetch(); // RST 4 M3 T3
        case  767: _goto(768); // RPE M1 T5
        case  768: if(!(cpu->f&I8085_PF)){_fetch();}_goto(769); // RPE M1 T6
        case  769: _t1(cpu->sp,I8085_STATUS_MR);_goto(770); // RPE M2 T1
        case  770: _rd();cpu->sp++;_goto(771); // RPE M2 T2
        case  771: _ready_rd();cpu->z=_gd();_goto(772); // RPE M2 T3
        case  772: _t1(cpu->sp,I8085_STATUS_MR);_goto(773); // RPE M3 T1
        case  773: _rd();cpu->sp++;_goto(774); // RPE M3 T2
        case  774: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RPE M3 T3
        case  775: _goto(776); // PCHL M1 T5
        case  776: cpu->pc=cpu->hl;_fetch(); // PCHL M1 T6
        case  777: _t1(cpu->pc,I8085_STATUS_MR);_goto(778); // JPE a16 M2 T1
        case  778: _rd();cpu->pc++;_goto(779); // JPE a16 M2 T2
        case  779: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_PF)){cpu->pc++;_fetch();}_goto(780); // JPE a16 M2 T3
        case  780: _t1(cpu->pc,I8085_STATUS_MR);_goto(781); // JPE a16 M3 T1
        case  781: _rd();cpu->pc++;_goto(782); // JPE a16 M3 T2
        case  782: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JPE a16 M3 T3
        case  783: if((cpu->f&I8085_PF)){cpu->sp--;}_goto(784); // CPE a16 M1 T5
        case  784: _goto(785); // CPE a16 M1 T6
        case  785: _t1(cpu->pc,I8085_STATUS_MR);_goto(786); // CPE a16 M2 T1
        case  786: _rd();cpu->pc++;_goto(787); // CPE a16 M2 T2
        case  787: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_PF)){cpu->pc++;_fetch();}_goto(788); // CPE a16 M2 T3
        case  788: _t1(cpu->pc,I8085_STATUS_MR);_goto(789); // CPE a16 M3 T1
        case  789: _rd();cpu->pc++;_goto(790); // CPE a16 M3 T2
        case  790: _ready_rd();cpu->w=_gd();_goto(791); // CPE a16 M3 T3
        case  791: _t1(cpu->sp,I8085_STATUS_MW);_goto(792); // CPE a16 M4 T1
        case  792: _wr(cpu->pch);cpu->sp--;_goto(793); // CPE a16 M4 T2
        case  793: _ready_wr();_goto(794); // CPE a16 M4 T3
        case  794: _t1(cpu->sp,I8085_STATUS_MW);_goto(795); // CPE a16 M5 T1
        case  795: _wr(cpu->pcl);_goto(796); // CPE a16 M5 T2
        case  796: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CPE a16 M5 T3
        case  797: _t1(cpu->pc,I8085_STATUS_MR);_goto(798); // XRI d8 M2 T1
        case  798: _rd();cpu->pc++;_goto(799); // XRI d8 M2 T2
        case  799: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_XRA,cpu->tmp);_fetch(); // XRI d8 M2 T3
        case  800: cpu->sp--;_goto(801); // RST 5 M1 T5
        case  801: _goto(802); // RST 5 M1 T6
        case  802: _t1(cpu->sp,I8085_STATUS_MW);_goto(803); // RST 5 M2 T1
        case  803: _wr(cpu->pch);cpu->sp--;_goto(804); // RST 5 M2 T2
        case  804: _ready_wr();_goto(805); // RST 5 M2 T3
        case  805: _t1(cpu->sp,I8085_STATUS_MW);_goto(806); // RST 5 M3 T1
        case  806: _wr(cpu->pcl);_goto(807); // RST 5 M3 T2
        case  807: _ready_wr();cpu->pc=0x28;_fetch(); // RST 5 M3 T3
        case  808: _goto(809); // RP M1 T5
        case  809: if(!(!(cpu->f&I8085_SF))){_fetch();}_goto(810); // RP M1 T6
        case  810: _t1(cpu->sp,I8085_STATUS_MR);_goto(811); // RP M2 T1
        case  811: _rd();cpu->sp++;_goto(812); // RP M2 T2
        case  812: _ready_rd();cpu->z=_gd();_goto(813); // RP M2 T3
        case  813: _t1(cpu->sp,I8085_STATUS_MR);_goto(814); // RP M3 T1
        case  814: _rd();cpu->sp++;_goto(815); // RP M3 T2
        case  815: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RP M3 T3
        case  816: _t1(cpu->sp,I8085_STATUS_MR);_goto(817); // POP PSW M2 T1
        case  817: _rd();cpu->sp++;_goto(818); // POP PSW M2 T2
        case  818: _ready_rd();cpu->f=_gd();cpu->f=_i8085_fixf(cpu->f);_goto(819); // POP PSW M2 T3
        case  819: _t1(cpu->sp,I8085_STATUS_MR);_goto(820); // POP PSW M3 T1
        case  820: _rd();cpu->sp++;_goto(821); // POP PSW M3 T2
        case  821: _ready_rd();cpu->a=_gd();_fetch(); // POP PSW M3 T3
        case  822: _t1(cpu->pc,I8085_STATUS_MR);_goto(823); // JP a16 M2 T1
        case  823: _rd();cpu->pc++;_goto(824); // JP a16 M2 T2
        case  824: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_SF))){cpu->pc++;_fetch();}_goto(825); // JP a16 M2 T3
        case  825: _t1(cpu->pc,I8085_STATUS_MR);_goto(826); // JP a16 M3 T1
        case  826: _rd();cpu->pc++;_goto(827); // JP a16 M3 T2
        case  827: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JP a16 M3 T3
        case  828: if((!(cpu->f&I8085_SF))){cpu->sp--;}_goto(829); // CP a16 M1 T5
        case  829: _goto(830); // CP a16 M1 T6
        case  830: _t1(cpu->pc,I8085_STATUS_MR);_goto(831); // CP a16 M2 T1
        case  831: _rd();cpu->pc++;_goto(832); // CP a16 M2 T2
        case  832: _ready_rd();cpu->z=_gd();if(!(!(cpu->f&I8085_SF))){cpu->pc++;_fetch();}_goto(833); // CP a16 M2 T3
        case  833: _t1(cpu->pc,I8085_STATUS_MR);_goto(834); // CP a16 M3 T1
        case  834: _rd();cpu->pc++;_goto(835); // CP a16 M3 T2
        case  835: _ready_rd();cpu->w=_gd();_goto(836); // CP a16 M3 T3
        case  836: _t1(cpu->sp,I8085_STATUS_MW);_goto(837); // CP a16 M4 T1
        case  837: _wr(cpu->pch);cpu->sp--;_goto(838); // CP a16 M4 T2
        case  838: _ready_wr();_goto(839); // CP a16 M4 T3
        case  839: _t1(cpu->sp,I8085_STATUS_MW);_goto(840); // CP a16 M5 T1
        case  840: _wr(cpu->pcl);_goto(841); // CP a16 M5 T2
        case  841: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CP a16 M5 T3
        case  842: cpu->sp--;_goto(843); // PUSH PSW M1 T5
        case  843: _goto(844); // PUSH PSW M1 T6
        case  844: _t1(cpu->sp,I8085_STATUS_MW);_goto(845); // PUSH PSW M2 T1
        case  845: _wr(cpu->a);cpu->sp--;_goto(846); // PUSH PSW M2 T2
        case  846: _ready_wr();_goto(847); // PUSH PSW M2 T3
        case  847: _t1(cpu->sp,I8085_STATUS_MW);_goto(848); // PUSH PSW M3 T1
        case  848: _wr(cpu->f);_goto(849); // PUSH PSW M3 T2
        case  849: _ready_wr();_fetch(); // PUSH PSW M3 T3
        case  850: _t1(cpu->pc,I8085_STATUS_MR);_goto(851); // ORI d8 M2 T1
        case  851: _rd();cpu->pc++;_goto(852); // ORI d8 M2 T2
        case  852: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_ORA,cpu->tmp);_fetch(); // ORI d8 M2 T3
        case  853: cpu->sp--;_goto(854); // RST 6 M1 T5
        case  854: _goto(855); // RST 6 M1 T6
        case  855: _t1(cpu->sp,I8085_STATUS_MW);_goto(856); // RST 6 M2 T1
        case  856: _wr(cpu->pch);cpu->sp--;_goto(857); // RST 6 M2 T2
        case  857: _ready_wr();_goto(858); // RST 6 M2 T3
        case  858: _t1(cpu->sp,I8085_STATUS_MW);_goto(859); // RST 6 M3 T1
        case  859: _wr(cpu->pcl);_goto(860); // RST 6 M3 T2
        case  860: _ready_wr();cpu->pc=0x30;_fetch(); // RST 6 M3 T3
        case  861: _goto(862); // RM M1 T5
        case  862: if(!(cpu->f&I8085_SF)){_fetch();}_goto(863); // RM M1 T6
        case  863: _t1(cpu->sp,I8085_STATUS_MR);_goto(864); // RM M2 T1
        case  864: _rd();cpu->sp++;_goto(865); // RM M2 T2
        case  865: _ready_rd();cpu->z=_gd();_goto(866); // RM M2 T3
        case  866: _t1(cpu->sp,I8085_STATUS_MR);_goto(867); // RM M3 T1
        case  867: _rd();cpu->sp++;_goto(868); // RM M3 T2
        case  868: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RM M3 T3
        case  869: _goto(870); // SPHL M1 T5
        case  870: cpu->sp=cpu->hl;_fetch(); // SPHL M1 T6
        case  871: _t1(cpu->pc,I8085_STATUS_MR);_goto(872); // JM a16 M2 T1
        case  872: _rd();cpu->pc++;_goto(873); // JM a16 M2 T2
        case  873: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_SF)){cpu->pc++;_fetch();}_goto(874); // JM a16 M2 T3
        case  874: _t1(cpu->pc,I8085_STATUS_MR);_goto(875); // JM a16 M3 T1
        case  875: _rd();cpu->pc++;_goto(876); // JM a16 M3 T2
        case  876: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JM a16 M3 T3
        case  877: if((cpu->f&I8085_SF)){cpu->sp--;}_goto(878); // CM a16 M1 T5
        case  878: _goto(879); // CM a16 M1 T6
        case  879: _t1(cpu->pc,I8085_STATUS_MR);_goto(880); // CM a16 M2 T1
        case  880: _rd();cpu->pc++;_goto(881); // CM a16 M2 T2
        case  881: _ready_rd();cpu->z=_gd();if(!(cpu->f&I8085_SF)){cpu->pc++;_fetch();}_goto(882); // CM a16 M2 T3
        case  882: _t1(cpu->pc,I8085_STATUS_MR);_goto(883); // CM a16 M3 T1
        case  883: _rd();cpu->pc++;_goto(884); // CM a16 M3 T2
        case  884: _ready_rd();cpu->w=_gd();_goto(885); // CM a16 M3 T3
        case  885: _t1(cpu->sp,I8085_STATUS_MW);_goto(886); // CM a16 M4 T1
        case  886: _wr(cpu->pch);cpu->sp--;_goto(887); // CM a16 M4 T2
        case  887: _ready_wr();_goto(888); // CM a16 M4 T3
        case  888: _t1(cpu->sp,I8085_STATUS_MW);_goto(889); // CM a16 M5 T1
        case  889: _wr(cpu->pcl);_goto(890); // CM a16 M5 T2
        case  890: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CM a16 M5 T3
        case  891: _t1(cpu->pc,I8085_STATUS_MR);_goto(892); // CPI d8 M2 T1
        case  892: _rd();cpu->pc++;_goto(893); // CPI d8 M2 T2
        case  893: _ready_rd();cpu->tmp=_gd();_i8085_alu(cpu,I8085_ALU_CMP,cpu->tmp);_fetch(); // CPI d8 M2 T3
        case  894: cpu->sp--;_goto(895); // RST 7 M1 T5
        case  895: _goto(896); // RST 7 M1 T6
        case  896: _t1(cpu->sp,I8085_STATUS_MW);_goto(897); // RST 7 M2 T1
        case  897: _wr(cpu->pch);cpu->sp--;_goto(898); // RST 7 M2 T2
        case  898: _ready_wr();_goto(899); // RST 7 M2 T3
        case  899: _t1(cpu->sp,I8085_STATUS_MW);_goto(900); // RST 7 M3 T1
        case  900: _wr(cpu->pcl);_goto(901); // RST 7 M3 T2
        case  901: _ready_wr();cpu->pc=0x38;_fetch(); // RST 7 M3 T3
        // %>

        //=== M1, the opcode fetch, shared by every instruction. Its T4 (and
        //    T5, T6) are the instruction's own first steps.
        case I8085_M1_T1: {
            // A valid interrupt sampled two states before this T1 is taken
            // here (Figure 2-11; p. 2-16), except straight after EI, which
            // enables interrupts only after the next instruction.
            uint16_t v = 0;
            if (cpu->opcode != 0xFB) {
                v = _i8085_select(cpu, cpu->irq_sample[2]);
            }
            if (v) {
                _i8085_take(cpu, v);
                _t1(cpu->pc, I8085_STATUS_INT);
                _goto(I8085_INT_T2);
            }
            _t1(cpu->pc, I8085_STATUS_OF);
            _goto(I8085_M1_T2);
        }
        case I8085_M1_T2: _rd(); cpu->pc++; _goto(I8085_M1_T3);
        case I8085_M1_T3: _ready_rd(); cpu->opcode = _gd(); _goto(cpu->opcode);

        //=== RST n.5 / TRAP: the CPU makes its own restart (Figure 2-19). M1 is
        //    a six-state bus idle cycle (ALE and PC out, IO/M high, no RD,
        //    READY ignored, PC not incremented); M2 and M3 push PC.
        case I8085_INT_T1: _t1(cpu->pc, I8085_STATUS_INT); _goto(I8085_INT_T2);
        case I8085_INT_T2: _goto(I8085_INT_T3);
        case I8085_INT_T3: _goto(I8085_INT_T4);
        case I8085_INT_T4: _goto(I8085_INT_T5);
        case I8085_INT_T5: cpu->sp--; _goto(I8085_INT_T6);
        case I8085_INT_T6: _goto(I8085_INT_M2_T1);
        case I8085_INT_M2_T1: _t1(cpu->sp, I8085_STATUS_MW); _goto(I8085_INT_M2_T2);
        case I8085_INT_M2_T2: _wr(cpu->pch); cpu->sp--; _goto(I8085_INT_M2_T3);
        case I8085_INT_M2_T3: _ready_wr(); _goto(I8085_INT_M3_T1);
        case I8085_INT_M3_T1: _t1(cpu->sp, I8085_STATUS_MW); _goto(I8085_INT_M3_T2);
        case I8085_INT_M3_T2: _wr(cpu->pcl); _goto(I8085_INT_M3_T3);
        case I8085_INT_M3_T3: _ready_wr(); cpu->pc = cpu->int_vector; _fetch();

        //=== THALT (Figures 2-11, 2-20): left by a valid interrupt sampled in
        //    a THALT state; the next state is T1 of the restart. RESET is
        //    i8085_reset(). HOLD: not modeled.
        case I8085_HALT: {
            uint16_t v = _i8085_select(cpu, cpu->irq_sample[0]);
            if (v) {
                _i8085_take(cpu, v);
                cpu->status = I8085_STATUS_HALT;
                _goto(I8085_INT_T1);
            }
            cpu->status = I8085_STATUS_HALT;
            goto step_to;
        }

        default: CHIPS_ASSERT(false);
    }
step_to:
    pins |= cpu->status;
    if (cpu->sod) {
        pins |= I8085_SOD;
    }
    return pins;
}

#undef _goto
#undef _fetch
#undef _unimplemented
#undef _gd
#undef _t1
#undef _rd
#undef _wr
#undef _ready_rd
#undef _ready_wr
#undef _bi

#endif // CHIPS_IMPL

/*
    This file is an altered version of floooh/chips' code-generated CPU
    headers (https://github.com/floooh/chips): a new 8085 decoder generated
    by a fork of codegen/z80_gen.py. The notice below is floooh/chips'
    license, kept as its clause 3 requires.

    zlib/libpng license

    Copyright (c) 2018 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution.

    The ALU and flag arithmetic is ported from m8080:

    MIT License
    Copyright (c) 2019 Pedro Minicz

    Permission is hereby granted, free of charge, to any person obtaining a copy of
    this software and associated documentation files (the "Software"), to deal in
    the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is furnished to do
    so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/
