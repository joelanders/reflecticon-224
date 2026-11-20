#include "sbc.hpp"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace lex224x;

static std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
static void load_rom(Machine& machine, unsigned base, const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (bytes.empty() || base + bytes.size() > machine.memory.size()) throw std::runtime_error("Invalid ROM size");
    std::copy(bytes.begin(), bytes.end(), machine.memory.begin() + base);
}
static void print_state(const Machine& machine) {
    const auto state = machine.snapshot();
    std::cout << "S " << machine.cycles << ' ' << state.pc << ' ' << state.sp << ' '
        << unsigned(state.a) << ' ' << unsigned(state.flags) << ' ' << state.bc << ' ' << state.de << ' ' << state.hl << ' '
        << machine.halted << ' ' << machine.interrupt_enabled() << ' ' << machine.boards->wcs_writes << ' '
        << machine.boards->audio_count << ' ' << machine.boards->reset_edges << ' '
        << unsigned(machine.remote.status()) << ' ' << machine.remote.connected;
    for (unsigned address : {0x2000,0x2001,0x2002,0x206b,0x3c00,0x3c01,0x3ff0,0x41fc,0x41fd,0x41fe,0x41ff})
        std::cout << ' ' << unsigned(machine.peek(address));
    for (auto digit : machine.panel.digits) std::cout << ' ' << unsigned(digit);
    std::cout << '\n';
}
static void fixture(const std::filesystem::path& rom, unsigned grade, unsigned levels_left = 31, unsigned levels_right = 31) {
    Machine machine;
    if (levels_left > 31 || levels_right > 31) throw std::runtime_error("Level-detector pins need five bits");
    machine.set_audio({0, 0, std::uint8_t(levels_left)}, {0, 0, std::uint8_t(levels_right)});
    load_rom(machine, 0, rom);
    std::fill(machine.memory.begin() + 0x2000, machine.memory.begin() + 0x2800, 0);
    machine.memory[0x206b] = 0xa5;
    machine.panel.pots[0] = 0x34;
    machine.panel.switches[0] &= 0x7f;
    machine.wcs_observer = [](const WcsWrite& event) {
        std::cout << "W " << event.cpu_t1 << ' ' << event.writer_pc << ' ' << event.address << ' '
            << unsigned(event.value) << ' ' << event.committed_at << '\n';
    };
    machine.audio_observer = [](const AudioEvent& event) {
        std::cout << "A " << event.time;
        for (unsigned index = 0; index < 3; ++index)
            std::cout << ' ' << event.value[index] << ' ' << event.known[index] << ' ' << event.high_z[index];
        std::cout << '\n';
    };
    for (unsigned reset = 0; reset < 2; ++reset) {
        if (reset) machine.reset();
        std::cout << "G " << grade << ' ' << reset << '\n';
        for (unsigned index = 1; index <= 200; ++index) {
            auto target = index * 100;
            if (target == 200)
                std::cout << "K " << machine.key(0x21, true) << ' ' << machine.fader(5, 254) << '\n';
            machine.run_until(target);
            print_state(machine);
        }
    }
}

static void load_factory(Machine& machine, const std::filesystem::path& directory) {
    if (!machine.boards->cpu_present) throw std::runtime_error("Use the machine build for CPU execution");
    if (std::filesystem::is_regular_file(directory)) {
        load_rom(machine, 0, directory);
        return;
    }
    for (unsigned chip = 1; chip <= 4; ++chip) {
        auto path = directory / ("SBC" + std::to_string(chip) + " 2716.BIN");
        if (chip <= 2 || std::filesystem::exists(path)) load_rom(machine, (chip - 1) * 0x800, path);
    }
    for (unsigned chip = 1; chip <= 8; ++chip) {
        auto path = directory / ("NVS" + std::to_string(chip) + " 2732.BIN");
        if (std::filesystem::exists(path)) load_rom(machine, 0x8000 + (chip - 1) * 0x1000, path);
    }
}
static void watch_diagnostics(Machine& machine) {
    // Observation addresses in caller-supplied v8.1 SBC1, documented in
    // 2026-09-20-firmware-diagnostic-observation.md. They never alter execution.
    for (unsigned pc : {0x028c,0x0294,0x059c,0x05c8,0x05cc,0x05cd,0x05dd,0x0b61}) machine.pc_watches[pc] = true;
    machine.pc_observer = [](std::uint64_t cycles, LexCpuSnapshot state) {
        std::cout << "P " << cycles << ' ' << std::hex << state.pc << ' ' << unsigned(state.a) << ' '
            << unsigned(state.flags) << ' ' << state.bc << ' ' << state.de << ' ' << state.hl << ' ' << state.sp
            << std::dec << '\n' << std::flush;
    };
}
static void save_wcs(const Machine& machine, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    for (unsigned address = 0x4000; address < 0x4200; ++address) output.put(char(machine.peek(address)));
    if (!output) throw std::runtime_error("Cannot save WCS");
}
struct AudioTrace {
    Machine& machine;
    std::ofstream events, csv;
    std::array<std::uint64_t, 4> captures{}, unknown{}, nonzero{}, absolute_sum{};
    std::array<unsigned, 4> peak{};
    std::uint64_t hash = 14695981039346656037ULL;
    std::uint64_t source_overlap = 0, undriven_source = 0;

    AudioTrace(Machine& machine, const std::filesystem::path& prefix) : machine(machine),
        events(prefix.string() + ".events"), csv(prefix.string() + ".audio.csv") {
        if (!events || !csv) throw std::runtime_error("Cannot create audio trace");
        csv << "tick,channels,dac,dac_known,dac_high_z,gain,gain_known,source_tick,source_drivers,source_overlap";
        for (unsigned channel = 0; channel < 4; ++channel) csv << ",sample" << channel << ",known" << channel;
        csv << '\n';
        machine.audio_observer = [this](const AudioEvent& event) { capture(event); };
    }
    void capture(const AudioEvent& event) {
        events << "A " << event.time;
        for (unsigned index = 0; index < 3; ++index) {
            events << ' ' << event.value[index] << ' ' << event.known[index] << ' ' << event.high_z[index];
            for (auto value : {event.value[index], event.known[index], event.high_z[index]})
                hash = (hash ^ value) * 1099511628211ULL;
        }
        events << '\n';
        hash = (hash ^ event.time) * 1099511628211ULL;
        csv << event.time << ',' << event.value[0] << ',' << event.value[1] << ',' << event.known[1] << ','
            << event.high_z[1] << ',' << event.value[2] << ',' << event.known[2] << ',' << event.source_time << ','
            << unsigned(event.source_drivers) << ',' << event.source_overlap;
        source_overlap += event.source_overlap != 0;
        undriven_source += event.source_drivers == 0;
        for (unsigned channel = 0; channel < 4; ++channel) {
            int sample = std::int16_t(machine.boards->held_value[channel]);
            unsigned known = machine.boards->held_known[channel];
            csv << ',' << sample << ',' << known;
            if (!(event.value[0] & (1u << channel))) continue;
            captures[channel]++;
            if (known != 65535) { unknown[channel]++; continue; }
            nonzero[channel] += sample != 0;
            unsigned magnitude = sample < 0 ? -sample : sample;
            absolute_sum[channel] += magnitude;
            peak[channel] = std::max(peak[channel], magnitude);
        }
        csv << '\n';
    }
    void summary(const std::filesystem::path& prefix, Tick begin, Tick end, double wall_seconds) {
        std::ofstream output(prefix.string() + ".summary.json");
        output << "{\n  \"start_tick\": " << begin << ",\n  \"end_tick\": " << end
            << ",\n  \"wall_seconds_with_trace\": " << wall_seconds << ",\n  \"event_hash\": " << hash
            << ",\n  \"source_overlap_events\": " << source_overlap << ",\n  \"undriven_source_events\": " << undriven_source
            << ",\n  \"channels\": [\n";
        for (unsigned channel = 0; channel < 4; ++channel)
            output << "    {\"captures\": " << captures[channel] << ", \"unknown\": " << unknown[channel]
                << ", \"nonzero\": " << nonzero[channel] << ", \"peak\": " << peak[channel]
                << ", \"absolute_sum\": " << absolute_sum[channel] << "}" << (channel == 3 ? "\n" : ",\n");
        output << "  ]\n}\n";
    }
};

// Controls change between instructions, as in the existing interactive host.
// Each E record gives the actual cycle after any instruction overshoot.
struct ControlEvent {
    std::uint64_t cycle;
    std::string action;
    std::vector<std::string> arguments;
};
static std::vector<ControlEvent> read_controls(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read control script " + path.string());
    std::vector<ControlEvent> events;
    std::string line;
    while (std::getline(input, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream fields(line);
        ControlEvent event{};
        if (!(fields >> event.cycle)) {
            if (line.find_first_not_of(" \t\r") != std::string::npos)
                throw std::runtime_error("Expected a CPU cycle in control script");
            continue;
        }
        if (!(fields >> event.action)) throw std::runtime_error("Missing control action");
        std::string argument;
        while (fields >> argument) event.arguments.push_back(argument);
        if (!events.empty() && event.cycle < events.back().cycle)
            throw std::runtime_error("Control script cycles must not decrease");
        events.push_back(std::move(event));
    }
    return events;
}
static void apply_control(Machine& machine, const ControlEvent& event, const std::filesystem::path& prefix) {
    auto number = [&](unsigned index, unsigned maximum) {
        if (index >= event.arguments.size()) throw std::runtime_error("Missing argument for " + event.action);
        std::size_t consumed = 0;
        auto value = std::stoul(event.arguments[index], &consumed, 0);
        if (consumed != event.arguments[index].size() || value > maximum)
            throw std::runtime_error("Invalid argument for " + event.action);
        return unsigned(value);
    };
    auto count = [&](unsigned wanted) {
        if (event.arguments.size() != wanted) throw std::runtime_error("Wrong argument count for " + event.action);
    };
    if (event.action == "button") {
        count(2);
        machine.panel.switches[number(0, 2)] = std::uint8_t(~number(1, 255));
    } else if (event.action == "pot") {
        count(2);
        machine.panel.pots[number(0, 5)] = number(1, 255);
    } else if (event.action == "key") {
        count(2);
        if (!machine.key(number(0, 255), number(1, 1))) throw std::runtime_error("LARC rejected key");
    } else if (event.action == "fader") {
        count(2);
        if (!machine.fader(number(0, 5), number(1, 254))) throw std::runtime_error("LARC rejected fader");
    } else if (event.action == "audio") {
        count(4);
        machine.set_audio({std::uint16_t(number(0, 4095)), std::uint8_t(number(1, 3)), 31},
                          {std::uint16_t(number(2, 4095)), std::uint8_t(number(3, 3)), 31});
    } else if (event.action == "snapshot") {
        count(1);
        save_wcs(machine, prefix.string() + "." + event.arguments[0] + ".wcs.bin");
        print_state(machine);
    } else throw std::runtime_error("Unknown control action " + event.action);
    std::cout << "E " << machine.cycles << ' ' << event.action;
    for (const auto& argument : event.arguments) std::cout << ' ' << argument;
    std::cout << '\n';
}

static void mix_event(std::uint64_t& hash, const AudioEvent& event) {
    for (unsigned index = 0; index < 3; ++index)
        for (auto value : {event.value[index], event.known[index], event.high_z[index]})
            hash = (hash ^ value) * 1099511628211ULL;
    hash = (hash ^ event.time) * 1099511628211ULL;
}
static void report_benchmark(unsigned pass, Tick begin, Tick end, double seconds, std::uint64_t hash) {
    double simulated = double(end - begin) / ticks_per_second;
    std::cout << "B " << pass << " wall_seconds=" << seconds << " simulated_seconds=" << simulated
        << " realtime=" << simulated / seconds << " ns_per_row=" << seconds * 1e9 * row_period / (end - begin)
        << " checksum=" << hash << '\n' << std::flush;
}
static void factory(const std::filesystem::path& directory, std::uint64_t deadline,
                    const std::filesystem::path& prefix, const std::filesystem::path& script,
                    std::uint64_t benchmark_cycles) {
    Machine machine;
    load_factory(machine, directory);
    if (std::filesystem::is_directory(directory)) watch_diagnostics(machine);
    auto events = script.empty() ? std::vector<ControlEvent>{} : read_controls(script);
    if (!events.empty() && events.back().cycle > deadline)
        throw std::runtime_error("Control script extends past requested CPU cycle");
    AudioTrace trace(machine, prefix);
    std::ofstream writes(prefix.string() + ".wcs.csv");
    if (!writes) throw std::runtime_error("Cannot create WCS trace");
    writes << "cpu_t1,writer_pc,address,value,commit_tick\n";
    machine.wcs_observer = [&](const WcsWrite& event) {
        writes << event.cpu_t1 << ',' << event.writer_pc << ',' << event.address << ','
            << unsigned(event.value) << ',' << event.committed_at << '\n';
    };
    const auto started = std::chrono::steady_clock::now();
    std::size_t event_index = 0;
    std::uint64_t checkpoint = 250000;
    while (machine.cycles < deadline || (event_index < events.size() && events[event_index].cycle <= machine.cycles)) {
        while (event_index < events.size() && events[event_index].cycle <= machine.cycles)
            apply_control(machine, events[event_index++], prefix);
        if (machine.cycles >= checkpoint) {
            print_state(machine);
            checkpoint += 250000;
            std::cout << std::flush;
        }
        if (machine.cycles >= deadline) break;
        auto stop = std::min(checkpoint, deadline);
        if (event_index < events.size()) stop = std::min(stop, events[event_index].cycle);
        machine.run_until(stop);
    }
    print_state(machine);
    save_wcs(machine, prefix.string() + ".wcs.bin");
    std::ofstream nvs(prefix.string() + ".nvs.bin", std::ios::binary);
    nvs.write(reinterpret_cast<const char*>(machine.memory.data() + 0x2000), 2048);
    if (!nvs) throw std::runtime_error("Cannot save retained RAM");
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    trace.summary(prefix, 0, machine.context->time(), seconds);
    std::cout << "T " << seconds << " wall_seconds " << double(machine.context->time()) / ticks_per_second << " simulated_seconds\n";

    // Optional measurement starts after the requested trace/snapshot interval.
    // Execution continues normally; observers do no file I/O in this interval.
    machine.wcs_observer = {};
    for (unsigned pass = 0; benchmark_cycles && pass < 3; ++pass) {
        std::uint64_t hash = 14695981039346656037ULL;
        machine.audio_observer = [&](const AudioEvent& event) { mix_event(hash, event); };
        auto begin = machine.context->time();
        auto start = std::chrono::steady_clock::now();
        machine.run_until(machine.cycles + benchmark_cycles);
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        report_benchmark(pass, begin, machine.context->time(), elapsed, hash);
    }
    machine.audio_observer = {};
}

static void load_static(Machine& machine, const std::filesystem::path& image) {
    if (machine.boards->cpu_present) throw std::runtime_error("Use the static build for preloaded WCS execution");
    auto bytes = read_file(image);
    if (bytes.size() != 512) throw std::runtime_error("WCS image must contain 512 CPU-order bytes");
    for (unsigned address = 0; address < 128; ++address) {
        std::uint32_t instruction = 0;
        for (unsigned lane = 0; lane < 4; ++lane)
            instruction |= std::uint32_t(bytes[(address ^ 127) * 4 + lane] ^ 255) << (8 * lane);
        machine.boards->machine_host->load_word(address, instruction);
    }
    machine.queue_audio(first_marker + 4096 * row_period, {512, 0, 31}, {});
    machine.queue_audio(first_marker + 6144 * row_period, {}, {});
}
static void static_audio(const std::filesystem::path& image, std::uint64_t rows, const std::filesystem::path& prefix) {
    Machine machine;
    load_static(machine, image);
    AudioTrace trace(machine, prefix);
    const auto end = first_marker + rows * row_period;
    const auto started = std::chrono::steady_clock::now();
    machine.advance_ticks(end);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    trace.summary(prefix, 0, end, seconds);
    std::cout << "static rows=" << rows << " captures=" << machine.boards->audio_count
        << " wall_seconds_with_trace=" << seconds << '\n';
}
static void benchmark_static(const std::filesystem::path& image, std::uint64_t rows) {
    if (!rows) throw std::runtime_error("Benchmark needs a nonzero row count");
    for (unsigned pass = 0; pass < 5; ++pass) {
        Machine machine;
        load_static(machine, image);
        std::uint64_t hash = 14695981039346656037ULL;
        machine.audio_observer = [&](const AudioEvent& event) { mix_event(hash, event); };
        auto begin = machine.context->time();
        auto end = first_marker + rows * row_period;
        const auto started = std::chrono::steady_clock::now();
        machine.advance_ticks(end);
        auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        report_benchmark(pass, begin, end, seconds, hash);
    }
}

int main(int argc, char** argv) {
    try {
        if ((argc == 4 || argc == 6) && std::string(argv[1]) == "fixture") {
            fixture(argv[2], std::stoul(argv[3]), argc == 6 ? std::stoul(argv[4]) : 31, argc == 6 ? std::stoul(argv[5]) : 31);
            return 0;
        }
        if (argc >= 5 && argc <= 7 && (std::string(argv[1]) == "factory" || std::string(argv[1]) == "run")) {
            factory(argv[2], std::stoull(argv[3]), argv[4], argc >= 6 ? argv[5] : "", argc == 7 ? std::stoull(argv[6]) : 0);
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "static") {
            static_audio(argv[2], std::stoull(argv[3]), argv[4]);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "bench-static") {
            benchmark_static(argv[2], std::stoull(argv[3]));
            return 0;
        }
        throw std::runtime_error("Usage: machine fixture AUTHORED_ROM TIMING_GRADE | factory ROM_DIRECTORY CPU_CYCLES OUTPUT_PREFIX [CONTROL_SCRIPT [BENCH_CYCLES]] | static WCS_IMAGE ROWS OUTPUT_PREFIX | bench-static WCS_IMAGE ROWS");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
