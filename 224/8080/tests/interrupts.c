// Interrupt tests for i8080.h, against the User's Manual (1975):
// p. 2-11 and Figure 2-8 (interrupt acknowledge), Figure 2-4 (when INT is
// taken), p. 2-13 and Figure 2-12 (leaving the halt state), chapter 4 EI
// ("enabled following the execution of the next instruction").
//
// The system side plays an 8228 (or 8238) wired for RST 7: in an interrupt
// acknowledge cycle it puts 0xFF on the data bus.
//
//   cc -std=c11 -Wall -Wextra -I.. -o interrupts interrupts.c && ./interrupts
#define CHIPS_IMPL
#include "i8080.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static uint8_t mem[1 << 16];
static i8080_t cpu;
static uint64_t pins;
static uint8_t status;

typedef struct { uint8_t status; uint16_t address; uint8_t data; int state; } Cycle;
static Cycle cycles[64];
static int num_cycles;
static int state;

// Run `ticks` states with INT held at `irq`. Records every bus cycle.
static void run(int ticks, bool irq) {
    for (int i = 0; i < ticks; i++) {
        uint64_t in = pins | I8080_READY;
        if (irq) {
            in |= I8080_INT;
        } else {
            in &= ~I8080_INT;
        }
        pins = i8080_tick(&cpu, in);
        uint16_t addr = I8080_GET_ADDR(pins);
        if (pins & I8080_SYNC) {
            status = I8080_GET_DATA(pins);
            if (num_cycles < 64) {
                cycles[num_cycles] = (Cycle){status, addr, 0, state};
            }
            num_cycles++;
        }
        if (pins & I8080_DBIN) {
            uint8_t data = mem[addr];
            if (status & I8080_STATUS_INTA) {
                data = 0xFF;    // the 8228 or 8238 jams RST 7
            }
            I8080_SET_DATA(pins, data);
            cycles[num_cycles - 1].data = data;
        }
        if ((pins & I8080_WR) && !(pins & I8080_WAIT)) {
            mem[addr] = I8080_GET_DATA(pins);
            cycles[num_cycles - 1].data = I8080_GET_DATA(pins);
        }
        state++;
    }
}

static void start(const uint8_t* program, int n) {
    memset(mem, 0, sizeof mem);
    memcpy(mem, program, (size_t)n);
    mem[0x38] = 0x76;       // the RST 7 handler: HLT
    pins = i8080_init(&cpu);
    cpu.sp = 0x1000;
    num_cycles = 0;
    state = 0;
}

static int find(uint8_t st) {
    for (int i = 0; i < num_cycles; i++) {
        if (cycles[i].status == st) {
            return i;
        }
    }
    return -1;
}

int main(void) {
    // 1. EI, then INT held high: the instruction after EI still runs, then
    //    the interrupt is acknowledged at the next boundary.
    {
        const uint8_t program[] = {0xFB, 0x00, 0x00, 0x00};   // EI; NOP; NOP; NOP
        start(program, sizeof program);
        run(4 + 4 + 11 + 4, true);
        int i = find(I8080_CYCLE_INTA);
        CHECK(i == 2, "interrupt acknowledged at bus cycle %d, expected 2 (after EI and one NOP)", i);
        if (i == 2) {
            const Cycle* c = cycles;
            CHECK(c[0].address == 0x0000 && c[1].address == 0x0001, "EI and NOP fetched first");
            // INTA M1 at the PC of the next instruction, not incremented; then
            // RST 7: 5-state M1, two stack writes (T1 at 5 and 8), 11 states
            CHECK(c[2].address == 0x0002 && c[2].data == 0xFF && c[2].state == 8, "INTA cycle %04X data %02X state %d",
                  c[2].address, c[2].data, c[2].state);
            CHECK(c[3].status == I8080_CYCLE_SWRITE && c[3].address == 0x0FFF && c[3].data == 0x00 &&
                  c[3].state == 8 + 5, "stack write PCH: %02X %04X=%02X at %d", c[3].status, c[3].address,
                  c[3].data, c[3].state);
            CHECK(c[4].status == I8080_CYCLE_SWRITE && c[4].address == 0x0FFE && c[4].data == 0x02 &&
                  c[4].state == 8 + 8, "stack write PCL: %02X %04X=%02X at %d", c[4].status, c[4].address,
                  c[4].data, c[4].state);
            CHECK(c[5].status == I8080_CYCLE_FETCH && c[5].address == 0x0038 && c[5].state == 8 + 11,
                  "handler fetch %04X at %d", c[5].address, c[5].state);
        }
        CHECK(!cpu.inte && !(pins & I8080_INTE), "INTE cleared by the acknowledge");
    }

    // 2. INTE off: INT is ignored.
    {
        const uint8_t program[] = {0xF3, 0x00, 0x00, 0x00, 0x00};  // DI; NOPs
        start(program, sizeof program);
        run(20, true);
        CHECK(find(I8080_CYCLE_INTA) < 0, "interrupt taken with INTE off");
    }

    // 3. INT only arrives after EI's successor: taken at the next boundary.
    {
        const uint8_t program[] = {0xFB, 0x00, 0x00, 0x00, 0x00};
        start(program, sizeof program);
        run(12, false);     // EI, NOP, NOP: 12 states
        run(20, true);
        int i = find(I8080_CYCLE_INTA);
        CHECK(i == 3 && cycles[i].address == 0x0003, "late INT: acknowledged at cycle %d", i);
    }

    // 4. EI; HLT; then INT: leaves the halt state with an "acknowledge while
    //    halted" cycle, and pushes the address after HLT.
    {
        const uint8_t program[] = {0xFB, 0x76};
        start(program, sizeof program);
        run(30, false);
        CHECK(cpu.step == I8080_HALT_TWH && (pins & I8080_WAIT), "halted after EI; HLT");
        CHECK(find(I8080_CYCLE_HALT) == 2, "halt acknowledge cycle");
        run(12, true);
        int i = find(I8080_CYCLE_INTA_HALT);
        CHECK(i == 3 && cycles[i].address == 0x0002 && cycles[i].data == 0xFF,
              "left halt by interrupt: cycle %d", i);
        CHECK(mem[0x0FFF] == 0x00 && mem[0x0FFE] == 0x02, "pushed %02X%02X, expected 0002", mem[0x0FFF],
              mem[0x0FFE]);
        run(4, false);
        CHECK(cycles[i + 3].status == I8080_CYCLE_FETCH && cycles[i + 3].address == 0x0038,
              "went on to the handler: %02X at %04X", cycles[i + 3].status, cycles[i + 3].address);
    }

    // 4b. INT already high when the halt cycle's T2 ends: HLT still takes its
    //     7 states (one TWH, Figure 2-4), so the acknowledge's T1 is state 11:
    //     EI 0-3, HLT M1 4-7, halt T1 8, T2 9, TWH 10.
    {
        const uint8_t program[] = {0xFB, 0x76};
        start(program, sizeof program);
        run(10, false);
        run(4, true);
        int i = find(I8080_CYCLE_INTA_HALT);
        CHECK(i == 3 && cycles[i].state == 11, "acknowledge from halt at state %d, expected 11",
              i >= 0 ? cycles[i].state : -1);
    }

    // 5. DI; HLT; INT: stays halted (only RESET gets out).
    {
        const uint8_t program[] = {0xF3, 0x76};
        start(program, sizeof program);
        run(30, true);
        CHECK(cpu.step == I8080_HALT_TWH && (pins & I8080_WAIT), "still halted");
        CHECK(find(I8080_CYCLE_INTA_HALT) < 0 && find(I8080_CYCLE_INTA) < 0, "no acknowledge");
        pins = i8080_reset(&cpu);
        CHECK(cpu.pc == 0 && cpu.step == I8080_M1_T1, "RESET restarts at 0");
    }

    // 6. A pending ALU result survives an interrupt: ADD's A lands in T2 of
    //    the acknowledge cycle.
    {
        const uint8_t program[] = {0x3E, 0x05, 0x06, 0x03, 0xFB, 0x00, 0x80, 0x00};  // MVI A,5; MVI B,3; EI; NOP; ADD B
        start(program, sizeof program);
        run(7 + 7 + 4 + 4 + 4, false);
        run(2, true);       // INTA T1, T2
        CHECK(cpu.a == 0x08, "A after ADD B, in the acknowledge's T2: %02X", cpu.a);
    }

    printf("interrupts: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
