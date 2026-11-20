// Smoke test for the first instructions of i8080.h, checked against the
// Intel User's Manual (1975): state counts from Table 2-2, status words
// from Table 2-1, pin levels from Figures 2-5 to 2-7 and 2-11.
//
//   cc -std=c11 -Wall -Wextra -I.. -o smoke smoke.c && ./smoke
#define CHIPS_IMPL
#include "i8080.h"
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static const uint8_t program[] = {
    0x31, 0x00, 0x20,   // 0000 LXI SP,2000h   10 states
    0x06, 0x12,         // 0003 MVI B,12h       7
    0x3E, 0x34,         // 0005 MVI A,34h       7
    0x21, 0x00, 0x30,   // 0007 LXI H,3000h    10
    0x36, 0x56,         // 000A MVI M,56h      10
    0x32, 0x01, 0x30,   // 000C STA 3001h      13
    0xC3, 0x20, 0x00,   // 000F JMP 0020h      10
};
static const uint8_t at_0020[] = {
    0x00,               // 0020 NOP             4
    0x76,               // 0021 HLT             4 + M2 (T1, T2), then TWH
};
static const int expected_states[] = {10, 7, 7, 10, 10, 13, 10, 4};
#define NUM_INSTRUCTIONS 8

// One bus cycle as seen at T1.
typedef struct { uint8_t status; uint16_t address; } Cycle;
static const Cycle expected_cycles[] = {
    {I8080_CYCLE_FETCH, 0x0000}, {I8080_CYCLE_MREAD, 0x0001}, {I8080_CYCLE_MREAD, 0x0002},
    {I8080_CYCLE_FETCH, 0x0003}, {I8080_CYCLE_MREAD, 0x0004},
    {I8080_CYCLE_FETCH, 0x0005}, {I8080_CYCLE_MREAD, 0x0006},
    {I8080_CYCLE_FETCH, 0x0007}, {I8080_CYCLE_MREAD, 0x0008}, {I8080_CYCLE_MREAD, 0x0009},
    {I8080_CYCLE_FETCH, 0x000A}, {I8080_CYCLE_MREAD, 0x000B}, {I8080_CYCLE_MWRITE, 0x3000},
    {I8080_CYCLE_FETCH, 0x000C}, {I8080_CYCLE_MREAD, 0x000D}, {I8080_CYCLE_MREAD, 0x000E},
    {I8080_CYCLE_MWRITE, 0x3001},
    {I8080_CYCLE_FETCH, 0x000F}, {I8080_CYCLE_MREAD, 0x0010}, {I8080_CYCLE_MREAD, 0x0011},
    {I8080_CYCLE_FETCH, 0x0020},
    {I8080_CYCLE_FETCH, 0x0021}, {I8080_CYCLE_HALT, 0x0022},
};
#define NUM_CYCLES ((int)(sizeof expected_cycles / sizeof expected_cycles[0]))

// Run the program until halted. `ready` decides READY for each tick, given
// how many ticks ago the last T1 was (T2 = 1, the first READY check = 2).
// Returns the number of wait states seen.
static int run(bool (*ready)(int since_t1), bool check_levels) {
    static uint8_t mem[1 << 16];
    for (int i = 0; i < (1 << 16); i++) {
        mem[i] = 0;
    }
    for (unsigned i = 0; i < sizeof program; i++) {
        mem[i] = program[i];
    }
    for (unsigned i = 0; i < sizeof at_0020; i++) {
        mem[0x20 + i] = at_0020[i];
    }

    i8080_t cpu;
    uint64_t pins = i8080_init(&cpu);
    int instruction = -1, states_in_instruction = 0, waits = 0, waits_in_instruction = 0;
    int cycle = 0, halted_ticks = 0, since_t1 = 0;
    // T2 is the state after T1 (SYNC); TW follows T2 or another TW.
    bool previous_was_t1 = false, previous_was_t2_or_tw = false;
    for (int tick = 0; tick < 1000 && halted_ticks < 5; tick++) {
        if (i8080_opdone(&cpu)) {
            if (instruction >= 0 && instruction < NUM_INSTRUCTIONS) {
                CHECK(states_in_instruction - waits_in_instruction == expected_states[instruction],
                      "instruction %d took %d states (+%d waits), expected %d", instruction,
                      states_in_instruction - waits_in_instruction, waits_in_instruction,
                      expected_states[instruction]);
            }
            instruction++;
            states_in_instruction = 0;
            waits_in_instruction = 0;
        }
        uint64_t in = pins;
        since_t1++;
        bool r = ready(since_t1);
        if (r) {
            in |= I8080_READY;
        } else {
            in &= ~I8080_READY;
        }
        pins = i8080_tick(&cpu, in);
        states_in_instruction++;

        if (pins & I8080_SYNC) {
            since_t1 = 0;
            // T1: the status word is on the data pins
            CHECK(cycle < NUM_CYCLES, "more bus cycles than expected");
            if (cycle < NUM_CYCLES) {
                CHECK(I8080_GET_DATA(pins) == expected_cycles[cycle].status &&
                      I8080_GET_ADDR(pins) == expected_cycles[cycle].address,
                      "bus cycle %d: status %02X at %04X, expected %02X at %04X", cycle,
                      I8080_GET_DATA(pins), I8080_GET_ADDR(pins), expected_cycles[cycle].status,
                      expected_cycles[cycle].address);
            }
            cycle++;
        }
        bool halt_cycle = cycle == NUM_CYCLES && expected_cycles[cycle - 1].status == I8080_CYCLE_HALT;
        bool is_tw = (pins & I8080_WAIT) && previous_was_t2_or_tw && !halt_cycle;
        if (halt_cycle && (pins & I8080_WAIT)) {
            // TWH: straight after the halt cycle's T2, with no READY check (Figure 2-11)
            CHECK(!previous_was_t1, "halted in the halt cycle's T2");
            halted_ticks++;
        } else if (pins & I8080_WAIT) {
            // a TW: only after T2 or another TW, and only when READY was low
            CHECK(is_tw && !r, "WAIT outside a wait state at tick %d", tick);
            waits++;
            waits_in_instruction++;
        }
        if (check_levels) {
            CHECK(!((pins & I8080_DBIN) && (pins & I8080_WR)), "DBIN and WR together");
            CHECK(!((pins & I8080_SYNC) && (pins & (I8080_DBIN | I8080_WR))), "SYNC with DBIN or WR");
            if (previous_was_t1 && !halt_cycle) {
                // T2: a read raises DBIN; a write has its data on D but no WR/ yet
                uint8_t status = expected_cycles[cycle - 1].status;
                if (status & I8080_STATUS_WO) {
                    CHECK(pins & I8080_DBIN, "read T2 without DBIN at tick %d", tick);
                } else {
                    CHECK(!(pins & (I8080_DBIN | I8080_WR)), "write T2 with DBIN or WR at tick %d", tick);
                }
            }
        }
        previous_was_t2_or_tw = previous_was_t1 || is_tw;
        previous_was_t1 = (pins & I8080_SYNC) != 0;

        // the system side: memory
        if (pins & I8080_DBIN) {
            I8080_SET_DATA(pins, mem[I8080_GET_ADDR(pins)]);
        }
        if ((pins & I8080_WR) && !(pins & I8080_WAIT)) {
            mem[I8080_GET_ADDR(pins)] = I8080_GET_DATA(pins);
        }
    }
    CHECK(cycle == NUM_CYCLES, "saw %d bus cycles, expected %d", cycle, NUM_CYCLES);
    CHECK(halted_ticks == 5, "did not halt");
    CHECK(!cpu.unimplemented, "hit an unimplemented opcode");
    CHECK(cpu.sp == 0x2000 && cpu.b == 0x12 && cpu.a == 0x34 && cpu.hl == 0x3000 && cpu.pc == 0x0022,
          "registers: SP=%04X B=%02X A=%02X HL=%04X PC=%04X", cpu.sp, cpu.b, cpu.a, cpu.hl, cpu.pc);
    CHECK(mem[0x3000] == 0x56 && mem[0x3001] == 0x34, "memory: [3000]=%02X [3001]=%02X",
          mem[0x3000], mem[0x3001]);
    return waits;
}

static bool always_ready(int since_t1) {
    (void)since_t1;
    return true;
}

// READY low for the first two checks of every bus cycle: two TWs each
static bool two_waits(int since_t1) {
    return since_t1 >= 4;
}

int main(void) {
    int waits = run(always_ready, true);
    CHECK(waits == 0, "wait states with READY always high: %d", waits);
    // every bus cycle but the halt cycle (which goes straight to TWH) gets two
    waits = run(two_waits, true);
    CHECK(waits == 2 * (NUM_CYCLES - 1), "%d wait states, expected %d", waits, 2 * (NUM_CYCLES - 1));
    printf("smoke: %s (%d wait states in the READY-low run)\n", failures ? "FAILED" : "ok", waits);
    return failures ? 1 : 0;
}
