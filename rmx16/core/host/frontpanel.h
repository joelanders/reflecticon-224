// RMX16 reverberation front panel (DMX 34.3), behavioural model from the
// reconciled netlist (kicad/frontpanel) and the processor board's ribbons.
//
// Bus (processor 8255s via ribbon E4 -> FP E33, and FP E34 -> E5):
//   E6 port A  -> FP data D0-D7 through E35 (74LS244, enabled by SEL low).
//                 Bit order D(n) = PA(n): proven by the firmware's text on
//                 the DL1414s ("AMBIENCE", "RMX 3.2"). The netlists show the
//                 low nibble reversed (processor E4 pins 1-4 = PA3..PA0, or
//                 the FP's flagged sequential bus order): see notes.
//   E3 port A  -> PA0 SEL, PA1 E38 G2B (write strobe, low), PA2/3/4 ADDR
//                 A/B/C, PA5 A0, PA6 A1 (DL1414 character address).
//   FP D bus   -> E6 port B (D0-D7); D6/D7 also to E3 PC5/PC6.
//
// Writes (SEL low; E38 decodes A/B/C, strobed by G2B):
//   Y0 E21 74LS273: digit selects. D0 E10, D1 E9, D2 E8, D3 E7, D4 E6, D5 E5
//                   (via E20 75492), D6 E39 (via E24).
//   Y1 E23 74LS273: D0 E40, D1 E14, D4 E12, D7 E15 (via E24 75492);
//                   D2 D3 D5 D6 via E22 7406 (inverting) to the TIL327 sign
//                   digits: E11 pin 8 (minus bar) D6, pin 7 (vertical bar) D5;
//                   E13 pin 8 D3, pin 7 D2. "+" lights both bars. (Roles from
//                   the firmware: nudging the filters gives -n with one bar
//                   and +n with both.)
//   Y2 E29 74LS273: segments, active low: D0 a ... D6 g, D7 dp (the
//                   firmware's font at 9340 is complemented before writing).
//   Y3 E25 74LS174: LEDs 1-4.   Y4 E19 74LS174: LEDs 5-10.
//   Y5 E17 DL1414 (left 4 characters), Y6 E18 DL1414 (right 4).
//   Y7 E3 74LS273: analog mux / ADC control.
// Reads (SEL high; E37 decodes A/B/C):
//   Y0 E26 keys 1-6 on D0-D5; Y1 E31 keys 7-10 on D0-D3;
//   Y2-Y5 keyboard matrix columns (E32) with rows KR1-4 on D0-D3 (E30);
//   Y6 AD7574 (pots via the DG201 mux).
// Pots: E3 (Y7) bits drive the DG201's switch inputs (a switch is on when
// its input is low): D0 -> DELAY pot (the one above the pot key), D2 ->
// INPUT pot, D1 -> OUTPUT pot; D3 -> the buffer into the ADC. Key roles
// (from the firmware and the user manual): S1 pot, S2 program, S3 predelay,
// S4 decay, S5 low filter, S6 high filter, S7 save, S8 recall, S9 nudge
// down, S10 nudge up; LED10..LED3 are S1..S8's key LEDs.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// physical left-to-right order of the numeric displays (layout sheet 2)
enum { FP_E39, FP_E5, FP_E6, FP_E7, FP_E8, FP_E9, FP_E10, FP_E11, FP_E12, FP_E13, FP_E14, FP_E15, FP_E40, FP_NDIG };
static const char* fp_digit_names[FP_NDIG] = {"E39", "E5", "E6", "E7", "E8", "E9", "E10", "E11", "E12", "E13", "E14", "E15", "E40"};

typedef struct {
    uint8_t e21, e23, e29, e25, e19, e3;
    uint8_t seg[FP_NDIG];       // last segment pattern (active high: bit0 a .. bit6 g, bit7 dp) seen lit
    uint32_t lit[FP_NDIG];      // how many times each digit was selected
    char alpha[8];              // DL1414 characters, left to right
    uint8_t keys[2];            // E26 (keys 1-6 on D0-D5), E31 (keys 10,9,8,7 on D0-D3): bit set = pressed
    uint8_t matrix[4];          // keyboard matrix: column Y2..Y5 -> row bits KR1..KR4 (set = pressed)
    uint8_t pot[3];             // DELAY, INPUT, OUTPUT pot positions (0-255)
    uint8_t prev_ctrl;
    uint32_t writes[8], reads[8];
} fp_t;

static inline void fp_init(fp_t* fp) {
    memset(fp, 0, sizeof *fp);
    memset(fp->alpha, ' ', sizeof fp->alpha);
    fp->prev_ctrl = 0xFF;
    fp->pot[0] = fp->pot[1] = fp->pot[2] = 0x80;
}

static inline void fp_light(fp_t* fp) {
    static const struct { int reg; uint8_t bit; int digit; } map[] = {
        {0, 0x01, FP_E10}, {0, 0x02, FP_E9}, {0, 0x04, FP_E8}, {0, 0x08, FP_E7}, {0, 0x10, FP_E6},
        {0, 0x20, FP_E5}, {0, 0x40, FP_E39},
        {1, 0x01, FP_E40}, {1, 0x02, FP_E14}, {1, 0x10, FP_E12}, {1, 0x80, FP_E15},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        uint8_t r = map[i].reg ? fp->e23 : fp->e21;
        if (r & map[i].bit) {
            fp->seg[map[i].digit] = (uint8_t)~fp->e29;
            fp->lit[map[i].digit]++;
        }
    }
}

// The FP's 8255-side interface. ctrl = E3 port A, data = E6 port A.
// Call on every write to either port; a falling G2B with SEL low strobes.
static inline void fp_write_ports(fp_t* fp, uint8_t ctrl, uint8_t data) {
    bool sel = ctrl & 0x01, g2b = (ctrl & 0x02) != 0;
    bool was_g2b = (fp->prev_ctrl & 0x02) != 0;
    int y = ((ctrl >> 2) & 1) | (((ctrl >> 3) & 1) << 1) | (((ctrl >> 4) & 1) << 2);
    int a = ((ctrl >> 5) & 1) | (((ctrl >> 6) & 1) << 1);
    if (!sel && !g2b && was_g2b) {
        fp->writes[y]++;
        switch (y) {
            case 0: fp->e21 = data; fp_light(fp); break;
            case 1: fp->e23 = data; fp_light(fp); break;
            case 2: fp->e29 = data; break;
            case 3: fp->e25 = data; break;
            case 4: fp->e19 = data; break;
            case 5: fp->alpha[3 - a] = (char)(data & 0x7F); break;     // DL1414 digit 0 is rightmost
            case 6: fp->alpha[7 - a] = (char)(data & 0x7F); break;
            case 7: fp->e3 = data; break;
        }
    }
    fp->prev_ctrl = ctrl;
}

// The FP data bus as seen when SEL is high (a read cycle): 0xFF = nothing driving.
static inline uint8_t fp_read_bus(fp_t* fp, uint8_t ctrl) {
    if (!(ctrl & 0x01)) {
        return 0xFF;
    }
    int y = ((ctrl >> 2) & 1) | (((ctrl >> 3) & 1) << 1) | (((ctrl >> 4) & 1) << 2);
    fp->reads[y]++;
    switch (y) {
        case 0: return (uint8_t)(0xC0 | (~fp->keys[0] & 0x3F));        // keys pull low when pressed (assumed)
        case 1: return (uint8_t)(0xF0 | (~fp->keys[1] & 0x0F));
        case 2: case 3: case 4: case 5: return (uint8_t)(0xF0 | (~fp->matrix[y - 2] & 0x0F));
        case 6: {
            // the DG201 switch that is on (input low) picks the pot
            if (!(fp->e3 & 0x01)) return fp->pot[0];
            if (!(fp->e3 & 0x04)) return fp->pot[1];
            if (!(fp->e3 & 0x02)) return fp->pot[2];
            return 0x00;
        }
        default: return 0xFF;
    }
}

// Press or release a key by name: "S1".."S10" (the function keys, netlist
// KEY1..KEY10, pulled to ground when pressed) or "M<col><row>" for the
// keyboard matrix (col 0-3 = E37 Y2-Y5, row 1-4 = KR1-KR4).
static inline bool fp_key(fp_t* fp, const char* name, bool down) {
    int n = 0, c = 0, r = 0;
    if (sscanf(name, "S%d", &n) == 1 && n >= 1 && n <= 10) {
        // E26: KEY1 D0, KEY2 D1, KEY3 D2, KEY4 D3, KEY5 D4, KEY6 D5;
        // E31: KEY10 D0, KEY9 D1, KEY8 D2, KEY7 D3
        int reg = n <= 6 ? 0 : 1;
        int bit = n <= 6 ? n - 1 : 10 - n;
        if (down) fp->keys[reg] |= (uint8_t)(1 << bit); else fp->keys[reg] &= (uint8_t)~(1 << bit);
        return true;
    }
    if (sscanf(name, "M%1d%1d", &c, &r) == 2 && c >= 0 && c < 4 && r >= 1 && r <= 4) {
        if (down) fp->matrix[c] |= (uint8_t)(1 << (r - 1)); else fp->matrix[c] &= (uint8_t)~(1 << (r - 1));
        return true;
    }
    return false;
}

// LEDs lit (active low latch bits): LED1-4 from E25 D0-D3, LED5-10 from E19 D5..D0
static inline void fp_leds(const fp_t* fp, char* out) {
    for (int i = 0; i < 4; i++) out[i] = (fp->e25 >> i) & 1 ? '.' : '*';
    for (int i = 0; i < 6; i++) out[4 + i] = (fp->e19 >> (5 - i)) & 1 ? '.' : '*';
    out[10] = 0;
}

static inline void fp_render(const fp_t* fp, FILE* out) {
    // three text rows of 7-segment art, then the alphanumerics and LEDs
    char row[3][FP_NDIG * 4 + 4];
    memset(row, ' ', sizeof row);
    for (int d = 0; d < FP_NDIG; d++) {
        uint8_t s = fp->lit[d] ? fp->seg[d] : 0;
        int x = d * 4;
        if (d == FP_E11 || d == FP_E13) {
            // TIL327: minus bar and vertical bar
            bool minus = d == FP_E11 ? (fp->e23 & 0x40) : (fp->e23 & 0x08);
            bool vert = d == FP_E11 ? (fp->e23 & 0x20) : (fp->e23 & 0x04);
            row[1][x + 1] = minus ? (vert ? '+' : '-') : (vert ? '|' : ' ');
            continue;
        }
        row[0][x + 1] = (s & 0x01) ? '_' : ' ';
        row[1][x + 0] = (s & 0x20) ? '|' : ' ';
        row[1][x + 1] = (s & 0x40) ? '_' : ' ';
        row[1][x + 2] = (s & 0x02) ? '|' : ' ';
        row[2][x + 0] = (s & 0x10) ? '|' : ' ';
        row[2][x + 1] = (s & 0x08) ? '_' : ' ';
        row[2][x + 2] = (s & 0x04) ? '|' : ' ';
        row[2][x + 3] = (s & 0x80) ? '.' : ' ';
    }
    for (int r = 0; r < 3; r++) {
        row[r][FP_NDIG * 4] = 0;
        fprintf(out, "  %s%s\n", row[r], r == 1 ? "" : "");
    }
    fprintf(out, "  ");
    for (int d = 0; d < FP_NDIG; d++) fprintf(out, "%-4s", fp_digit_names[d]);
    fprintf(out, "\n  signs: E11 %c  E13 %c", (fp->e23 & 0x40) ? ((fp->e23 & 0x20) ? '+' : '-') : ' ',
            (fp->e23 & 0x08) ? ((fp->e23 & 0x04) ? '+' : '-') : ' ');
    fprintf(out, "\n  segs:");
    for (int d = 0; d < FP_NDIG; d++) fprintf(out, " %s=%02X/%u", fp_digit_names[d], fp->seg[d], fp->lit[d]);
    char leds[11];
    fp_leds(fp, leds);
    fprintf(out, "\n  [%.8s]   LED1-10 %s   E23 %02X  E3 %02X\n", fp->alpha, leds, fp->e23, fp->e3);
}
