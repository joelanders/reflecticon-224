#pragma once
// The SBC's two peripheral chips, as the firmware sees them: the 8255 that
// talks to the 224X front panel (display digits, buttons, pots) and the 8251
// serial port that talks to the 224XL's LARC remote. Both machines that run
// the firmware (the board-level machine and the emulator) use this file.
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace lexicon224x::sbc {

// Parallel panel and its 8255 latches. Pot/switch positions are external
// controls and survive reset. Display storage and conversion state do not.
struct Panel {
    std::array<std::uint8_t, 9> digits{};
    std::array<std::uint8_t, 6> pots{};
    std::array<std::uint8_t, 3> switches{};
    std::uint8_t mode{}, port_a{}, port_b{}, port_c{}, address{}, controls{}, converted{}, waiting_value{};
    std::optional<std::uint64_t> conversion_time;

    Panel() { pots.fill(128); switches.fill(255); reset(); }
    void reset() {
        digits.fill(0); address = 15; controls = converted = waiting_value = 0;
        mode = 0x9b; port_a = port_b = port_c = 0; conversion_time.reset();
    }
    void settle(std::uint64_t cycle) {
        if (conversion_time && cycle >= *conversion_time) {
            converted = waiting_value;
            conversion_time.reset();
        }
    }
    void drive_controls(std::uint8_t pins, std::uint64_t cycle) {
        settle(cycle);
        if (!(controls & 1) && (pins & 1) && address < pots.size()) {
            waiting_value = pots[address];
            conversion_time = cycle + 263;
        }
        controls = pins;
    }
    void write(std::uint8_t port, std::uint8_t value, std::uint64_t cycle) {
        switch (port) {
        case 0xe4:
            port_a = value;
            if (address < digits.size()) digits[address] = ~value;
            break;
        case 0xe5: port_b = value; address = ~value & 15; break;
        case 0xe6:
            port_c = (mode & 0x60) == 0x40 ? (port_c & 0xf8) | (value & 7) : value;
            drive_controls(port_c, cycle);
            break;
        case 0xe7:
            if (value & 0x80) {
                mode = value; port_a = port_b = port_c = 0; address = 15;
            } else {
                unsigned bit = (value >> 1) & 7;
                if ((mode & 0x60) == 0x40 && bit >= 3)
                    throw std::runtime_error("8255 mode-2 handshake bit is outside the modeled boundary");
                port_c = value & 1 ? port_c | (1u << bit) : port_c & ~(1u << bit);
            }
            drive_controls(port_c, cycle);
            break;
        default: throw std::runtime_error("Unconnected panel write");
        }
    }
    std::uint8_t read(std::uint8_t port, std::uint64_t cycle) {
        settle(cycle);
        if (port == 0xe5) return port_b;
        if (port == 0xe6) return port_c;
        if (port != 0xe4) throw std::runtime_error("Unconnected panel read");
        std::uint8_t cable_byte = 255;
        if (controls & 2) {
            if (address < 6) cable_byte = converted;
            else if (address < 9) cable_byte = switches[address - 6];
        }
        return ~cable_byte;
    }
};

// The inherited serial boundary exchanges complete 8N2 characters with LARC.
// It preserves character deadlines, receiver overrun, interrupts and protocol
// state; it does not claim to simulate individual UART bits.
struct Remote {
    static constexpr std::uint64_t character_cycles = 2292;
    std::array<char, 48> display{};
    std::array<std::uint8_t, 4> indicators{};
    std::array<std::uint8_t, 256> queue{};
    std::optional<std::uint64_t> transmit_time, receive_time;
    std::optional<std::uint8_t> received;
    std::uint64_t cycle = 0;
    std::uint8_t mode = 0, command = 0, transmitting = 0, errors = 0;
    unsigned head = 0, count = 0, cursor = 0, indicator_bytes = 0, unknown_commands = 0;
    bool expecting_mode = true, text = false, connected = false;

    void reset() {
        display.fill(' '); indicators.fill(0);
        transmit_time.reset(); receive_time.reset(); received.reset(); cycle = 0;
        mode = command = transmitting = errors = 0;
        head = count = cursor = indicator_bytes = unknown_commands = 0;
        expecting_mode = true; text = connected = false;
    }
    std::optional<std::uint64_t> next_event() const {
        if (transmit_time && receive_time) return std::min(*transmit_time, *receive_time);
        return transmit_time ? transmit_time : receive_time;
    }
    bool interrupt_pending() const {
        return ((command & 4) && received) || ((command & 1) && !transmit_time);
    }
    std::uint8_t status() const { return (transmit_time ? 0 : 5) | (received ? 2 : 0) | errors; }
    std::uint8_t read() { auto value = received.value_or(0); received.reset(); return value; }
    void control(std::uint8_t value) {
        if (expecting_mode) { mode = value; expecting_mode = false; return; }
        if (value & 0x40) {
            mode = command = transmitting = errors = 0; expecting_mode = true;
            transmit_time.reset(); receive_time.reset(); received.reset(); head = count = 0;
        } else { command = value; if (value & 0x10) errors = 0; }
    }
    void write(std::uint8_t value) {
        if (!(command & 1)) return;
        if (transmit_time) throw std::runtime_error("8251 write while transmitter is busy");
        transmitting = value;
        transmit_time = cycle + character_cycles;
    }
    bool enqueue(std::uint8_t value) {
        if (count == queue.size()) return false;
        queue[(head + count++) % queue.size()] = value;
        if (!receive_time) receive_time = cycle + character_cycles;
        return true;
    }
    bool key(std::uint8_t make, bool down) {
        return make >= 0x20 && make < 0x40 && enqueue(down ? make : make & ~0x20);
    }
    bool fader(unsigned slot, std::uint8_t value) {
        if (slot >= 6 || value < 2 || value > 254 || queue.size() - count < 2) return false;
        enqueue(0x40 | slot); enqueue(value); return true;
    }
    void receive_command(std::uint8_t value) {
        if (indicator_bytes) { indicators[4 - indicator_bytes--] = value; return; }
        if (value == 0xe0) {
            display.fill(' '); indicators.fill(0); cursor = indicator_bytes = unknown_commands = 0;
            text = false; connected = true;
            if (!enqueue(0xc8)) throw std::runtime_error("LARC reply queue full");
        } else if (value == 0xe3 || value < 0x80) return;
        else if (value == 0x83) text = false;
        else if (value == 0x80 || value == 0x8c) { cursor = value & 0x1f; text = true; }
        else if (text && value >= 0xa0 && value <= 0xdf) {
            if (cursor < display.size()) display[cursor++] = char(value & 0x7f);
        } else if (value == 0xc0) indicator_bytes = 4;
        else unknown_commands++;
    }
    void advance(std::uint64_t deadline) {
        while (auto event = next_event()) {
            if (*event > deadline) break;
            cycle = *event;
            if (transmit_time == event) { transmit_time.reset(); receive_command(transmitting); }
            if (receive_time == event) {
                if (command & 4) {
                    if (received) errors |= 0x10;
                    else received = queue[head];
                }
                head = (head + 1) % queue.size();
                receive_time = --count ? std::optional(cycle + character_cycles) : std::nullopt;
            }
        }
        cycle = deadline;
    }
};

}  // namespace lexicon224x::sbc
