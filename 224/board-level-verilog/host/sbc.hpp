#pragma once
// Host side of SBC: the cycle-stepped 8080 (one clock state per tick, its
// pins in and out), ROM/RAM, and the byte-level panel/8251 peripherals.
// External DSP transactions go through sbc.sv, which returns the wait states
// the backplane's XACK/ inserts. No DSP arithmetic, memory or conversion
// executes here.
#include "Vmachine_host.h"
#include "Vmachine_host_machine_host.h"
#include "verilated.h"
// The 8080 core is compiled into this program (its functions are static).
#define I8080_API static
#define CHIPS_IMPL
#include "../../8080/i8080.h"
#undef CHIPS_IMPL
#include "../../sbc/memory_map.hpp"
#include "../../sbc/peripherals.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace lex224x {
using Tick = std::uint64_t;
constexpr Tick cpu_period = 281250;
constexpr Tick row_period = 168750;
constexpr Tick first_marker = 207531;
constexpr Tick ticks_per_second = 576000000000ULL;

// The SBC's front panel (8255), LARC serial (8251) and memory map are shared
// with the emulator (../../sbc/).
using lexicon224x::sbc::Panel;
using lexicon224x::sbc::Remote;
using lexicon224x::sbc::is_wcs;
using lexicon224x::sbc::is_sbc_port;

struct AudioPins {
    std::uint16_t adc = 0;
    std::uint8_t gain = 0, levels_n = 31;
};
struct AudioEvent {
    Tick time;
    std::array<std::uint16_t, 3> value, known, high_z; // channel, DAC, gain
    Tick source_time = 0;
    std::uint8_t source_drivers = 0;
    std::uint16_t source_overlap = 0;
};
struct LexCpuSnapshot {
    std::uint16_t pc, sp, bc, de, hl;
    std::uint8_t a, flags, interrupt_enabled;
};
struct WcsWrite {
    std::uint64_t cpu_t1;
    std::uint16_t writer_pc, address;
    std::uint8_t value;
    Tick committed_at;
};

class Machine {
public:
    std::array<std::uint8_t, 65536> memory;
    std::uint8_t dip_switches = 255;
    Panel panel;
    Remote remote;
    std::uint64_t cycles = 0;
    bool halted = false;
    std::function<void(const AudioEvent&)> audio_observer;
    std::function<void(const WcsWrite&)> wcs_observer;
    std::array<bool, 65536> pc_watches{};
    std::function<void(std::uint64_t, LexCpuSnapshot)> pc_observer;
    std::unique_ptr<VerilatedContext> context;
    std::unique_ptr<Vmachine_host> boards;

    Machine() {
        memory.fill(255);
        reset();
    }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    void reset() {
        pins = i8080_init(&cpu); bus = Bus{}; cycles = 0; halted = false;
        std::fill(memory.begin() + 0x2800, memory.begin() + 0x8000, 255);
        panel.reset(); remote.reset(); observed_audio_count = 0; input_events.clear();
        boards.reset(); context = std::make_unique<VerilatedContext>(); context->threads(1);
        boards = std::make_unique<Vmachine_host>(context.get());
        set_audio(audio_left, audio_right);
    }
    LexCpuSnapshot snapshot() const {
        i8080_t r = i8080_registers(&cpu);
        return {r.pc, r.sp, r.bc, r.de, r.hl, r.a, std::uint8_t((r.f & 0xd5) | 2), std::uint8_t(r.inte)};
    }
    // INTE set, and not straight after EI (the core applies the same rule)
    bool interrupt_enabled() const { return cpu.inte && cpu.opcode != 0xfb; }
    std::uint8_t peek(std::uint16_t address) const {
        if (is_wcs(address)) return boards->machine_host->peek_wcs(address);
        if (address == lexicon224x::sbc::dip_switch_address) return dip_switches;
        return memory[address];
    }
    void set_audio(AudioPins left, AudioPins right) {
        audio_left = left; audio_right = right;
        boards->adc_left = left.adc; boards->adc_right = right.adc;
        boards->gain_left = left.gain; boards->gain_right = right.gain;
        boards->levels_left_n = left.levels_n; boards->levels_right_n = right.levels_n;
        boards->eval();
    }
    void queue_audio(Tick at_time, AudioPins left, AudioPins right) {
        if (at_time < context->time() || (!input_events.empty() && at_time <= input_events.back().time))
            throw std::invalid_argument("Audio input timestamps must increase and must not be in the past");
        input_events.push_back({at_time, left, right});
    }
    void advance_ticks(Tick deadline) {
        if (deadline < context->time()) throw std::logic_error("Machine time moved backwards");
        apply_input();
        while (context->time() < deadline) {
            Tick next = deadline;
            if (boards->eventsPending()) next = std::min(next, boards->nextTimeSlot());
            if (auto event = remote.next_event()) next = std::min(next, *event * cpu_period);
            if (!input_events.empty()) next = std::min(next, input_events.front().time);
            context->time(next);
            boards->eval();
            remote.advance(next / cpu_period);
            observe_audio();
            apply_input();
        }
    }
    void advance(std::uint64_t cycle) { advance_ticks(cycle * cpu_period); }
    void run_until(std::uint64_t deadline) {
        while (cycles < deadline) {
            if (step_deferred()) continue;
            advance(cycles);
            auto wake = deadline;
            if (auto event = remote.next_event()) wake = std::min(wake, *event);
            if (wake <= cycles) throw std::logic_error("No future HALT wakeup");
            cycles = wake;
            advance(cycles);
        }
        advance(cycles);
    }
    unsigned step() { auto elapsed = step_deferred(); advance(cycles); return elapsed; }
    bool key(std::uint8_t make, bool down) {
        remote.advance(context->time() / cpu_period); return remote.key(make, down);
    }
    bool fader(unsigned slot, std::uint8_t value) {
        remote.advance(context->time() / cpu_period); return remote.fader(slot, value);
    }

private:
    // The 8080, one clock state per i8080_tick(). A bus cycle is T1 (status and
    // address, SYNC), T2 (DBIN for a read; WR/ and the byte for a write), then
    // TW while READY is low, then T3. The backplane parts (WCS at 0x4000-0x41FF,
    // DSP ports 0-9) answer through sbc.sv: a transfer starts at its T1 and
    // sbc.sv reports how many wait states XACK/ inserted. Everything else is on
    // the SBC and always ready.
    i8080_t cpu{};
    std::uint64_t pins = 0;
    struct Bus {
        std::uint8_t status = 0;
        std::uint16_t address = 0;
        std::uint64_t t1 = 0;           // state index of T1
        unsigned waits = 0;             // TW states before T3
        std::uint8_t value = 0;         // a read's byte, taken at T3
    } bus;
    std::uint16_t instruction_pc = 0;
    std::uint64_t observed_audio_count = 0;
    struct InputEvent { Tick time; AudioPins left, right; };
    std::deque<InputEvent> input_events;
    AudioPins audio_left, audio_right;

    void apply_input() {
        if (!input_events.empty() && input_events.front().time == context->time()) {
            auto event = input_events.front(); input_events.pop_front();
            set_audio(event.left, event.right);
        }
    }

    void observe_audio() {
        if (boards->audio_count == observed_audio_count) return;
        if (boards->audio_count != observed_audio_count + 1) throw std::logic_error("Missed audio event");
        observed_audio_count = boards->audio_count;
        AudioEvent event{boards->audio_time, {}, {}, {}};
        event.source_time = boards->audio_source_time;
        event.source_drivers = boards->audio_source_drivers;
        event.source_overlap = boards->audio_source_overlap;
        for (unsigned index = 0; index < 3; ++index) {
            event.value[index] = boards->audio_value[index];
            event.known[index] = boards->audio_known[index];
            event.high_z[index] = boards->audio_high_z[index];
        }
        // The two unused gain-counter bits remain high; only OGA0/1 leave FPC.
        event.value[2] |= 12; event.known[2] |= 12;
        if (audio_observer) audio_observer(event);
    }

    // One backplane transfer whose T1 is state `t1`; returns the byte read and the wait states.
    std::pair<std::uint8_t, unsigned> transfer(unsigned kind, std::uint16_t address, std::uint64_t t1,
                                               std::uint8_t value = 0) {
        const auto end_cycle = t1 + 3;
        advance(end_cycle - 3);
        boards->machine_host->begin_transfer(kind, address, value);
        boards->eval();
        const auto timeout = context->time() + 512 * cpu_period;
        while (!boards->transfer_complete && context->time() < timeout)
            advance_ticks(std::min(timeout, context->time() + cpu_period));
        if (!boards->transfer_complete) throw std::runtime_error("External byte transfer exceeded watchdog");
        if (context->time() != (end_cycle + boards->transfer_waits) * cpu_period)
            throw std::logic_error("SBC completion is not the CPU's T3 boundary: tick=" +
                std::to_string(context->time()) + ", nominal_end=" + std::to_string(end_cycle) +
                ", waits=" + std::to_string(boards->transfer_waits) + ", address=" + std::to_string(address));
        if ((kind == 0 || kind == 2) && (boards->read_known != 255 || boards->read_high_z))
            throw std::runtime_error("CPU sampled X/Z at port/address " + std::to_string(address) +
                ", PC=" + std::to_string(instruction_pc) + ", known=" + std::to_string(boards->read_known) +
                ", high_z=" + std::to_string(boards->read_high_z));
        if (kind == 1 && wcs_observer)
            wcs_observer({end_cycle - 3, instruction_pc, address, value, boards->wcs_commit_time});
        return {boards->read_value, boards->transfer_waits};
    }
    // The SBC's own ports (the panel's 8255, the LARC's 8251) act as at the end of T3.
    std::pair<std::uint8_t, unsigned> read_port(std::uint8_t port, std::uint64_t t1) {
        if (is_sbc_port(port)) {
            advance(t1 + 3);
            if (port == lexicon224x::sbc::SerialData) return {remote.read(), 0};
            if (port == lexicon224x::sbc::SerialControl) return {remote.status(), 0};
            return {panel.read(port, t1 + 3), 0};
        }
        if (port > 9) throw std::runtime_error("Unconnected input port " + std::to_string(port));
        return transfer(2, port, t1);
    }
    unsigned write_port(std::uint8_t port, std::uint8_t value, std::uint64_t t1) {
        if (is_sbc_port(port)) {
            advance(t1 + 3);
            if (port == lexicon224x::sbc::SerialData) remote.write(value);
            else if (port == lexicon224x::sbc::SerialControl) remote.control(value);
            else panel.write(port, value, t1 + 3);
            return 0;
        }
        if (port > 7 || port == 4) throw std::runtime_error("Unconnected output port " + std::to_string(port));
        return transfer(3, port, t1, value).second;
    }
    // One instruction (or one interrupt acknowledge); returns its states, 0 if halted.
    unsigned step_deferred() {
        if (auto event = remote.next_event(); event && *event <= cycles) advance(cycles);
        const auto start = cycles;
        if (remote.interrupt_pending() && interrupt_enabled()) {
            instruction_pc = cpu.pc;
            run_instruction(true);
            advance(cycles);
            return unsigned(cycles - start);
        }
        if (halted) return 0;
        instruction_pc = cpu.pc;
        if (pc_watches[instruction_pc]) {
            pc_watches[instruction_pc] = false;
            if (pc_observer) pc_observer(cycles, snapshot());
        }
        run_instruction(false);
        return unsigned(cycles - start);
    }
    void run_instruction(bool interrupt) {
        do {
            tick(interrupt);
        } while (!i8080_opdone(&cpu) && !i8080_halted(&cpu));
        halted = i8080_halted(&cpu);
    }

    // One clock state.
    void tick(bool interrupt) {
        const std::uint64_t state = cycles;
        std::uint64_t in = pins & ~(I8080_READY | I8080_INT);
        if (state < bus.t1 + 2 || state >= bus.t1 + 2 + bus.waits) in |= I8080_READY;
        if (interrupt) in |= I8080_INT;
        if ((pins & I8080_DBIN) && (in & I8080_READY)) I8080_SET_DATA(in, bus.value);
        pins = i8080_tick(&cpu, in);
        cycles++;
        if (pins & I8080_SYNC) {
            bus = Bus{};
            bus.status = I8080_GET_DATA(pins);
            bus.address = I8080_GET_ADDR(pins);
            bus.t1 = state;
        } else if (state == bus.t1 + 1) {
            t2();
        }
    }

    // T2: the byte to be written is on the bus; the addressed part starts its work.
    void t2() {
        const auto status = bus.status;
        const auto address = bus.address;
        const auto port = std::uint8_t(address);
        const auto data = I8080_GET_DATA(pins);
        const bool reading = status & I8080_STATUS_WO;
        if (status & I8080_STATUS_HLTA) return;
        if (status & I8080_STATUS_INTA) { bus.value = 0xff; return; }   // the 8238 answers with RST 7
        if (status & I8080_STATUS_INP) { std::tie(bus.value, bus.waits) = read_port(port, bus.t1); return; }
        if (status & I8080_STATUS_OUT) { bus.waits = write_port(port, data, bus.t1); return; }
        if (is_wcs(address)) {
            auto [value, waits] = transfer(reading ? 0 : 1, address, bus.t1, data);
            bus.value = value; bus.waits = waits;
        } else if (reading) {
            bus.value = peek(address);
        } else if (address >= lexicon224x::sbc::ram_start && address < lexicon224x::sbc::ram_end) {
            memory[address] = data;
        }
    }
};
} // namespace lex224x
