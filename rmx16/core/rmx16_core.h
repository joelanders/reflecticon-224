/* The RMX16 machine (host/machine.h: 8085 firmware, front panel, DSP, at one CPU state per step)
 * behind a small C API, so the C++ engine never sees the C headers. Built once as C
 * (rmx16_core.c). No global state: one rmxc_t per instance. */
#ifndef RMX16_CORE_H
#define RMX16_CORE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rmxc rmxc_t;

#define RMXC_RATE 40000      /* DSP samples per second (5.12 MHz master clock / 128) */
#define RMXC_NDIG 13         /* 7-segment digits, left to right: E39 E5 E6 ... E15 E40 */

/* allocates; NULL if out of memory */
rmxc_t* rmxc_create(void);
void rmxc_destroy(rmxc_t* m);
/* slot 0 = R316 boot ROM (2 KB, or a 4 KB dump that repeats it), 1-4 = IC1-IC4 (4 KB).
 * 1 if the length fits. */
int rmxc_load(rmxc_t* m, int slot, const uint8_t* data, int len);
/* power on; the battery RAM keeps its contents */
void rmxc_power(rmxc_t* m);
/* one DSP sample with input `in`; the DACs into *a, *b. 0 if the CPU stopped. */
int rmxc_run_sample(rmxc_t* m, int16_t in, int16_t* a, int16_t* b);
/* keys: 1-10 = S1-S10 (pot, program, predelay, decay, LF, HF, save, recall, nudge down, nudge
 * up); 100 + 10*col + row = keypad (col 0 = 1 4 7 *, 1 = 2 5 8 0, 2 = 3 6 9 #, 3 = A-D; row 1-4) */
void rmxc_key(rmxc_t* m, int code, int down);
/* pots: 0 DELAY, 1 INPUT, 2 OUTPUT; 0-255 */
void rmxc_pot(rmxc_t* m, int which, int value);

typedef struct {
    uint8_t seg[RMXC_NDIG];   /* last pattern each digit was lit with: bit0 a .. bit6 g, bit7 dp */
    uint32_t lit[RMXC_NDIG];  /* how many times each digit has been lit (it multiplexes at ~50 Hz) */
    uint8_t signs;            /* E23: 0x40 E11 minus bar, 0x20 E11 vertical bar, 0x08 / 0x04 E13 */
    char alpha[8];            /* the DL1414 characters, left to right */
    uint16_t leds;            /* bit i = LED i+1 lit */
} rmxc_panel_t;
void rmxc_panel(const rmxc_t* m, rmxc_panel_t* out);

/* the 6116 battery RAM (2 KB) */
uint8_t* rmxc_battery(rmxc_t* m);
/* the MCB 5B microcode store, 64 x 4 bytes */
const uint8_t* rmxc_ucode(const rmxc_t* m);

#ifdef __cplusplus
}
#endif
#endif
