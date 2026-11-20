// Run a CP/M .COM 8080 test program (TST8080, 8080PRE, 8080EXM, CPUTEST)
// on the cycle-stepped i8080.h, clock by clock.
//
// A minimal CP/M: the program loads at 0x0100; address 0x0000 holds
// OUT 0 (warm boot = the program is finished); address 0x0005 (the BDOS
// entry) holds OUT 1 and RET, and the host carries out BDOS function 2
// (print E) or 9 (print the '$'-terminated string at DE) when it sees the
// output to port 1. The word at 0x0006 (0xC901) is the top of memory
// programs take their stack from.
//
//   exerciser PROGRAM.COM [--waits]     --waits: READY randomly low
#define CHIPS_IMPL
#include "i8080.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mem[1 << 16];

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s PROGRAM.COM [--waits]\n", argv[0]);
        return 2;
    }
    bool waits = argc > 2 && strcmp(argv[2], "--waits") == 0;
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    size_t n = fread(mem + 0x100, 1, sizeof mem - 0x100, f);
    fclose(f);
    printf("%s: %zu bytes\n", argv[1], n);

    mem[0x0000] = 0xD3;     // OUT 0: finished
    mem[0x0001] = 0x00;
    mem[0x0005] = 0xD3;     // OUT 1: BDOS call
    mem[0x0006] = 0x01;
    mem[0x0007] = 0xC9;     // RET

    i8080_t cpu;
    uint64_t pins = i8080_init(&cpu);
    cpu.pc = 0x0100;
    uint64_t ticks = 0, wait_states = 0;
    uint64_t rng = 0x2545F4914F6CDD1DULL;
    uint8_t status = 0;
    bool done = false;
    while (!done) {
        uint64_t in = pins | I8080_READY;
        if (waits) {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            if ((rng & 7) == 0) {
                in &= ~I8080_READY;
            }
        }
        pins = i8080_tick(&cpu, in);
        ticks++;
        uint16_t addr = I8080_GET_ADDR(pins);
        if (pins & I8080_SYNC) {
            status = I8080_GET_DATA(pins);
        }
        if (pins & I8080_WAIT) {
            if (cpu.step == I8080_HALT_TWH) {
                printf("\nhalted at %04X\n", cpu.pc);
                break;
            }
            wait_states++;
        }
        if (pins & I8080_DBIN) {
            I8080_SET_DATA(pins, mem[addr]);
        }
        if ((pins & I8080_WR) && !(pins & I8080_WAIT)) {
            if (status & I8080_STATUS_OUT) {
                uint8_t port = (uint8_t)addr;
                if (port == 0) {
                    done = true;
                } else if (port == 1 && cpu.c == 2) {
                    putchar(cpu.e);
                } else if (port == 1 && cpu.c == 9) {
                    for (uint16_t p = cpu.de; mem[p] != '$'; p++) {
                        putchar(mem[p]);
                    }
                }
                fflush(stdout);
            } else {
                mem[addr] = I8080_GET_DATA(pins);
            }
        }
        if (cpu.unimplemented) {
            printf("\nunimplemented opcode %02X\n", cpu.opcode);
            return 1;
        }
    }
    printf("\n%s: %llu states (+%llu wait states)\n", argv[1], (unsigned long long)(ticks - wait_states),
           (unsigned long long)wait_states);
    return 0;
}
