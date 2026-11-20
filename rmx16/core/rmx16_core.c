/* The C side of rmx16_core.h: the RMX16 project's emu/host/machine.h, unchanged (SOURCE.md). */
#define CHIPS_IMPL
#include "host/machine.h"
#include "rmx16_core.h"
#include <stdlib.h>

struct rmxc {
    rmx_t m;
};

rmxc_t* rmxc_create(void) { return (rmxc_t*)calloc(1, sizeof(rmxc_t)); }

void rmxc_destroy(rmxc_t* m) { free(m); }

int rmxc_load(rmxc_t* m, int slot, const uint8_t* data, int len) {
    if (slot == 0) {
        if (len != 2048 && len != 4096) return 0;
        memcpy(m->m.boot, data, 2048);
        return 1;
    }
    if (slot < 1 || slot > 4 || len != 4096) return 0;
    memcpy(m->m.eprom[slot - 1], data, 4096);
    return 1;
}

void rmxc_power(rmxc_t* m) { rmx_reset(&m->m, (uint64_t)(0.001 * RMX_CPU_HZ)); }

int rmxc_run_sample(rmxc_t* m, int16_t in, int16_t* a, int16_t* b) {
    if (!rmx_run_sample(&m->m, in)) return 0;
    *a = m->m.dsp.dac_a;
    *b = m->m.dsp.dac_b;
    return 1;
}

void rmxc_key(rmxc_t* m, int code, int down) {
    char name[8];
    if (code >= 1 && code <= 10) snprintf(name, sizeof name, "S%d", code);
    else if (code >= 100 && code < 140) snprintf(name, sizeof name, "M%d%d", (code - 100) / 10, (code - 100) % 10);
    else return;
    fp_key(&m->m.fp, name, down != 0);
}

void rmxc_pot(rmxc_t* m, int which, int value) {
    if (which >= 0 && which < 3) m->m.fp.pot[which] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

void rmxc_panel(const rmxc_t* m, rmxc_panel_t* out) {
    const fp_t* fp = &m->m.fp;
    memcpy(out->seg, fp->seg, RMXC_NDIG);
    memcpy(out->lit, fp->lit, sizeof out->lit);
    out->signs = fp->e23;
    memcpy(out->alpha, fp->alpha, 8);
    char leds[11];
    fp_leds(fp, leds);
    out->leds = 0;
    for (int i = 0; i < 10; i++)
        if (leds[i] == '*') out->leds |= (uint16_t)(1u << i);
}

uint8_t* rmxc_battery(rmxc_t* m) { return m->m.ram6116; }

const uint8_t* rmxc_ucode(const rmxc_t* m) { return &m->m.ucode[0][0]; }
