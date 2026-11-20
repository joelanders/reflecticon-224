// The disassembler's lengths (i8080dasm.h) against the core (i8080.h): for
// every opcode, run it once and count the memory reads at the bytes after
// the opcode. The core must fetch exactly length - 1 of them.
//
//   cc -std=c11 -Wall -Wextra -I.. -o dasm dasm.c && ./dasm
#define CHIPS_IMPL
#include "i8080.h"
#include "i8080dasm.h"
#include <stdio.h>
#include <string.h>

// LXI SP,8000h; LXI H,9000h; JMP 1000h: the stack and HL point far from
// the instruction under test, which sits at 1000h with 5Ah 41h after it.
static const uint8_t prelude[] = {0x31, 0x00, 0x80, 0x21, 0x00, 0x90, 0xC3, 0x00, 0x10};
static const uint16_t at = 0x1000;

static int operand_reads(uint8_t opcode) {
    static uint8_t mem[1 << 16];
    memset(mem, 0, sizeof mem);
    memcpy(mem, prelude, sizeof prelude);
    mem[at] = opcode;
    mem[at + 1] = 0x5A;
    mem[at + 2] = 0x41;
    i8080_t cpu;
    uint64_t pins = i8080_init(&cpu);
    int instruction = -1, reads = 0;
    for (int tick = 0; tick < 200 && instruction < 4; tick++) {
        if (i8080_opdone(&cpu)) {
            instruction++;
        }
        pins = i8080_tick(&cpu, pins | I8080_READY);
        if ((pins & I8080_SYNC) && instruction == 3) {
            uint16_t address = I8080_GET_ADDR(pins);
            bool memory_read = I8080_GET_DATA(pins) == I8080_CYCLE_MREAD;
            if (memory_read && (address == at + 1 || address == at + 2)) {
                reads++;
            }
        }
        if (pins & I8080_DBIN) {
            I8080_SET_DATA(pins, mem[I8080_GET_ADDR(pins)]);
        }
        if (pins & I8080_WR) {
            mem[I8080_GET_ADDR(pins)] = I8080_GET_DATA(pins);
        }
    }
    return reads;
}

int main(void) {
    int failures = 0;
    for (int opcode = 0; opcode < 256; opcode++) {
        int reads = operand_reads((uint8_t)opcode);
        if (reads != i8080dasm_length((uint8_t)opcode) - 1) {
            failures++;
            printf("FAIL %02X: the core reads %d operand bytes, the table says %d\n", opcode, reads,
                   i8080dasm_length((uint8_t)opcode) - 1);
        }
    }
    uint8_t bytes[3] = {0x32, 0x23, 0x41};
    char text[24];
    int length = i8080dasm(bytes, text, sizeof text);
    if (length != 3 || strcmp(text, "STA 4123") != 0) {
        failures++;
        printf("FAIL STA: %d \"%s\"\n", length, text);
    }
    if (failures) {
        printf("dasm: %d failures\n", failures);
        return 1;
    }
    printf("dasm: ok (256 opcodes' lengths match the core's operand fetches)\n");
    return 0;
}
