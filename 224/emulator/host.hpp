#pragma once
// The 224X: its control computer (the 8080 SBC) wired to the DSP boards.
// This file is the backplane: it connects the parts, each in its own file,
// and runs them in time order.
//
//   SBC              the single-board computer: a National Semiconductor
//                    BLC-11 or equivalent (224X Service Manual 3.3), built
//                    like Intel's iSBC 80/10: an 8080 (../../i8080-cycle/
//                    i8080.h, one clock state per step), its 8224 clock and
//                    8238 system controller: below; memory map and ports:
//                    ../sbc/memory_map.hpp; control head and LARC: ../sbc/peripherals.hpp
//   T&C              the DSP program's sequencer (the row machine,
//                    ../isa-level-cpp/lexicon224x.hpp); the SBC's access to the WCS:
//                    wcs_access.hpp; diagnostic ports: tc_diagnostics.hpp
//   ARU, DMEM, FPC   the row machine; DMEM's CPU side: dmem.hpp; FPC
//                    headroom registers: fpc_headroom.hpp
//   time             timing.hpp (clocks and edges), scheduler.hpp (the
//                    order of events; simulation machinery)
//
// Every part has the same shape: state, plus the events that change it.
// - The row machine: fetch(), converter_clock() and execute() on a Machine,
//   each at its time in the row.
// - The 8080: i8080_tick(), one clock state, pins in and pins out.
// - The board parts: inputs are methods named after the hardware event
//   that calls them (at_marker, at_fetch, at_execute, on_request,
//   on_port_write, on_reset_n, on_wr_xreg, on_read_released); outputs are
//   const queries. Each file's header lists its inputs and outputs.
// This file calls them in time order.
//
// The timing reproduces the board-level machine (../board-level-verilog:
// sbc.sv, tc.sv, dmem.sv, fpc.sv). Times are 1/576 ns ticks.
#include "../isa-level-cpp/lexicon224x.hpp"
#include "dmem.hpp"
#include "fpc_headroom.hpp"
#include "../sbc/memory_map.hpp"
#include "../sbc/peripherals.hpp"
#include "scheduler.hpp"
#include "tc_diagnostics.hpp"
#include "timing.hpp"
#include "trace.hpp"
#include "wcs_access.hpp"
// The 8080 core is compiled into each program that includes this file (its
// functions are static), like the header-only row machine.
#define I8080_API static
#define CHIPS_IMPL
#include "../8080/i8080.h"
#undef CHIPS_IMPL
#include <array>
#include <functional>
#include <algorithm>
#include <memory>
#include <stdexcept>

// The SBC's memory map, front panel and LARC are shared with the board-level machine.
namespace lexicon224x::cpu { using namespace lexicon224x::sbc; }
#include <string>

namespace lexicon224x::cpu {

struct AudioEvent {
    Tick time;
    unsigned channels, dac, gain;
};

struct WcsWrite {
    uint64_t cpu_t1;
    uint16_t writer_pc, address;
    uint8_t value;
    Tick committed_at;
};

// The CPU registers as the program sees them between instructions.
struct CpuSnapshot {
    uint16_t pc, sp, bc, de, hl;
    uint8_t a, flags, interrupt_enabled;
};

class Host {
public:
    std::array<uint8_t, 65536> memory;
    uint8_t dip_switches = 255;
    Panel panel;
    Remote remote;
    uint64_t cycles = 0;        // CPU clock states since power-up (one per 488.28 ns)
    bool halted = false;
    std::unique_ptr<Machine> dsp = std::make_unique<Machine>();
    uint64_t audio_count = 0;
    // Observers. The CPU runs ahead of the DSP between the bus cycles that
    // join them (run_until), so a DSP-side observer (audio, row) sees
    // `cycles`, the panel and the LARC in its future, and a CPU-side one
    // (pc, port, wcs) sees the DSP in its past. Each event carries its own
    // hardware time; read that, and the side the observer belongs to.
    std::function<void(const AudioEvent &)> audio_observer;
    std::function<void(const WcsWrite &)> wcs_observer;
    std::array<bool, 65536> pc_watches{};
    std::function<void(uint64_t, CpuSnapshot)> pc_observer;
    std::function<void(uint64_t, uint16_t, bool, unsigned, uint8_t)> port_trace;  // cycle, pc, write, port, value
    std::function<void(uint64_t, const Machine &)> row_observer;  // after each row executes: its row number, the machine

    // The 224X/224XL, or the original 224 (its T&C decode, memory map and
    // serial port; ../sbc/memory_map.hpp).
    explicit Host(Model model = Model::Lexicon224X) : model(model) {
        memory.fill(255);
        pins = i8080_init(&cpu);
        scheduler.set_cpu_time(state_start(0) + phi2_rise);
        dsp->model = model;
        redecode_wcs(*dsp);
        if (model == Model::Lexicon224) {
            scheduler.timing = timing_224;
        }
        wcs_access.model = model;
        diagnostics.model = model;
    }

    const Model model;

    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;

    CpuSnapshot snapshot() const {
        i8080_t r = i8080_registers(&cpu);
        return {r.pc, r.sp, r.bc, r.de, r.hl, r.a, r.f, r.inte};
    }

    // Whether an interrupt would be taken at the next instruction boundary:
    // INTE set, and not straight after EI (the core applies the same rule).
    bool interrupt_enabled() const {
        return cpu.inte && cpu.opcode != 0xFB;
    }

    uint64_t wcs_writes() const {
        return wcs_access.writes_committed();
    }

    uint64_t reset_edges() const {
        return dmem.reset_edge_count();
    }

    Tick now() const {
        return scheduler.now();
    }

    uint8_t peek(uint16_t address) const {
        if (is_wcs(address)) {
            return uint8_t(~(dsp->wcs[wcs_row(address)] >> (8 * wcs_lane(address))));
        }
        if (address == dip_switch_address) {
            return dip_switches;
        }
        return memory[address];
    }

    void set_audio(unsigned adc_left, unsigned gain_left, unsigned adc_right, unsigned gain_right) {
        dsp->adc_left = adc_left;
        dsp->gain_left = gain_left;
        dsp->adc_right = adc_right;
        dsp->gain_right = gain_right;
    }

    // The AIN's level detectors for input channel 1 (left, 0) or 2 (right, 1):
    // bit k set = comparator k exceeded (fpc_headroom.hpp). They feed the
    // headroom registers the firmware reads for its headroom LEDs.
    void set_level_detectors(unsigned channel, unsigned asserted) {
        level_detectors[channel] = asserted;
    }

    // What a headroom register holds now (fpc_headroom.hpp held()), for a meter.
    uint8_t headroom_held(unsigned channel) const {
        return headroom.held(channel);
    }

    // Run until `deadline` CPU states have passed, finishing the instruction
    // in progress: the CPU and the DSP together, in time order. The DSP ends
    // at the start of the next CPU state.
    void run_until(uint64_t deadline) {
        run_deadline = deadline;
        stop_requested = at_instruction_boundary() && cycles >= deadline;
        if (trace) {
            // Traced: one timeline, everything in time order, as the lenses expect.
            while (!stop_requested) {
                step_one(~Tick(0));
            }
        } else {
            // The CPU runs ahead through its own board's bus cycles (ROM, RAM,
            // the control head, the LARC): nothing on the DSP boards can see
            // those, and what they schedule for the boards carries its time.
            // A cycle that reaches the WCS or ports 0-9 first brings the DSP
            // up to the state's time (phases and effects at or before it, as
            // the single timeline orders them: the DSP side first at a tie)
            // and then runs in step with it. The DSP never runs ahead of the
            // CPU, so a request is always made before the marker that grants it.
            while (!stop_requested) {
                Tick state_time = scheduler.next_cpu_time();
                if (bus.board != Board::Sbc) {
                    scheduler.run_dsp_until(state_time, [this](RowPhase kind, uint64_t row_number) {
                        run_row_phase(kind, row_number);
                    });
                }
                scheduler.enter_cpu_state();
                cpu_state();
            }
        }
        advance(cycles);
    }

    bool key(uint8_t make, bool down) {
        remote.advance(scheduler.now() / cpu_period);
        return remote.key(make, down);
    }

    bool fader(unsigned slot, uint8_t value) {
        remote.advance(scheduler.now() / cpu_period);
        return remote.fader(slot, value);
    }

    // The DSP row phase that runs next: where in its row the row machine
    // stands (for dsp_trace.hpp).
    RowPhase next_row_phase() const {
        return scheduler.next_phase();
    }

    // Report what happens to `probe` (trace.hpp; timeline.hpp puts
    // it into words); null stops it.
    void set_trace(Trace *probe) {
        trace = probe;
        wcs_access.set_trace(probe);
        diagnostics.set_trace(probe);
    }

private:
    Scheduler scheduler;
    WcsAccess wcs_access{scheduler, *dsp};
    TcDiagnostics diagnostics{scheduler};
    Dmem dmem{scheduler};
    FpcHeadroom headroom{scheduler};
    unsigned level_detectors[2] = {0, 0};   // the AIN's comparators, per input channel (set_level_detectors)

    // Run the next row phase, bus effect or CPU state (at or before `deadline`).
    bool step_one(Tick deadline) {
        return scheduler.step_one(
            deadline,
            [this](RowPhase kind, uint64_t row_number) {
                run_row_phase(kind, row_number);
            },
            [this] {
                cpu_state();
            });
    }

    // Run the DSP rows and bus effects up to `deadline` (the CPU's next state
    // is later). The LARC's serial line keeps the same time.
    void advance_ticks(Tick deadline) {
        scheduler.run_dsp_until(deadline, [this](RowPhase kind, uint64_t row_number) {
            run_row_phase(kind, row_number);
        });
        scheduler.stand_at(deadline);
        remote.advance(scheduler.now() / cpu_period);
    }

    void advance(uint64_t cycle) {
        advance_ticks(cycle * cpu_period);
    }

    // ---- DSP rows: the T&C's row phases -----------------------------------------------
    uint32_t marker_microinstruction = 0;  // the MI at this row's marker (fetched in the previous row)
    bool marker_protect = false, marker_reset = false;   // ... its protect and RESET bits

    // Inlined into the scheduler's whole-row path, where `kind` is a constant.
    LEXICON_ALWAYS_INLINE void run_row_phase(RowPhase kind, uint64_t row_number) {
        if (trace) {
            trace->row_phase(scheduler.now(), kind, row_number);
        }
        switch (kind) {
            case RowPhase::Begin:
                begin_row();
                break;
            case RowPhase::ExecutePrevious:
                // Row 0's window has no previous row to finish.
                if (row_number != uint64_t(-1)) {
                    execute_row(row_number);
                }
                break;
            case RowPhase::Fetch:
                fetch_row(row_number);
                break;
            case RowPhase::ResetDecode:
                // RESET_N follows the microinstruction at this row's marker
                // (the 224X: the RESET row's, while the flush row runs). The
                // 224 has no flush row: RESET_N follows the RESET row as it
                // is fetched, so a single-cycle pass halts with the PC at 0
                // ("the 224 halts by continually repeating the first program
                // step", 224 Service Manual 2.1, E40).
                if (dsp->model == Model::Lexicon224) {
                    dmem.on_reset_n(dsp->mi.reset);
                } else {
                    dmem.on_reset_n(marker_reset);
                }
                break;
            case RowPhase::Converter:
                converter_row();
                break;
        }
    }

    // The row marker: the diagnostics sample, and the T&C grants CPU requests.
    LEXICON_ALWAYS_INLINE void begin_row() {
        marker_microinstruction = dsp->microinstruction;
        marker_protect = dsp->mi.protect;
        marker_reset = dsp->mi.reset;
        diagnostics.at_marker(marker_microinstruction, dsp->finishing.negative);
        wcs_access.at_marker(marker_protect, dmem.run_level_high());
    }

    // The fetch, unless a granted CPU access displaces it.
    LEXICON_ALWAYS_INLINE void fetch_row(uint64_t row_number) {
        dsp->run = dmem.run_level_high();
        bool previous_protect = dsp->mi.protect;
        bool previous_reset = dsp->mi.reset;
        bool displaced = wcs_access.displaced(scheduler.timing.marker(row_number));
        fetch(*dsp, displaced);
        wcs_access.at_fetch(previous_protect, previous_reset, dsp->mi.protect);
        if (trace && displaced) {
            trace->fetch_displaced(scheduler.now(), row_number);
        }
    }

    LEXICON_ALWAYS_INLINE void converter_row() {
        dsp->xreg_from_cpu = dmem.xreg_to_dsp();  // what the XREG input bank drives for WR_DA now
        bool ch1_before = dsp->fpc.ch1;
        converter_clock(*dsp);
        // The FPC clocks the two headroom registers "on opposite edges of
        // the channel select signal, CH1" (224X Service Manual 3.8), register
        // HR1 (port 8, "CH1 HEADROOM") with channel 1's levels, HR2 with
        // channel 2's. (Which edge clocks which follows the board model's
        // index, fpc.sv; either order gives the same meter.)
        if (dsp->fpc.ch1 != ch1_before) {
            unsigned channel = 0;
            if (dsp->fpc.ch1) {
                channel = 1;
            }
            headroom.sample(channel, level_detectors[channel]);
        }
        if (dsp->dac_channels) {
            audio_count++;
            if (audio_observer) {
                audio_observer({scheduler.now() + scheduler.timing.dac_observed - scheduler.timing.converter_offset, dsp->dac_channels,
                                dsp->dac_code, dsp->dac_gain});
            }
        }
    }

    // The previous row's microinstruction executes, and the ARU clocks its
    // three edges of this row. A CPU WCS access can hold the operand register
    // on any of them: edge 0 is the older multiply's second shift, edge 1 this
    // one's load, edge 2 its first shift.
    LEXICON_ALWAYS_INLINE void execute_row(uint64_t row_number) {
        Tick next_marker = scheduler.timing.marker(row_number + 1);
        dmem.at_execute(next_marker, *dsp);
        dsp->xreg_from_cpu = dmem.xreg_to_dsp();
        if (next_marker > wcs_access.holds_end()) {
            for (unsigned edge = 0; edge < 3; edge++) {
                dsp->operand_held[edge] = false;    // what held() answers past the window
            }
        } else {
            for (unsigned edge = 0; edge < 3; edge++) {
                dsp->operand_held[edge] = wcs_access.held(scheduler.timing.aruck_edge(next_marker, edge));
            }
        }
        execute(*dsp);
        if (row_observer) {
            row_observer(row_number, *dsp);
        }
        diagnostics.at_arithmetic(dsp->clocked, dsp->saturated, next_marker);
        if (trace) {
            const Hold clocks[3] = {Hold::SecondShift, Hold::Load, Hold::FirstShift};
            for (unsigned edge = 0; edge < 3; edge++) {
                if (dsp->operand_held[edge]) {
                    trace->multiplicand_held(scheduler.timing.aruck_edge(next_marker, edge), clocks[edge]);
                }
            }
        }
        // WR_XREG/ falls later in this row and saves the bus word for the CPU.
        if (dsp->mi.wr_xreg) {
            dmem.on_wr_xreg(dsp->xreg_to_cpu, next_marker + scheduler.timing.xreg_capture_offset);
        }
    }

    // ---- The CPU and its bus (SBC: 8080, 8224 clock, 8238 system controller) ------
    // The 8080 (../../i8080-cycle/i8080.h) runs one clock state per event, at
    // the state's phi2 rise (2 crystal periods in): that is when its outputs
    // change and when the SBC samples a read's byte. What the pins say:
    //   T1  SYNC, address and status word: the 8238 latches the status.
    //   T2  the system bus command (Multibus: MRDC/, MWTC/, IORC/, IOWC/): a
    //       read's starts now, with DBIN. The 8238's writes are "advanced",
    //       but on the iSBC 80/10 that early strobe stays on the board; the
    //       system bus's MWTC/ and IOWC/ are it synchronized with the 8080's
    //       WR/, so they start at the end of T2 (iSBC 80/10 Hardware
    //       Reference Manual, 3.2.1 and Figure 3-4).
    //   TW  READY was low: when the 8224 sampled it (3 crystal periods into
    //       the previous state), the addressed board had not answered.
    //   T3  a read's byte is taken now; a write ends.
    // On the backplane (the WCS, ports 0-9) READY is the XACK/ line: the T&C
    // and DMEM each assert it some time after their command starts, and drop
    // it when the command ends. Wait states are whatever that makes them.
    i8080_t cpu{};
    uint64_t pins = 0;
    bool int_pin = false;                   // INT, sampled at each instruction boundary
    uint16_t instruction_pc = 0;            // PC of the instruction in progress
    uint64_t instruction_start = 0;         // its first state
    uint64_t run_deadline = 0;              // run_until(): stop at the first boundary from here
    bool stop_requested = false;
    Trace *trace = nullptr;                 // see set_trace()

    // Which board answers a bus cycle.
    enum class Board {
        Sbc,        // ROM, RAM, the control head and the LARC: on the SBC, always ready
        Tc,         // the WCS: the T&C's XACK/
        Dmem,       // ports 0-9: DMEM's XACK/
    };

    // The bus cycle in progress, as the 8238 latched it at T1.
    struct BusCycle {
        uint8_t status = 0;
        uint16_t address = 0;
        uint64_t t1 = 0;                        // state index of its T1
        Board board = Board::Sbc;
        bool command_started = false;           // DMEM: its I/OR/ or I/OW/ has started,
        Tick command = 0;                       // ... at this time
        WcsAccess::Request *wcs = nullptr;      // the T&C: the request, once made
    };
    BusCycle bus;

    bool at_instruction_boundary() const {
        return i8080_opdone(&cpu) || i8080_halted(&cpu);
    }

    // One CPU clock state, at its phi2 rise.
    void cpu_state() {
        uint64_t state = cycles;
        if (at_instruction_boundary()) {
            start_instruction();
        }
        uint64_t in = pins & ~(I8080_READY | I8080_INT);
        if (state == 0 || ready_at(state_start(state - 1) + ready_sample)) {
            in |= I8080_READY;
        }
        if (int_pin) {
            in |= I8080_INT;
        }
        // T3 of a read: the byte on the bus now.
        if ((pins & I8080_DBIN) && (in & I8080_READY)) {
            I8080_SET_DATA(in, read_byte(state));
        }
        pins = i8080_tick(&cpu, in);
        cycles++;
        if (pins & I8080_SYNC) {
            begin_bus_cycle(state);
        } else if (state == bus.t1 + 1) {
            command();
        } else if ((pins & I8080_WR) && !(pins & I8080_WAIT)) {
            end_write(state);
        }
        halted = i8080_halted(&cpu);
        if (trace) {
            trace->cpu_state(state_start(state), in, pins, halted);
        }
        scheduler.set_cpu_time(state_start(cycles) + phi2_rise);
        if (at_instruction_boundary() && cycles >= run_deadline) {
            stop_requested = true;
        }
    }

    // At an instruction boundary (and in every halted state): INT, and the
    // diagnostic PC watches.
    void start_instruction() {
        // The LARC's 8251 is the only interrupt source; the 8238 answers the
        // acknowledge with RST 7 (its INTA/ pin is tied to +12 V).
        remote.advance(cycles);
        int_pin = remote.interrupt_pending();
        if (int_pin && interrupt_enabled()) {
            instruction_pc = cpu.pc;
            instruction_start = cycles;
            halted = false;
            return;
        }
        if (i8080_halted(&cpu)) {
            return;
        }
        instruction_pc = cpu.pc;
        instruction_start = cycles;
        if (pc_watches[instruction_pc]) {
            pc_watches[instruction_pc] = false;
            if (pc_observer) {
                pc_observer(cycles, snapshot());
            }
        }
    }

    // READY as the 8224 sampled it at `t`.
    bool ready_at(Tick t) const {
        switch (bus.board) {
            case Board::Sbc:
                return true;
            case Board::Tc:
                return bus.wcs && bus.wcs->scheduled && bus.wcs->ack_time < t;
            case Board::Dmem:
                return bus.command_started && bus.command + Dmem::command_to_xack < t;
        }
        return true;
    }

    // T1: the 8238 latches the status word; the address says which board answers.
    void begin_bus_cycle(uint64_t state) {
        bus = BusCycle{};
        bus.status = I8080_GET_DATA(pins);
        bus.address = I8080_GET_ADDR(pins);
        bus.t1 = state;
        bool io = (bus.status & (I8080_STATUS_INP | I8080_STATUS_OUT)) != 0;
        bool no_transfer = (bus.status & (I8080_STATUS_HLTA | I8080_STATUS_INTA)) != 0;
        if (io && !sbc_port(uint8_t(bus.address))) {
            bus.board = Board::Dmem;
        } else if (!io && !no_transfer && is_wcs(bus.address)) {
            bus.board = Board::Tc;
        }
    }

    // T2: the system bus command starts (a read's now, a write's at the end of T2).
    void command() {
        const uint8_t status = bus.status;
        const Tick t1 = state_start(bus.t1);
        const uint8_t port = uint8_t(bus.address);     // I/O: the port is on both address halves
        if (status & (I8080_STATUS_HLTA | I8080_STATUS_INTA)) {
            return;
        }
        if (status & I8080_STATUS_INP) {
            if (bus.board == Board::Dmem) {
                if (port > HeadroomRight) {
                    throw std::runtime_error("Unconnected input port " + std::to_string(port));
                }
                bus.command_started = true;             // I/OR/, with DBIN
                bus.command = scheduler.now();
            }
            return;
        }
        if (status & I8080_STATUS_OUT) {
            if (bus.board == Board::Dmem) {
                if (port > XregHighByte || port == MonitorControl) {
                    throw std::runtime_error("Unconnected output port " + std::to_string(port));
                }
                bus.command_started = true;             // I/OW/, with WR/
                bus.command = state_after(t1, 2);
                dmem.on_port_strobe(port, bus.command + Dmem::port_decode_delay);
            }
            return;
        }
        bool reading = (status & I8080_STATUS_WO) != 0;
        if (bus.board == Board::Tc) {
            if (reading) {
                if (trace) {
                    trace->bus_command(scheduler.now(), BusCommand::MemoryRead);
                }
                bus.wcs = &wcs_access.on_request(true, wcs_row(bus.address), wcs_lane(bus.address), 0, t1);
            } else {
                if (trace) {
                    trace->bus_command(state_after(t1, 2), BusCommand::MemoryWrite);
                }
                uint8_t value = I8080_GET_DATA(pins);
                scheduler.at(state_after(t1, 2), [this, value, t1] {
                    bus.wcs = &wcs_access.on_request(false, wcs_row(bus.address), wcs_lane(bus.address), value, t1);
                }, true);
            }
            return;
        }
        if (!reading && bus.address >= writable_start() && bus.address < ram_end) {
            memory[bus.address] = I8080_GET_DATA(pins);     // RAM; ROM ignores writes
        }
    }

    // T3 of a read (state `state`): the byte the addressed part drives now.
    uint8_t read_byte(uint64_t state) {
        if (bus.status & I8080_STATUS_INTA) {
            return 0xFF;                                    // the 8238 jams RST 7
        }
        if (bus.status & I8080_STATUS_INP) {
            return read_port(uint8_t(bus.address), state);
        }
        if (bus.board == Board::Tc) {
            return read_wcs();
        }
        return peek(bus.address);
    }

    // The T&C drives the WCS byte while its lane is enabled. It works out
    // the CPU's sample time as the CPU's own READY logic does; they must agree.
    uint8_t read_wcs() {
        const WcsAccess::Request &access = *bus.wcs;
        Tick now = scheduler.now();
        if (now != access.sampled_at) {
            throw std::logic_error("the T&C's idea of the CPU's sample time is not the CPU's");
        }
        if (now < access.read_enable || now >= access.read_release) {
            throw std::runtime_error("CPU sampled an undriven WCS read");
        }
        return uint8_t(~(dsp->wcs[access.row] >> (8 * access.lane)));
    }

    // IN at T3 (state `state`): DSP ports 0-9 are sampled now, where I/OR/
    // ends; the control head's 8255 and the LARC's 8251 are read as at the end of T3.
    uint8_t read_port(uint8_t port, uint64_t state) {
        uint64_t end = state + 1;
        if (sbc_port(port)) {
            remote.advance(end);
            if (port == serial_data()) {
                return remote.read();
            }
            if (port == serial_control()) {
                return remote.status();
            }
            return panel.read(port, end);
        }
        // DAT0/..DAT7/ are active low: the SBC complements every byte it reads.
        uint8_t result = uint8_t(~read_dsp_port(port));
        if (port == HeadroomLeft || port == HeadroomRight) {
            headroom.on_read_released(port - HeadroomLeft, scheduler.now() + Dmem::port_decode_delay);
        }
        if (port_trace) {
            port_trace(instruction_start, instruction_pc, false, port, result);
        }
        return result;
    }

    // T3 of a write (state `state`): the write ends.
    void end_write(uint64_t state) {
        uint8_t value = I8080_GET_DATA(pins);
        if (bus.status & I8080_STATUS_OUT) {
            uint8_t port = uint8_t(bus.address);
            if (bus.board == Board::Sbc) {
                uint64_t end = state + 1;                   // written as at the end of T3
                remote.advance(end);
                if (port == serial_data()) {
                    remote.write(value);
                } else if (port == serial_control()) {
                    remote.control(value);
                } else {
                    panel.write(port, value, end);
                }
                return;
            }
            // I/OW/ rises at phi2 in the state after T3; DMEM decodes it 20 ns
            // later. DAT0/..DAT7/ are active low: the byte on the bus is the complement.
            Tick rise = state_start(state + 1) + phi2_rise + Dmem::port_decode_delay;
            dmem.on_port_release(port, uint8_t(~value), rise);
            if (port_trace) {
                port_trace(instruction_start, instruction_pc, true, port, value);
            }
            return;
        }
        if (bus.board == Board::Tc && wcs_observer) {
            wcs_observer({bus.t1, instruction_pc, bus.address, value, bus.wcs->commit_at});
        }
    }

    // The SBC's own ports and RAM, which differ between the models (../sbc/memory_map.hpp).
    bool sbc_port(uint8_t port) const {
        if (model == Model::Lexicon224) {
            return is_sbc_port_224(port);
        }
        return is_sbc_port(port);
    }

    uint8_t serial_data() const {
        if (model == Model::Lexicon224) {
            return SerialData224;
        }
        return SerialData;
    }

    uint8_t serial_control() const {
        if (model == Model::Lexicon224) {
            return SerialControl224;
        }
        return SerialControl;
    }

    uint32_t writable_start() const {
        if (model == Model::Lexicon224) {
            return ram_start_224;
        }
        return ram_start;
    }

    // Which board answers a DSP input port.
    uint8_t read_dsp_port(unsigned port) {
        switch (port) {
            case OffsetLow:
                return uint8_t(dsp->microinstruction);      // OFST/ low byte
            case OffsetHigh:
                return uint8_t(dsp->microinstruction >> 8);  // OFST/ high byte
            case BusTest:
                return dmem.bus_test_register();
            case ArithmeticMonitor:
                return diagnostics.arithmetic_monitor();
            case MicroinstructionMonitor:
                return diagnostics.microinstruction_monitor();
            case TimingMonitor:
                return diagnostics.timing_monitor();
            case TransferLow:
                return uint8_t(dmem.xreg_to_cpu());
            case TransferHigh:
                return uint8_t(dmem.xreg_to_cpu() >> 8);
            case HeadroomLeft:
            case HeadroomRight:
                return headroom.byte(port - HeadroomLeft);
            default:
                throw std::runtime_error("DSP input port " + std::to_string(port) + " is not modeled");
        }
    }
};

}  // namespace lexicon224x::cpu
