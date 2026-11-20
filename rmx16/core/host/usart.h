// The program interface card's 8251 USART (E8, at FC00 data / FC01 control),
// enough for the RMX16's remote-terminal port. Behavioural.
//
// Interrupt wiring (hypothesis, see notes 2026-10-17-2130):
//   RxRDY (8251 pin 14, "INT") -> ribbon E1 pin 9 -> processor LKA/PAD1 ->
//     RST 5.5. The firmware's RST 5.5 handler (9362) reads FC00 and parses
//     remote commands.
//   TxEMPTY (8251 pin 18) -> progif finger B17 -> baseboard -> processor
//     finger B9 -> RST 7.5. The firmware's RST 7.5 handler (94A8) sends the
//     next byte of its transmit buffer.
// Character time is a parameter: the crystal on the program interface card
// (X1, unmarked) and so the baud rate are unknown.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    // control state: after reset the next control write is a mode word
    bool expect_mode;
    int sync_left;
    uint8_t mode, command;
    // transmitter
    bool tx_busy;               // a character is shifting out (TxEMPTY low)
    uint64_t tx_done_at;
    bool tx_holding;            // a character waits in the transmit buffer
    uint8_t tx_hold;
    // receiver
    bool rx_ready;
    uint8_t rx_data;
    const char* rx_script;      // bytes still to arrive
    uint64_t rx_next_at;
    uint64_t char_ticks;        // one character time in CPU states
    // a log of transmitted bytes
    uint8_t txlog[4096];
    int ntx;
} usart_t;

static void usart_init(usart_t* u, uint64_t char_ticks) {
    memset(u, 0, sizeof *u);
    u->expect_mode = true;
    u->char_ticks = char_ticks;
}

static bool usart_txempty(const usart_t* u) {
    return !u->tx_busy && !u->tx_holding;
}

static uint8_t usart_status(const usart_t* u) {
    uint8_t s = 0x80;                       // DSR (assumed asserted)
    if (!u->tx_holding) s |= 0x01;          // TxRDY
    if (u->rx_ready) s |= 0x02;             // RxRDY
    if (usart_txempty(u)) s |= 0x04;        // TxEMPTY
    return s;
}

static void usart_write(usart_t* u, uint16_t addr, uint8_t data, uint64_t now) {
    if (addr & 1) {
        if (u->expect_mode) {
            u->mode = data;
            u->expect_mode = false;
            // synchronous modes are followed by sync characters
            u->sync_left = (data & 3) == 0 ? ((data & 0x80) ? 1 : 2) : 0;
        } else if (u->sync_left > 0) {
            u->sync_left--;
        } else {
            u->command = data;
            if (data & 0x40) {              // internal reset
                u->expect_mode = true;
                u->tx_busy = u->tx_holding = u->rx_ready = false;
            }
            if (data & 0x10) {              // error reset
            }
        }
    } else {
        // data: into the holding buffer; the shifter takes it when free
        u->tx_hold = data;
        u->tx_holding = true;
        if (!u->tx_busy) {
            u->tx_holding = false;
            u->tx_busy = true;
            u->tx_done_at = now + u->char_ticks;
            if (u->ntx < (int)sizeof u->txlog) u->txlog[u->ntx++] = data;
        }
    }
}

static uint8_t usart_read(usart_t* u, uint16_t addr) {
    if (addr & 1) {
        return usart_status(u);
    }
    u->rx_ready = false;
    return u->rx_data;
}

// advance time; returns nothing, updates TxEMPTY / RxRDY
static void usart_tick(usart_t* u, uint64_t now) {
    if (u->tx_busy && now >= u->tx_done_at) {
        u->tx_busy = false;
        if (u->tx_holding) {
            u->tx_holding = false;
            u->tx_busy = true;
            u->tx_done_at = now + u->char_ticks;
            if (u->ntx < (int)sizeof u->txlog) u->txlog[u->ntx++] = u->tx_hold;
        }
    }
    if (u->rx_script && *u->rx_script && now >= u->rx_next_at && !u->rx_ready) {
        u->rx_data = (uint8_t)*u->rx_script++;
        u->rx_ready = true;
        u->rx_next_at = now + u->char_ticks;
    }
}
