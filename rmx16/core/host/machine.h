// The RMX16 as one machine: the processor board (DMX 23.5), the program
// interface card (SCN802-045), the front panel, the remote port's 8251 and
// the DSP (MCB 5A/5B, RAM 4, multipliers; host/dsp.h), stepped one CPU state
// at a time. Shared by the command-line host (rmx16.c) and the web build
// (web/web.c). The memory map is from the reconciled netlists
// (kicad/processor, kicad/progif; see notes/opus-5.5/2026-10-17-1700-*.md):
//
//   0000-07FF  boot ROM R316 (2716, E13)             processor E8 Y0
//   0800-0FFF  6116 battery RAM (E10)                processor E8 Y1
//   1000-1003  8255 E6 (front panel upper/lower)     processor E8 Y2
//   1800-1803  8255 E3                               processor E8 Y3
//   2000-27FF  ribbon pin 14 (no device)             processor E8 Y4
//   8000-BFFF  IC1-IC4 (2732, E2-E5)                 progif E14 (LK2 open)
//   F000-F3FF  2114 RAM x2 (E6, E7)                  progif E16 Y4
//   F800-F802  write: microcode loader latches       progif E16 Y6 -> E17
//              E9 (address), E10 (data), E11 (control, bit 7? = WR REQUEST)
//   FC00-FC01  8251 (A0 = C/D)                       progif E16 Y7
//
// Decoding is partial, as on the boards: each select covers its whole block
// (the 8255s and the 8251 see only A0/A1 or A0). Unmapped reads return 0xFF.
//
// The including file defines CHIPS_IMPL once (it brings in the i8085 and
// i8255 implementations).
#ifndef RMX16_MACHINE_H
#define RMX16_MACHINE_H
#include "../i8085-cycle/i8085.h"
#include "../vendor/chips/i8255.h"
#include "frontpanel.h"
#include "usart.h"
#include "dsp.h"

#define RMX_CPU_HZ 3072000      // 6.144 MHz crystal / 2
// DSP_RATE / RMX_CPU_HZ = 40000 / 3072000 = 5 / 384 exactly: one sample per 76.8 states
#define RMX_PHASE_STEP 5
#define RMX_PHASE_WRAP 384

typedef struct rmx rmx_t;
// optional peripheral-access log: kind 'r'/'w' (8255, 8251, microcode
// latches), '?' unmapped read, '!' write to ROM or unmapped space
typedef void (*rmx_log_fn)(rmx_t* m, char kind, uint16_t addr, uint8_t data);

struct rmx {
    i8085_t cpu;
    uint64_t pins;
    uint16_t addr;
    bool written;
    uint64_t ticks;
    uint16_t cur_pc;            // PC of the instruction being executed
    uint8_t boot[2048], eprom[4][4096], ram6116[2048], ram2114[1024];
    uint8_t latch[3];           // F800 address, F801 data, F802 control
    // MCB 5B microcode store: 64 steps x 32 bits, written a byte lane at a
    // time. The latched address's bits 5-0 select the step (RA0-RA5), bits
    // 7-6 the byte lane (74LS139 E16 -> the HM6551 pairs). A write request
    // is F802 bit 0 pulsed low (E11 Q -> WR REQUEST -> MCB 5A WRREQ); the
    // MCB does the write in its next free slot. Modelled as done when bit 0
    // goes low.
    uint8_t ucode[64][4];
    uint32_t ucode_writes;
    uint64_t ucode_last_write;
    i8255_t ppi_e6, ppi_e3;
    fp_t fp;
    usart_t usart;
    dsp_t dsp;
    uint32_t dsp_phase;
    uint64_t samples;           // DSP samples run
    bool sample_due;            // set by rmx_tick when a sample ran this state
    rmx_log_fn log;
    void* user;
};

static inline void rmx_logacc(rmx_t* m, char kind, uint16_t addr, uint8_t data) {
    if (m->log) m->log(m, kind, addr, data);
}

// Port inputs come from the front panel's data bus (ribbon FP E34 -> E5):
// E6 PB0-7 = D0-7; D6 and D7 also reach E3 PC5 and PC6 (processor nets PC5
// and PC6 include E6.24 and E6.25). Everything else floats high.
static inline uint8_t rmx_ppi_access(rmx_t* m, i8255_t* ppi, uint16_t addr, bool wr, uint8_t data) {
    uint64_t p = I8255_CS | (wr ? I8255_WR : I8255_RD);
    if (addr & 1) p |= I8255_A0;
    if (addr & 2) p |= I8255_A1;
    I8255_SET_DATA(p, data);
    p |= I8255_PA_PINS | I8255_PB_PINS | I8255_PC_PINS;
    if (!wr) {
        uint8_t bus = fp_read_bus(&m->fp, m->ppi_e3.pa.outp);
        if (ppi == &m->ppi_e6) {
            I8255_SET_PB(p, bus);
        } else {
            uint8_t pc = 0xFF & ~0x60;
            pc |= (uint8_t)(((bus >> 6) & 1) << 5 | ((bus >> 7) & 1) << 6);
            I8255_SET_PC(p, pc);
        }
    }
    p = i8255_tick(ppi, p);
    if (wr) {
        // the front panel sees E3 PA (control) and E6 PA (data)
        fp_write_ports(&m->fp, m->ppi_e3.pa.outp, m->ppi_e6.pa.outp);
    }
    return I8255_GET_DATA(p);
}

static inline uint8_t rmx_mem_read(rmx_t* m, uint16_t addr) {
    if (addr < 0x0800) return m->boot[addr];
    if (addr < 0x1000) return m->ram6116[addr & 0x7FF];
    if (addr < 0x1800) { uint8_t d = rmx_ppi_access(m, &m->ppi_e6, addr, false, 0); rmx_logacc(m, 'r', addr, d); return d; }
    if (addr < 0x2000) { uint8_t d = rmx_ppi_access(m, &m->ppi_e3, addr, false, 0); rmx_logacc(m, 'r', addr, d); return d; }
    if (addr >= 0x8000 && addr < 0xC000) return m->eprom[(addr - 0x8000) >> 12][addr & 0xFFF];
    if (addr >= 0xF000 && addr < 0xF400) return m->ram2114[addr & 0x3FF];
    if (addr >= 0xFC00) { uint8_t d = usart_read(&m->usart, addr); rmx_logacc(m, 'r', addr, d); return d; }
    rmx_logacc(m, '?', addr, 0xFF);
    return 0xFF;
}

static inline void rmx_mem_write(rmx_t* m, uint16_t addr, uint8_t data) {
    if (addr >= 0x0800 && addr < 0x1000) { m->ram6116[addr & 0x7FF] = data; return; }
    if (addr >= 0x1000 && addr < 0x1800) { rmx_ppi_access(m, &m->ppi_e6, addr, true, data); rmx_logacc(m, 'w', addr, data); return; }
    if (addr >= 0x1800 && addr < 0x2000) { rmx_ppi_access(m, &m->ppi_e3, addr, true, data); rmx_logacc(m, 'w', addr, data); return; }
    if (addr >= 0xF000 && addr < 0xF400) { m->ram2114[addr & 0x3FF] = data; return; }
    if (addr >= 0xF800 && addr < 0xFC00) {
        if ((addr & 7) == 2 && (m->latch[2] & 1) && !(data & 1)) {
            m->ucode[m->latch[0] & 0x3F][m->latch[0] >> 6] = m->latch[1];
            m->ucode_writes++;
            m->ucode_last_write = m->ticks;
        }
        if ((addr & 7) < 3) m->latch[addr & 7] = data;
        rmx_logacc(m, 'w', addr, data);
        return;
    }
    if (addr >= 0xFC00) { usart_write(&m->usart, addr, data, m->ticks); rmx_logacc(m, 'w', addr, data); return; }
    rmx_logacc(m, '!', addr, data);
}

// Reset everything except the ROMs and the battery RAM. char_ticks = one
// serial character time in CPU states (the remote port's baud rate is unknown).
static inline void rmx_reset(rmx_t* m, uint64_t char_ticks) {
    i8255_init(&m->ppi_e6);
    i8255_init(&m->ppi_e3);
    fp_init(&m->fp);
    usart_init(&m->usart, char_ticks);
    dsp_init(&m->dsp);
    memset(m->ucode, 0, sizeof m->ucode);
    memset(m->latch, 0, sizeof m->latch);
    memset(m->ram2114, 0, sizeof m->ram2114);
    m->ucode_writes = 0;
    m->ucode_last_write = 0;
    m->ticks = 0;
    m->samples = 0;
    m->dsp_phase = 0;
    m->addr = 0;
    m->written = false;
    m->pins = i8085_init(&m->cpu);
}

// One CPU state. in = the DSP input used if a sample falls in this state
// (sets m->sample_due; the DACs are then m->dsp.dac_a / dac_b). Returns false
// if the CPU stopped (unimplemented opcode, or halted with interrupts off).
static inline bool rmx_tick(rmx_t* m, int16_t in) {
    m->sample_due = false;
    m->dsp_phase += RMX_PHASE_STEP;
    if (m->dsp_phase >= RMX_PHASE_WRAP) {
        m->dsp_phase -= RMX_PHASE_WRAP;
        dsp_sample(&m->dsp, (const uint8_t (*)[4])m->ucode, in);
        m->samples++;
        m->sample_due = true;
    }
    if (i8085_opdone(&m->cpu)) m->cur_pc = m->cpu.pc;
    usart_tick(&m->usart, m->ticks);
    uint64_t irq = 0;
    if (m->usart.rx_ready) irq |= I8085_RST55;         // RxRDY -> RST 5.5 (link)
    if (usart_txempty(&m->usart)) irq |= I8085_RST75;  // TxEMPTY -> RST 7.5 (via the baseboard)
    m->pins = i8085_tick(&m->cpu, (m->pins & ~(I8085_TRAP | I8085_RST75 | I8085_RST65 | I8085_RST55)) | irq | I8085_READY);
    if (m->cpu.unimplemented) return false;
    if (m->pins & I8085_ALE) {
        m->addr = I8085_GET_ADDR(m->pins);
        m->written = false;
    }
    if ((m->pins & I8085_RD) && !(m->pins & I8085_IOM)) {
        I8085_SET_AD(m->pins, rmx_mem_read(m, m->addr));
    }
    if ((m->pins & I8085_WR) && !m->written && !(m->pins & I8085_IOM)) {
        m->written = true;
        rmx_mem_write(m, m->addr, I8085_GET_AD(m->pins));
    }
    m->ticks++;
    return !(i8085_halted(&m->cpu) && !m->cpu.inte);
}

// Run to the next DSP sample; false if the CPU stopped.
static inline bool rmx_run_sample(rmx_t* m, int16_t in) {
    do {
        if (!rmx_tick(m, in)) return false;
    } while (!m->sample_due);
    return true;
}
#endif
