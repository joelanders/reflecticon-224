// kit::Host: the thread plumbing every machine plugin needs, JUCE-free so tests drive it the way
// a plugin does.
//
//   message thread   install() a booted machine; prepare() (audio stopped); readouts(), latency();
//                    panel_key(), panel_input(), panel() for a hardware panel (never blocks on the
//                    machine: keys and inputs are queued for the worker, the panel is a cached copy)
//   worker thread    prepares installed machines, applies parameter changes (polled every ~2 ms
//                    from the bound atomics), hands panel keys and inputs to the machine, refreshes
//                    readouts and the panel state, frees retired machines
//   audio thread     process(): takes the newest installed machine at the start of a block
//
// Machine hand-off (from dmx15-80's engine): pending -> current -> retired, all atomic pointers;
// the audio thread swaps pending in only when the previous retired one has been freed, so it
// never frees anything itself.
#pragma once
#include "machine.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace kit {

class Host {
public:
    // values: one atomic per Info::params entry, holding plain values (a JUCE plugin binds the
    // APVTS raw parameter values). They must outlive the Host.
    Host(const MachineType& type, std::vector<const std::atomic<float>*> values)
        : type_(type), values_(std::move(values)), scratch_(values_.size()) {
        for (auto& in : type_.info.panel.inputs) inputs_.push_back({in.id, in.def});
        worker_ = std::thread([this] { work(); });
    }
    ~Host() {
        {
            std::lock_guard<std::mutex> g(wake_m_);
            quit_ = true;
        }
        wake_.notify_all();
        worker_.join();
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
        delete current_;
    }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    const MachineType& type() const { return type_; }

    // ---- message thread ----
    // Hand over a booted machine. Synchronous when the rate is known: prepared and given the
    // current parameters before this returns, so the next block (an offline render's first)
    // hears it set up. With the rate not yet known, prepare() finishes the job.
    void install(std::unique_ptr<Machine> m) {
        std::lock_guard<std::mutex> g(control_);
        if (!m) return;
        control_machine_ = m.get();
        {
            std::lock_guard<std::mutex> q(queue_m_);
            for (auto& in : inputs_) control_machine_->panel_input(in.id, in.value);
        }
        if (rate_ > 0) {
            m->prepare(rate_, block_);
            apply(true);
            delete pending_.exchange(m.release());
        } else {
            staged_ = std::move(m);
            apply(true);
        }
        latency_.store(control_machine_->latency());
    }
    // Never concurrent with process() (hosts guarantee it for prepareToPlay).
    void prepare(int host_rate, int max_block) {
        std::lock_guard<std::mutex> g(control_);
        rate_ = host_rate;
        block_ = std::max(1, max_block);
        if (staged_) delete pending_.exchange(staged_.release());
        take_pending();                          // the audio thread is stopped: swap in here
        delete retired_.exchange(nullptr);
        if (current_) {
            current_->prepare(rate_, block_);
            latency_.store(current_->latency());
        }
    }
    bool has_machine() const {
        std::lock_guard<std::mutex> g(control_);
        return control_machine_ != nullptr;
    }
    long latency() const { return latency_.load(); }
    Readouts readouts() const {
        std::lock_guard<std::mutex> g(readouts_m_);
        return readouts_;
    }
    std::vector<Preset> presets() const {
        std::lock_guard<std::mutex> g(control_);
        return control_machine_ ? control_machine_->presets() : std::vector<Preset>{};
    }
    std::string machine_state() const {
        std::lock_guard<std::mutex> g(control_);
        return control_machine_ ? control_machine_->state() : std::string{};
    }
    // Apply parameter changes now rather than at the worker's next poll (tests, offline renders).
    void sync() {
        std::lock_guard<std::mutex> g(control_);
        apply(false);
    }
    uint64_t param_updates() const { return updates_.load(); }
    // Read the readouts from the machine now (tests; the worker does it for live machines).
    void refresh_readouts() {
        std::lock_guard<std::mutex> g(control_);
        refresh();
    }

    // ---- hardware panel (message thread; none of these wait for the machine) ----
    void panel_key(int id, bool down) {
        {
            std::lock_guard<std::mutex> q(queue_m_);
            keys_.push_back({id, down});
        }
        wake_.notify_all();
    }
    void panel_input(int id, float value) {
        {
            std::lock_guard<std::mutex> q(queue_m_);
            bool found = false;
            for (auto& in : inputs_)
                if (in.id == id) in.value = value, found = true;
            if (!found) inputs_.push_back({id, value});
            inputs_dirty_ = true;
        }
        wake_.notify_all();
    }
    float panel_input_value(int id) const {
        std::lock_guard<std::mutex> q(queue_m_);
        for (auto& in : inputs_)
            if (in.id == id) return in.value;
        return 0.0f;
    }
    PanelState panel() const {
        std::lock_guard<std::mutex> g(readouts_m_);
        return panel_;
    }
    // Take settings the panel put the machine in (PanelState::adopt) into the host parameters:
    // `write` sets them (on this thread, e.g. through the plugin's parameter objects) while the
    // worker is held off, and what it wrote becomes the applied set, so the machine is not asked
    // to type back what it already holds.
    template <class F>
    void adopt(F&& write) {
        std::lock_guard<std::mutex> g(control_);
        write();
        for (size_t i = 0; i < scratch_.size(); i++) scratch_[i] = values_[i]->load(std::memory_order_relaxed);
        applied_ = scratch_;
    }
    // Read the panel from the machine now (tests).
    void refresh_panel() {
        std::lock_guard<std::mutex> g(control_);
        feed_panel();
        refresh_panel_locked();
    }

    // ---- audio thread ----
    void process(const float* const* in, float* out_l, float* out_r, int n) {
        take_pending();
        if (!current_) {
            std::fill(out_l, out_l + n, 0.0f);
            std::fill(out_r, out_r + n, 0.0f);
            return;
        }
        current_->process(in, out_l, out_r, n);
    }

private:
    void take_pending() {
        if (retired_.load(std::memory_order_acquire)) return;   // the previous one is not freed yet
        Machine* p = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (!p) return;
        retired_.store(current_, std::memory_order_release);
        current_ = p;
    }

    // control_ held
    void apply(bool force) {
        if (!control_machine_) return;
        for (size_t i = 0; i < scratch_.size(); i++) scratch_[i] = values_[i]->load(std::memory_order_relaxed);
        if (!force && scratch_ == applied_) return;   // the poll: no allocation unless something changed
        applied_ = scratch_;
        control_machine_->set_params(applied_);
        updates_++;
        refresh();
    }
    // control_ held
    void refresh() {
        if (!control_machine_) return;
        Readouts r = control_machine_->readouts();
        std::lock_guard<std::mutex> g(readouts_m_);
        readouts_ = std::move(r);
    }

    // control_ held: queued panel keys and inputs to the machine
    void feed_panel() {
        if (!has_panel_) return;
        std::lock_guard<std::mutex> q(queue_m_);
        if (control_machine_) {
            for (auto& k : keys_) control_machine_->panel_key(k.id, k.down);
            if (inputs_dirty_)
                for (auto& in : inputs_) control_machine_->panel_input(in.id, in.value);
        }
        keys_.clear();
        inputs_dirty_ = false;
    }
    // control_ held
    void refresh_panel_locked() {
        if (!control_machine_) return;
        PanelState p = control_machine_->panel();
        std::lock_guard<std::mutex> g(readouts_m_);
        panel_ = std::move(p);
    }

    void work() {
        std::unique_lock<std::mutex> lk(wake_m_);
        while (!quit_) {
            wake_.wait_for(lk, std::chrono::milliseconds(2));
            if (quit_) break;
            lk.unlock();
            {
                std::lock_guard<std::mutex> g(control_);
                feed_panel();
                apply(false);
                if (control_machine_ && has_panel_ && ++panel_ticks_ >= 12) {
                    panel_ticks_ = 0;
                    refresh_panel_locked();
                }
                if (control_machine_ && control_machine_->live_readouts() && ++ticks_ >= 25) {
                    ticks_ = 0;
                    refresh();
                }
            }
            delete retired_.exchange(nullptr, std::memory_order_acq_rel);
            lk.lock();
        }
    }

    const MachineType& type_;
    std::vector<const std::atomic<float>*> values_;

    mutable std::mutex control_;         // every call into a machine's control side
    Machine* control_machine_ = nullptr; // the newest installed (control side)
    std::unique_ptr<Machine> staged_;    // installed before the rate was known
    int rate_ = 0, block_ = 512, ticks_ = 0;
    std::vector<float> scratch_, applied_;
    std::atomic<long> latency_{0};
    std::atomic<uint64_t> updates_{0};

    mutable std::mutex readouts_m_;
    Readouts readouts_;
    PanelState panel_;
    const bool has_panel_ = !type_.info.panel.empty();
    int panel_ticks_ = 0;

    struct KeyEv {
        int id;
        bool down;
    };
    struct Input {
        int id;
        float value;
    };
    mutable std::mutex queue_m_;   // keys_, inputs_; held briefly (panel_key/panel_input only queue)
    std::vector<KeyEv> keys_;
    std::vector<Input> inputs_;
    bool inputs_dirty_ = false;

    std::atomic<Machine*> pending_{nullptr}, retired_{nullptr};
    Machine* current_ = nullptr;         // audio thread (and prepare(), with audio stopped)

    std::mutex wake_m_;
    std::condition_variable wake_;
    bool quit_ = false;
    std::thread worker_;
};

}  // namespace kit
