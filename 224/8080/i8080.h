#pragma once
/*#
    # i8080.h

    A cycle-stepped Intel 8080 in a C header, in the style of floooh's
    `chips/z80.h` (https://github.com/floooh/chips, zlib license; see
    LICENSE-floooh-chips). One call to `i8080_tick()` runs one clock state
    (T1, T2, TW, T3, T4 or T5) and returns the CPU's pins.

    The ALU and flag arithmetic is ported from Pedro Minicz's m8080 (2019,
    MIT license; see LICENSE-m8080), which the Lexicon row machine used
    before this core.

    Parts of this file between `// <% name` and `// %>` are written by
    `codegen/i8080_gen.py` from `codegen/i8080_desc.yml`. Edit those two
    files, not the generated parts.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the
    implementation, or define `I8080_API` as `static` as well and include it
    with CHIPS_IMPL in every file that uses it. (The register pairs use
    anonymous structs in unions, which C++ compilers accept as an extension.)

    ## Pins

    ***************************************
    *            +-----------+            *
    *  SYNC  <---|           |---> A0     *
    *  DBIN  <---|           |---> ..     *
    *  WR    <---|           |---> A15    *
    *  WAIT  <---|   8080    |            *
    *  INTE  <---|           |<--> D0     *
    *  READY --->|           |<--> ..     *
    *  INT   --->|           |<--> D7     *
    *            +-----------+            *
    ***************************************

    All pins are positive logic here: I8080_WR set means the chip's WR/ pin
    is low (writing). HOLD/HLDA are not modeled yet.

    ## What the pins mean after each tick

    The pins `i8080_tick()` returns are the levels at the **end** of the
    state it just ran (Intel User's Manual, 1975, Figures 2-5 to 2-7):

    | state | pins                                                          |
    |-------|---------------------------------------------------------------|
    | T1    | A = address, D = **status word**, SYNC                        |
    | T2    | A; a read raises DBIN; a write puts its data on D             |
    | TW    | as T2, plus WAIT; a write also has WR                         |
    | T3    | a read has taken D (DBIN has fallen); a write has D and WR    |
    | T4 T5 | internal work only                                            |
    | TWH   | halted: WAIT                                                  |

    And the inputs the CPU looks at, in the pins you pass in:

    - **READY** is looked at by the tick after T2 or TW. If it is low, that
      tick is another TW (the 8080 samples READY during T2/TW).
    - **D** for a read is taken by the tick that runs T3. So when a
      returned state has DBIN up, put the byte on D before the next call.
    - **INT** is looked at by the tick that would start the next instruction
      (and by every halted tick). If INTE is set, that tick is instead T1 of
      an interrupt acknowledge (status I8080_CYCLE_INTA): DBIN rises in T2 as
      for a fetch, and the byte you put on D is executed as the instruction
      (0xFF = RST 7 from an 8228 or 8238 wired for it). The PC is not incremented.

    The status word at T1 tells an 8228 or 8238 system controller what the cycle
    is. Its bits are I8080_STATUS_INTA .. I8080_STATUS_MEMR; the ten cycle
    types are I8080_CYCLE_FETCH .. I8080_CYCLE_INTA_HALT.

    ## Usage

    ~~~C
        i8080_t cpu;
        uint64_t pins = i8080_init(&cpu);
        uint8_t mem[1 << 16];
        for (;;) {
            pins = i8080_tick(&cpu, pins | I8080_READY);
            if (pins & I8080_DBIN) {
                I8080_SET_DATA(pins, mem[I8080_GET_ADDR(pins)]);
            }
            if (pins & I8080_WR) {
                mem[I8080_GET_ADDR(pins)] = I8080_GET_DATA(pins);
            }
        }
    ~~~
#*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// address pins
#define I8080_PIN_A0    (0)
#define I8080_PIN_A15   (15)
// data pins
#define I8080_PIN_D0    (16)
#define I8080_PIN_D7    (23)
// outputs
#define I8080_PIN_SYNC  (24)    // T1: the status word is on D0..D7
#define I8080_PIN_DBIN  (25)    // data bus in: put the read byte on D
#define I8080_PIN_WR    (26)    // WR/ low: D holds the byte being written
#define I8080_PIN_WAIT  (27)    // in a wait state (TW) or halted (TWH)
#define I8080_PIN_INTE  (28)    // interrupts enabled
// inputs
#define I8080_PIN_READY (32)    // memory or I/O is ready; low inserts TW states
#define I8080_PIN_INT   (33)    // interrupt request

#define I8080_SYNC  (1ULL<<I8080_PIN_SYNC)
#define I8080_DBIN  (1ULL<<I8080_PIN_DBIN)
#define I8080_WR    (1ULL<<I8080_PIN_WR)
#define I8080_WAIT  (1ULL<<I8080_PIN_WAIT)
#define I8080_INTE  (1ULL<<I8080_PIN_INTE)
#define I8080_READY (1ULL<<I8080_PIN_READY)
#define I8080_INT   (1ULL<<I8080_PIN_INT)

// the outputs that are recomputed on every tick
#define I8080_CTRL_PIN_MASK (I8080_SYNC|I8080_DBIN|I8080_WR|I8080_WAIT|I8080_INTE)

#define I8080_GET_ADDR(p) ((uint16_t)(p))
#define I8080_SET_ADDR(p,a) {p=((p)&~0xFFFFULL)|((a)&0xFFFFULL);}
#define I8080_GET_DATA(p) ((uint8_t)((p)>>16))
#define I8080_SET_DATA(p,d) {p=((p)&~0xFF0000ULL)|((((uint64_t)(d))<<16)&0xFF0000ULL);}

// Status word bits, on D0..D7 during T1 (User's Manual Table 2-1).
#define I8080_STATUS_INTA  (1<<0)   // interrupt acknowledge
#define I8080_STATUS_WO    (1<<1)   // WO/: 0 = write or output, 1 = read or input
#define I8080_STATUS_STACK (1<<2)   // the address is SP
#define I8080_STATUS_HLTA  (1<<3)   // halt acknowledge
#define I8080_STATUS_OUT   (1<<4)   // output cycle
#define I8080_STATUS_M1    (1<<5)   // opcode fetch
#define I8080_STATUS_INP   (1<<6)   // input cycle
#define I8080_STATUS_MEMR  (1<<7)   // memory read

// The ten machine-cycle types and their status words (Table 2-1).
#define I8080_CYCLE_FETCH     (0xA2)
#define I8080_CYCLE_MREAD     (0x82)
#define I8080_CYCLE_MWRITE    (0x00)
#define I8080_CYCLE_SREAD     (0x86)
#define I8080_CYCLE_SWRITE    (0x04)
#define I8080_CYCLE_INPUT     (0x42)
#define I8080_CYCLE_OUTPUT    (0x10)
#define I8080_CYCLE_INTA      (0x23)
#define I8080_CYCLE_HALT      (0x8A)
#define I8080_CYCLE_INTA_HALT (0x2B)

// Flag bits in F (the low byte of PSW): S Z 0 AC 0 P 1 CY.
#define I8080_CF (1<<0)     // carry
#define I8080_PF (1<<2)     // parity (even)
#define I8080_HF (1<<4)     // auxiliary carry
#define I8080_ZF (1<<6)     // zero
#define I8080_SF (1<<7)     // sign

// ALU operations that finish in T2 of the next fetch (Table 2-2 note 9).
enum {
    I8080_ALU_NONE = 0,
    I8080_ALU_ADD, I8080_ALU_ADC, I8080_ALU_SUB, I8080_ALU_SBB,
    I8080_ALU_ANA, I8080_ALU_XRA, I8080_ALU_ORA, I8080_ALU_CMP,
    I8080_ALU_RLC, I8080_ALU_RRC, I8080_ALU_RAL, I8080_ALU_RAR,
};

// CPU state
typedef struct {
    uint16_t step;          // the next decoder step (which state of which instruction)
    uint8_t opcode;         // instruction register
    bool inte;              // interrupt enable flip-flop
    bool unimplemented;     // hit an opcode the description file does not cover yet
    uint8_t tmp;            // ALU temporary register (TMP)
    uint8_t act;            // ALU temporary accumulator (ACT)
    uint8_t alu;            // I8080_ALU_*: an operation waiting for the next fetch's T2
    union { struct { uint8_t f; uint8_t a; }; uint16_t psw; };
    union { struct { uint8_t c; uint8_t b; }; uint16_t bc; };
    union { struct { uint8_t e; uint8_t d; }; uint16_t de; };
    union { struct { uint8_t l; uint8_t h; }; uint16_t hl; };
    union { struct { uint8_t z; uint8_t w; }; uint16_t wz; };   // temporary pair
    union { struct { uint8_t spl; uint8_t sph; }; uint16_t sp; };
    union { struct { uint8_t pcl; uint8_t pch; }; uint16_t pc; };
} i8080_t;

// The storage class of the functions below. A program can define it as
// `static` and compile the implementation into the file that uses it.
#ifndef I8080_API
#define I8080_API
#endif

// initialize a new 8080 and return the initial pins
I8080_API uint64_t i8080_init(i8080_t* cpu);
// RESET: PC = 0, INTE = 0, next state is T1 of a fetch; registers keep their values
I8080_API uint64_t i8080_reset(i8080_t* cpu);
// run one clock state, return the pins at its end
I8080_API uint64_t i8080_tick(i8080_t* cpu, uint64_t pins);
// true when the next tick starts a new instruction (T1 of a fetch)
I8080_API bool i8080_opdone(const i8080_t* cpu);
// true when halted: HLT has finished (its 7 states) and the CPU sits in TWH
I8080_API bool i8080_halted(const i8080_t* cpu);
// The registers as the program sees them between instructions: a copy with
// any ALU result still waiting for the next fetch's T2 (note 9) applied.
I8080_API i8080_t i8080_registers(const i8080_t* cpu);

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
#define I8080_M1_T1 980
#define I8080_M1_T2 981
#define I8080_M1_T3 982
#define I8080_INTA_T2 983
#define I8080_HALT_TWH1 984
#define I8080_HALT_TWH 985
// %>

uint64_t i8080_init(i8080_t* cpu) {
    CHIPS_ASSERT(cpu);
    memset(cpu, 0, sizeof(i8080_t));
    cpu->f = 0x02;      // bit 1 always reads 1
    return i8080_reset(cpu);
}

uint64_t i8080_reset(i8080_t* cpu) {
    cpu->pc = 0;
    cpu->inte = false;
    cpu->step = I8080_M1_T1;
    return 0;
}

bool i8080_opdone(const i8080_t* cpu) {
    return cpu->step == I8080_M1_T1;
}

bool i8080_halted(const i8080_t* cpu) {
    return cpu->step == I8080_HALT_TWH;
}

// ---- ALU ------------------------------------------------------------------
// Ported from m8080 (MIT). Flags: S Z 0 AC 0 P 1 CY; bit 1 always set.

// sign, zero and (even) parity of a result
static inline uint8_t _i8080_szp(uint8_t v) {
    uint8_t p = v;
    p ^= p >> 4;
    p ^= p >> 2;
    p ^= p >> 1;
    uint8_t flags = v & I8080_SF;
    if (v == 0) {
        flags |= I8080_ZF;
    }
    if (!(p & 1)) {
        flags |= I8080_PF;
    }
    return flags;
}

// Carry out of bit 7 and bit 3 of a + b + carry_in. For subtraction the
// 8080 adds the complement, so AC is set when there is *no* borrow out of
// bit 3 and CY is set when there *is* a borrow out of bit 7.
static inline void _i8080_add(i8080_t* cpu, uint8_t a, uint8_t b, unsigned carry_in, bool store) {
    unsigned res = (unsigned)a + b + carry_in;
    uint8_t flags = 0x02 | _i8080_szp((uint8_t)res);
    if (res >> 8) {
        flags |= I8080_CF;
    }
    if (((a & 0x0F) + (b & 0x0F) + carry_in) >> 4) {
        flags |= I8080_HF;
    }
    cpu->f = flags;
    if (store) {
        cpu->a = (uint8_t)res;
    }
}

static inline void _i8080_sub(i8080_t* cpu, uint8_t a, uint8_t b, unsigned borrow_in, bool store) {
    int res = (int)a - (int)b - (int)borrow_in;
    int low = (int)(a & 0x0F) - (int)(b & 0x0F) - (int)borrow_in;
    uint8_t flags = 0x02 | _i8080_szp((uint8_t)res);
    if (res < 0) {
        flags |= I8080_CF;
    }
    if (low >= 0) {
        flags |= I8080_HF;
    }
    cpu->f = flags;
    if (store) {
        cpu->a = (uint8_t)res;
    }
}

// The operation left by an instruction, carried out in T2 of the next fetch:
// A and the flags from ACT (A as it was) and TMP (the operand).
static inline void _i8080_alu_finish(i8080_t* cpu) {
    uint8_t a = cpu->act;
    uint8_t t = cpu->tmp;
    unsigned cy = cpu->f & I8080_CF;
    switch (cpu->alu) {
        case I8080_ALU_ADD:
            _i8080_add(cpu, a, t, 0, true);
            break;
        case I8080_ALU_ADC:
            _i8080_add(cpu, a, t, cy, true);
            break;
        case I8080_ALU_SUB:
            _i8080_sub(cpu, a, t, 0, true);
            break;
        case I8080_ALU_SBB:
            _i8080_sub(cpu, a, t, cy, true);
            break;
        case I8080_ALU_CMP:
            _i8080_sub(cpu, a, t, 0, false);
            break;
        case I8080_ALU_ANA:
            // AC is the OR of bit 3 of the two operands
            cpu->a = a & t;
            cpu->f = 0x02 | _i8080_szp(cpu->a);
            if ((a | t) & 0x08) {
                cpu->f |= I8080_HF;
            }
            break;
        case I8080_ALU_XRA:
            cpu->a = a ^ t;
            cpu->f = 0x02 | _i8080_szp(cpu->a);
            break;
        case I8080_ALU_ORA:
            cpu->a = a | t;
            cpu->f = 0x02 | _i8080_szp(cpu->a);
            break;
        // rotates change only CY
        case I8080_ALU_RLC:
            cpu->a = (uint8_t)(a << 1 | a >> 7);
            cpu->f = (cpu->f & ~I8080_CF) | (a >> 7);
            break;
        case I8080_ALU_RRC:
            cpu->a = (uint8_t)(a >> 1 | a << 7);
            cpu->f = (cpu->f & ~I8080_CF) | (a & 1);
            break;
        case I8080_ALU_RAL:
            cpu->a = (uint8_t)(a << 1 | cy);
            cpu->f = (cpu->f & ~I8080_CF) | (a >> 7);
            break;
        case I8080_ALU_RAR:
            cpu->a = (uint8_t)(a >> 1 | cy << 7);
            cpu->f = (cpu->f & ~I8080_CF) | (a & 1);
            break;
        default:
            break;
    }
    cpu->alu = I8080_ALU_NONE;
}

// INR/DCR: S Z P from the result, AC as for an add/subtract of 1; CY unchanged.
static inline uint8_t _i8080_inr(i8080_t* cpu, uint8_t v) {
    uint8_t res = (uint8_t)(v + 1);
    cpu->f = (cpu->f & I8080_CF) | 0x02 | _i8080_szp(res);
    if ((res & 0x0F) == 0) {
        cpu->f |= I8080_HF;
    }
    return res;
}

static inline uint8_t _i8080_dcr(i8080_t* cpu, uint8_t v) {
    uint8_t res = (uint8_t)(v - 1);
    cpu->f = (cpu->f & I8080_CF) | 0x02 | _i8080_szp(res);
    if ((res & 0x0F) != 0x0F) {
        cpu->f |= I8080_HF;
    }
    return res;
}

// One half of DAD: ACT + TMP (+ CY for the high half) into *dst; CY only.
static inline void _i8080_dad(i8080_t* cpu, uint8_t* dst, bool with_carry) {
    unsigned carry_in = 0;
    if (with_carry) {
        carry_in = cpu->f & I8080_CF;
    }
    unsigned res = (unsigned)cpu->act + cpu->tmp + carry_in;
    *dst = (uint8_t)res;
    cpu->f = (cpu->f & ~I8080_CF) | (res >> 8);
}

// DAA (as m8080): add 6 to the low digit if it is over 9 or AC is set; then
// 6 to the high digit if it is over 9 or CY is set. CY can be set, not reset.
static inline void _i8080_daa(i8080_t* cpu) {
    uint8_t a = cpu->a;
    uint8_t cy = cpu->f & I8080_CF;
    uint8_t ac = cpu->f & I8080_HF;
    if (ac || (a & 0x0F) > 0x09) {
        cy |= (uint8_t)((a + 0x06) >> 8);
        if (((a & 0x0F) + 0x06) >> 4) {
            ac = I8080_HF;
        } else {
            ac = 0;
        }
        a = (uint8_t)(a + 0x06);
    }
    if (cy || (a & 0xF0) > 0x90) {
        cy |= (uint8_t)((a + 0x60) >> 8);
        a = (uint8_t)(a + 0x60);
    }
    cpu->a = a;
    cpu->f = 0x02 | _i8080_szp(a) | ac | cy;
}

i8080_t i8080_registers(const i8080_t* cpu) {
    i8080_t copy = *cpu;
    if (copy.alu) {
        _i8080_alu_finish(&copy);
    }
    return copy;
}

// ---- state helpers ----------------------------------------------------------
// Each generated `case` is one clock state. `in` is the pins passed in; `pins`
// is what this state returns.
#define _goto(n)        cpu->step=(n);goto step_to
#define _fetch()        _goto(I8080_M1_T1)
#define _unimplemented() cpu->unimplemented=true;goto step_to
#define _gd()           I8080_GET_DATA(in)
// T1: address and status word out, SYNC
#define _t1(ab,status)  I8080_SET_ADDR(pins,ab);I8080_SET_DATA(pins,status);pins|=I8080_SYNC
// T2 of a read: DBIN rises
#define _dbin()         pins|=I8080_DBIN
// T2 of a write: the data replaces the status word on D
#define _sd(d)          I8080_SET_DATA(pins,d)
// T3 of a read: if READY was low, this tick is a TW instead (same step next time)
#define _ready_rd()     if(!(in&I8080_READY)){pins|=I8080_WAIT|I8080_DBIN;goto step_to;}
// T3 of a write: WR/ is low from the state after T2 through T3, TWs included
#define _ready_wr()     pins|=I8080_WR;if(!(in&I8080_READY)){pins|=I8080_WAIT;goto step_to;}

uint64_t i8080_tick(i8080_t* cpu, uint64_t pins) {
    const uint64_t in = pins;
    pins &= ~I8080_CTRL_PIN_MASK;
    switch (cpu->step) {
        // <% decoder
        case    0: _fetch(); // NOP M1 T4
        case    1: _goto(256); // LXI B,d16 M1 T4
        case    2: _goto(262); // STAX B M1 T4
        case    3: _goto(265); // INX B M1 T4
        case    4: cpu->tmp=_i8080_inr(cpu,cpu->b);_goto(266); // INR B M1 T4
        case    5: cpu->tmp=_i8080_dcr(cpu,cpu->b);_goto(267); // DCR B M1 T4
        case    6: _goto(268); // MVI B,d8 M1 T4
        case    7: cpu->act=cpu->a;cpu->alu=I8080_ALU_RLC;_fetch(); // RLC M1 T4
        case    8: _fetch(); // NOP M1 T4
        case    9: _goto(271); // DAD B M1 T4
        case   10: _goto(277); // LDAX B M1 T4
        case   11: _goto(280); // DCX B M1 T4
        case   12: cpu->tmp=_i8080_inr(cpu,cpu->c);_goto(281); // INR C M1 T4
        case   13: cpu->tmp=_i8080_dcr(cpu,cpu->c);_goto(282); // DCR C M1 T4
        case   14: _goto(283); // MVI C,d8 M1 T4
        case   15: cpu->act=cpu->a;cpu->alu=I8080_ALU_RRC;_fetch(); // RRC M1 T4
        case   16: _fetch(); // NOP M1 T4
        case   17: _goto(286); // LXI D,d16 M1 T4
        case   18: _goto(292); // STAX D M1 T4
        case   19: _goto(295); // INX D M1 T4
        case   20: cpu->tmp=_i8080_inr(cpu,cpu->d);_goto(296); // INR D M1 T4
        case   21: cpu->tmp=_i8080_dcr(cpu,cpu->d);_goto(297); // DCR D M1 T4
        case   22: _goto(298); // MVI D,d8 M1 T4
        case   23: cpu->act=cpu->a;cpu->alu=I8080_ALU_RAL;_fetch(); // RAL M1 T4
        case   24: _fetch(); // NOP M1 T4
        case   25: _goto(301); // DAD D M1 T4
        case   26: _goto(307); // LDAX D M1 T4
        case   27: _goto(310); // DCX D M1 T4
        case   28: cpu->tmp=_i8080_inr(cpu,cpu->e);_goto(311); // INR E M1 T4
        case   29: cpu->tmp=_i8080_dcr(cpu,cpu->e);_goto(312); // DCR E M1 T4
        case   30: _goto(313); // MVI E,d8 M1 T4
        case   31: cpu->act=cpu->a;cpu->alu=I8080_ALU_RAR;_fetch(); // RAR M1 T4
        case   32: _fetch(); // NOP M1 T4
        case   33: _goto(316); // LXI H,d16 M1 T4
        case   34: _goto(322); // SHLD a16 M1 T4
        case   35: _goto(334); // INX H M1 T4
        case   36: cpu->tmp=_i8080_inr(cpu,cpu->h);_goto(335); // INR H M1 T4
        case   37: cpu->tmp=_i8080_dcr(cpu,cpu->h);_goto(336); // DCR H M1 T4
        case   38: _goto(337); // MVI H,d8 M1 T4
        case   39: _i8080_daa(cpu);_fetch(); // DAA M1 T4
        case   40: _fetch(); // NOP M1 T4
        case   41: _goto(340); // DAD H M1 T4
        case   42: _goto(346); // LHLD a16 M1 T4
        case   43: _goto(358); // DCX H M1 T4
        case   44: cpu->tmp=_i8080_inr(cpu,cpu->l);_goto(359); // INR L M1 T4
        case   45: cpu->tmp=_i8080_dcr(cpu,cpu->l);_goto(360); // DCR L M1 T4
        case   46: _goto(361); // MVI L,d8 M1 T4
        case   47: cpu->a=~cpu->a;_fetch(); // CMA M1 T4
        case   48: _fetch(); // NOP M1 T4
        case   49: _goto(364); // LXI SP,d16 M1 T4
        case   50: _goto(370); // STA a16 M1 T4
        case   51: _goto(379); // INX SP M1 T4
        case   52: _goto(380); // INR M M1 T4
        case   53: _goto(386); // DCR M M1 T4
        case   54: _goto(392); // MVI M,d8 M1 T4
        case   55: cpu->f|=I8080_CF;_fetch(); // STC M1 T4
        case   56: _fetch(); // NOP M1 T4
        case   57: _goto(398); // DAD SP M1 T4
        case   58: _goto(404); // LDA a16 M1 T4
        case   59: _goto(413); // DCX SP M1 T4
        case   60: cpu->tmp=_i8080_inr(cpu,cpu->a);_goto(414); // INR A M1 T4
        case   61: cpu->tmp=_i8080_dcr(cpu,cpu->a);_goto(415); // DCR A M1 T4
        case   62: _goto(416); // MVI A,d8 M1 T4
        case   63: cpu->f^=I8080_CF;_fetch(); // CMC M1 T4
        case   64: cpu->tmp=cpu->b;_goto(419); // MOV B,B M1 T4
        case   65: cpu->tmp=cpu->c;_goto(420); // MOV B,C M1 T4
        case   66: cpu->tmp=cpu->d;_goto(421); // MOV B,D M1 T4
        case   67: cpu->tmp=cpu->e;_goto(422); // MOV B,E M1 T4
        case   68: cpu->tmp=cpu->h;_goto(423); // MOV B,H M1 T4
        case   69: cpu->tmp=cpu->l;_goto(424); // MOV B,L M1 T4
        case   70: _goto(425); // MOV B,M M1 T4
        case   71: cpu->tmp=cpu->a;_goto(428); // MOV B,A M1 T4
        case   72: cpu->tmp=cpu->b;_goto(429); // MOV C,B M1 T4
        case   73: cpu->tmp=cpu->c;_goto(430); // MOV C,C M1 T4
        case   74: cpu->tmp=cpu->d;_goto(431); // MOV C,D M1 T4
        case   75: cpu->tmp=cpu->e;_goto(432); // MOV C,E M1 T4
        case   76: cpu->tmp=cpu->h;_goto(433); // MOV C,H M1 T4
        case   77: cpu->tmp=cpu->l;_goto(434); // MOV C,L M1 T4
        case   78: _goto(435); // MOV C,M M1 T4
        case   79: cpu->tmp=cpu->a;_goto(438); // MOV C,A M1 T4
        case   80: cpu->tmp=cpu->b;_goto(439); // MOV D,B M1 T4
        case   81: cpu->tmp=cpu->c;_goto(440); // MOV D,C M1 T4
        case   82: cpu->tmp=cpu->d;_goto(441); // MOV D,D M1 T4
        case   83: cpu->tmp=cpu->e;_goto(442); // MOV D,E M1 T4
        case   84: cpu->tmp=cpu->h;_goto(443); // MOV D,H M1 T4
        case   85: cpu->tmp=cpu->l;_goto(444); // MOV D,L M1 T4
        case   86: _goto(445); // MOV D,M M1 T4
        case   87: cpu->tmp=cpu->a;_goto(448); // MOV D,A M1 T4
        case   88: cpu->tmp=cpu->b;_goto(449); // MOV E,B M1 T4
        case   89: cpu->tmp=cpu->c;_goto(450); // MOV E,C M1 T4
        case   90: cpu->tmp=cpu->d;_goto(451); // MOV E,D M1 T4
        case   91: cpu->tmp=cpu->e;_goto(452); // MOV E,E M1 T4
        case   92: cpu->tmp=cpu->h;_goto(453); // MOV E,H M1 T4
        case   93: cpu->tmp=cpu->l;_goto(454); // MOV E,L M1 T4
        case   94: _goto(455); // MOV E,M M1 T4
        case   95: cpu->tmp=cpu->a;_goto(458); // MOV E,A M1 T4
        case   96: cpu->tmp=cpu->b;_goto(459); // MOV H,B M1 T4
        case   97: cpu->tmp=cpu->c;_goto(460); // MOV H,C M1 T4
        case   98: cpu->tmp=cpu->d;_goto(461); // MOV H,D M1 T4
        case   99: cpu->tmp=cpu->e;_goto(462); // MOV H,E M1 T4
        case  100: cpu->tmp=cpu->h;_goto(463); // MOV H,H M1 T4
        case  101: cpu->tmp=cpu->l;_goto(464); // MOV H,L M1 T4
        case  102: _goto(465); // MOV H,M M1 T4
        case  103: cpu->tmp=cpu->a;_goto(468); // MOV H,A M1 T4
        case  104: cpu->tmp=cpu->b;_goto(469); // MOV L,B M1 T4
        case  105: cpu->tmp=cpu->c;_goto(470); // MOV L,C M1 T4
        case  106: cpu->tmp=cpu->d;_goto(471); // MOV L,D M1 T4
        case  107: cpu->tmp=cpu->e;_goto(472); // MOV L,E M1 T4
        case  108: cpu->tmp=cpu->h;_goto(473); // MOV L,H M1 T4
        case  109: cpu->tmp=cpu->l;_goto(474); // MOV L,L M1 T4
        case  110: _goto(475); // MOV L,M M1 T4
        case  111: cpu->tmp=cpu->a;_goto(478); // MOV L,A M1 T4
        case  112: cpu->tmp=cpu->b;_goto(479); // MOV M,B M1 T4
        case  113: cpu->tmp=cpu->c;_goto(482); // MOV M,C M1 T4
        case  114: cpu->tmp=cpu->d;_goto(485); // MOV M,D M1 T4
        case  115: cpu->tmp=cpu->e;_goto(488); // MOV M,E M1 T4
        case  116: cpu->tmp=cpu->h;_goto(491); // MOV M,H M1 T4
        case  117: cpu->tmp=cpu->l;_goto(494); // MOV M,L M1 T4
        case  118: _goto(497); // HLT M1 T4
        case  119: cpu->tmp=cpu->a;_goto(499); // MOV M,A M1 T4
        case  120: cpu->tmp=cpu->b;_goto(502); // MOV A,B M1 T4
        case  121: cpu->tmp=cpu->c;_goto(503); // MOV A,C M1 T4
        case  122: cpu->tmp=cpu->d;_goto(504); // MOV A,D M1 T4
        case  123: cpu->tmp=cpu->e;_goto(505); // MOV A,E M1 T4
        case  124: cpu->tmp=cpu->h;_goto(506); // MOV A,H M1 T4
        case  125: cpu->tmp=cpu->l;_goto(507); // MOV A,L M1 T4
        case  126: _goto(508); // MOV A,M M1 T4
        case  127: cpu->tmp=cpu->a;_goto(511); // MOV A,A M1 T4
        case  128: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD B M1 T4
        case  129: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD C M1 T4
        case  130: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD D M1 T4
        case  131: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD E M1 T4
        case  132: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD H M1 T4
        case  133: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD L M1 T4
        case  134: cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_goto(512); // ADD M M1 T4
        case  135: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_fetch(); // ADD A M1 T4
        case  136: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC B M1 T4
        case  137: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC C M1 T4
        case  138: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC D M1 T4
        case  139: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC E M1 T4
        case  140: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC H M1 T4
        case  141: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC L M1 T4
        case  142: cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_goto(515); // ADC M M1 T4
        case  143: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_fetch(); // ADC A M1 T4
        case  144: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB B M1 T4
        case  145: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB C M1 T4
        case  146: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB D M1 T4
        case  147: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB E M1 T4
        case  148: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB H M1 T4
        case  149: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB L M1 T4
        case  150: cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_goto(518); // SUB M M1 T4
        case  151: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_fetch(); // SUB A M1 T4
        case  152: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB B M1 T4
        case  153: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB C M1 T4
        case  154: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB D M1 T4
        case  155: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB E M1 T4
        case  156: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB H M1 T4
        case  157: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB L M1 T4
        case  158: cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_goto(521); // SBB M M1 T4
        case  159: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_fetch(); // SBB A M1 T4
        case  160: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA B M1 T4
        case  161: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA C M1 T4
        case  162: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA D M1 T4
        case  163: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA E M1 T4
        case  164: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA H M1 T4
        case  165: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA L M1 T4
        case  166: cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_goto(524); // ANA M M1 T4
        case  167: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_fetch(); // ANA A M1 T4
        case  168: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA B M1 T4
        case  169: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA C M1 T4
        case  170: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA D M1 T4
        case  171: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA E M1 T4
        case  172: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA H M1 T4
        case  173: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA L M1 T4
        case  174: cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_goto(527); // XRA M M1 T4
        case  175: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_fetch(); // XRA A M1 T4
        case  176: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA B M1 T4
        case  177: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA C M1 T4
        case  178: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA D M1 T4
        case  179: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA E M1 T4
        case  180: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA H M1 T4
        case  181: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA L M1 T4
        case  182: cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_goto(530); // ORA M M1 T4
        case  183: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_fetch(); // ORA A M1 T4
        case  184: cpu->tmp=cpu->b;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP B M1 T4
        case  185: cpu->tmp=cpu->c;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP C M1 T4
        case  186: cpu->tmp=cpu->d;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP D M1 T4
        case  187: cpu->tmp=cpu->e;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP E M1 T4
        case  188: cpu->tmp=cpu->h;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP H M1 T4
        case  189: cpu->tmp=cpu->l;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP L M1 T4
        case  190: cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_goto(533); // CMP M M1 T4
        case  191: cpu->tmp=cpu->a;cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_fetch(); // CMP A M1 T4
        case  192: _goto(536); // RNZ M1 T4
        case  193: _goto(543); // POP B M1 T4
        case  194: _goto(549); // JNZ a16 M1 T4
        case  195: _goto(555); // JMP a16 M1 T4
        case  196: if((!(cpu->f&I8080_ZF))){cpu->sp--;}_goto(561); // CNZ a16 M1 T4
        case  197: cpu->sp--;_goto(574); // PUSH B M1 T4
        case  198: cpu->act=cpu->a;cpu->alu=I8080_ALU_ADD;_goto(581); // ADI d8 M1 T4
        case  199: cpu->w=0;cpu->sp--;_goto(584); // RST 0 M1 T4
        case  200: _goto(591); // RZ M1 T4
        case  201: _goto(598); // RET M1 T4
        case  202: _goto(604); // JZ a16 M1 T4
        case  203: _goto(610); // JMP a16 M1 T4
        case  204: if((cpu->f&I8080_ZF)){cpu->sp--;}_goto(616); // CZ a16 M1 T4
        case  205: cpu->sp--;_goto(629); // CALL a16 M1 T4
        case  206: cpu->act=cpu->a;cpu->alu=I8080_ALU_ADC;_goto(642); // ACI d8 M1 T4
        case  207: cpu->w=0;cpu->sp--;_goto(645); // RST 1 M1 T4
        case  208: _goto(652); // RNC M1 T4
        case  209: _goto(659); // POP D M1 T4
        case  210: _goto(665); // JNC a16 M1 T4
        case  211: _goto(671); // OUT d8 M1 T4
        case  212: if((!(cpu->f&I8080_CF))){cpu->sp--;}_goto(677); // CNC a16 M1 T4
        case  213: cpu->sp--;_goto(690); // PUSH D M1 T4
        case  214: cpu->act=cpu->a;cpu->alu=I8080_ALU_SUB;_goto(697); // SUI d8 M1 T4
        case  215: cpu->w=0;cpu->sp--;_goto(700); // RST 2 M1 T4
        case  216: _goto(707); // RC M1 T4
        case  217: _goto(714); // RET M1 T4
        case  218: _goto(720); // JC a16 M1 T4
        case  219: _goto(726); // IN d8 M1 T4
        case  220: if((cpu->f&I8080_CF)){cpu->sp--;}_goto(732); // CC a16 M1 T4
        case  221: cpu->sp--;_goto(745); // CALL a16 M1 T4
        case  222: cpu->act=cpu->a;cpu->alu=I8080_ALU_SBB;_goto(758); // SBI d8 M1 T4
        case  223: cpu->w=0;cpu->sp--;_goto(761); // RST 3 M1 T4
        case  224: _goto(768); // RPO M1 T4
        case  225: _goto(775); // POP H M1 T4
        case  226: _goto(781); // JPO a16 M1 T4
        case  227: _goto(787); // XTHL M1 T4
        case  228: if((!(cpu->f&I8080_PF))){cpu->sp--;}_goto(801); // CPO a16 M1 T4
        case  229: cpu->sp--;_goto(814); // PUSH H M1 T4
        case  230: cpu->act=cpu->a;cpu->alu=I8080_ALU_ANA;_goto(821); // ANI d8 M1 T4
        case  231: cpu->w=0;cpu->sp--;_goto(824); // RST 4 M1 T4
        case  232: _goto(831); // RPE M1 T4
        case  233: _goto(838); // PCHL M1 T4
        case  234: _goto(839); // JPE a16 M1 T4
        case  235: {uint16_t t=cpu->hl;cpu->hl=cpu->de;cpu->de=t;}_fetch(); // XCHG M1 T4
        case  236: if((cpu->f&I8080_PF)){cpu->sp--;}_goto(845); // CPE a16 M1 T4
        case  237: cpu->sp--;_goto(858); // CALL a16 M1 T4
        case  238: cpu->act=cpu->a;cpu->alu=I8080_ALU_XRA;_goto(871); // XRI d8 M1 T4
        case  239: cpu->w=0;cpu->sp--;_goto(874); // RST 5 M1 T4
        case  240: _goto(881); // RP M1 T4
        case  241: _goto(888); // POP PSW M1 T4
        case  242: _goto(894); // JP a16 M1 T4
        case  243: cpu->inte=false;_fetch(); // DI M1 T4
        case  244: if((!(cpu->f&I8080_SF))){cpu->sp--;}_goto(900); // CP a16 M1 T4
        case  245: cpu->sp--;_goto(913); // PUSH PSW M1 T4
        case  246: cpu->act=cpu->a;cpu->alu=I8080_ALU_ORA;_goto(920); // ORI d8 M1 T4
        case  247: cpu->w=0;cpu->sp--;_goto(923); // RST 6 M1 T4
        case  248: _goto(930); // RM M1 T4
        case  249: _goto(937); // SPHL M1 T4
        case  250: _goto(938); // JM a16 M1 T4
        case  251: cpu->inte=true;_fetch(); // EI M1 T4
        case  252: if((cpu->f&I8080_SF)){cpu->sp--;}_goto(944); // CM a16 M1 T4
        case  253: cpu->sp--;_goto(957); // CALL a16 M1 T4
        case  254: cpu->act=cpu->a;cpu->alu=I8080_ALU_CMP;_goto(970); // CPI d8 M1 T4
        case  255: cpu->w=0;cpu->sp--;_goto(973); // RST 7 M1 T4
        case  256: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(257); // LXI B,d16 M2 T1
        case  257: _dbin();cpu->pc++;_goto(258); // LXI B,d16 M2 T2
        case  258: _ready_rd();cpu->c=_gd();_goto(259); // LXI B,d16 M2 T3
        case  259: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(260); // LXI B,d16 M3 T1
        case  260: _dbin();cpu->pc++;_goto(261); // LXI B,d16 M3 T2
        case  261: _ready_rd();cpu->b=_gd();_fetch(); // LXI B,d16 M3 T3
        case  262: _t1(cpu->bc,I8080_CYCLE_MWRITE);_goto(263); // STAX B M2 T1
        case  263: _sd(cpu->a);_goto(264); // STAX B M2 T2
        case  264: _ready_wr();_fetch(); // STAX B M2 T3
        case  265: cpu->bc++;_fetch(); // INX B M1 T5
        case  266: cpu->b=cpu->tmp;_fetch(); // INR B M1 T5
        case  267: cpu->b=cpu->tmp;_fetch(); // DCR B M1 T5
        case  268: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(269); // MVI B,d8 M2 T1
        case  269: _dbin();cpu->pc++;_goto(270); // MVI B,d8 M2 T2
        case  270: _ready_rd();cpu->b=_gd();_fetch(); // MVI B,d8 M2 T3
        case  271: cpu->act=cpu->c;_goto(272); // DAD B M2 T1
        case  272: cpu->tmp=cpu->l;_goto(273); // DAD B M2 T2
        case  273: _i8080_dad(cpu,&cpu->l,false);_goto(274); // DAD B M2 T3
        case  274: cpu->act=cpu->b;_goto(275); // DAD B M3 T1
        case  275: cpu->tmp=cpu->h;_goto(276); // DAD B M3 T2
        case  276: _i8080_dad(cpu,&cpu->h,true);_fetch(); // DAD B M3 T3
        case  277: _t1(cpu->bc,I8080_CYCLE_MREAD);_goto(278); // LDAX B M2 T1
        case  278: _dbin();_goto(279); // LDAX B M2 T2
        case  279: _ready_rd();cpu->a=_gd();_fetch(); // LDAX B M2 T3
        case  280: cpu->bc--;_fetch(); // DCX B M1 T5
        case  281: cpu->c=cpu->tmp;_fetch(); // INR C M1 T5
        case  282: cpu->c=cpu->tmp;_fetch(); // DCR C M1 T5
        case  283: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(284); // MVI C,d8 M2 T1
        case  284: _dbin();cpu->pc++;_goto(285); // MVI C,d8 M2 T2
        case  285: _ready_rd();cpu->c=_gd();_fetch(); // MVI C,d8 M2 T3
        case  286: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(287); // LXI D,d16 M2 T1
        case  287: _dbin();cpu->pc++;_goto(288); // LXI D,d16 M2 T2
        case  288: _ready_rd();cpu->e=_gd();_goto(289); // LXI D,d16 M2 T3
        case  289: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(290); // LXI D,d16 M3 T1
        case  290: _dbin();cpu->pc++;_goto(291); // LXI D,d16 M3 T2
        case  291: _ready_rd();cpu->d=_gd();_fetch(); // LXI D,d16 M3 T3
        case  292: _t1(cpu->de,I8080_CYCLE_MWRITE);_goto(293); // STAX D M2 T1
        case  293: _sd(cpu->a);_goto(294); // STAX D M2 T2
        case  294: _ready_wr();_fetch(); // STAX D M2 T3
        case  295: cpu->de++;_fetch(); // INX D M1 T5
        case  296: cpu->d=cpu->tmp;_fetch(); // INR D M1 T5
        case  297: cpu->d=cpu->tmp;_fetch(); // DCR D M1 T5
        case  298: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(299); // MVI D,d8 M2 T1
        case  299: _dbin();cpu->pc++;_goto(300); // MVI D,d8 M2 T2
        case  300: _ready_rd();cpu->d=_gd();_fetch(); // MVI D,d8 M2 T3
        case  301: cpu->act=cpu->e;_goto(302); // DAD D M2 T1
        case  302: cpu->tmp=cpu->l;_goto(303); // DAD D M2 T2
        case  303: _i8080_dad(cpu,&cpu->l,false);_goto(304); // DAD D M2 T3
        case  304: cpu->act=cpu->d;_goto(305); // DAD D M3 T1
        case  305: cpu->tmp=cpu->h;_goto(306); // DAD D M3 T2
        case  306: _i8080_dad(cpu,&cpu->h,true);_fetch(); // DAD D M3 T3
        case  307: _t1(cpu->de,I8080_CYCLE_MREAD);_goto(308); // LDAX D M2 T1
        case  308: _dbin();_goto(309); // LDAX D M2 T2
        case  309: _ready_rd();cpu->a=_gd();_fetch(); // LDAX D M2 T3
        case  310: cpu->de--;_fetch(); // DCX D M1 T5
        case  311: cpu->e=cpu->tmp;_fetch(); // INR E M1 T5
        case  312: cpu->e=cpu->tmp;_fetch(); // DCR E M1 T5
        case  313: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(314); // MVI E,d8 M2 T1
        case  314: _dbin();cpu->pc++;_goto(315); // MVI E,d8 M2 T2
        case  315: _ready_rd();cpu->e=_gd();_fetch(); // MVI E,d8 M2 T3
        case  316: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(317); // LXI H,d16 M2 T1
        case  317: _dbin();cpu->pc++;_goto(318); // LXI H,d16 M2 T2
        case  318: _ready_rd();cpu->l=_gd();_goto(319); // LXI H,d16 M2 T3
        case  319: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(320); // LXI H,d16 M3 T1
        case  320: _dbin();cpu->pc++;_goto(321); // LXI H,d16 M3 T2
        case  321: _ready_rd();cpu->h=_gd();_fetch(); // LXI H,d16 M3 T3
        case  322: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(323); // SHLD a16 M2 T1
        case  323: _dbin();cpu->pc++;_goto(324); // SHLD a16 M2 T2
        case  324: _ready_rd();cpu->z=_gd();_goto(325); // SHLD a16 M2 T3
        case  325: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(326); // SHLD a16 M3 T1
        case  326: _dbin();cpu->pc++;_goto(327); // SHLD a16 M3 T2
        case  327: _ready_rd();cpu->w=_gd();_goto(328); // SHLD a16 M3 T3
        case  328: _t1(cpu->wz,I8080_CYCLE_MWRITE);_goto(329); // SHLD a16 M4 T1
        case  329: _sd(cpu->l);cpu->wz++;_goto(330); // SHLD a16 M4 T2
        case  330: _ready_wr();_goto(331); // SHLD a16 M4 T3
        case  331: _t1(cpu->wz,I8080_CYCLE_MWRITE);_goto(332); // SHLD a16 M5 T1
        case  332: _sd(cpu->h);_goto(333); // SHLD a16 M5 T2
        case  333: _ready_wr();_fetch(); // SHLD a16 M5 T3
        case  334: cpu->hl++;_fetch(); // INX H M1 T5
        case  335: cpu->h=cpu->tmp;_fetch(); // INR H M1 T5
        case  336: cpu->h=cpu->tmp;_fetch(); // DCR H M1 T5
        case  337: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(338); // MVI H,d8 M2 T1
        case  338: _dbin();cpu->pc++;_goto(339); // MVI H,d8 M2 T2
        case  339: _ready_rd();cpu->h=_gd();_fetch(); // MVI H,d8 M2 T3
        case  340: cpu->act=cpu->l;_goto(341); // DAD H M2 T1
        case  341: cpu->tmp=cpu->l;_goto(342); // DAD H M2 T2
        case  342: _i8080_dad(cpu,&cpu->l,false);_goto(343); // DAD H M2 T3
        case  343: cpu->act=cpu->h;_goto(344); // DAD H M3 T1
        case  344: cpu->tmp=cpu->h;_goto(345); // DAD H M3 T2
        case  345: _i8080_dad(cpu,&cpu->h,true);_fetch(); // DAD H M3 T3
        case  346: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(347); // LHLD a16 M2 T1
        case  347: _dbin();cpu->pc++;_goto(348); // LHLD a16 M2 T2
        case  348: _ready_rd();cpu->z=_gd();_goto(349); // LHLD a16 M2 T3
        case  349: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(350); // LHLD a16 M3 T1
        case  350: _dbin();cpu->pc++;_goto(351); // LHLD a16 M3 T2
        case  351: _ready_rd();cpu->w=_gd();_goto(352); // LHLD a16 M3 T3
        case  352: _t1(cpu->wz,I8080_CYCLE_MREAD);_goto(353); // LHLD a16 M4 T1
        case  353: _dbin();cpu->wz++;_goto(354); // LHLD a16 M4 T2
        case  354: _ready_rd();cpu->l=_gd();_goto(355); // LHLD a16 M4 T3
        case  355: _t1(cpu->wz,I8080_CYCLE_MREAD);_goto(356); // LHLD a16 M5 T1
        case  356: _dbin();_goto(357); // LHLD a16 M5 T2
        case  357: _ready_rd();cpu->h=_gd();_fetch(); // LHLD a16 M5 T3
        case  358: cpu->hl--;_fetch(); // DCX H M1 T5
        case  359: cpu->l=cpu->tmp;_fetch(); // INR L M1 T5
        case  360: cpu->l=cpu->tmp;_fetch(); // DCR L M1 T5
        case  361: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(362); // MVI L,d8 M2 T1
        case  362: _dbin();cpu->pc++;_goto(363); // MVI L,d8 M2 T2
        case  363: _ready_rd();cpu->l=_gd();_fetch(); // MVI L,d8 M2 T3
        case  364: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(365); // LXI SP,d16 M2 T1
        case  365: _dbin();cpu->pc++;_goto(366); // LXI SP,d16 M2 T2
        case  366: _ready_rd();cpu->spl=_gd();_goto(367); // LXI SP,d16 M2 T3
        case  367: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(368); // LXI SP,d16 M3 T1
        case  368: _dbin();cpu->pc++;_goto(369); // LXI SP,d16 M3 T2
        case  369: _ready_rd();cpu->sph=_gd();_fetch(); // LXI SP,d16 M3 T3
        case  370: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(371); // STA a16 M2 T1
        case  371: _dbin();cpu->pc++;_goto(372); // STA a16 M2 T2
        case  372: _ready_rd();cpu->z=_gd();_goto(373); // STA a16 M2 T3
        case  373: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(374); // STA a16 M3 T1
        case  374: _dbin();cpu->pc++;_goto(375); // STA a16 M3 T2
        case  375: _ready_rd();cpu->w=_gd();_goto(376); // STA a16 M3 T3
        case  376: _t1(cpu->wz,I8080_CYCLE_MWRITE);_goto(377); // STA a16 M4 T1
        case  377: _sd(cpu->a);_goto(378); // STA a16 M4 T2
        case  378: _ready_wr();_fetch(); // STA a16 M4 T3
        case  379: cpu->sp++;_fetch(); // INX SP M1 T5
        case  380: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(381); // INR M M2 T1
        case  381: _dbin();_goto(382); // INR M M2 T2
        case  382: _ready_rd();cpu->tmp=_gd();cpu->tmp=_i8080_inr(cpu,cpu->tmp);_goto(383); // INR M M2 T3
        case  383: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(384); // INR M M3 T1
        case  384: _sd(cpu->tmp);_goto(385); // INR M M3 T2
        case  385: _ready_wr();_fetch(); // INR M M3 T3
        case  386: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(387); // DCR M M2 T1
        case  387: _dbin();_goto(388); // DCR M M2 T2
        case  388: _ready_rd();cpu->tmp=_gd();cpu->tmp=_i8080_dcr(cpu,cpu->tmp);_goto(389); // DCR M M2 T3
        case  389: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(390); // DCR M M3 T1
        case  390: _sd(cpu->tmp);_goto(391); // DCR M M3 T2
        case  391: _ready_wr();_fetch(); // DCR M M3 T3
        case  392: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(393); // MVI M,d8 M2 T1
        case  393: _dbin();cpu->pc++;_goto(394); // MVI M,d8 M2 T2
        case  394: _ready_rd();cpu->tmp=_gd();_goto(395); // MVI M,d8 M2 T3
        case  395: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(396); // MVI M,d8 M3 T1
        case  396: _sd(cpu->tmp);_goto(397); // MVI M,d8 M3 T2
        case  397: _ready_wr();_fetch(); // MVI M,d8 M3 T3
        case  398: cpu->act=cpu->spl;_goto(399); // DAD SP M2 T1
        case  399: cpu->tmp=cpu->l;_goto(400); // DAD SP M2 T2
        case  400: _i8080_dad(cpu,&cpu->l,false);_goto(401); // DAD SP M2 T3
        case  401: cpu->act=cpu->sph;_goto(402); // DAD SP M3 T1
        case  402: cpu->tmp=cpu->h;_goto(403); // DAD SP M3 T2
        case  403: _i8080_dad(cpu,&cpu->h,true);_fetch(); // DAD SP M3 T3
        case  404: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(405); // LDA a16 M2 T1
        case  405: _dbin();cpu->pc++;_goto(406); // LDA a16 M2 T2
        case  406: _ready_rd();cpu->z=_gd();_goto(407); // LDA a16 M2 T3
        case  407: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(408); // LDA a16 M3 T1
        case  408: _dbin();cpu->pc++;_goto(409); // LDA a16 M3 T2
        case  409: _ready_rd();cpu->w=_gd();_goto(410); // LDA a16 M3 T3
        case  410: _t1(cpu->wz,I8080_CYCLE_MREAD);_goto(411); // LDA a16 M4 T1
        case  411: _dbin();_goto(412); // LDA a16 M4 T2
        case  412: _ready_rd();cpu->a=_gd();_fetch(); // LDA a16 M4 T3
        case  413: cpu->sp--;_fetch(); // DCX SP M1 T5
        case  414: cpu->a=cpu->tmp;_fetch(); // INR A M1 T5
        case  415: cpu->a=cpu->tmp;_fetch(); // DCR A M1 T5
        case  416: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(417); // MVI A,d8 M2 T1
        case  417: _dbin();cpu->pc++;_goto(418); // MVI A,d8 M2 T2
        case  418: _ready_rd();cpu->a=_gd();_fetch(); // MVI A,d8 M2 T3
        case  419: cpu->b=cpu->tmp;_fetch(); // MOV B,B M1 T5
        case  420: cpu->b=cpu->tmp;_fetch(); // MOV B,C M1 T5
        case  421: cpu->b=cpu->tmp;_fetch(); // MOV B,D M1 T5
        case  422: cpu->b=cpu->tmp;_fetch(); // MOV B,E M1 T5
        case  423: cpu->b=cpu->tmp;_fetch(); // MOV B,H M1 T5
        case  424: cpu->b=cpu->tmp;_fetch(); // MOV B,L M1 T5
        case  425: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(426); // MOV B,M M2 T1
        case  426: _dbin();_goto(427); // MOV B,M M2 T2
        case  427: _ready_rd();cpu->b=_gd();_fetch(); // MOV B,M M2 T3
        case  428: cpu->b=cpu->tmp;_fetch(); // MOV B,A M1 T5
        case  429: cpu->c=cpu->tmp;_fetch(); // MOV C,B M1 T5
        case  430: cpu->c=cpu->tmp;_fetch(); // MOV C,C M1 T5
        case  431: cpu->c=cpu->tmp;_fetch(); // MOV C,D M1 T5
        case  432: cpu->c=cpu->tmp;_fetch(); // MOV C,E M1 T5
        case  433: cpu->c=cpu->tmp;_fetch(); // MOV C,H M1 T5
        case  434: cpu->c=cpu->tmp;_fetch(); // MOV C,L M1 T5
        case  435: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(436); // MOV C,M M2 T1
        case  436: _dbin();_goto(437); // MOV C,M M2 T2
        case  437: _ready_rd();cpu->c=_gd();_fetch(); // MOV C,M M2 T3
        case  438: cpu->c=cpu->tmp;_fetch(); // MOV C,A M1 T5
        case  439: cpu->d=cpu->tmp;_fetch(); // MOV D,B M1 T5
        case  440: cpu->d=cpu->tmp;_fetch(); // MOV D,C M1 T5
        case  441: cpu->d=cpu->tmp;_fetch(); // MOV D,D M1 T5
        case  442: cpu->d=cpu->tmp;_fetch(); // MOV D,E M1 T5
        case  443: cpu->d=cpu->tmp;_fetch(); // MOV D,H M1 T5
        case  444: cpu->d=cpu->tmp;_fetch(); // MOV D,L M1 T5
        case  445: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(446); // MOV D,M M2 T1
        case  446: _dbin();_goto(447); // MOV D,M M2 T2
        case  447: _ready_rd();cpu->d=_gd();_fetch(); // MOV D,M M2 T3
        case  448: cpu->d=cpu->tmp;_fetch(); // MOV D,A M1 T5
        case  449: cpu->e=cpu->tmp;_fetch(); // MOV E,B M1 T5
        case  450: cpu->e=cpu->tmp;_fetch(); // MOV E,C M1 T5
        case  451: cpu->e=cpu->tmp;_fetch(); // MOV E,D M1 T5
        case  452: cpu->e=cpu->tmp;_fetch(); // MOV E,E M1 T5
        case  453: cpu->e=cpu->tmp;_fetch(); // MOV E,H M1 T5
        case  454: cpu->e=cpu->tmp;_fetch(); // MOV E,L M1 T5
        case  455: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(456); // MOV E,M M2 T1
        case  456: _dbin();_goto(457); // MOV E,M M2 T2
        case  457: _ready_rd();cpu->e=_gd();_fetch(); // MOV E,M M2 T3
        case  458: cpu->e=cpu->tmp;_fetch(); // MOV E,A M1 T5
        case  459: cpu->h=cpu->tmp;_fetch(); // MOV H,B M1 T5
        case  460: cpu->h=cpu->tmp;_fetch(); // MOV H,C M1 T5
        case  461: cpu->h=cpu->tmp;_fetch(); // MOV H,D M1 T5
        case  462: cpu->h=cpu->tmp;_fetch(); // MOV H,E M1 T5
        case  463: cpu->h=cpu->tmp;_fetch(); // MOV H,H M1 T5
        case  464: cpu->h=cpu->tmp;_fetch(); // MOV H,L M1 T5
        case  465: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(466); // MOV H,M M2 T1
        case  466: _dbin();_goto(467); // MOV H,M M2 T2
        case  467: _ready_rd();cpu->h=_gd();_fetch(); // MOV H,M M2 T3
        case  468: cpu->h=cpu->tmp;_fetch(); // MOV H,A M1 T5
        case  469: cpu->l=cpu->tmp;_fetch(); // MOV L,B M1 T5
        case  470: cpu->l=cpu->tmp;_fetch(); // MOV L,C M1 T5
        case  471: cpu->l=cpu->tmp;_fetch(); // MOV L,D M1 T5
        case  472: cpu->l=cpu->tmp;_fetch(); // MOV L,E M1 T5
        case  473: cpu->l=cpu->tmp;_fetch(); // MOV L,H M1 T5
        case  474: cpu->l=cpu->tmp;_fetch(); // MOV L,L M1 T5
        case  475: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(476); // MOV L,M M2 T1
        case  476: _dbin();_goto(477); // MOV L,M M2 T2
        case  477: _ready_rd();cpu->l=_gd();_fetch(); // MOV L,M M2 T3
        case  478: cpu->l=cpu->tmp;_fetch(); // MOV L,A M1 T5
        case  479: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(480); // MOV M,B M2 T1
        case  480: _sd(cpu->tmp);_goto(481); // MOV M,B M2 T2
        case  481: _ready_wr();_fetch(); // MOV M,B M2 T3
        case  482: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(483); // MOV M,C M2 T1
        case  483: _sd(cpu->tmp);_goto(484); // MOV M,C M2 T2
        case  484: _ready_wr();_fetch(); // MOV M,C M2 T3
        case  485: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(486); // MOV M,D M2 T1
        case  486: _sd(cpu->tmp);_goto(487); // MOV M,D M2 T2
        case  487: _ready_wr();_fetch(); // MOV M,D M2 T3
        case  488: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(489); // MOV M,E M2 T1
        case  489: _sd(cpu->tmp);_goto(490); // MOV M,E M2 T2
        case  490: _ready_wr();_fetch(); // MOV M,E M2 T3
        case  491: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(492); // MOV M,H M2 T1
        case  492: _sd(cpu->tmp);_goto(493); // MOV M,H M2 T2
        case  493: _ready_wr();_fetch(); // MOV M,H M2 T3
        case  494: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(495); // MOV M,L M2 T1
        case  495: _sd(cpu->tmp);_goto(496); // MOV M,L M2 T2
        case  496: _ready_wr();_fetch(); // MOV M,L M2 T3
        case  497: _t1(cpu->pc,I8080_CYCLE_HALT);_goto(498); // HLT M2 T1
        case  498: _goto(I8080_HALT_TWH1); // HLT M2 T2
        case  499: _t1(cpu->hl,I8080_CYCLE_MWRITE);_goto(500); // MOV M,A M2 T1
        case  500: _sd(cpu->tmp);_goto(501); // MOV M,A M2 T2
        case  501: _ready_wr();_fetch(); // MOV M,A M2 T3
        case  502: cpu->a=cpu->tmp;_fetch(); // MOV A,B M1 T5
        case  503: cpu->a=cpu->tmp;_fetch(); // MOV A,C M1 T5
        case  504: cpu->a=cpu->tmp;_fetch(); // MOV A,D M1 T5
        case  505: cpu->a=cpu->tmp;_fetch(); // MOV A,E M1 T5
        case  506: cpu->a=cpu->tmp;_fetch(); // MOV A,H M1 T5
        case  507: cpu->a=cpu->tmp;_fetch(); // MOV A,L M1 T5
        case  508: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(509); // MOV A,M M2 T1
        case  509: _dbin();_goto(510); // MOV A,M M2 T2
        case  510: _ready_rd();cpu->a=_gd();_fetch(); // MOV A,M M2 T3
        case  511: cpu->a=cpu->tmp;_fetch(); // MOV A,A M1 T5
        case  512: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(513); // ADD M M2 T1
        case  513: _dbin();_goto(514); // ADD M M2 T2
        case  514: _ready_rd();cpu->tmp=_gd();_fetch(); // ADD M M2 T3
        case  515: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(516); // ADC M M2 T1
        case  516: _dbin();_goto(517); // ADC M M2 T2
        case  517: _ready_rd();cpu->tmp=_gd();_fetch(); // ADC M M2 T3
        case  518: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(519); // SUB M M2 T1
        case  519: _dbin();_goto(520); // SUB M M2 T2
        case  520: _ready_rd();cpu->tmp=_gd();_fetch(); // SUB M M2 T3
        case  521: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(522); // SBB M M2 T1
        case  522: _dbin();_goto(523); // SBB M M2 T2
        case  523: _ready_rd();cpu->tmp=_gd();_fetch(); // SBB M M2 T3
        case  524: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(525); // ANA M M2 T1
        case  525: _dbin();_goto(526); // ANA M M2 T2
        case  526: _ready_rd();cpu->tmp=_gd();_fetch(); // ANA M M2 T3
        case  527: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(528); // XRA M M2 T1
        case  528: _dbin();_goto(529); // XRA M M2 T2
        case  529: _ready_rd();cpu->tmp=_gd();_fetch(); // XRA M M2 T3
        case  530: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(531); // ORA M M2 T1
        case  531: _dbin();_goto(532); // ORA M M2 T2
        case  532: _ready_rd();cpu->tmp=_gd();_fetch(); // ORA M M2 T3
        case  533: _t1(cpu->hl,I8080_CYCLE_MREAD);_goto(534); // CMP M M2 T1
        case  534: _dbin();_goto(535); // CMP M M2 T2
        case  535: _ready_rd();cpu->tmp=_gd();_fetch(); // CMP M M2 T3
        case  536: if(!(!(cpu->f&I8080_ZF))){_fetch();}_goto(537); // RNZ M1 T5
        case  537: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(538); // RNZ M2 T1
        case  538: _dbin();cpu->sp++;_goto(539); // RNZ M2 T2
        case  539: _ready_rd();cpu->z=_gd();_goto(540); // RNZ M2 T3
        case  540: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(541); // RNZ M3 T1
        case  541: _dbin();cpu->sp++;_goto(542); // RNZ M3 T2
        case  542: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RNZ M3 T3
        case  543: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(544); // POP B M2 T1
        case  544: _dbin();cpu->sp++;_goto(545); // POP B M2 T2
        case  545: _ready_rd();cpu->c=_gd();_goto(546); // POP B M2 T3
        case  546: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(547); // POP B M3 T1
        case  547: _dbin();cpu->sp++;_goto(548); // POP B M3 T2
        case  548: _ready_rd();cpu->b=_gd();_fetch(); // POP B M3 T3
        case  549: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(550); // JNZ a16 M2 T1
        case  550: _dbin();cpu->pc++;_goto(551); // JNZ a16 M2 T2
        case  551: _ready_rd();cpu->z=_gd();_goto(552); // JNZ a16 M2 T3
        case  552: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(553); // JNZ a16 M3 T1
        case  553: _dbin();cpu->pc++;_goto(554); // JNZ a16 M3 T2
        case  554: _ready_rd();cpu->w=_gd();if((!(cpu->f&I8080_ZF))){cpu->pc=cpu->wz;}_fetch(); // JNZ a16 M3 T3
        case  555: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(556); // JMP a16 M2 T1
        case  556: _dbin();cpu->pc++;_goto(557); // JMP a16 M2 T2
        case  557: _ready_rd();cpu->z=_gd();_goto(558); // JMP a16 M2 T3
        case  558: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(559); // JMP a16 M3 T1
        case  559: _dbin();cpu->pc++;_goto(560); // JMP a16 M3 T2
        case  560: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JMP a16 M3 T3
        case  561: _goto(562); // CNZ a16 M1 T5
        case  562: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(563); // CNZ a16 M2 T1
        case  563: _dbin();cpu->pc++;_goto(564); // CNZ a16 M2 T2
        case  564: _ready_rd();cpu->z=_gd();_goto(565); // CNZ a16 M2 T3
        case  565: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(566); // CNZ a16 M3 T1
        case  566: _dbin();cpu->pc++;_goto(567); // CNZ a16 M3 T2
        case  567: _ready_rd();cpu->w=_gd();if(!(!(cpu->f&I8080_ZF))){_fetch();}_goto(568); // CNZ a16 M3 T3
        case  568: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(569); // CNZ a16 M4 T1
        case  569: _sd(cpu->pch);cpu->sp--;_goto(570); // CNZ a16 M4 T2
        case  570: _ready_wr();_goto(571); // CNZ a16 M4 T3
        case  571: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(572); // CNZ a16 M5 T1
        case  572: _sd(cpu->pcl);_goto(573); // CNZ a16 M5 T2
        case  573: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CNZ a16 M5 T3
        case  574: _goto(575); // PUSH B M1 T5
        case  575: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(576); // PUSH B M2 T1
        case  576: _sd(cpu->b);cpu->sp--;_goto(577); // PUSH B M2 T2
        case  577: _ready_wr();_goto(578); // PUSH B M2 T3
        case  578: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(579); // PUSH B M3 T1
        case  579: _sd(cpu->c);_goto(580); // PUSH B M3 T2
        case  580: _ready_wr();_fetch(); // PUSH B M3 T3
        case  581: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(582); // ADI d8 M2 T1
        case  582: _dbin();cpu->pc++;_goto(583); // ADI d8 M2 T2
        case  583: _ready_rd();cpu->tmp=_gd();_fetch(); // ADI d8 M2 T3
        case  584: _goto(585); // RST 0 M1 T5
        case  585: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(586); // RST 0 M2 T1
        case  586: _sd(cpu->pch);cpu->sp--;_goto(587); // RST 0 M2 T2
        case  587: _ready_wr();_goto(588); // RST 0 M2 T3
        case  588: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(589); // RST 0 M3 T1
        case  589: _sd(cpu->pcl);_goto(590); // RST 0 M3 T2
        case  590: _ready_wr();cpu->z=0x00;cpu->pc=cpu->wz;_fetch(); // RST 0 M3 T3
        case  591: if(!(cpu->f&I8080_ZF)){_fetch();}_goto(592); // RZ M1 T5
        case  592: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(593); // RZ M2 T1
        case  593: _dbin();cpu->sp++;_goto(594); // RZ M2 T2
        case  594: _ready_rd();cpu->z=_gd();_goto(595); // RZ M2 T3
        case  595: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(596); // RZ M3 T1
        case  596: _dbin();cpu->sp++;_goto(597); // RZ M3 T2
        case  597: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RZ M3 T3
        case  598: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(599); // RET M2 T1
        case  599: _dbin();cpu->sp++;_goto(600); // RET M2 T2
        case  600: _ready_rd();cpu->z=_gd();_goto(601); // RET M2 T3
        case  601: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(602); // RET M3 T1
        case  602: _dbin();cpu->sp++;_goto(603); // RET M3 T2
        case  603: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RET M3 T3
        case  604: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(605); // JZ a16 M2 T1
        case  605: _dbin();cpu->pc++;_goto(606); // JZ a16 M2 T2
        case  606: _ready_rd();cpu->z=_gd();_goto(607); // JZ a16 M2 T3
        case  607: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(608); // JZ a16 M3 T1
        case  608: _dbin();cpu->pc++;_goto(609); // JZ a16 M3 T2
        case  609: _ready_rd();cpu->w=_gd();if((cpu->f&I8080_ZF)){cpu->pc=cpu->wz;}_fetch(); // JZ a16 M3 T3
        case  610: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(611); // JMP a16 M2 T1
        case  611: _dbin();cpu->pc++;_goto(612); // JMP a16 M2 T2
        case  612: _ready_rd();cpu->z=_gd();_goto(613); // JMP a16 M2 T3
        case  613: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(614); // JMP a16 M3 T1
        case  614: _dbin();cpu->pc++;_goto(615); // JMP a16 M3 T2
        case  615: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // JMP a16 M3 T3
        case  616: _goto(617); // CZ a16 M1 T5
        case  617: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(618); // CZ a16 M2 T1
        case  618: _dbin();cpu->pc++;_goto(619); // CZ a16 M2 T2
        case  619: _ready_rd();cpu->z=_gd();_goto(620); // CZ a16 M2 T3
        case  620: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(621); // CZ a16 M3 T1
        case  621: _dbin();cpu->pc++;_goto(622); // CZ a16 M3 T2
        case  622: _ready_rd();cpu->w=_gd();if(!(cpu->f&I8080_ZF)){_fetch();}_goto(623); // CZ a16 M3 T3
        case  623: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(624); // CZ a16 M4 T1
        case  624: _sd(cpu->pch);cpu->sp--;_goto(625); // CZ a16 M4 T2
        case  625: _ready_wr();_goto(626); // CZ a16 M4 T3
        case  626: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(627); // CZ a16 M5 T1
        case  627: _sd(cpu->pcl);_goto(628); // CZ a16 M5 T2
        case  628: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CZ a16 M5 T3
        case  629: _goto(630); // CALL a16 M1 T5
        case  630: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(631); // CALL a16 M2 T1
        case  631: _dbin();cpu->pc++;_goto(632); // CALL a16 M2 T2
        case  632: _ready_rd();cpu->z=_gd();_goto(633); // CALL a16 M2 T3
        case  633: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(634); // CALL a16 M3 T1
        case  634: _dbin();cpu->pc++;_goto(635); // CALL a16 M3 T2
        case  635: _ready_rd();cpu->w=_gd();_goto(636); // CALL a16 M3 T3
        case  636: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(637); // CALL a16 M4 T1
        case  637: _sd(cpu->pch);cpu->sp--;_goto(638); // CALL a16 M4 T2
        case  638: _ready_wr();_goto(639); // CALL a16 M4 T3
        case  639: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(640); // CALL a16 M5 T1
        case  640: _sd(cpu->pcl);_goto(641); // CALL a16 M5 T2
        case  641: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CALL a16 M5 T3
        case  642: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(643); // ACI d8 M2 T1
        case  643: _dbin();cpu->pc++;_goto(644); // ACI d8 M2 T2
        case  644: _ready_rd();cpu->tmp=_gd();_fetch(); // ACI d8 M2 T3
        case  645: _goto(646); // RST 1 M1 T5
        case  646: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(647); // RST 1 M2 T1
        case  647: _sd(cpu->pch);cpu->sp--;_goto(648); // RST 1 M2 T2
        case  648: _ready_wr();_goto(649); // RST 1 M2 T3
        case  649: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(650); // RST 1 M3 T1
        case  650: _sd(cpu->pcl);_goto(651); // RST 1 M3 T2
        case  651: _ready_wr();cpu->z=0x08;cpu->pc=cpu->wz;_fetch(); // RST 1 M3 T3
        case  652: if(!(!(cpu->f&I8080_CF))){_fetch();}_goto(653); // RNC M1 T5
        case  653: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(654); // RNC M2 T1
        case  654: _dbin();cpu->sp++;_goto(655); // RNC M2 T2
        case  655: _ready_rd();cpu->z=_gd();_goto(656); // RNC M2 T3
        case  656: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(657); // RNC M3 T1
        case  657: _dbin();cpu->sp++;_goto(658); // RNC M3 T2
        case  658: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RNC M3 T3
        case  659: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(660); // POP D M2 T1
        case  660: _dbin();cpu->sp++;_goto(661); // POP D M2 T2
        case  661: _ready_rd();cpu->e=_gd();_goto(662); // POP D M2 T3
        case  662: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(663); // POP D M3 T1
        case  663: _dbin();cpu->sp++;_goto(664); // POP D M3 T2
        case  664: _ready_rd();cpu->d=_gd();_fetch(); // POP D M3 T3
        case  665: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(666); // JNC a16 M2 T1
        case  666: _dbin();cpu->pc++;_goto(667); // JNC a16 M2 T2
        case  667: _ready_rd();cpu->z=_gd();_goto(668); // JNC a16 M2 T3
        case  668: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(669); // JNC a16 M3 T1
        case  669: _dbin();cpu->pc++;_goto(670); // JNC a16 M3 T2
        case  670: _ready_rd();cpu->w=_gd();if((!(cpu->f&I8080_CF))){cpu->pc=cpu->wz;}_fetch(); // JNC a16 M3 T3
        case  671: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(672); // OUT d8 M2 T1
        case  672: _dbin();cpu->pc++;_goto(673); // OUT d8 M2 T2
        case  673: _ready_rd();cpu->z=_gd();cpu->w=cpu->z;_goto(674); // OUT d8 M2 T3
        case  674: _t1(cpu->wz,I8080_CYCLE_OUTPUT);_goto(675); // OUT d8 M3 T1
        case  675: _sd(cpu->a);_goto(676); // OUT d8 M3 T2
        case  676: _ready_wr();_fetch(); // OUT d8 M3 T3
        case  677: _goto(678); // CNC a16 M1 T5
        case  678: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(679); // CNC a16 M2 T1
        case  679: _dbin();cpu->pc++;_goto(680); // CNC a16 M2 T2
        case  680: _ready_rd();cpu->z=_gd();_goto(681); // CNC a16 M2 T3
        case  681: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(682); // CNC a16 M3 T1
        case  682: _dbin();cpu->pc++;_goto(683); // CNC a16 M3 T2
        case  683: _ready_rd();cpu->w=_gd();if(!(!(cpu->f&I8080_CF))){_fetch();}_goto(684); // CNC a16 M3 T3
        case  684: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(685); // CNC a16 M4 T1
        case  685: _sd(cpu->pch);cpu->sp--;_goto(686); // CNC a16 M4 T2
        case  686: _ready_wr();_goto(687); // CNC a16 M4 T3
        case  687: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(688); // CNC a16 M5 T1
        case  688: _sd(cpu->pcl);_goto(689); // CNC a16 M5 T2
        case  689: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CNC a16 M5 T3
        case  690: _goto(691); // PUSH D M1 T5
        case  691: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(692); // PUSH D M2 T1
        case  692: _sd(cpu->d);cpu->sp--;_goto(693); // PUSH D M2 T2
        case  693: _ready_wr();_goto(694); // PUSH D M2 T3
        case  694: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(695); // PUSH D M3 T1
        case  695: _sd(cpu->e);_goto(696); // PUSH D M3 T2
        case  696: _ready_wr();_fetch(); // PUSH D M3 T3
        case  697: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(698); // SUI d8 M2 T1
        case  698: _dbin();cpu->pc++;_goto(699); // SUI d8 M2 T2
        case  699: _ready_rd();cpu->tmp=_gd();_fetch(); // SUI d8 M2 T3
        case  700: _goto(701); // RST 2 M1 T5
        case  701: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(702); // RST 2 M2 T1
        case  702: _sd(cpu->pch);cpu->sp--;_goto(703); // RST 2 M2 T2
        case  703: _ready_wr();_goto(704); // RST 2 M2 T3
        case  704: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(705); // RST 2 M3 T1
        case  705: _sd(cpu->pcl);_goto(706); // RST 2 M3 T2
        case  706: _ready_wr();cpu->z=0x10;cpu->pc=cpu->wz;_fetch(); // RST 2 M3 T3
        case  707: if(!(cpu->f&I8080_CF)){_fetch();}_goto(708); // RC M1 T5
        case  708: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(709); // RC M2 T1
        case  709: _dbin();cpu->sp++;_goto(710); // RC M2 T2
        case  710: _ready_rd();cpu->z=_gd();_goto(711); // RC M2 T3
        case  711: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(712); // RC M3 T1
        case  712: _dbin();cpu->sp++;_goto(713); // RC M3 T2
        case  713: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RC M3 T3
        case  714: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(715); // RET M2 T1
        case  715: _dbin();cpu->sp++;_goto(716); // RET M2 T2
        case  716: _ready_rd();cpu->z=_gd();_goto(717); // RET M2 T3
        case  717: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(718); // RET M3 T1
        case  718: _dbin();cpu->sp++;_goto(719); // RET M3 T2
        case  719: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RET M3 T3
        case  720: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(721); // JC a16 M2 T1
        case  721: _dbin();cpu->pc++;_goto(722); // JC a16 M2 T2
        case  722: _ready_rd();cpu->z=_gd();_goto(723); // JC a16 M2 T3
        case  723: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(724); // JC a16 M3 T1
        case  724: _dbin();cpu->pc++;_goto(725); // JC a16 M3 T2
        case  725: _ready_rd();cpu->w=_gd();if((cpu->f&I8080_CF)){cpu->pc=cpu->wz;}_fetch(); // JC a16 M3 T3
        case  726: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(727); // IN d8 M2 T1
        case  727: _dbin();cpu->pc++;_goto(728); // IN d8 M2 T2
        case  728: _ready_rd();cpu->z=_gd();cpu->w=cpu->z;_goto(729); // IN d8 M2 T3
        case  729: _t1(cpu->wz,I8080_CYCLE_INPUT);_goto(730); // IN d8 M3 T1
        case  730: _dbin();_goto(731); // IN d8 M3 T2
        case  731: _ready_rd();cpu->a=_gd();_fetch(); // IN d8 M3 T3
        case  732: _goto(733); // CC a16 M1 T5
        case  733: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(734); // CC a16 M2 T1
        case  734: _dbin();cpu->pc++;_goto(735); // CC a16 M2 T2
        case  735: _ready_rd();cpu->z=_gd();_goto(736); // CC a16 M2 T3
        case  736: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(737); // CC a16 M3 T1
        case  737: _dbin();cpu->pc++;_goto(738); // CC a16 M3 T2
        case  738: _ready_rd();cpu->w=_gd();if(!(cpu->f&I8080_CF)){_fetch();}_goto(739); // CC a16 M3 T3
        case  739: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(740); // CC a16 M4 T1
        case  740: _sd(cpu->pch);cpu->sp--;_goto(741); // CC a16 M4 T2
        case  741: _ready_wr();_goto(742); // CC a16 M4 T3
        case  742: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(743); // CC a16 M5 T1
        case  743: _sd(cpu->pcl);_goto(744); // CC a16 M5 T2
        case  744: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CC a16 M5 T3
        case  745: _goto(746); // CALL a16 M1 T5
        case  746: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(747); // CALL a16 M2 T1
        case  747: _dbin();cpu->pc++;_goto(748); // CALL a16 M2 T2
        case  748: _ready_rd();cpu->z=_gd();_goto(749); // CALL a16 M2 T3
        case  749: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(750); // CALL a16 M3 T1
        case  750: _dbin();cpu->pc++;_goto(751); // CALL a16 M3 T2
        case  751: _ready_rd();cpu->w=_gd();_goto(752); // CALL a16 M3 T3
        case  752: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(753); // CALL a16 M4 T1
        case  753: _sd(cpu->pch);cpu->sp--;_goto(754); // CALL a16 M4 T2
        case  754: _ready_wr();_goto(755); // CALL a16 M4 T3
        case  755: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(756); // CALL a16 M5 T1
        case  756: _sd(cpu->pcl);_goto(757); // CALL a16 M5 T2
        case  757: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CALL a16 M5 T3
        case  758: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(759); // SBI d8 M2 T1
        case  759: _dbin();cpu->pc++;_goto(760); // SBI d8 M2 T2
        case  760: _ready_rd();cpu->tmp=_gd();_fetch(); // SBI d8 M2 T3
        case  761: _goto(762); // RST 3 M1 T5
        case  762: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(763); // RST 3 M2 T1
        case  763: _sd(cpu->pch);cpu->sp--;_goto(764); // RST 3 M2 T2
        case  764: _ready_wr();_goto(765); // RST 3 M2 T3
        case  765: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(766); // RST 3 M3 T1
        case  766: _sd(cpu->pcl);_goto(767); // RST 3 M3 T2
        case  767: _ready_wr();cpu->z=0x18;cpu->pc=cpu->wz;_fetch(); // RST 3 M3 T3
        case  768: if(!(!(cpu->f&I8080_PF))){_fetch();}_goto(769); // RPO M1 T5
        case  769: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(770); // RPO M2 T1
        case  770: _dbin();cpu->sp++;_goto(771); // RPO M2 T2
        case  771: _ready_rd();cpu->z=_gd();_goto(772); // RPO M2 T3
        case  772: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(773); // RPO M3 T1
        case  773: _dbin();cpu->sp++;_goto(774); // RPO M3 T2
        case  774: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RPO M3 T3
        case  775: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(776); // POP H M2 T1
        case  776: _dbin();cpu->sp++;_goto(777); // POP H M2 T2
        case  777: _ready_rd();cpu->l=_gd();_goto(778); // POP H M2 T3
        case  778: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(779); // POP H M3 T1
        case  779: _dbin();cpu->sp++;_goto(780); // POP H M3 T2
        case  780: _ready_rd();cpu->h=_gd();_fetch(); // POP H M3 T3
        case  781: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(782); // JPO a16 M2 T1
        case  782: _dbin();cpu->pc++;_goto(783); // JPO a16 M2 T2
        case  783: _ready_rd();cpu->z=_gd();_goto(784); // JPO a16 M2 T3
        case  784: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(785); // JPO a16 M3 T1
        case  785: _dbin();cpu->pc++;_goto(786); // JPO a16 M3 T2
        case  786: _ready_rd();cpu->w=_gd();if((!(cpu->f&I8080_PF))){cpu->pc=cpu->wz;}_fetch(); // JPO a16 M3 T3
        case  787: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(788); // XTHL M2 T1
        case  788: _dbin();cpu->sp++;_goto(789); // XTHL M2 T2
        case  789: _ready_rd();cpu->z=_gd();_goto(790); // XTHL M2 T3
        case  790: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(791); // XTHL M3 T1
        case  791: _dbin();_goto(792); // XTHL M3 T2
        case  792: _ready_rd();cpu->w=_gd();_goto(793); // XTHL M3 T3
        case  793: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(794); // XTHL M4 T1
        case  794: _sd(cpu->h);cpu->sp--;_goto(795); // XTHL M4 T2
        case  795: _ready_wr();_goto(796); // XTHL M4 T3
        case  796: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(797); // XTHL M5 T1
        case  797: _sd(cpu->l);_goto(798); // XTHL M5 T2
        case  798: _ready_wr();_goto(799); // XTHL M5 T3
        case  799: cpu->hl=cpu->wz;_goto(800); // XTHL M5 T4
        case  800: _fetch(); // XTHL M5 T5
        case  801: _goto(802); // CPO a16 M1 T5
        case  802: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(803); // CPO a16 M2 T1
        case  803: _dbin();cpu->pc++;_goto(804); // CPO a16 M2 T2
        case  804: _ready_rd();cpu->z=_gd();_goto(805); // CPO a16 M2 T3
        case  805: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(806); // CPO a16 M3 T1
        case  806: _dbin();cpu->pc++;_goto(807); // CPO a16 M3 T2
        case  807: _ready_rd();cpu->w=_gd();if(!(!(cpu->f&I8080_PF))){_fetch();}_goto(808); // CPO a16 M3 T3
        case  808: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(809); // CPO a16 M4 T1
        case  809: _sd(cpu->pch);cpu->sp--;_goto(810); // CPO a16 M4 T2
        case  810: _ready_wr();_goto(811); // CPO a16 M4 T3
        case  811: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(812); // CPO a16 M5 T1
        case  812: _sd(cpu->pcl);_goto(813); // CPO a16 M5 T2
        case  813: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CPO a16 M5 T3
        case  814: _goto(815); // PUSH H M1 T5
        case  815: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(816); // PUSH H M2 T1
        case  816: _sd(cpu->h);cpu->sp--;_goto(817); // PUSH H M2 T2
        case  817: _ready_wr();_goto(818); // PUSH H M2 T3
        case  818: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(819); // PUSH H M3 T1
        case  819: _sd(cpu->l);_goto(820); // PUSH H M3 T2
        case  820: _ready_wr();_fetch(); // PUSH H M3 T3
        case  821: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(822); // ANI d8 M2 T1
        case  822: _dbin();cpu->pc++;_goto(823); // ANI d8 M2 T2
        case  823: _ready_rd();cpu->tmp=_gd();_fetch(); // ANI d8 M2 T3
        case  824: _goto(825); // RST 4 M1 T5
        case  825: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(826); // RST 4 M2 T1
        case  826: _sd(cpu->pch);cpu->sp--;_goto(827); // RST 4 M2 T2
        case  827: _ready_wr();_goto(828); // RST 4 M2 T3
        case  828: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(829); // RST 4 M3 T1
        case  829: _sd(cpu->pcl);_goto(830); // RST 4 M3 T2
        case  830: _ready_wr();cpu->z=0x20;cpu->pc=cpu->wz;_fetch(); // RST 4 M3 T3
        case  831: if(!(cpu->f&I8080_PF)){_fetch();}_goto(832); // RPE M1 T5
        case  832: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(833); // RPE M2 T1
        case  833: _dbin();cpu->sp++;_goto(834); // RPE M2 T2
        case  834: _ready_rd();cpu->z=_gd();_goto(835); // RPE M2 T3
        case  835: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(836); // RPE M3 T1
        case  836: _dbin();cpu->sp++;_goto(837); // RPE M3 T2
        case  837: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RPE M3 T3
        case  838: cpu->pc=cpu->hl;_fetch(); // PCHL M1 T5
        case  839: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(840); // JPE a16 M2 T1
        case  840: _dbin();cpu->pc++;_goto(841); // JPE a16 M2 T2
        case  841: _ready_rd();cpu->z=_gd();_goto(842); // JPE a16 M2 T3
        case  842: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(843); // JPE a16 M3 T1
        case  843: _dbin();cpu->pc++;_goto(844); // JPE a16 M3 T2
        case  844: _ready_rd();cpu->w=_gd();if((cpu->f&I8080_PF)){cpu->pc=cpu->wz;}_fetch(); // JPE a16 M3 T3
        case  845: _goto(846); // CPE a16 M1 T5
        case  846: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(847); // CPE a16 M2 T1
        case  847: _dbin();cpu->pc++;_goto(848); // CPE a16 M2 T2
        case  848: _ready_rd();cpu->z=_gd();_goto(849); // CPE a16 M2 T3
        case  849: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(850); // CPE a16 M3 T1
        case  850: _dbin();cpu->pc++;_goto(851); // CPE a16 M3 T2
        case  851: _ready_rd();cpu->w=_gd();if(!(cpu->f&I8080_PF)){_fetch();}_goto(852); // CPE a16 M3 T3
        case  852: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(853); // CPE a16 M4 T1
        case  853: _sd(cpu->pch);cpu->sp--;_goto(854); // CPE a16 M4 T2
        case  854: _ready_wr();_goto(855); // CPE a16 M4 T3
        case  855: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(856); // CPE a16 M5 T1
        case  856: _sd(cpu->pcl);_goto(857); // CPE a16 M5 T2
        case  857: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CPE a16 M5 T3
        case  858: _goto(859); // CALL a16 M1 T5
        case  859: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(860); // CALL a16 M2 T1
        case  860: _dbin();cpu->pc++;_goto(861); // CALL a16 M2 T2
        case  861: _ready_rd();cpu->z=_gd();_goto(862); // CALL a16 M2 T3
        case  862: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(863); // CALL a16 M3 T1
        case  863: _dbin();cpu->pc++;_goto(864); // CALL a16 M3 T2
        case  864: _ready_rd();cpu->w=_gd();_goto(865); // CALL a16 M3 T3
        case  865: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(866); // CALL a16 M4 T1
        case  866: _sd(cpu->pch);cpu->sp--;_goto(867); // CALL a16 M4 T2
        case  867: _ready_wr();_goto(868); // CALL a16 M4 T3
        case  868: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(869); // CALL a16 M5 T1
        case  869: _sd(cpu->pcl);_goto(870); // CALL a16 M5 T2
        case  870: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CALL a16 M5 T3
        case  871: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(872); // XRI d8 M2 T1
        case  872: _dbin();cpu->pc++;_goto(873); // XRI d8 M2 T2
        case  873: _ready_rd();cpu->tmp=_gd();_fetch(); // XRI d8 M2 T3
        case  874: _goto(875); // RST 5 M1 T5
        case  875: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(876); // RST 5 M2 T1
        case  876: _sd(cpu->pch);cpu->sp--;_goto(877); // RST 5 M2 T2
        case  877: _ready_wr();_goto(878); // RST 5 M2 T3
        case  878: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(879); // RST 5 M3 T1
        case  879: _sd(cpu->pcl);_goto(880); // RST 5 M3 T2
        case  880: _ready_wr();cpu->z=0x28;cpu->pc=cpu->wz;_fetch(); // RST 5 M3 T3
        case  881: if(!(!(cpu->f&I8080_SF))){_fetch();}_goto(882); // RP M1 T5
        case  882: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(883); // RP M2 T1
        case  883: _dbin();cpu->sp++;_goto(884); // RP M2 T2
        case  884: _ready_rd();cpu->z=_gd();_goto(885); // RP M2 T3
        case  885: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(886); // RP M3 T1
        case  886: _dbin();cpu->sp++;_goto(887); // RP M3 T2
        case  887: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RP M3 T3
        case  888: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(889); // POP PSW M2 T1
        case  889: _dbin();cpu->sp++;_goto(890); // POP PSW M2 T2
        case  890: _ready_rd();cpu->f=_gd();cpu->f=(cpu->f&0xD5)|0x02;_goto(891); // POP PSW M2 T3
        case  891: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(892); // POP PSW M3 T1
        case  892: _dbin();cpu->sp++;_goto(893); // POP PSW M3 T2
        case  893: _ready_rd();cpu->a=_gd();_fetch(); // POP PSW M3 T3
        case  894: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(895); // JP a16 M2 T1
        case  895: _dbin();cpu->pc++;_goto(896); // JP a16 M2 T2
        case  896: _ready_rd();cpu->z=_gd();_goto(897); // JP a16 M2 T3
        case  897: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(898); // JP a16 M3 T1
        case  898: _dbin();cpu->pc++;_goto(899); // JP a16 M3 T2
        case  899: _ready_rd();cpu->w=_gd();if((!(cpu->f&I8080_SF))){cpu->pc=cpu->wz;}_fetch(); // JP a16 M3 T3
        case  900: _goto(901); // CP a16 M1 T5
        case  901: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(902); // CP a16 M2 T1
        case  902: _dbin();cpu->pc++;_goto(903); // CP a16 M2 T2
        case  903: _ready_rd();cpu->z=_gd();_goto(904); // CP a16 M2 T3
        case  904: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(905); // CP a16 M3 T1
        case  905: _dbin();cpu->pc++;_goto(906); // CP a16 M3 T2
        case  906: _ready_rd();cpu->w=_gd();if(!(!(cpu->f&I8080_SF))){_fetch();}_goto(907); // CP a16 M3 T3
        case  907: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(908); // CP a16 M4 T1
        case  908: _sd(cpu->pch);cpu->sp--;_goto(909); // CP a16 M4 T2
        case  909: _ready_wr();_goto(910); // CP a16 M4 T3
        case  910: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(911); // CP a16 M5 T1
        case  911: _sd(cpu->pcl);_goto(912); // CP a16 M5 T2
        case  912: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CP a16 M5 T3
        case  913: _goto(914); // PUSH PSW M1 T5
        case  914: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(915); // PUSH PSW M2 T1
        case  915: _sd(cpu->a);cpu->sp--;_goto(916); // PUSH PSW M2 T2
        case  916: _ready_wr();_goto(917); // PUSH PSW M2 T3
        case  917: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(918); // PUSH PSW M3 T1
        case  918: _sd(cpu->f);_goto(919); // PUSH PSW M3 T2
        case  919: _ready_wr();_fetch(); // PUSH PSW M3 T3
        case  920: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(921); // ORI d8 M2 T1
        case  921: _dbin();cpu->pc++;_goto(922); // ORI d8 M2 T2
        case  922: _ready_rd();cpu->tmp=_gd();_fetch(); // ORI d8 M2 T3
        case  923: _goto(924); // RST 6 M1 T5
        case  924: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(925); // RST 6 M2 T1
        case  925: _sd(cpu->pch);cpu->sp--;_goto(926); // RST 6 M2 T2
        case  926: _ready_wr();_goto(927); // RST 6 M2 T3
        case  927: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(928); // RST 6 M3 T1
        case  928: _sd(cpu->pcl);_goto(929); // RST 6 M3 T2
        case  929: _ready_wr();cpu->z=0x30;cpu->pc=cpu->wz;_fetch(); // RST 6 M3 T3
        case  930: if(!(cpu->f&I8080_SF)){_fetch();}_goto(931); // RM M1 T5
        case  931: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(932); // RM M2 T1
        case  932: _dbin();cpu->sp++;_goto(933); // RM M2 T2
        case  933: _ready_rd();cpu->z=_gd();_goto(934); // RM M2 T3
        case  934: _t1(cpu->sp,I8080_CYCLE_SREAD);_goto(935); // RM M3 T1
        case  935: _dbin();cpu->sp++;_goto(936); // RM M3 T2
        case  936: _ready_rd();cpu->w=_gd();cpu->pc=cpu->wz;_fetch(); // RM M3 T3
        case  937: cpu->sp=cpu->hl;_fetch(); // SPHL M1 T5
        case  938: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(939); // JM a16 M2 T1
        case  939: _dbin();cpu->pc++;_goto(940); // JM a16 M2 T2
        case  940: _ready_rd();cpu->z=_gd();_goto(941); // JM a16 M2 T3
        case  941: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(942); // JM a16 M3 T1
        case  942: _dbin();cpu->pc++;_goto(943); // JM a16 M3 T2
        case  943: _ready_rd();cpu->w=_gd();if((cpu->f&I8080_SF)){cpu->pc=cpu->wz;}_fetch(); // JM a16 M3 T3
        case  944: _goto(945); // CM a16 M1 T5
        case  945: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(946); // CM a16 M2 T1
        case  946: _dbin();cpu->pc++;_goto(947); // CM a16 M2 T2
        case  947: _ready_rd();cpu->z=_gd();_goto(948); // CM a16 M2 T3
        case  948: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(949); // CM a16 M3 T1
        case  949: _dbin();cpu->pc++;_goto(950); // CM a16 M3 T2
        case  950: _ready_rd();cpu->w=_gd();if(!(cpu->f&I8080_SF)){_fetch();}_goto(951); // CM a16 M3 T3
        case  951: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(952); // CM a16 M4 T1
        case  952: _sd(cpu->pch);cpu->sp--;_goto(953); // CM a16 M4 T2
        case  953: _ready_wr();_goto(954); // CM a16 M4 T3
        case  954: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(955); // CM a16 M5 T1
        case  955: _sd(cpu->pcl);_goto(956); // CM a16 M5 T2
        case  956: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CM a16 M5 T3
        case  957: _goto(958); // CALL a16 M1 T5
        case  958: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(959); // CALL a16 M2 T1
        case  959: _dbin();cpu->pc++;_goto(960); // CALL a16 M2 T2
        case  960: _ready_rd();cpu->z=_gd();_goto(961); // CALL a16 M2 T3
        case  961: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(962); // CALL a16 M3 T1
        case  962: _dbin();cpu->pc++;_goto(963); // CALL a16 M3 T2
        case  963: _ready_rd();cpu->w=_gd();_goto(964); // CALL a16 M3 T3
        case  964: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(965); // CALL a16 M4 T1
        case  965: _sd(cpu->pch);cpu->sp--;_goto(966); // CALL a16 M4 T2
        case  966: _ready_wr();_goto(967); // CALL a16 M4 T3
        case  967: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(968); // CALL a16 M5 T1
        case  968: _sd(cpu->pcl);_goto(969); // CALL a16 M5 T2
        case  969: _ready_wr();cpu->pc=cpu->wz;_fetch(); // CALL a16 M5 T3
        case  970: _t1(cpu->pc,I8080_CYCLE_MREAD);_goto(971); // CPI d8 M2 T1
        case  971: _dbin();cpu->pc++;_goto(972); // CPI d8 M2 T2
        case  972: _ready_rd();cpu->tmp=_gd();_fetch(); // CPI d8 M2 T3
        case  973: _goto(974); // RST 7 M1 T5
        case  974: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(975); // RST 7 M2 T1
        case  975: _sd(cpu->pch);cpu->sp--;_goto(976); // RST 7 M2 T2
        case  976: _ready_wr();_goto(977); // RST 7 M2 T3
        case  977: _t1(cpu->sp,I8080_CYCLE_SWRITE);_goto(978); // RST 7 M3 T1
        case  978: _sd(cpu->pcl);_goto(979); // RST 7 M3 T2
        case  979: _ready_wr();cpu->z=0x38;cpu->pc=cpu->wz;_fetch(); // RST 7 M3 T3
        // %>

        //=== M1, the opcode fetch, shared by every instruction.
        //    Its T4 (and T5) are the instruction's own first steps.
        case I8080_M1_T1:
            // An interrupt is taken between instructions when INT and INTE are
            // both set (Figure 2-4), but not straight after EI: interrupts are
            // enabled "following the execution of the next instruction" (EI,
            // chapter 4). `opcode` is still the instruction that just ended.
            if ((in & I8080_INT) && cpu->inte && cpu->opcode != 0xFB) {
                cpu->inte = false;
                _t1(cpu->pc, I8080_CYCLE_INTA);
                _goto(I8080_INTA_T2);
            }
            _t1(cpu->pc, I8080_CYCLE_FETCH);
            _goto(I8080_M1_T2);
        case I8080_M1_T2:
            _dbin();
            cpu->pc++;
            // the previous instruction's ALU result lands now (note 9)
            if (cpu->alu) {
                _i8080_alu_finish(cpu);
            }
            _goto(I8080_M1_T3);
        case I8080_M1_T3: _ready_rd(); cpu->opcode = _gd(); _goto(cpu->opcode);

        //=== Interrupt acknowledge: a fetch whose PC is not incremented; the
        //    interrupting device (on our SBC the 8238, RST 7) puts the
        //    instruction on the bus for T3 (p. 2-11, Figure 2-8).
        case I8080_INTA_T2:
            _dbin();
            if (cpu->alu) {
                _i8080_alu_finish(cpu);
            }
            _goto(I8080_M1_T3);

        //=== Halted (TWH): WAIT high, buses idle. RESET, or an interrupt
        //    with INTE set, ends it (p. 2-13, Figures 2-4 and 2-12). HOLD: not
        //    modeled. The halt cycle's T2 always goes on to one TWH (Figure
        //    2-4; HLT is 7 states); INT sampled during a TWH makes the next
        //    state T1 of an interrupt acknowledge.
        case I8080_HALT_TWH1:
            pins |= I8080_WAIT;
            _goto(I8080_HALT_TWH);
        case I8080_HALT_TWH:
            if ((in & I8080_INT) && cpu->inte) {
                cpu->inte = false;
                _t1(cpu->pc, I8080_CYCLE_INTA_HALT);
                _goto(I8080_INTA_T2);
            }
            pins |= I8080_WAIT;
            goto step_to;

        default: CHIPS_ASSERT(false);
    }
step_to:
    if (cpu->inte) {
        pins |= I8080_INTE;
    }
    return pins;
}

#undef _goto
#undef _fetch
#undef _unimplemented
#undef _gd
#undef _t1
#undef _dbin
#undef _sd
#undef _ready_rd
#undef _ready_wr

#endif // CHIPS_IMPL

/*
    This file is an altered version of floooh/chips' code-generated CPU
    headers (https://github.com/floooh/chips): a new 8080 decoder generated
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
