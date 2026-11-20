// Tests for source/params (M5) and source/state (M6), no JUCE and no ROMs:
// a fake machine stands in for the operator, a fake host for the DAW.
//
//   params_test WEB_DIR     (WEB_DIR = ../web-demo/page, for the catalogs)
//
// Exit status 0 only if every check passed.
#include "../../source/catalog/catalog.hpp"
#include "../../source/catalog/share.hpp"
#include "../../source/params/command_queue.hpp"
#include "../../source/params/layout.hpp"
#include "../../source/params/scheduler.hpp"
#include "../../source/params/writeback.hpp"
#include "../../source/state/plugin_state.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------
// Allocation counting (as tests/rt_alloc): every operator new/delete made
// while `watching` is set on this thread is counted.
// ---------------------------------------------------------------------
static thread_local bool watching = false;
static std::atomic<uint64_t> watchedNews{0};
static std::atomic<uint64_t> watchedDeletes{0};

void *operator new(std::size_t size) {
    if (watching) {
        watchedNews++;
    }
    if (size == 0) {
        size = 1;
    }
    void *p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}
void *operator new[](std::size_t size) {
    return operator new(size);
}
void operator delete(void *p) noexcept {
    if (watching && p != nullptr) {
        watchedDeletes++;
    }
    std::free(p);
}
void operator delete[](void *p) noexcept {
    operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept {
    operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    operator delete(p);
}

using namespace lexparams;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const std::string &what) {
    checks++;
    if (!ok) {
        failures++;
        if (failures <= 60) {
            std::printf("FAIL %s\n", what.c_str());
        }
    }
}

static std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        std::exit(2);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// ---------------------------------------------------------------------
// A fake machine: programs with presets, tasks that take machine time.
// ---------------------------------------------------------------------
struct Event {
    uint64_t frame;
    int what;   // 0 program, 1 variation, 2 slider, 3 toggle, 10+ direct
    int a;
    float b;
    bool operator==(const Event &) const = default;
};

class FakeMachine : public OperatorSink {
public:
    static constexpr int kPrograms = 6;
    static constexpr uint64_t kProgramFrames = 20000;
    static constexpr uint64_t kVariationFrames = 12000;
    static constexpr uint64_t kSliderFrames = 4800;
    static constexpr uint64_t kToggleFrames = 6000;

    SliderRange range = kLarcRange;
    bool isReady = true;
    uint64_t clock = 0;          // frames rendered
    std::vector<Event> log;      // reserved: pushing never allocates in the watched region

    int program = 0;
    int variation = 1;
    uint8_t stored[kSliders] = {};
    uint8_t toggles = 0b0101;    // DYN DECAY and DECAY OPT on

    FakeMachine() {
        log.reserve(100000);
        loadPreset();
    }

    static int sliderCount(int p) {
        return 10 + 4 * p;   // program 5 has 30
    }
    static int variations(int p) {
        if (p == 0) {
            return 1;
        }
        return 7;
    }
    int preset(int p, int v, int k) const {
        return range.min + (p * 37 + v * 11 + k * 7) % (range.max - range.min + 1);
    }
    void loadPreset() {
        for (int k = 1; k <= kSliders; k++) {
            stored[k - 1] = uint8_t(preset(program, variation, k));
        }
    }

    // The task in flight, applied when it ends (as RAM settles).
    int taskKind = -1;
    int taskA = 0;
    int taskB = 0;
    uint64_t taskEnd = 0;

    void settle() {
        if (taskKind < 0 || clock < taskEnd) {
            return;
        }
        if (taskKind == 0) {
            program = taskA;
            variation = 1;
            loadPreset();
        } else if (taskKind == 1) {
            variation = taskA;
            loadPreset();
        } else if (taskKind == 2) {
            int raw = taskB;
            if (taskA == 1) {
                raw &= ~3;   // slider_1 holds only multiples of 4: the host must follow
            }
            stored[taskA - 1] = uint8_t(raw);
        } else if (taskKind == 3) {
            if (taskB != 0) {
                toggles = uint8_t(toggles | (1u << taskA));
            } else {
                toggles = uint8_t(toggles & ~(1u << taskA));
            }
        }
        taskKind = -1;
    }
    void advance(int frames) {
        clock += uint64_t(frames);
        settle();
    }
    void begin(int kind, int a, int b, uint64_t frames) {
        taskKind = kind;
        taskA = a;
        taskB = b;
        taskEnd = clock + frames;
        log.push_back({clock, kind, a, float(b)});
    }

    bool ready() const override {
        return isReady;
    }
    bool busy() const override {
        return taskKind >= 0;
    }
    bool loadProgram(int index) override {
        if (index < 0 || index >= kPrograms) {
            return false;
        }
        begin(0, index, 0, kProgramFrames);
        return true;
    }
    bool loadVariation(int v) override {
        if (v < 1 || v > variations(program)) {
            return false;
        }
        begin(1, v, 0, kVariationFrames);
        return true;
    }
    bool moveSlider(int k, int raw) override {
        if (k < 1 || k > sliderCount(program)) {
            return false;
        }
        begin(2, k, raw, kSliderFrames);
        return true;
    }
    bool setToggle(int toggle, bool on) override {
        if (toggle == kMute) {
            return false;   // not offered by this fake remote
        }
        int value = 0;
        if (on) {
            value = 1;
        }
        begin(3, toggle, value, kToggleFrames);
        return true;
    }
    void setLevelDb(float db) override {
        log.push_back({clock, 10, 0, db});
    }
    void setDryWet(float wet) override {
        log.push_back({clock, 11, 0, wet});
    }
    void setOutput(int side, int dac) override {
        log.push_back({clock, 12, side, float(dac)});
    }
    void setAnalog(bool on) override {
        float value = 0.0f;
        if (on) {
            value = 1.0f;
        }
        log.push_back({clock, 13, 0, value});
    }
    void readState(MachineState &out) override {
        out = MachineState{};
        out.program = program;
        out.variation = variation;
        out.sliderCount = sliderCount(program);
        out.rangeMin = range.min;
        out.rangeMax = range.max;
        for (int k = 0; k < kSliders; k++) {
            out.sliders[k] = stored[k];
            out.stored[k] = stored[k];
        }
        out.toggles = toggles;
        out.togglesKnown = 0b0111;   // DYN DECAY, MODE ENH, DECAY OPT (no MUTE)
    }

    std::vector<Event> tasks() const {
        std::vector<Event> out;
        for (const Event &e : log) {
            if (e.what < 10) {
                out.push_back(e);
            }
        }
        return out;
    }
};

// ---------------------------------------------------------------------
// A fake host: holds the parameters' normalized values; every value the
// plugin sets is echoed back later (as a host that re-sends what it
// recorded), jittered within the same raw step.
// ---------------------------------------------------------------------
class FakeHost : public HostParams {
public:
    float values[kParamCount];
    std::vector<std::pair<int, float>> echoes;
    int sets = 0;
    SliderRange range = kLarcRange;

    FakeHost() {
        for (int p = 0; p < kParamCount; p++) {
            values[p] = param(p).defaultNorm;
        }
        echoes.reserve(10000);
    }
    float get(int p) const override {
        return values[p];
    }
    void set(int p, float value) override {
        values[p] = value;
        echoes.push_back({p, value});
        sets++;
    }
    // Hand the echoes back, each moved by a fraction of its step (the same raw).
    void deliverEchoes() {
        for (const auto &[p, value] : echoes) {
            float step = 0.0f;
            const ParamInfo &info = param(p);
            if (info.kind == Kind::Slider) {
                step = 1.0f / float(range.max - range.min);
            } else if (info.kind == Kind::Program) {
                step = 1.0f / float(kProgramSlots - 1);
            } else if (info.kind == Kind::Variation) {
                step = 1.0f / float(kVariations - 1);
            }
            float jittered = value + 0.3f * step;
            if (jittered > 1.0f) {
                jittered = value - 0.3f * step;
            }
            values[p] = jittered;
        }
        echoes.clear();
    }
    // A user/automation move to a raw value.
    void move(int p, int raw) {
        values[p] = fromInt(p, raw, range);
    }
};

// The processor's loop, minus JUCE: per block drain, poll, run; a timer
// on the "message thread" runs the write-back.
struct Rig {
    FakeMachine machine;
    FakeHost host;
    Scheduler scheduler;
    CommandQueue queue;
    WriteBack writeBack;

    explicit Rig(SchedulerConfig config = {}) : scheduler(config) {}

    void block(int frames) {
        queue.drainInto(scheduler);
        scheduler.pollHost(host.values);
        scheduler.run(machine, frames, [&](int, int count, bool) { machine.advance(count); });
    }
    void blocks(int count, int frames = 256) {
        for (int i = 0; i < count; i++) {
            block(frames);
        }
    }
    int timer() {
        return writeBack.poll(scheduler.channel(), host);
    }
    // Run until the machine is idle and nothing is pending, with the timer
    // every `every` blocks. Returns false on timeout.
    bool settle(int maxBlocks = 4000, int every = 8, bool echo = true) {
        for (int i = 0; i < maxBlocks; i++) {
            block(256);
            if (i % every == 0) {
                timer();
                if (echo) {
                    host.deliverEchoes();
                }
            }
            Published s = scheduler.channel().read();
            if (s.ready != 0 && s.pendingMask == 0 && s.busyParam < 0 && !machine.busy() && i > 4) {
                timer();
                if (echo) {
                    host.deliverEchoes();
                }
                block(256);
                block(256);
                Published after = scheduler.channel().read();
                if (after.pendingMask == 0 && after.busyParam < 0 && !machine.busy() &&
                    writeBack.poll(scheduler.channel(), host) == 0) {
                    return true;
                }
            }
        }
        return false;
    }
    // Host and machine agree on every known operator parameter.
    bool consistent(std::string *why = nullptr) {
        Published s = scheduler.channel().read();
        for (int p = 0; p < kParamCount; p++) {
            if (!isOperatorKind(param(p).kind)) {
                continue;
            }
            int value = 0;
            if (!publishedValue(s, p, value)) {
                continue;
            }
            if (toInt(p, host.values[p], s.machine.range()) != value) {
                if (why != nullptr) {
                    *why = std::string(param(p).id) + " host " +
                           std::to_string(toInt(p, host.values[p], s.machine.range())) + " machine " +
                           std::to_string(value);
                }
                return false;
            }
        }
        return true;
    }
};

// =====================================================================
// Layout
// =====================================================================
static void testLayout() {
    std::set<std::string> ids;
    for (int p = 0; p < kParamCount; p++) {
        ids.insert(param(p).id);
        check(indexOf(param(p).id) == p, std::string("indexOf ") + param(p).id);
        check(param(p).defaultNorm >= 0.0f && param(p).defaultNorm <= 1.0f, std::string("default ") + param(p).id);
    }
    check(int(ids.size()) == kParamCount, "parameter IDs are unique");
    // The frozen v1 IDs (append only: this list must never change).
    const char *v1[] = {"program", "variation", "slider_1", "slider_36", "toggle_dyn_decay", "toggle_mode_enh",
                        "toggle_decay_opt", "toggle_mute", "level_db", "dry_wet", "output_left", "output_right",
                        "analog"};
    int v1Index[] = {0, 1, 2, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46};
    for (int i = 0; i < 13; i++) {
        check(indexOf(v1[i]) == v1Index[i], std::string("frozen index of ") + v1[i]);
    }
    check(indexOf("nope") == -1, "unknown ID");
    for (int k = 1; k <= kSliders; k++) {
        check(param(sliderParam(k)).sub == k && param(sliderParam(k)).kind == Kind::Slider,
              "slider_" + std::to_string(k));
        check(std::string(param(sliderParam(k)).id) == "slider_" + std::to_string(k), "slider id " + std::to_string(k));
    }
    // Round trips.
    SliderRange ranges[2] = {kLarcRange, kPanelRange};
    for (SliderRange range : ranges) {
        bool ok = true;
        for (int raw = range.min; raw <= range.max; raw++) {
            float n = fromInt(kSlider1, raw, range);
            if (toInt(kSlider1, n, range) != raw || n < 0.0f || n > 1.0f) {
                ok = false;
            }
            // anything within 0.49 of a step maps to the same raw
            float step = 1.0f / float(range.max - range.min);
            if (toInt(kSlider1, n + 0.49f * step, range) != raw && raw < range.max) {
                ok = false;
            }
            if (toInt(kSlider1, n - 0.49f * step, range) != raw && raw > range.min) {
                ok = false;
            }
        }
        check(ok, "slider raw round trip, range " + std::to_string(range.min) + ".." + std::to_string(range.max));
        check(toInt(kSlider1, 0.0f, range) == range.min && toInt(kSlider1, 1.0f, range) == range.max, "slider ends");
        check(toInt(kSlider1, -3.0f, range) == range.min && toInt(kSlider1, 7.0f, range) == range.max &&
                  toInt(kSlider1, std::nanf(""), range) == range.min,
              "slider clamps");
    }
    for (int i = 0; i < kProgramSlots; i++) {
        check(toInt(kProgram, fromInt(kProgram, i, kLarcRange), kLarcRange) == i, "program round trip");
    }
    for (int v = 1; v <= kVariations; v++) {
        check(toInt(kVariation, fromInt(kVariation, v, kLarcRange), kLarcRange) == v, "variation round trip");
    }
    for (int d = 0; d < 4; d++) {
        check(toInt(kOutputLeft, fromInt(kOutputLeft, d, kLarcRange), kLarcRange) == d, "output round trip");
    }
    check(toInt(kOutputLeft, param(kOutputLeft).defaultNorm, kLarcRange) == 0, "default L = A");
    check(toInt(kOutputRight, param(kOutputRight).defaultNorm, kLarcRange) == 2, "default R = C");
    check(std::fabs(levelDbOf(param(kLevelDb).defaultNorm)) < 1e-5f, "default level 0 dB");
    check(levelDbOf(0.0f) == -24.0f && levelDbOf(1.0f) == 30.0f, "level range");
    check(std::fabs(levelDbOf(levelNormOf(-6.0f)) + 6.0f) < 1e-5f, "level round trip");
    check(toInt(kAnalog, param(kAnalog).defaultNorm, kLarcRange) == 1, "default analog on");
    check(param(kDryWet).defaultNorm == 1.0f, "default wet");
    for (int t = 0; t < kToggles; t++) {
        check(param(toggleParam(t)).sub == t, "toggle index");
    }
}

// =====================================================================
// Queue
// =====================================================================
static void testQueue() {
    SpscRing<int, 8> ring;
    int out = 0;
    check(!ring.tryPop(out), "empty ring");
    for (int i = 0; i < 8; i++) {
        check(ring.tryPush(i), "push within capacity");
    }
    check(!ring.tryPush(99), "full ring refuses");
    for (int i = 0; i < 8; i++) {
        check(ring.tryPop(out) && out == i, "FIFO order");
    }

    // Overflow: the backlog coalesces per parameter, flushes in order.
    CommandQueue queue;
    for (size_t i = 0; i < CommandQueue::kCapacity; i++) {
        queue.push(kDryWet, 0.5f);
    }
    check(queue.backlogCount() == 0, "no backlog while the ring has room");
    queue.push(sliderParam(3), 0.1f);
    queue.push(sliderParam(5), 0.2f);
    queue.push(sliderParam(3), 0.3f);   // coalesces, keeps its place before slider_5
    check(queue.backlogCount() == 2, "backlog: one slot per parameter");

    struct Collect {
        std::vector<Command> got;
        void submit(const Command &c) {
            got.push_back(c);
        }
    } collect;
    queue.drainInto(collect);
    check(collect.got.size() == CommandQueue::kCapacity, "drain empties the ring");
    check(queue.flush(), "flush moves the backlog");
    collect.got.clear();
    queue.drainInto(collect);
    check(collect.got.size() == 2 && collect.got[0].param == sliderParam(3) && collect.got[0].value == 0.3f &&
              collect.got[1].param == sliderParam(5),
          "backlog order and latest value");

    // Threads: one producer, one consumer, nothing lost or reordered.
    SpscRing<Command, 64> shared;
    const int total = 200000;
    std::thread producer([&] {
        for (int i = 0; i < total; i++) {
            Command c;
            c.frame = uint64_t(i);
            while (!shared.tryPush(c)) {
                std::this_thread::yield();
            }
        }
    });
    int next = 0;
    bool ordered = true;
    while (next < total) {
        Command c;
        if (shared.tryPop(c)) {
            if (c.frame != uint64_t(next)) {
                ordered = false;
            }
            next++;
        }
    }
    producer.join();
    check(ordered, "SPSC across threads: every command, in order");

    // Seqlock across threads: a reader never sees a torn snapshot.
    Seqlock<Published> lock;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        Published p;
        for (uint32_t g = 1; !stop.load(); g++) {
            p.generation = g;
            p.machine.program = int32_t(g);
            for (int k = 0; k < kSliders; k++) {
                p.machine.sliders[k] = uint8_t(g);
            }
            for (int i = 0; i < kParamCount; i++) {
                p.hostSeen[i] = float(g);
            }
            lock.write(p);
        }
    });
    bool whole = true;
    uint32_t lastSeen = 0;
    for (int i = 0; i < 200000; i++) {
        Published p = lock.read();
        if (p.generation == 0) {
            continue;
        }
        if (uint32_t(p.machine.program) != p.generation || p.machine.sliders[35] != uint8_t(p.generation) ||
            p.hostSeen[kParamCount - 1] != float(p.generation) || p.generation < lastSeen) {
            whole = false;
        }
        lastSeen = p.generation;
    }
    stop = true;
    writer.join();
    check(whole, "seqlock: consistent snapshots across threads");
}

// =====================================================================
// Scheduler: coalescing, hold/replay, supersede, no-ops, rejection
// =====================================================================
static void testCoalescing() {
    Rig rig;
    rig.blocks(4);
    rig.host.move(kProgram, 2);   // a long load
    rig.blocks(2);
    check(rig.machine.busy() && rig.machine.taskKind == 0, "program load running");
    for (int i = 0; i < 100; i++) {
        rig.host.move(sliderParam(3), 20 + i);   // a drag, one value per block
        rig.block(64);
    }
    check(rig.settle(), "coalescing settles");
    std::vector<Event> tasks = rig.machine.tasks();
    int moves = 0;
    int last = -1;
    for (const Event &e : tasks) {
        if (e.what == 2 && e.a == 3) {
            moves++;
            last = int(e.b);
        }
    }
    check(moves <= 3, "a 100-step drag during a load becomes at most a few moves (" + std::to_string(moves) + ")");
    check(last == 119, "the latest value wins");
    check(rig.machine.stored[2] == 119, "machine holds the final value");
    std::string why;
    check(rig.consistent(&why), "coalescing: host == machine " + why);

    // Queue path: the editor pushes 50 values between two blocks.
    Rig rig2;
    rig2.blocks(4);
    for (int i = 0; i < 50; i++) {
        rig2.queue.push(sliderParam(2), fromInt(sliderParam(2), 60 + i, kLarcRange));
    }
    rig2.blocks(40);
    int moves2 = 0;
    for (const Event &e : rig2.machine.tasks()) {
        if (e.what == 2) {
            moves2++;
            check(e.b == 109.0f, "queued drag: one move to the latest");
        }
    }
    check(moves2 == 1, "queued drag coalesces to one move");
}

static void testHoldReplay() {
    Rig rig;
    rig.machine.isReady = false;
    rig.machine.program = 1;
    rig.machine.variation = 1;
    rig.machine.loadPreset();
    auto at = [&](int p, int raw) {
        Command c;
        c.param = uint16_t(p);
        c.value = fromInt(p, raw, kLarcRange);
        rig.scheduler.submit(c);
        rig.block(128);
    };
    at(sliderParam(3), 50);         // superseded by the program
    at(kProgram, 2);
    at(sliderParam(3), 60);         // superseded by the variation
    at(kVariation, 4);
    at(toggleParam(kDynDecay), 0);
    Command level;
    level.param = kLevelDb;
    level.value = levelNormOf(-6.0f);
    rig.scheduler.submit(level);
    at(sliderParam(5), 70);
    at(sliderParam(3), 80);         // after the variation: kept, after slider_5
    at(sliderParam(5), 75);         // coalesces, keeps its place
    rig.blocks(20);
    check(rig.machine.tasks().empty(), "nothing dispatched before ready");
    bool anyDirect = false;
    for (const Event &e : rig.machine.log) {
        if (e.what >= 10) {
            anyDirect = true;
        }
    }
    check(!anyDirect, "directs held before ready");
    Published s = rig.scheduler.channel().read();
    check(s.ready == 0, "published not ready");
    check(rig.timer() == 0, "no write-back before ready");

    rig.machine.isReady = true;   // the swap
    uint64_t readyAt = rig.machine.clock;
    check(rig.settle(), "hold/replay settles");
    std::vector<Event> tasks = rig.machine.tasks();
    std::vector<std::pair<int, int>> got;
    for (const Event &e : tasks) {
        if (e.what == 2) {
            got.push_back({e.what * 100 + e.a, int(e.b)});
        } else {
            got.push_back({e.what * 100 + e.a, int(e.b)});
        }
    }
    std::vector<std::pair<int, int>> want = {{2, 0}, {104, 0}, {205, 75}, {203, 80}, {300, 0}};
    check(got == want, "replay order: program, variation, sliders by arrival, toggles; latest values");
    if (got != want) {
        for (auto &g : got) {
            std::printf("  got %d %d\n", g.first, g.second);
        }
    }
    check(!tasks.empty() && tasks[0].frame == readyAt, "first replayed task starts at the ready frame");
    bool levelApplied = false;
    for (const Event &e : rig.machine.log) {
        if (e.what == 10) {
            levelApplied = e.frame == readyAt && std::fabs(e.b + 6.0f) < 1e-4f;
        }
    }
    check(levelApplied, "held level applied at ready");
    int directs = 0;
    for (const Event &e : rig.machine.log) {
        if (e.what >= 10 && e.frame == readyAt) {
            directs++;
        }
    }
    check(directs == 5, "all directs (re)applied at ready");
    std::string why;
    check(rig.consistent(&why), "hold/replay: host == machine " + why);

    // Held across a load: moves during a load run after it, on the new program.
    Rig rig2;
    rig2.blocks(4);
    rig2.host.move(kProgram, 4);
    rig2.blocks(3);
    rig2.host.move(sliderParam(20), 33);   // program 4 has 26 sliders; program 0 only 10
    rig2.blocks(3);
    check(rig2.settle(), "held move settles");
    std::vector<Event> t2 = rig2.machine.tasks();
    check(t2.size() == 2 && t2[0].what == 0 && t2[1].what == 2 && t2[1].a == 20 && t2[1].b == 33.0f,
          "a move made during a load runs after it");
    check(rig2.machine.stored[19] == 33, "held move applied to the new program");

    // Engine replacement: not ready again, pending kept; clearPending drops them.
    Rig rig3;
    rig3.blocks(4);
    rig3.machine.isReady = false;
    rig3.blocks(2);
    rig3.host.move(sliderParam(2), 99);
    rig3.blocks(2);
    rig3.scheduler.clearPending();
    rig3.machine.isReady = true;
    rig3.blocks(40);
    check(rig3.machine.tasks().empty(), "clearPending drops held requests");
}

static void testSupersedeNoopReject() {
    Rig rig;
    rig.blocks(4);
    rig.timer();   // push the booted machine's values
    rig.host.deliverEchoes();
    rig.blocks(10);
    check(rig.machine.tasks().empty(), "the boot write-back and its echoes dispatch nothing");
    check(rig.scheduler.droppedCount() > 0, "echoes were seen and dropped as no-ops");

    // No-op: a move to the value the machine already holds.
    rig.host.move(sliderParam(4), rig.machine.stored[3]);
    rig.blocks(10);
    check(rig.machine.tasks().empty(), "a move to the held value is dropped");

    // Move away and back while a task runs: nothing left to do but the first.
    int original = rig.machine.stored[5];
    rig.host.move(sliderParam(7), 200);   // starts at once
    rig.block(128);
    rig.host.move(sliderParam(6), 150);   // pending
    rig.block(128);
    rig.host.move(sliderParam(6), original);   // back: pending replaced, dropped at dispatch
    check(rig.settle(), "away-and-back settles");
    int moves6 = 0;
    for (const Event &e : rig.machine.tasks()) {
        if (e.what == 2 && e.a == 6) {
            moves6++;
        }
    }
    check(moves6 == 0, "away-and-back while busy dispatches nothing for that slider");

    // Rejection: program 20 does not exist; the write-back puts the host back.
    int before = rig.machine.program;
    rig.host.move(kProgram, 20);
    check(rig.settle(), "rejection settles");
    check(rig.scheduler.rejectedCount() == 1, "program 20 rejected");
    check(toInt(kProgram, rig.host.values[kProgram], kLarcRange) == before, "host program restored after rejection");
    // Variation 5 on program 0 (only V1) is rejected too.
    rig.host.move(kVariation, 5);
    check(rig.settle(), "variation rejection settles");
    check(toInt(kVariation, rig.host.values[kVariation], kLarcRange) == 1, "host variation restored");
    // slider_30 on a program with 10 sliders: rejected, host put back... to
    // nothing: the machine has no slider_30, so the host keeps its value.
    rig.host.move(sliderParam(30), 77);
    check(rig.settle(), "slider rejection settles");
    // Mute is not offered by this fake remote: rejected; its state is unknown
    // so the host keeps the value (nothing to push).
    rig.host.move(toggleParam(kMute), 1);
    check(rig.settle(), "mute rejection settles");
    std::string why;
    check(rig.consistent(&why), "after rejections: host == machine " + why);
}

// =====================================================================
// Write-back and echo suppression, with a host that echoes
// =====================================================================
static void testWriteBackEcho() {
    Rig rig;
    rig.machine.program = 2;   // boots on another program than the host's default
    rig.machine.variation = 3;
    rig.machine.loadPreset();
    rig.blocks(2);
    int pushed = rig.timer();
    check(pushed > 10, "first write-back pushes the booted machine's values (" + std::to_string(pushed) + ")");
    check(toInt(kProgram, rig.host.values[kProgram], kLarcRange) == 2, "program pushed");
    check(toInt(kVariation, rig.host.values[kVariation], kLarcRange) == 3, "variation pushed");
    check(toInt(toggleParam(kDynDecay), rig.host.values[toggleParam(kDynDecay)], kLarcRange) == 1, "toggle pushed");
    rig.blocks(4);
    check(rig.machine.tasks().empty(), "pushed values do not come back as moves");
    rig.host.deliverEchoes();   // the host hands every value back, jittered
    rig.blocks(4);
    rig.timer();
    rig.host.deliverEchoes();
    rig.blocks(4);
    check(rig.machine.tasks().empty(), "echoed (jittered) values do not come back as moves");
    std::string why;

    // Late echoes: the host re-sends the old program's values, unchanged,
    // while the next load is already running. They must not become moves.
    // (A late echo JITTERED off the value the host already holds would be
    // seen as a move and applied after the load: the host and machine still
    // agree afterwards, but the preset is lost. Real hosts echo the value
    // they hold; the scheduler cannot tell such a value from a deliberate
    // move to it, which restore and automation need.)
    rig.host.move(kProgram, 4);
    std::vector<std::pair<int, float>> late;
    for (int k = 1; k <= 10; k++) {
        late.push_back({sliderParam(k), rig.host.values[sliderParam(k)]});
    }
    rig.blocks(3);
    check(rig.machine.busy() && rig.machine.taskKind == 0, "load 4 running");
    for (const auto &[p, value] : late) {
        rig.host.values[p] = value;
    }
    rig.blocks(3);
    check(rig.settle(), "late echoes settle");
    int lateMoves = 0;
    for (const Event &e : rig.machine.tasks()) {
        if (e.what == 2) {
            lateMoves++;
        }
    }
    check(lateMoves == 0, "late echoes during a load are not moves");
    check(rig.consistent(&why), "after late echoes: host == machine " + why);
    rig.machine.log.clear();

    // A program change from the host: after the load the presets are pushed.
    rig.host.move(kProgram, 5);
    check(rig.settle(), "program change settles");
    std::vector<Event> tasks = rig.machine.tasks();
    check(tasks.size() == 1 && tasks[0].what == 0 && tasks[0].a == 5, "one task: the load (no echo moves)");
    check(rig.consistent(&why), "after the load: host == machine " + why);
    for (int k = 1; k <= FakeMachine::sliderCount(5); k++) {
        check(toInt(sliderParam(k), rig.host.values[sliderParam(k)], kLarcRange) == rig.machine.preset(5, 1, k),
              "host slider_" + std::to_string(k) + " shows the preset");
    }

    // A move the firmware stores differently (slider_1 keeps multiples of 4):
    // the host follows, and that push is not a move either.
    rig.host.move(sliderParam(1), 101);
    check(rig.settle(), "quantized move settles");
    check(rig.machine.stored[0] == 100, "machine stored 100");
    check(toInt(sliderParam(1), rig.host.values[sliderParam(1)], kLarcRange) == 100, "host follows to 100");
    int moves = 0;
    for (const Event &e : rig.machine.tasks()) {
        if (e.what == 2) {
            moves++;
        }
    }
    check(moves == 1, "the 101 -> 100 write-back did not echo into a second move");

    // During a drag the write-back never pushes the dragged slider back.
    int lastPush = -1;
    for (int i = 0; i < 60; i++) {
        rig.host.move(sliderParam(2), 40 + 2 * i);
        rig.block(512);
        uint64_t setsBefore = uint64_t(rig.host.sets);
        float before = rig.host.values[sliderParam(2)];
        rig.timer();
        if (rig.host.values[sliderParam(2)] != before) {
            lastPush = i;
        }
        (void)setsBefore;
    }
    check(lastPush == -1, "no write-back to a slider while the user drags it");
    check(rig.settle(), "drag settles");
    check(rig.machine.stored[1] == 158, "drag ends on the last value");
    check(rig.consistent(&why), "after the drag: host == machine " + why);

    // Race: the host moves between a publish and the timer: its move wins.
    rig.host.move(kVariation, 4);
    for (int i = 0; i < 200 && (rig.machine.busy() || rig.machine.tasks().back().what != 1); i++) {
        rig.block(256);
    }
    rig.blocks(2);   // the load has ended and been published; the timer has not run
    rig.host.move(sliderParam(4), 222);   // automation moves right now
    rig.timer();
    check(toInt(sliderParam(4), rig.host.values[sliderParam(4)], kLarcRange) == 222,
          "a host move made after the publish is not overwritten");
    check(rig.writeBack.skippedMovedCount() > 0, "skipped because the host moved");
    check(rig.settle(), "race settles");
    check(rig.machine.stored[3] == 222, "the host's move reached the machine");
    check(rig.consistent(&why), "after the race: host == machine " + why);
}

// =====================================================================
// Determinism: a stamped timeline dispatches the same tasks at the same
// frames whatever the host block size.
// =====================================================================
static std::vector<Event> runTimeline(const std::vector<Command> &timeline, int blockSize, uint64_t total,
                                      uint64_t readyAt, std::mt19937 *randomBlocks,
                                      std::vector<uint64_t> *gridEnds) {
    FakeMachine machine;
    machine.isReady = readyAt == 0;
    Scheduler scheduler;
    size_t next = 0;
    uint64_t frame = 0;
    while (frame < total) {
        int n = blockSize;
        if (randomBlocks != nullptr) {
            n = 1 + int((*randomBlocks)() % 3000);
        }
        if (frame < readyAt && frame + uint64_t(n) > readyAt) {
            n = int(readyAt - frame);   // the swap happens at a block start
        }
        if (frame + uint64_t(n) > total) {
            n = int(total - frame);
        }
        if (frame == readyAt) {
            machine.isReady = true;
        }
        // Events due in this block are submitted at its start, stamped.
        while (next < timeline.size() && timeline[next].frame < frame + uint64_t(n)) {
            scheduler.submit(timeline[next]);
            next++;
        }
        scheduler.run(machine, n, [&](int, int count, bool gridEnd) {
            machine.advance(count);
            if (gridEnd && gridEnds != nullptr) {
                gridEnds->push_back(machine.clock);
            }
        });
        frame += uint64_t(n);
    }
    return machine.log;
}

static void testDeterminism() {
    std::mt19937 rng(12345);
    std::vector<Command> timeline;
    uint64_t total = 48000 * 20;
    for (int i = 0; i < 400; i++) {
        Command c;
        c.frame = uint64_t(rng() % total);
        int r = int(rng() % 100);
        if (r < 3) {
            c.param = kProgram;
            c.value = fromInt(kProgram, int(rng() % 7), kLarcRange);
        } else if (r < 6) {
            c.param = kVariation;
            c.value = fromInt(kVariation, 1 + int(rng() % 8), kLarcRange);
        } else if (r < 10) {
            c.param = uint16_t(toggleParam(int(rng() % 4)));
            c.value = float(rng() % 2);
        } else if (r < 16) {
            c.param = uint16_t(kLevelDb + int(rng() % 5));
            c.value = float(rng() % 1000) / 999.0f;
        } else {
            c.param = uint16_t(sliderParam(1 + int(rng() % 36)));
            c.value = float(rng() % 1000) / 999.0f;
        }
        timeline.push_back(c);
    }
    std::stable_sort(timeline.begin(), timeline.end(),
                     [](const Command &a, const Command &b) { return a.frame < b.frame; });

    for (uint64_t readyAt : {uint64_t(0), uint64_t(50001)}) {
        std::vector<uint64_t> refGrid;
        std::vector<Event> reference = runTimeline(timeline, 128, total, readyAt, nullptr, &refGrid);
        size_t taskCount = 0;
        for (const Event &e : reference) {
            if (e.what < 10) {
                taskCount++;
            }
        }
        check(taskCount > 50, "the timeline dispatches tasks (" + std::to_string(taskCount) + ")");
        std::string tag = " (ready at " + std::to_string(readyAt) + ")";
        for (int blockSize : {1, 7, 64, 100, 256, 500, 1024, 4096}) {
            std::vector<uint64_t> grid;
            std::vector<Event> got = runTimeline(timeline, blockSize, total, readyAt, nullptr, &grid);
            check(got == reference, "same dispatches with block size " + std::to_string(blockSize) + tag);
            check(grid == refGrid, "same grid points with block size " + std::to_string(blockSize) + tag);
        }
        std::mt19937 blocks(777);
        std::vector<Event> got = runTimeline(timeline, 0, total, readyAt, &blocks, nullptr);
        check(got == reference, "same dispatches with random block sizes" + tag);
    }
}

// =====================================================================
// No allocation on the audio side (and in the write-back).
// =====================================================================
static void testNoAllocation() {
    Rig rig;
    rig.blocks(4);
    std::mt19937 rng(99);
    float editorValues[64];
    for (int i = 0; i < 64; i++) {
        editorValues[i] = float(rng() % 1000) / 999.0f;
    }
    uint64_t newsBefore = watchedNews.load();
    uint64_t deletesBefore = watchedDeletes.load();
    for (int b = 0; b < 3000; b++) {
        // host automation and editor commands
        if (b % 3 == 0) {
            rig.host.move(sliderParam(1 + int(rng() % 36)), 2 + int(rng() % 253));
        }
        if (b % 97 == 0) {
            rig.host.move(kProgram, int(rng() % 6));
        }
        if (b % 13 == 0) {
            rig.host.values[kLevelDb] = editorValues[b % 64];
        }
        watching = true;
        if (b % 5 == 0) {
            rig.queue.push(sliderParam(2), editorValues[b % 64]);
        }
        rig.queue.flush();
        rig.block(1 + int(rng() % 700));
        if (b % 10 == 0) {
            rig.timer();
        }
        watching = false;
        rig.host.echoes.clear();
    }
    check(rig.scheduler.dispatchedCount() > 20, "the no-allocation run exercised dispatches");
    check(watchedNews.load() == newsBefore && watchedDeletes.load() == deletesBefore,
          "no operator new/delete in drain/poll/run/publish/write-back (" +
              std::to_string(watchedNews.load() - newsBefore) + " news)");
    // The timeline's overflow path too.
    Scheduler scheduler;
    FakeMachine machine;
    watching = true;
    for (int i = 0; i < 2000; i++) {
        Command c;
        c.param = uint16_t(sliderParam(1 + i % 36));
        c.value = 0.5f;
        c.frame = uint64_t(100000 + i);
        scheduler.submit(c);
    }
    scheduler.run(machine, 4096, [&](int, int count, bool) { machine.advance(count); });
    watching = false;
    check(watchedNews.load() == newsBefore, "timeline overflow does not allocate");
}

// =====================================================================
// Rate limit: minGapFrames between tasks.
// =====================================================================
static void testRateLimit() {
    SchedulerConfig config;
    config.minGapFrames = 9600;
    Rig rig(config);
    rig.blocks(4);
    for (int k = 1; k <= 6; k++) {
        rig.host.move(sliderParam(k), 10 + k);
    }
    check(rig.settle(), "rate-limited moves settle");
    std::vector<Event> tasks = rig.machine.tasks();
    check(tasks.size() == 6, "six moves");
    bool spaced = true;
    for (size_t i = 1; i < tasks.size(); i++) {
        if (tasks[i].frame < tasks[i - 1].frame + FakeMachine::kSliderFrames + config.minGapFrames) {
            spaced = false;
        }
    }
    check(spaced, "tasks start at least minGapFrames after the previous one ended");
}

// =====================================================================
// Catalog change: host values are not replayed as moves.
// =====================================================================
static void testCatalogChange() {
    Rig rig;
    rig.blocks(4);
    check(rig.settle(), "boot settles");
    rig.machine.isReady = false;   // a new firmware set: the old machine goes
    rig.blocks(4);
    rig.machine.range = kPanelRange;   // the new machine: panel range, its own program
    rig.machine.program = 3;
    rig.machine.variation = 1;
    rig.machine.loadPreset();
    rig.host.range = kPanelRange;
    rig.machine.isReady = true;
    check(rig.settle(), "new machine settles");
    check(rig.machine.tasks().empty(), "a catalog change sends no moves");
    std::string why;
    check(rig.consistent(&why), "the new machine's values are pushed " + why);
}

// =====================================================================
// State
// =====================================================================
static void testStateFormat() {
    using namespace lexstate;
    PluginState state;
    state.rom = "eb3a7a765a703ece";
    state.share = "fw=eb3a7a765a703ece&p=1.1&v=2&s=1.0.200,2.4.30&t=DYN_DECAY-1,MODE_ENH-0";
    state.levelDb = -7.25f;
    state.dryWet = 0.3333333f;
    state.outLeft = 1;
    state.outRight = 3;
    state.analog = false;
    std::string blob = serialise(state);
    PluginState back;
    std::string error;
    check(parse(blob, back, &error), "parse ok");
    check(back == state, "state round trip");
    check(serialise(back) == blob, "serialise is stable");
    check(blob.rfind("lexplug-state 1\n", 0) == 0, "magic line");

    // Defaults, unknown keys kept and re-saved, CRLF, bad values.
    std::string newer = "lexplug-state 3\r\nrom abc\r\nfuture_key some value\r\nlevel_db 99\r\nanalog x\r\n"
                        "output_left Z\r\nmystery 1\r\n";
    PluginState read;
    std::vector<std::string> problems;
    check(parse(newer, read, &error, &problems), "a newer blob parses");
    check(read.version == 3 && read.rom == "abc" && read.share.empty(), "known keys of a newer blob");
    check(read.levelDb == 0.0f && read.analog && read.outLeft == 0 && read.outRight == 2 && read.dryWet == 1.0f,
          "bad values keep defaults");
    check(problems.size() == 3, "bad values reported (" + std::to_string(problems.size()) + ")");
    check(read.extra.size() == 2 && read.extra[0].first == "future_key" && read.extra[0].second == "some value",
          "unknown keys kept");
    std::string resaved = serialise(read);
    check(resaved.find("future_key some value\n") != std::string::npos && resaved.find("mystery 1\n") != std::string::npos,
          "unknown keys re-saved");

    check(!parse("", read, &error), "empty blob refused");
    check(!parse("<xml/>", read, &error), "foreign blob refused");
    check(!parse("lexplug-state x\n", read, &error), "bad version refused");
    PluginState defaults;
    check(parse("lexplug-state 1\n", read, &error) && read == defaults, "bare magic = defaults");

    // Newlines in values cannot split lines.
    PluginState evil;
    evil.share = "fw=a&p=1.1\nlevel_db 30";
    check(parse(serialise(evil), read) && read.levelDb == 0.0f, "a newline in a value does not inject a key");

    // Missing ROMs: the kept state survives a save.
    StateKeeper keeper;
    keeper.restored(state);
    PluginState live;   // no machine: empty rom/share; live directs
    live.levelDb = 3.0f;
    PluginState saved = keeper.forSave(live);
    check(saved.rom == state.rom && saved.share == state.share && saved.levelDb == 3.0f,
          "saving while ROMs are missing keeps the restored rom/share");
    PluginState again;
    check(parse(serialise(saved), again) && again.share == state.share, "and it round-trips");
    keeper.applied();
    live.rom = "other";
    live.share = "fw=other&p=2.1";
    check(keeper.forSave(live).share == live.share, "once applied, the live state is saved");

    float values[kParamCount] = {};
    directValues(state, values);
    check(std::fabs(levelDbOf(values[kLevelDb]) + 7.25f) < 1e-4f && toInt(kOutputLeft, values[kOutputLeft], kLarcRange) == 1 &&
              toInt(kOutputRight, values[kOutputRight], kLarcRange) == 3 && toInt(kAnalog, values[kAnalog], kLarcRange) == 0,
          "directValues");
}

// A machine backed by a real catalog: loads set the presets, moves set
// stored bytes (for the save -> restore round trip).
class CatalogMachine : public OperatorSink {
public:
    explicit CatalogMachine(const lexcat::Catalog &catalog) : catalog_(catalog) {}
    const lexcat::Catalog &catalog_;
    int program = 0;
    int variation = 1;
    std::vector<std::vector<int>> stored;
    uint8_t toggles = 0;
    int busyFrames = 0;

    void load(int index, int v) {
        program = index;
        variation = v;
        const lexcat::Program &p = catalog_.programs[size_t(index)];
        const std::vector<std::vector<int>> *raw = p.rawFor(p.presetVariation(v));
        stored.clear();
        for (size_t i = 0; i < p.pages.size(); i++) {
            stored.emplace_back(p.pages[i].sliders.size(), 0);
            if (raw != nullptr && i < raw->size()) {
                for (size_t s = 0; s < stored[i].size() && s < (*raw)[i].size(); s++) {
                    stored[i][s] = (*raw)[i][s];
                }
            }
        }
    }
    bool ready() const override {
        return true;
    }
    bool busy() const override {
        return busyFrames > 0;
    }
    void advance(int frames) {
        busyFrames -= frames;
        if (busyFrames < 0) {
            busyFrames = 0;
        }
    }
    bool loadProgram(int index) override {
        if (index < 0 || size_t(index) >= catalog_.programs.size()) {
            return false;
        }
        load(index, 1);
        busyFrames = 20000;
        return true;
    }
    bool loadVariation(int v) override {
        const lexcat::Program &p = catalog_.programs[size_t(program)];
        bool offered = false;
        for (int x : p.variations) {
            if (x == v) {
                offered = true;
            }
        }
        if (!offered) {
            return false;
        }
        load(program, v);
        busyFrames = 12000;
        return true;
    }
    bool moveSlider(int k, int raw) override {
        const lexcat::Program &p = catalog_.programs[size_t(program)];
        if (k < 1 || size_t(k) > p.generic.size()) {
            return false;
        }
        const lexcat::SliderRef &ref = p.generic[size_t(k - 1)];
        stored[size_t(ref.pageIndex)][size_t(ref.slot)] = raw;
        busyFrames = 4800;
        return true;
    }
    bool setToggle(int toggle, bool on) override {
        bool offered = false;
        for (const lexcat::Toggle &t : catalog_.toggles()) {
            if (t.label == kToggleLabels[toggle]) {
                offered = true;
            }
        }
        if (!offered) {
            return false;
        }
        if (on) {
            toggles = uint8_t(toggles | (1u << toggle));
        } else {
            toggles = uint8_t(toggles & ~(1u << toggle));
        }
        busyFrames = 6000;
        return true;
    }
    void setLevelDb(float) override {}
    void setDryWet(float) override {}
    void setOutput(int, int) override {}
    void setAnalog(bool) override {}
    void readState(MachineState &out) override {
        out = MachineState{};
        const lexcat::Program &p = catalog_.programs[size_t(program)];
        out.program = program;
        out.variation = variation;
        out.sliderCount = int(p.generic.size());
        out.rangeMin = catalog_.rawMin();
        out.rangeMax = catalog_.rawMax();
        for (size_t k = 0; k < p.generic.size() && k < size_t(kSliders); k++) {
            const lexcat::SliderRef &ref = p.generic[k];
            out.stored[k] = uint8_t(stored[size_t(ref.pageIndex)][size_t(ref.slot)]);
            out.sliders[k] = out.stored[k];
        }
        for (const lexcat::Toggle &t : catalog_.toggles()) {
            for (int i = 0; i < kToggles; i++) {
                if (t.label == kToggleLabels[i]) {
                    out.togglesKnown = uint8_t(out.togglesKnown | (1u << i));
                }
            }
        }
        out.toggles = uint8_t(toggles & out.togglesKnown);
    }
};

static void testShareRoundTrip(const std::string &web) {
    using namespace lexstate;
    const char *files[] = {"90b0c38075310544", "b9eedc4b2c599a55", "eb3a7a765a703ece", "f9fea5a506d9816d",
                           "fe6fd343357b73f0"};
    std::mt19937 rng(4242);
    int roundTrips = 0;
    for (const char *file : files) {
        lexcat::Catalog catalog = lexcat::Catalog::parse(readFile(web + "/catalogs/" + file + ".json"));
        check(int(catalog.programs.size()) <= kProgramSlots, std::string(file) + ": programs fit the program parameter");
        for (const lexcat::Program &program : catalog.programs) {
            check(int(program.generic.size()) <= kSliders, program.key + ": sliders fit");
            if (!program.allPagesHaveColumns()) {
                continue;
            }
            for (int v : program.variations) {
                // The machine that saves: load, move some sliders, set toggles.
                CatalogMachine saver(catalog);
                saver.load(catalog.indexOf(program.key), v);
                for (size_t k = 1; k <= program.generic.size(); k++) {
                    if (rng() % 3 == 0) {
                        const lexcat::SliderRef &ref = program.generic[k - 1];
                        saver.stored[size_t(ref.pageIndex)][size_t(ref.slot)] =
                            catalog.rawMin() + int(rng() % unsigned(catalog.rawMax() - catalog.rawMin() + 1));
                    }
                }
                saver.toggles = uint8_t(rng() % 16);
                MachineState saved;
                saver.readState(saved);
                std::string share = shareFromMachine(catalog, saved);
                check(!share.empty(), program.key + ": share payload");
                PluginState state;
                state.rom = catalog.rom;
                state.share = share;
                PluginState parsed;
                check(parse(serialise(state), parsed) && parsed.share == share, "state carries the payload");

                // A new instance restores it through the scheduler.
                lexcat::ShareState link;
                check(lexcat::readSharePayload(parsed.share, link), program.key + ": payload reads back");
                std::vector<std::vector<int>> expected;
                check(expectedStored(catalog, link, expected), program.key + ": expected stored");
                std::vector<Command> commands;
                std::vector<std::string> problems;
                check(restoreCommands(catalog, link, commands, &problems), program.key + ": restore commands");
                check(problems.empty(), program.key + ": no restore problems");

                CatalogMachine restorer(catalog);
                restorer.load(0, 1);
                Scheduler scheduler;
                for (const Command &c : commands) {
                    scheduler.submit(c);
                }
                for (int i = 0; i < 2000 && (scheduler.busyParam() >= 0 || restorer.busy() || i < 3); i++) {
                    scheduler.run(restorer, 512, [&](int, int count, bool) { restorer.advance(count); });
                }
                scheduler.run(restorer, 512, [&](int, int count, bool) { restorer.advance(count); });
                check(restorer.program == saver.program && restorer.variation == saver.variation,
                      program.key + " V" + std::to_string(v) + ": program/variation restored");
                check(restorer.stored == saver.stored, program.key + " V" + std::to_string(v) + ": stored bytes restored");
                if (restorer.stored != saver.stored && getenv("PARAMS_DEBUG") != nullptr) {
                    std::printf("  payload %s\n", share.c_str());
                    for (size_t i = 0; i < saver.stored.size(); i++) {
                        for (size_t s = 0; s < saver.stored[i].size(); s++) {
                            if (saver.stored[i][s] != restorer.stored[i][s]) {
                                std::printf("  page %zu slot %zu saver %d restorer %d expected %d\n", i, s,
                                            saver.stored[i][s], restorer.stored[i][s], expected[i][s]);
                            }
                        }
                    }
                    for (const Command &c : commands) {
                        std::printf("  cmd %d %f\n", c.param, c.value);
                    }
                }
                check(restorer.stored == expected, program.key + ": expectedStored agrees");
                MachineState after;
                restorer.readState(after);
                check(after.toggles == saved.toggles, program.key + ": toggles restored");
                check(shareFromMachine(catalog, after) == share, program.key + ": saving again gives the same payload");
                roundTrips++;
            }
        }
    }
    check(roundTrips > 50, "share round trips ran (" + std::to_string(roundTrips) + ")");
    std::printf("  share round trips: %d program/variation states over 5 catalogs\n", roundTrips);
}

static void report(const char *name, bool ok) {
    if (ok) {
        std::printf("%-26s ok\n", name);
    } else {
        std::printf("%-26s FAILED\n", name);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: params_test WEB_DIR\n");
        return 2;
    }
    std::string web = argv[1];
    struct Test {
        const char *name;
        void (*run)();
    };
    Test tests[] = {
        {"layout", testLayout},
        {"queue", testQueue},
        {"coalescing", testCoalescing},
        {"hold/replay", testHoldReplay},
        {"supersede/no-op/reject", testSupersedeNoopReject},
        {"write-back + echo", testWriteBackEcho},
        {"determinism", testDeterminism},
        {"no allocation", testNoAllocation},
        {"rate limit", testRateLimit},
        {"catalog change", testCatalogChange},
        {"state format", testStateFormat},
    };
    for (const Test &t : tests) {
        int before = failures;
        t.run();
        report(t.name, failures == before);
    }
    int before = failures;
    testShareRoundTrip(web);
    report("share/state round trip", failures == before);
    std::printf("%d checks, %d failures\n", checks, failures);
    if (failures == 0) {
        return 0;
    }
    return 1;
}
