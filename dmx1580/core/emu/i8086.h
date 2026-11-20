// Small 8086 interpreter for the DMX 9.8 pitch-change board program.
//
// Real-mode 8086, one instruction per i86_step(). It covers the integer
// instruction set the uploaded program needs and most of the rest of the
// common one-byte opcodes. Anything it does not know stops with I86_TRAP and
// the opcode, so that a dropped bit that turns code into something odd shows up
// instead of being skipped. No interrupts, string ops, MUL/DIV, BCD or LOCK.
//
// Memory and I/O are callbacks. Cycle counts are rough (8086 book values
// without the EA/odd-address detail) and only used to measure the time between
// WAIT instructions.
#ifndef I8086_H
#define I8086_H
#include <stdint.h>

enum { AX, CX, DX, BX, SP, BP, SI, DI };
enum { ES, CS, SS, DS };
enum { I86_RUN, I86_WAIT, I86_HALT, I86_TRAP };
enum { F_C = 1, F_P = 4, F_A = 16, F_Z = 64, F_S = 128, F_D = 1024, F_O = 2048 };

typedef struct {
    uint16_t r[8], s[4], ip, f;
    int state;                 // I86_RUN / I86_WAIT (stopped at WAIT, TEST not low) / HALT / TRAP
    uint8_t trap_op;
    uint16_t trap_cs, trap_ip;
    uint32_t cycles;
    void *user;
    uint8_t (*rd)(void *u, uint32_t a);
    void (*wr)(void *u, uint32_t a, uint8_t d);
    uint16_t (*in)(void *u, uint16_t port, int word);
    void (*out)(void *u, uint16_t port, uint16_t v, int word);
    // decode scratch
    int seg_ovr;
} i86_t;

void i86_reset(i86_t *c);
int i86_step(i86_t *c, int test_low);   // returns state

#ifdef I8086_IMPL
static uint32_t i86_lin(uint16_t s, uint16_t o) { return ((uint32_t)s << 4) + o & 0xFFFFF; }
static uint8_t i86_fetch(i86_t *c) { return c->rd(c->user, i86_lin(c->s[CS], c->ip++)); }
static uint16_t i86_fetch16(i86_t *c) { uint16_t l = i86_fetch(c); return l | i86_fetch(c) << 8; }
static uint8_t *i86_r8p(i86_t *c, int n) {  // AL CL DL BL AH CH DH BH (little-endian host)
    return (uint8_t *)&c->r[n & 3] + (n >> 2);
}

void i86_reset(i86_t *c) {
    for (int i = 0; i < 8; i++) c->r[i] = 0;
    c->s[ES] = c->s[SS] = c->s[DS] = 0;
    c->s[CS] = 0xFFFF; c->ip = 0; c->f = 0xF002; c->state = I86_RUN; c->seg_ovr = -1;
}

// ---- ModR/M ----
typedef struct { int mod, reg, rm; uint32_t ea; } i86_modrm;
static i86_modrm i86_decode(i86_t *c) {
    i86_modrm m; uint8_t b = i86_fetch(c);
    m.mod = b >> 6; m.reg = b >> 3 & 7; m.rm = b & 7; m.ea = 0;
    if (m.mod == 3) return m;
    uint16_t off = 0; int seg = DS;
    switch (m.rm) {
    case 0: off = c->r[BX] + c->r[SI]; break;
    case 1: off = c->r[BX] + c->r[DI]; break;
    case 2: off = c->r[BP] + c->r[SI]; seg = SS; break;
    case 3: off = c->r[BP] + c->r[DI]; seg = SS; break;
    case 4: off = c->r[SI]; break;
    case 5: off = c->r[DI]; break;
    case 6: if (m.mod == 0) off = i86_fetch16(c); else { off = c->r[BP]; seg = SS; } break;
    case 7: off = c->r[BX]; break;
    }
    if (m.mod == 1) off += (int8_t)i86_fetch(c);
    else if (m.mod == 2) off += i86_fetch16(c);
    if (c->seg_ovr >= 0) seg = c->seg_ovr;
    m.ea = i86_lin(c->s[seg], off);
    c->cycles += 8;
    return m;
}
static uint16_t i86_get(i86_t *c, i86_modrm *m, int w) {
    if (m->mod == 3) return w ? c->r[m->rm] : *i86_r8p(c, m->rm);
    uint16_t v = c->rd(c->user, m->ea);
    if (w) v |= c->rd(c->user, (m->ea + 1) & 0xFFFFF) << 8;
    return v;
}
static void i86_put(i86_t *c, i86_modrm *m, int w, uint16_t v) {
    if (m->mod == 3) { if (w) c->r[m->rm] = v; else *i86_r8p(c, m->rm) = v; return; }
    c->wr(c->user, m->ea, v);
    if (w) c->wr(c->user, (m->ea + 1) & 0xFFFFF, v >> 8);
}
static uint16_t i86_getreg(i86_t *c, int n, int w) { return w ? c->r[n] : *i86_r8p(c, n); }
static void i86_setreg(i86_t *c, int n, int w, uint16_t v) { if (w) c->r[n] = v; else *i86_r8p(c, n) = v; }

// ---- flags ----
static void i86_szp(i86_t *c, uint32_t v, int w) {
    uint16_t m = w ? 0xFFFF : 0xFF, s = w ? 0x8000 : 0x80;
    c->f &= ~(F_Z | F_S | F_P);
    if (!(v & m)) c->f |= F_Z;
    if (v & s) c->f |= F_S;
    uint8_t p = v; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
    if (!(p & 1)) c->f |= F_P;
}
// op: 0 ADD 1 OR 2 ADC 3 SBB 4 AND 5 SUB 6 XOR 7 CMP
static uint16_t i86_alu(i86_t *c, int op, uint16_t a, uint16_t b, int w) {
    uint32_t m = w ? 0xFFFF : 0xFF, s = w ? 0x8000 : 0x80, r;
    int cin = c->f & F_C ? 1 : 0;
    c->f &= ~(F_C | F_O | F_A);
    switch (op) {
    case 0: case 2:
        r = (uint32_t)a + b + (op == 2 ? cin : 0);
        if (r > m) c->f |= F_C;
        if ((~(a ^ b) & (a ^ r)) & s) c->f |= F_O;
        break;
    case 3: case 5: case 7:
        r = (uint32_t)a - b - (op == 3 ? cin : 0);
        if ((uint32_t)a < (uint32_t)b + (op == 3 ? cin : 0)) c->f |= F_C;
        if (((a ^ b) & (a ^ r)) & s) c->f |= F_O;
        break;
    case 1: r = a | b; break;
    case 4: r = a & b; break;
    default: r = a ^ b; break;
    }
    if ((a ^ b ^ r) & 0x10) c->f |= F_A;
    r &= m;
    i86_szp(c, r, w);
    return op == 7 ? a : r;
}
static uint16_t i86_incdec(i86_t *c, uint16_t a, int dec, int w) {
    uint16_t cf = c->f & F_C;
    uint16_t r = i86_alu(c, dec ? 5 : 0, a, 1, w);
    c->f = (c->f & ~F_C) | cf;
    return r;
}
// shift/rotate group, count n: 0 ROL 1 ROR 2 RCL 3 RCR 4 SHL 5 SHR 7 SAR
static uint16_t i86_shift(i86_t *c, int op, uint16_t v, int n, int w) {
    uint32_t m = w ? 0xFFFF : 0xFF, s = w ? 0x8000 : 0x80;
    int bits = w ? 16 : 8;
    for (int i = 0; i < n; i++) {
        int cf = c->f & F_C ? 1 : 0, out;
        switch (op) {
        case 0: out = !!(v & s); v = (v << 1 | out) & m; break;
        case 1: out = v & 1; v = v >> 1 | (out ? s : 0); break;
        case 2: out = !!(v & s); v = (v << 1 | cf) & m; break;
        case 3: out = v & 1; v = v >> 1 | (cf ? s : 0); break;
        case 4: case 6: out = !!(v & s); v = v << 1 & m; break;
        case 5: out = v & 1; v >>= 1; break;
        default: out = v & 1; v = (v >> 1) | (v & s); break;
        }
        c->f = (c->f & ~F_C) | (out ? F_C : 0);
        if (op >= 4) i86_szp(c, v, w);
    }
    if (n == 1) {   // OF defined for count 1
        int o = 0;
        if (op == 0 || op == 2 || op == 4 || op == 6) o = !!(v & s) ^ !!(c->f & F_C);
        else if (op == 1 || op == 3) o = !!(v & s) ^ !!(v & (s >> 1));
        else if (op == 5) o = 0;   // set from the original top bit; rarely used
        c->f = (c->f & ~F_O) | (o ? F_O : 0);
    }
    (void)bits;
    return v;
}
static int i86_cond(i86_t *c, int cc) {
    int C = !!(c->f & F_C), Z = !!(c->f & F_Z), S = !!(c->f & F_S), O = !!(c->f & F_O), P = !!(c->f & F_P), r;
    switch (cc >> 1) {
    case 0: r = O; break;
    case 1: r = C; break;
    case 2: r = Z; break;
    case 3: r = C || Z; break;
    case 4: r = S; break;
    case 5: r = P; break;
    case 6: r = S != O; break;
    default: r = Z || S != O; break;
    }
    return cc & 1 ? !r : r;
}

static int i86_trap(i86_t *c, uint8_t op, uint16_t ip0) {
    c->state = I86_TRAP; c->trap_op = op; c->trap_cs = c->s[CS]; c->trap_ip = ip0;
    return c->state;
}

int i86_step(i86_t *c, int test_low) {
    if (c->state == I86_HALT || c->state == I86_TRAP) return c->state;
    if (c->state == I86_WAIT) {
        if (!test_low) return I86_WAIT;
        c->state = I86_RUN;
        c->cycles += 3;
        return I86_RUN;
    }
    uint16_t ip0 = c->ip;
    c->seg_ovr = -1;
    uint8_t op;
    for (;;) {
        op = i86_fetch(c);
        if ((op & 0xE7) == 0x26) { c->seg_ovr = op >> 3 & 3; c->cycles += 2; continue; }
        break;
    }
    int w = op & 1;
    if (op < 0x40 && (op & 7) < 6) {                 // ALU r/m,reg / reg,r/m / acc,imm
        int alu = op >> 3, form = op & 7;
        if (form < 4) {
            i86_modrm m = i86_decode(c);
            int d = op & 2;
            uint16_t a = d ? i86_getreg(c, m.reg, w) : i86_get(c, &m, w);
            uint16_t b = d ? i86_get(c, &m, w) : i86_getreg(c, m.reg, w);
            uint16_t r = i86_alu(c, alu, a, b, w);
            if (alu != 7) { if (d) i86_setreg(c, m.reg, w, r); else i86_put(c, &m, w, r); }
            c->cycles += m.mod == 3 ? 3 : 9;
        } else {
            uint16_t b = w ? i86_fetch16(c) : i86_fetch(c);
            uint16_t r = i86_alu(c, alu, i86_getreg(c, AX, w), b, w);
            if (alu != 7) i86_setreg(c, AX, w, r);
            c->cycles += 4;
        }
        return c->state;
    }
    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E: {     // PUSH sreg
        c->r[SP] -= 2; uint32_t a = i86_lin(c->s[SS], c->r[SP]); uint16_t v = c->s[op >> 3];
        c->wr(c->user, a, v); c->wr(c->user, (a + 1) & 0xFFFFF, v >> 8); c->cycles += 10; break; }
    case 0x07: case 0x17: case 0x1F: {                // POP sreg
        uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        c->s[op >> 3] = c->rd(c->user, a) | c->rd(c->user, (a + 1) & 0xFFFFF) << 8;
        c->r[SP] += 2; c->cycles += 8; break; }
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        c->r[op & 7] = i86_incdec(c, c->r[op & 7], 0, 1); c->cycles += 2; break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        c->r[op & 7] = i86_incdec(c, c->r[op & 7], 1, 1); c->cycles += 2; break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: {
        uint16_t v = c->r[op & 7]; c->r[SP] -= 2; uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        c->wr(c->user, a, v); c->wr(c->user, (a + 1) & 0xFFFFF, v >> 8); c->cycles += 11; break; }
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        uint16_t v = c->rd(c->user, a) | c->rd(c->user, (a + 1) & 0xFFFFF) << 8;
        c->r[SP] += 2; c->r[op & 7] = v; c->cycles += 8; break; }
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int8_t d = i86_fetch(c);
        if (i86_cond(c, op & 15)) { c->ip += d; c->cycles += 16; } else c->cycles += 4;
        break; }
    case 0x80: case 0x81: case 0x83: {
        i86_modrm m = i86_decode(c);
        uint16_t b = op == 0x81 ? i86_fetch16(c) : op == 0x83 ? (uint16_t)(int8_t)i86_fetch(c) : i86_fetch(c);
        uint16_t r = i86_alu(c, m.reg, i86_get(c, &m, w), b, w);
        if (m.reg != 7) i86_put(c, &m, w, r);
        c->cycles += m.mod == 3 ? 4 : 17; break; }
    case 0x84: case 0x85: {
        i86_modrm m = i86_decode(c);
        i86_alu(c, 4, i86_get(c, &m, w), i86_getreg(c, m.reg, w), w); c->cycles += 3; break; }
    case 0x86: case 0x87: {
        i86_modrm m = i86_decode(c);
        uint16_t a = i86_get(c, &m, w), b = i86_getreg(c, m.reg, w);
        i86_put(c, &m, w, b); i86_setreg(c, m.reg, w, a); c->cycles += 4; break; }
    case 0x88: case 0x89: { i86_modrm m = i86_decode(c); i86_put(c, &m, w, i86_getreg(c, m.reg, w)); c->cycles += 2; break; }
    case 0x8A: case 0x8B: { i86_modrm m = i86_decode(c); i86_setreg(c, m.reg, w, i86_get(c, &m, w)); c->cycles += 2; break; }
    case 0x8C: { i86_modrm m = i86_decode(c); i86_put(c, &m, 1, c->s[m.reg & 3]); c->cycles += 2; break; }
    case 0x8D: { i86_modrm m = i86_decode(c);
        if (m.mod == 3) return i86_trap(c, op, ip0);
        // LEA: recompute the offset without the segment
        c->r[m.reg] = (uint16_t)(m.ea - ((uint32_t)c->s[c->seg_ovr >= 0 ? c->seg_ovr : (m.rm == 2 || m.rm == 3 || (m.rm == 6 && m.mod)) ? SS : DS] << 4));
        c->cycles += 2; break; }
    case 0x8E: { i86_modrm m = i86_decode(c); c->s[m.reg & 3] = i86_get(c, &m, 1); c->cycles += 2; break; }
    case 0x90: c->cycles += 3; break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint16_t t = c->r[AX]; c->r[AX] = c->r[op & 7]; c->r[op & 7] = t; c->cycles += 3; break; }
    case 0x98: c->r[AX] = (int8_t)c->r[AX]; c->cycles += 2; break;
    case 0x99: c->r[DX] = c->r[AX] & 0x8000 ? 0xFFFF : 0; c->cycles += 5; break;
    case 0x9B:                                     // WAIT: stop until TEST is low
        c->cycles += 3;
        if (!test_low) c->state = I86_WAIT;
        break;
    case 0x9C: { c->r[SP] -= 2; uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        c->wr(c->user, a, c->f); c->wr(c->user, (a + 1) & 0xFFFFF, c->f >> 8); c->cycles += 10; break; }
    case 0x9D: { uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        c->f = (c->rd(c->user, a) | c->rd(c->user, (a + 1) & 0xFFFFF) << 8) | 0xF002; c->r[SP] += 2; c->cycles += 8; break; }
    case 0x9E: c->f = (c->f & 0xFF00) | (c->r[AX] >> 8 & 0xD5) | 2; c->cycles += 4; break;
    case 0x9F: c->r[AX] = (c->r[AX] & 0xFF) | (c->f & 0xFF) << 8; c->cycles += 4; break;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
        uint16_t off = i86_fetch16(c);
        uint32_t a = i86_lin(c->s[c->seg_ovr >= 0 ? c->seg_ovr : DS], off);
        if (op < 0xA2) {
            uint16_t v = c->rd(c->user, a);
            if (w) v |= c->rd(c->user, (a + 1) & 0xFFFFF) << 8;
            i86_setreg(c, AX, w, v);
        } else {
            c->wr(c->user, a, c->r[AX]);
            if (w) c->wr(c->user, (a + 1) & 0xFFFFF, c->r[AX] >> 8);
        }
        c->cycles += 10; break; }
    case 0xA8: case 0xA9: { uint16_t b = w ? i86_fetch16(c) : i86_fetch(c);
        i86_alu(c, 4, i86_getreg(c, AX, w), b, w); c->cycles += 4; break; }
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        *i86_r8p(c, op & 7) = i86_fetch(c); c->cycles += 4; break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        c->r[op & 7] = i86_fetch16(c); c->cycles += 4; break;
    case 0xC3: { uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        c->ip = c->rd(c->user, a) | c->rd(c->user, (a + 1) & 0xFFFFF) << 8; c->r[SP] += 2; c->cycles += 8; break; }
    case 0xC6: case 0xC7: { i86_modrm m = i86_decode(c);
        i86_put(c, &m, w, w ? i86_fetch16(c) : i86_fetch(c)); c->cycles += 10; break; }
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        i86_modrm m = i86_decode(c);
        int n = op & 2 ? c->r[CX] & 0xFF : 1;
        i86_put(c, &m, w, i86_shift(c, m.reg, i86_get(c, &m, w), n, w));
        c->cycles += (op & 2 ? 8 + 4 * n : 2) + (m.mod == 3 ? 0 : 13); break; }
    case 0xE2: { int8_t d = i86_fetch(c); if (--c->r[CX]) { c->ip += d; c->cycles += 17; } else c->cycles += 5; break; }
    case 0xE4: case 0xE5: { uint8_t p = i86_fetch(c); i86_setreg(c, AX, w, c->in(c->user, p, w)); c->cycles += 10; break; }
    case 0xE6: case 0xE7: { uint8_t p = i86_fetch(c); c->out(c->user, p, i86_getreg(c, AX, w), w); c->cycles += 10; break; }
    case 0xEC: case 0xED: i86_setreg(c, AX, w, c->in(c->user, c->r[DX], w)); c->cycles += 8; break;
    case 0xEE: case 0xEF: c->out(c->user, c->r[DX], i86_getreg(c, AX, w), w); c->cycles += 8; break;
    case 0xE8: { int16_t d = i86_fetch16(c); c->r[SP] -= 2; uint32_t a = i86_lin(c->s[SS], c->r[SP]);
        c->wr(c->user, a, c->ip); c->wr(c->user, (a + 1) & 0xFFFFF, c->ip >> 8); c->ip += d; c->cycles += 19; break; }
    case 0xE9: { int16_t d = i86_fetch16(c); c->ip += d; c->cycles += 15; break; }
    case 0xEA: { uint16_t o = i86_fetch16(c), s = i86_fetch16(c); c->ip = o; c->s[CS] = s; c->cycles += 15; break; }
    case 0xEB: { int8_t d = i86_fetch(c); c->ip += d; c->cycles += 15; break; }
    case 0xF4: c->state = I86_HALT; c->cycles += 2; break;
    case 0xF5: c->f ^= F_C; c->cycles += 2; break;
    case 0xF6: case 0xF7: {
        i86_modrm m = i86_decode(c);
        uint16_t v = i86_get(c, &m, w);
        switch (m.reg) {
        case 0: case 1: i86_alu(c, 4, v, w ? i86_fetch16(c) : i86_fetch(c), w); break;
        case 2: i86_put(c, &m, w, ~v); break;
        case 3: i86_put(c, &m, w, i86_alu(c, 5, 0, v, w)); if (v) c->f |= F_C; else c->f &= ~F_C; break;
        default: return i86_trap(c, op, ip0);       // MUL/IMUL/DIV/IDIV not needed
        }
        c->cycles += 3; break; }
    case 0xF8: c->f &= ~F_C; c->cycles += 2; break;
    case 0xF9: c->f |= F_C; c->cycles += 2; break;
    case 0xFA: case 0xFB: c->cycles += 2; break;     // CLI/STI: no interrupts modelled
    case 0xFC: c->f &= ~F_D; c->cycles += 2; break;
    case 0xFD: c->f |= F_D; c->cycles += 2; break;
    case 0xFE: case 0xFF: {
        i86_modrm m = i86_decode(c);
        if (m.reg < 2) { i86_put(c, &m, w, i86_incdec(c, i86_get(c, &m, w), m.reg, w)); c->cycles += 3; break; }
        if (w && m.reg == 4) { c->ip = i86_get(c, &m, 1); c->cycles += 11; break; }
        return i86_trap(c, op, ip0);
    }
    default:
        return i86_trap(c, op, ip0);
    }
    return c->state;
}
#endif
#endif
