// The machine as a Rack module drives it, one host sample at a time. No Rack
// here, so tests/core_check.cpp can drive it without the SDK.
//
// The JUCE plugin's machinery, unchanged: a LiveMachine (the Session, the
// operator for the set's remote, the RAM reader), the MachineSink and the
// lexparams::Scheduler. Programs, variations and slider moves are Commands
// for that scheduler (parameter index + normalized value, layout.hpp); it
// runs them as operator tasks one at a time, and publishes what the machine
// holds (Published, a seqlock any thread can read).
//
// A machine is built and booted (16 s of silence, then the operator's own
// startup, as the JUCE plugin's boot job) on a worker thread, then handed to
// the audio thread through an atomic pointer. The machine it replaces is
// parked in `retired_` and freed by the next boot or the destructor, never
// on the audio thread.
//
// The catalog (program names, pages, slider names and tables) is found by
// the set's hash in `catalog_dir` (<hash>.json, stored/<hash>.stored.json).
// Without one the machine runs with no operator (NoOperator): it plays its
// power-on program and takes no commands.
//
// Host samples go through lexplug::RateBridge (a straight wire at 48 kHz),
// one at a time. Outputs: two of A-D (A and C by default), mixed with the
// input as the JUCE plugin's render_chunk.
#pragma once
#include "live_machine.hpp"
#include "denormals.hpp"
#include "rate_bridge.hpp"
#include "rom_loading.hpp"
#include "roms/sha256.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace lexvcv {

enum class Status { NoRoms, Booting, Running, Failed };

// Paths are UTF-8 strings everywhere here, as Rack's are (osdialog, the
// patch, asset::plugin). std::filesystem::path(std::string) reads the bytes
// in the ANSI code page on Windows, so a folder named with anything outside
// ASCII would not be found; these convert explicitly.
inline std::filesystem::path utf8_path(const std::string &s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
inline std::string path_utf8(const std::filesystem::path &p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

class Core {
public:
    static constexpr uint64_t boot_frames = 16 * 48000;

    Core() {
        std::fill(std::begin(early_), std::end(early_), -1);
        set_sample_rate(48000);
    }

    ~Core() {
        stop_worker();
        sink_.attach(nullptr);
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
        delete live_;
    }

    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;

    // Where the catalogs are; empty: none (the machine then has no operator).
    void set_catalog_dir(std::string dir) {
        catalog_dir_ = std::move(dir);
    }

    // Not on the audio thread (allocates); Rack calls onSampleRateChange
    // with the engine stopped.
    void set_sample_rate(int rate) {
        host_rate_ = rate;
        bridge_.setup(rate, 1);
        for (auto &s : scratch_) {
            s.assign(size_t(bridge_.max_internal()), 0.0f);
        }
        for (auto &s : gained_) {
            s.assign(size_t(bridge_.max_internal()), 0.0f);
        }
    }

    int latency() const {
        return bridge_.latency();
    }

    // Boot the set in `dir` on the worker thread; the current machine plays
    // until the new one is ready. Not on the audio thread.
    void boot(const std::string &dir) {
        stop_worker();
        delete retired_.exchange(nullptr);
        set_status(Status::Booting, "booting " + dir);
        cancel_ = false;
        worker_ = std::thread([this, dir] { boot_on_worker(dir); });
    }

    Status status() const {
        return status_.load();
    }

    std::string status_text() const {
        std::lock_guard<std::mutex> lock(text_mutex_);
        return status_text_;
    }

    // The newest booted set's catalog (null: none, or not booted). Any
    // thread but the audio thread. The program index in published() refers
    // to the running machine's catalog, which is this one once the audio
    // thread has taken the machine (until then, check the index's range).
    std::shared_ptr<const lexcat::Catalog> catalog() const {
        std::lock_guard<std::mutex> lock(text_mutex_);
        return ui_catalog_;
    }

    // The running machine's catalog. Audio thread only.
    const lexcat::Catalog *audio_catalog() const {
        return live_ == nullptr ? nullptr : live_->live->catalog;
    }

    // What the machine holds, and which requests are pending. Any thread.
    lexparams::Published published() const {
        return sched_.channel().read();
    }

    // The firmware's display text of each named slider (slider_k at [k-1]),
    // as of the last state read. Any thread.
    lexplug::MachineSink::Texts slider_texts() const {
        return sink_.texts().read();
    }

    // Request a program (catalog index), a variation (1..8) or a slider_k
    // (lexparams::sliderParam(k)) value, in the remote's own units. Audio
    // thread.
    //
    // A slider moved while the LARC is already on its page with its pickup
    // live, and no other task running or waiting, goes straight to the
    // firmware as the LARC's own fader message (a drag: the firmware answers
    // in ~12 ms, where an operator task per value took ~370 ms). When the
    // drag has been still for drag_end_seconds, its last value goes to the
    // scheduler as an ordinary move, so the state and the slider's text are
    // read back as after any task. Anything else is an operator task.
    void request(int param, int value) {
        bool slider = param >= lexparams::kSlider1 && param < lexparams::kSlider1 + lexparams::kSliders;
        // A slider value is sent to the scheduler normalised by the machine's
        // slider range, which is known only once the machine is ready (the
        // default is the 224XL LARC's 2..254; the 224X's and the 224's
        // differ). Until then the value waits here, the last one per slider
        // (a patch's sliders are requested while the machine boots).
        if (slider && !sched_.ready()) {
            early_[param - lexparams::kSlider1] = value;
            early_waiting_ = true;
            return;
        }
        if (drag_param_ >= 0 && drag_param_ != param) {
            end_drag();   // another slider: the drag ends here
        }
        if (slider && live_move(param, value)) {
            return;
        }
        if (drag_param_ >= 0) {
            end_drag();
        }
        submit(param, value);
    }

    // During a drag and until the move that ends it has been read back: the
    // firmware's display text of the dragged slider, live. False when none.
    // Any thread.
    bool live_text(int k, char out[25]) const {
        if (live_k_.load(std::memory_order_acquire) != k) {
            return false;
        }
        for (int i = 0; i < 24; i++) {
            out[i] = live_chars_[i].load(std::memory_order_relaxed);
        }
        out[24] = 0;
        return out[0] != 0 && live_k_.load(std::memory_order_acquire) == k;
    }

    static constexpr double drag_end_seconds = 0.15;

private:
    void submit(int param, int value) {
        lexparams::Command c;
        c.param = uint16_t(param);
        c.value = lexparams::fromInt(param, value, sched_.machine().range());
        sched_.submit(c);
    }

    // The LARC's RAM (larc_operator.hpp): the page it is on, and each slot's
    // soft-pickup state (1 = live: the firmware takes the fader as it is).
    static constexpr uint16_t larc_page = 0x3c32, larc_pickup = 0x3c20;

    bool live_move(int param, int value) {
        Machine *m = live_;
        if (m == nullptr || m->stopped || !sched_.ready()) {
            return false;
        }
        const lexcat::Catalog *catalog = m->live->catalog;
        if (catalog == nullptr || catalog->remote != lexcat::Remote::Larc) {
            return false;
        }
        int busy = sched_.busyParam();
        if (busy >= 0 && busy != param) {
            return false;
        }
        lexparams::Published pub = sched_.channel().read();
        uint64_t mine = uint64_t(1) << param;
        if ((pub.pendingMask & ~mine) != 0) {
            return false;
        }
        const lexparams::MachineState &state = sched_.machine();
        if (state.program < 0 || size_t(state.program) >= catalog->programs.size()) {
            return false;
        }
        const lexcat::Program &program = catalog->programs[size_t(state.program)];
        int k = param - lexparams::kSlider1 + 1;
        if (k > int(program.generic.size())) {
            return false;
        }
        const lexcat::SliderRef &ref = program.generic[size_t(k - 1)];
        auto &engine = m->live->session.engine;
        if (int(engine.peek(larc_page)) + 1 != ref.page || engine.peek(uint16_t(larc_pickup + ref.slot)) != 1) {
            return false;
        }
        // A value of this slider waiting behind its own task would be sent
        // after these: drop it (nothing else waits, checked above).
        if ((pub.pendingMask & mine) != 0 && busy != param) {
            sched_.clearPending();
        }
        int raw = std::clamp(value, state.rangeMin, state.rangeMax);
        if (drag_param_ != param) {
            drag_param_ = param;
            drag_k_ = k;
            drag_name_ = &program.slider(ref).name;
            drag_value_ = -1;
            candidate_[0] = 0;   // follow_drag reads this slider's text afresh
            steady_ = 0;
        }
        if (raw != drag_value_) {
            engine.fader(unsigned(ref.slot), unsigned(raw));
            drag_value_ = raw;
        }
        drag_quiet_ = 0;
        return true;
    }

    // text[from, to) with leading and trailing spaces dropped and runs of
    // spaces made one; stops at a NUL.
    static void collapse(const char *text, int from, int to, char out[25]) {
        int len = 0;
        bool space = false;
        for (int i = from; i < to && text[i] != 0 && len < 24; i++) {
            if (text[i] == ' ') {
                space = len > 0;
                continue;
            }
            if (space && len < 23) {
                out[len++] = ' ';
            }
            space = false;
            out[len++] = text[i];
        }
        out[len] = 0;
    }

    // The drag's last value through the scheduler (see request()).
    void end_drag() {
        if (drag_param_ >= 0 && drag_value_ >= 0) {
            submit(drag_param_, drag_value_);
            ending_k_ = drag_k_;
        }
        drag_param_ = -1;
    }

    // Audio thread, every live_period host samples while a drag is on or its
    // ending move is being read back: notices the drag's end, and copies the
    // slider line's value text for live_text().
    void follow_drag() {
        Machine *m = live_;
        if (drag_param_ >= 0) {
            drag_quiet_ += live_period;
            if (drag_quiet_ >= int(drag_end_seconds * host_rate_)) {
                end_drag();
            }
        }
        int k = drag_param_ >= 0 ? drag_k_ : ending_k_;
        if (k <= 0 || m == nullptr) {
            live_k_.store(0, std::memory_order_release);
            return;
        }
        if (drag_param_ < 0) {
            // Ending: shown live until the ending move has been read back.
            lexparams::Published pub = sched_.channel().read();
            if (pub.busyParam < 0 && pub.pendingMask == 0) {
                ending_k_ = 0;
                live_k_.store(0, std::memory_order_release);
                return;
            }
        }
        // The LARC's slider line: name in 0-11, value in 12-23, each with
        // runs of spaces collapsed (the catalog's names keep the LARC's
        // padding, "LF DECAY   L", or not).
        const char *text = m->live->session.engine.larc_text();
        char name[25] = {0}, wanted[25] = {0}, value[25] = {0};
        collapse(text, 0, 12, name);
        if (drag_name_ != nullptr) {
            collapse(drag_name_->c_str(), 0, int(std::min<size_t>(drag_name_->size(), 24)), wanted);
        }
        if (drag_name_ == nullptr || std::strcmp(name, wanted) != 0) {
            return;   // the line shows something else (yet): keep the last text
        }
        collapse(text, 12, 24, value);
        // The firmware rewrites the line a character at a time over the
        // serial link, so a read can catch half of the next text ("105 SEC"
        // between "1.5 SEC" and "10 SEC"): a text counts once it has held
        // for steady_reads reads in a row (~5 ms).
        if (std::strcmp(value, candidate_) != 0) {
            std::memcpy(candidate_, value, sizeof candidate_);
            steady_ = 1;
            return;
        }
        if (++steady_ != steady_reads) {
            return;
        }
        live_k_.store(0, std::memory_order_release);
        for (int i = 0; i < 24; i++) {
            live_chars_[i].store(value[i], std::memory_order_relaxed);
        }
        live_k_.store(k, std::memory_order_release);
    }

public:

    // The direct controls, as the JUCE plugin's level_db, dry_wet and
    // output_left/right (its render_chunk applies them the same way): the
    // input gain before the machine, the outputs A-D (0-3) that feed the
    // host's left and right, and the mix of those with the gained input.
    // Audio thread.
    void set_level_db(float db) {
        if (db != level_db_) {
            level_db_ = db;
            mix_.gain = db == 0.0f ? 1.0 : std::pow(10.0, double(db) / 20.0);
        }
    }
    void set_wet(float wet) {
        mix_.wet = std::clamp(double(wet), 0.0, 1.0);
    }
    void set_outputs(int left, int right) {
        mix_.outLeft = std::clamp(left, 0, 3);
        mix_.outRight = std::clamp(right, 0, 3);
    }

    // The input's level detectors since the last call, per channel: bit k
    // set = detector k exceeded (fpc_headroom.hpp: five comparators 6 dB
    // apart, bit 0 = 0.28 V ... bit 4 = 5 V, the onset of ADC clipping).
    // Any thread; one reader.
    uint8_t take_headroom(int channel) {
        return hits_[channel & 1].exchange(0, std::memory_order_relaxed);
    }

    // The LARC's 48 characters (the 2 x 12 main display, then the 24 of the
    // slider line), copied from the audio thread every display_period host
    // samples. Any thread. False if the running machine has no LARC.
    static constexpr int display_chars = 48;
    bool display(char out[display_chars + 1]) const {
        for (int i = 0; i < display_chars; i++) {
            out[i] = display_[i].load(std::memory_order_relaxed);
        }
        out[display_chars] = 0;
        return larc_.load(std::memory_order_relaxed);
    }

    // Audio thread: one host sample in, one out. Denormals on while the
    // machine runs (denormals.hpp), whatever the thread's mode.
    void process(float in_l, float in_r, float &out_l, float &out_r) {
        DenormalsOn denormals;
        take_pending();
        if (early_waiting_ && sched_.ready()) {
            early_waiting_ = false;
            for (int i = 0; i < lexparams::kSliders; i++) {
                if (early_[i] >= 0) {
                    submit(lexparams::kSlider1 + i, early_[i]);
                    early_[i] = -1;
                }
            }
        }
        Machine *m = live_;
        bridge_.process(&in_l, &in_r, &out_l, &out_r, 1,
                        [this, m](const float *l, const float *r, float *ol, float *orr, int frames) {
                            float *four[4] = {scratch_[0].data(), scratch_[1].data(), scratch_[2].data(),
                                              scratch_[3].data()};
                            if (mix_.gain != 1.0) {
                                for (int i = 0; i < frames; i++) {
                                    gained_[0][size_t(i)] = float(mix_.gain * double(l[i]));
                                    gained_[1][size_t(i)] = float(mix_.gain * double(r[i]));
                                }
                                l = gained_[0].data();
                                r = gained_[1].data();
                            }
                            sched_.run(sink_, frames, [&](int offset, int count, bool) {
                                if (m == nullptr || m->stopped) {
                                    for (auto *o : four) {
                                        std::fill(o + offset, o + offset + count, 0.0f);
                                    }
                                    return;
                                }
                                float *o[4] = {four[0] + offset, four[1] + offset, four[2] + offset, four[3] + offset};
                                bool ok = false;
                                try {
                                    ok = m->live->render(l + offset, r + offset, o, count);
                                } catch (...) {
                                }
                                if (!ok) {
                                    m->stopped = true;
                                    sink_.set_stopped(true);
                                    status_.store(Status::Failed);
                                }
                            });
                            // render_chunk: wet * out + (1 - wet) * dry, in double.
                            const float *wet_l = four[mix_.outLeft], *wet_r = four[mix_.outRight];
                            double wet = mix_.wet;
                            for (int i = 0; i < frames; i++) {
                                ol[i] = float(wet * double(wet_l[i]) + (1.0 - wet) * double(l[i]));
                                orr[i] = float(wet * double(wet_r[i]) + (1.0 - wet) * double(r[i]));
                            }
                        });
        if (m != nullptr) {
            // The FPC's registers hold between the firmware's reads; looked
            // at every host sample, a hit is seen while it is held.
            auto &engine = m->live->session.engine;
            peaks_[0] |= uint8_t(~engine.headroom(0) & 0x1f);
            peaks_[1] |= uint8_t(~engine.headroom(1) & 0x1f);
        }
        if ((drag_param_ >= 0 || ending_k_ > 0) && --live_countdown_ <= 0) {
            live_countdown_ = live_period;
            follow_drag();
        }
        if (m != nullptr && --display_countdown_ <= 0) {
            display_countdown_ = display_period;
            for (int c = 0; c < 2; c++) {
                hits_[c].fetch_or(peaks_[c], std::memory_order_relaxed);
                peaks_[c] = 0;
            }
            auto &engine = m->live->session.engine;
            bool larc = engine.larc_connected();
            const char *text = engine.larc_text();
            bool ended = !larc;
            for (int i = 0; i < display_chars; i++) {
                ended = ended || text[i] == 0;
                display_[i].store(ended ? ' ' : text[i], std::memory_order_relaxed);
            }
            larc_.store(larc, std::memory_order_relaxed);
        }
    }

private:
    struct Machine {
        std::shared_ptr<const lexcat::CatalogLibrary> catalogs;
        std::unique_ptr<lexplug::LiveMachine> live;
        bool stopped = false;
    };

    static std::string read_file(const std::string &path) {
        std::ifstream in(utf8_path(path), std::ios::binary);
        if (!in) {
            return {};
        }
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    void boot_on_worker(std::string dir) {
        auto set = lexplug::load_rom_directory(utf8_path(dir));
        if (!set) {
            set_status(Status::NoRoms, "no ROM chips in " + dir);
            return;
        }
        // The catalog key: SHA-256 over the chips in base order (roms/recognize.hpp catalog_hash).
        std::sort(set->chips.begin(), set->chips.end(),
                  [](const lexplug::RomChip &a, const lexplug::RomChip &b) { return a.base < b.base; });
        lexplug::roms::Sha256 hash;
        for (const auto &chip : set->chips) {
            hash.update(chip.bytes.data(), chip.bytes.size());
        }
        std::string key = lexplug::roms::Sha256::hex(hash.finish()).substr(0, 16);
        auto library = std::make_shared<lexcat::CatalogLibrary>();
        std::string note;
        if (!catalog_dir_.empty()) {
            std::string json = read_file(catalog_dir_ + "/" + key + ".json");
            try {
                if (!json.empty()) {
                    library->add(json);
                    std::string stored = read_file(catalog_dir_ + "/stored/" + key + ".stored.json");
                    if (!stored.empty()) {
                        library->addStored(stored);
                    }
                }
            } catch (const std::exception &e) {
                note = std::string(" (catalog unreadable: ") + e.what() + ")";
            }
        }
        const lexcat::Catalog *catalog = library->find(key);
        if (catalog == nullptr && note.empty()) {
            note = " (no catalog for " + key + ": programs cannot be selected)";
        }

        auto m = std::make_unique<Machine>();
        m->catalogs = library;
        m->live = std::make_unique<lexplug::LiveMachine>(set->model, catalog);
        std::copy(key.begin(), key.end(), m->live->hash);
        auto &engine = m->live->session.engine;
        for (const auto &chip : set->chips) {
            engine.load(chip.bytes.data(), chip.bytes.size(), chip.base);
        }
        engine.set_analog(true);   // the analog boards, always (the machine's real sound)
        constexpr int chunk = 4800;
        std::vector<float> zero(chunk, 0.0f), scratch[4];
        for (auto &s : scratch) {
            s.assign(chunk, 0.0f);
        }
        float *out[4] = {scratch[0].data(), scratch[1].data(), scratch[2].data(), scratch[3].data()};
        try {
            while (m->live->session.frame() < boot_frames) {
                if (cancel_) {
                    return;
                }
                int n = int(std::min<uint64_t>(chunk, boot_frames - m->live->session.frame()));
                if (!m->live->render(zero.data(), zero.data(), out, n)) {
                    throw std::runtime_error("the operator reported a stop");
                }
            }
            // As the JUCE plugin: read the machine once as booted, then the
            // operator's own start (the LARC reads its toggles).
            m->live->reader.prepare(engine, catalog);
            {
                lexparams::MachineState booted;
                m->live->reader.read(engine, booted);
            }
            if (m->live->port->startup()) {
                while (m->live->port->busy()) {
                    if (cancel_) {
                        return;
                    }
                    if (!m->live->render(zero.data(), zero.data(), out, chunk)) {
                        throw std::runtime_error("the operator reported a stop");
                    }
                }
            }
        } catch (const std::exception &e) {
            set_status(Status::Failed, std::string("the machine stopped while booting: ") + e.what());
            return;
        }
        const char *models[2] = {"224X/224XL", "224"};
        std::string text = std::string("running the ") + models[set->model] + " from " + dir + note;
        std::shared_ptr<const lexcat::Catalog> shown;
        if (catalog != nullptr) {
            shown = std::shared_ptr<const lexcat::Catalog>(library, catalog);
        }
        delete pending_.exchange(m.release());
        {
            std::lock_guard<std::mutex> lock(text_mutex_);
            ui_catalog_ = shown;
        }
        set_status(Status::Running, text);
    }

    // Audio thread: take a booted machine if one waits and the retired slot
    // is free (the worker empties it before each boot).
    void take_pending() {
        if (pending_.load(std::memory_order_relaxed) == nullptr || retired_.load() != nullptr) {
            return;
        }
        Machine *next = pending_.exchange(nullptr);
        if (next == nullptr) {
            return;
        }
        bridge_.reset();
        drag_param_ = -1;
        ending_k_ = 0;
        live_k_.store(0, std::memory_order_release);
        sink_.attach(next->live.get());
        sink_.set_stopped(false);
        sched_.machineReplaced();
        retired_.store(live_);
        live_ = next;
    }

    void stop_worker() {
        cancel_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void set_status(Status s, std::string text) {
        std::lock_guard<std::mutex> lock(text_mutex_);
        status_text_ = std::move(text);
        status_.store(s);
    }

    lexplug::RateBridge bridge_;
    std::vector<float> scratch_[4];
    std::vector<float> gained_[2];
    lexplug::DirectMix mix_;
    float level_db_ = 0.0f;
    uint8_t peaks_[2] = {};                      // audio thread
    std::atomic<uint8_t> hits_[2] = {};          // audio thread -> UI
    lexplug::MachineSink sink_{mix_};
    lexparams::Scheduler sched_;
    Machine *live_ = nullptr;                    // audio thread only
    std::atomic<Machine *> pending_{nullptr};    // worker -> audio thread
    std::atomic<Machine *> retired_{nullptr};    // audio thread -> worker
    // The drag (audio thread): the slider param, its k, name and last value
    // sent, host samples since; then the ending move's k until read back.
    int host_rate_ = 48000;
    int drag_param_ = -1, drag_k_ = 0, drag_value_ = -1, drag_quiet_ = 0, ending_k_ = 0;
    const std::string *drag_name_ = nullptr;
    static constexpr int live_period = 64;
    int live_countdown_ = 0;
    int early_[lexparams::kSliders];   // see request(); -1 = none
    bool early_waiting_ = false;
    static constexpr int steady_reads = 4;
    char candidate_[25] = {0};
    int steady_ = 0;
    std::atomic<int> live_k_{0};                 // audio thread -> UI: live_text's slider, 0 = none
    std::atomic<char> live_chars_[24] = {};
    static constexpr int display_period = 2048;
    int display_countdown_ = 0;
    std::atomic<char> display_[display_chars] = {};
    std::atomic<bool> larc_{false};
    std::string catalog_dir_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<Status> status_{Status::NoRoms};
    mutable std::mutex text_mutex_;
    std::string status_text_ = "no ROM folder chosen";
    std::shared_ptr<const lexcat::Catalog> ui_catalog_;
};

}  // namespace lexvcv
