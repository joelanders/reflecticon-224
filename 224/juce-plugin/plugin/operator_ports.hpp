// The operators behind the plugin's OperatorPort: op::Machine
// (source/operator/machine.hpp) with the LARC operator (224XL,
// larc_operator.hpp), the 224X front-panel operator (panel_operator.hpp) or
// the 224's (panel224_operator.hpp, v4.x or v3.2 layout), driven one task at
// a time by the Scheduler.
//
// Slider values are the remote's own control values: LARC fader positions
// (2..254) and panel POT READINGS (0..253, the catalog tables' key). A pot
// reading is not always the stored byte (the 224's BASS stores pot >> 3):
// MachineSink::readState turns stored bytes back into positions.
//
// Each request starts one root task (op::Machine::spawn). The task is a
// coroutine function below that calls the operator as app.js does and
// records what it learned (program, variation, toggles) for describe().
// Every failure is caught (co_await caught(...)), so a root task always ends
// through its last line, which waits until the machine has nothing
// scheduled or waiting: a task ends at a pump segment's end with the machine
// idle, which is when op::Machine::spawn allows the next one. busy() is true
// from spawn until render() has seen the root finish and released it.
//
// LARC program loads follow bench/record_timeline.mjs select() and
// rows-sv/tests/real_ir_setup.mjs: up to 3 attempts; before a retry cancel a
// pending 2nd F, sleep 0.5 s and forget the current bank (the v8.1A firmware
// sometimes does not arm PROG and takes the program digit as a bank digit;
// the web page has the same bug). Panel loads retry inside the operator.
#pragma once
#include "operator_port.hpp"
#include "../source/operator/larc_operator.hpp"
#include "../source/operator/machine.hpp"
#include "../source/operator/panel224_operator.hpp"
#include "../source/operator/panel_operator.hpp"
#include "../source/operator/task.hpp"
#include <cstring>

namespace lexplug {

// What every operator port shares: the Machine, the one root task, what was
// learned.
class MachinePort : public OperatorPort {
public:
    explicit MachinePort(Engine &engine) : machine_(engine) {}

    ~MachinePort() override {
        machine_.cancel_all();
    }

    bool render(const float *left, const float *right, float *const out[4], int frames) override {
        bool ok = machine_.render(left, right, out, frames);
        if (root_ >= 0 && machine_.done(root_)) {
            machine_.release(root_);
            root_ = -1;
        }
        return ok;
    }

    bool busy() const override {
        return root_ >= 0;
    }

    void describe(OperatorKnowledge &out) const override {
        out = know_;
    }

protected:
    template <typename Make>
    bool start(Make make) {
        root_ = machine_.spawn(make);
        return true;
    }

    // The end of every task: let whatever it scheduled play out, so that the
    // machine is idle (and between segments) when the root finishes.
    static op::Task<void> settle(MachinePort *self) {
        while (!self->machine_.idle()) {
            co_await self->machine_.sleep(0.01);
        }
    }

    static void heard(OperatorKnowledge &know, int page, int slot, const op::Result<op::Echo> &result) {
        if (!result.ok || !result.value.present) {
            return;
        }
        know.echoCount++;
        know.echoPage = page;
        know.echoSlot = slot;
        std::memcpy(know.echo, result.value.value, sizeof know.echo);
        know.echo[sizeof know.echo - 1] = 0;
    }

    static void set_bit(OperatorKnowledge &know, int bit, bool on) {
        uint8_t mask = uint8_t(1u << bit);
        know.togglesKnown |= mask;
        if (on) {
            know.toggles |= mask;
        } else {
            know.toggles &= uint8_t(~mask);
        }
    }

    op::Machine machine_;
    int root_ = -1;
    OperatorKnowledge know_;
};

// ---------------------------------------------------------------------
// 224XL: the LARC remote.
// ---------------------------------------------------------------------
class LarcPort final : public MachinePort {
public:
    explicit LarcPort(Engine &engine) : MachinePort(engine), op_(machine_) {}

    // After the boot: read the toggles (PARAM taps), as app.js startEasyUI.
    bool startup() override {
        return start([this]() { return startup_task(this); });
    }

    bool loadProgram(int index, const lexcat::Program &program) override {
        int bank = program.bank;
        int number = program.program;
        return start([this, index, bank, number]() { return select_task(this, index, bank, number); });
    }

    bool loadVariation(int variation) override {
        if (variation < 1 || variation > 8) {
            return false;
        }
        return start([this, variation]() { return variation_task(this, variation); });
    }

    bool moveSlider(int page, int slot, int raw) override {
        if (slot < 0 || slot > 5) {
            return false;
        }
        return start([this, page, slot, raw]() { return move_task(this, page, slot, raw); });
    }

    bool setToggle(int toggle, bool on) override {
        if (toggle < 0 || toggle >= lexparams::kToggles) {
            return false;
        }
        return start([this, toggle, on]() { return toggle_task(this, toggle, on); });
    }

private:
    static void learn(OperatorKnowledge &know, const op::Toggles &toggles) {
        for (int k = 0; k < 3; k++) {
            if (toggles.value[k] >= 0) {
                set_bit(know, k, toggles.value[k] == 1);
            }
        }
    }

    static op::Task<void> startup_task(LarcPort *self) {
        op::Result<op::Toggles> result = co_await op::caught(self->op_.readToggles());
        if (result.ok) {
            learn(self->know_, result.value);
        }
        // The LARC starts unmuted.
        set_bit(self->know_, lexparams::kMute, false);
        co_await settle(self);
    }

    static op::Task<void> select_task(LarcPort *self, int index, int bank, int number) {
        op::LarcOperator &op = self->op_;
        bool loaded = false;
        for (int attempt = 0; attempt < 3 && !loaded; attempt++) {
            if (attempt > 0) {
                co_await op::caught(op.cancelShift());
                co_await self->machine_.sleep(0.5);
                op.current.bank = 0;
            }
            op::Result<bool> result = co_await op::caught(op.selectProgram(bank, number));
            loaded = result.ok && result.value;
        }
        if (loaded) {
            self->know_.program = index;
            self->know_.variation = op.current.variation;
        }
        co_await settle(self);
    }

    static op::Task<void> variation_task(LarcPort *self, int variation) {
        op::Result<bool> result = co_await op::caught(self->op_.loadVariation(variation));
        if (result.ok && result.value) {
            self->know_.variation = variation;
        }
        co_await settle(self);
    }

    static op::Task<void> move_task(LarcPort *self, int page, int slot, int raw) {
        op::Result<op::Echo> result = co_await op::caught(self->op_.moveSlider(page, unsigned(slot), unsigned(raw)));
        heard(self->know_, page, slot, result);
        co_await settle(self);
    }

    static op::Task<void> toggle_task(LarcPort *self, int toggle, bool on) {
        OperatorKnowledge &know = self->know_;
        if (toggle == lexparams::kMute) {
            // The MUTE key toggles; the state is what it last showed.
            bool muted = (know.toggles & (1u << lexparams::kMute)) != 0;
            if (muted != on) {
                op::Result<bool> result = co_await op::caught(self->op_.toggleMute());
                if (result.ok) {
                    set_bit(know, lexparams::kMute, result.value);
                }
            }
        } else {
            op::Result<op::Toggles> result = co_await op::caught(self->op_.setToggle(toggle, on));
            if (result.ok) {
                learn(know, result.value);
            }
        }
        co_await settle(self);
    }

    op::LarcOperator op_;
};

// ---------------------------------------------------------------------
// 224X: the front panel (no toggles).
// ---------------------------------------------------------------------
class PanelPort final : public MachinePort {
public:
    explicit PanelPort(Engine &engine) : MachinePort(engine), op_(machine_) {}

    bool loadProgram(int index, const lexcat::Program &program) override {
        if (program.identity <= 0) {
            return false;
        }
        unsigned identity = unsigned(program.identity);
        return start([this, index, identity]() { return select_task(this, index, identity); });
    }

    bool loadVariation(int variation) override {
        if (variation < 1 || variation > 8) {
            return false;
        }
        return start([this, variation]() { return variation_task(this, variation); });
    }

    bool moveSlider(int page, int slot, int raw) override {
        if (slot < 0 || slot > 5) {
            return false;
        }
        return start([this, page, slot, raw]() { return move_task(this, page, slot, raw); });
    }

    bool setToggle(int, bool) override {
        return false;
    }

private:
    static op::Task<void> select_task(PanelPort *self, int index, unsigned identity) {
        op::Result<bool> result = co_await op::caught(self->op_.selectProgram(identity));
        if (result.ok && result.value) {
            self->know_.program = index;
            self->know_.variation = self->op_.current.variation;
        }
        co_await settle(self);
    }

    static op::Task<void> variation_task(PanelPort *self, int variation) {
        op::Result<bool> result = co_await op::caught(self->op_.loadVariation(variation));
        if (result.ok && result.value) {
            self->know_.variation = variation;
        }
        co_await settle(self);
    }

    static op::Task<void> move_task(PanelPort *self, int page, int slot, int raw) {
        op::Result<op::Echo> result = co_await op::caught(self->op_.moveSlider(page, unsigned(slot), unsigned(raw)));
        heard(self->know_, page, slot, result);
        co_await settle(self);
    }

    op::PanelOperator op_;
};

// ---------------------------------------------------------------------
// The original 224: the front panel (MODE ENH and DECAY OPT are its
// PROGRAM 7 and 8; no variations but the program itself).
// ---------------------------------------------------------------------
class Panel224Port final : public MachinePort {
public:
    Panel224Port(Engine &engine, const char *layout)
        : MachinePort(engine), op_(machine_, op::panel224_layout_named(layout)) {}

    bool loadProgram(int index, const lexcat::Program &program) override {
        if (program.identity <= 0) {
            return false;
        }
        unsigned identity = unsigned(program.identity);
        return start([this, index, identity]() { return select_task(this, index, identity); });
    }

    bool loadVariation(int variation) override {
        if (variation != 1) {
            return false;
        }
        return start([this]() { return variation_task(this); });
    }

    bool moveSlider(int page, int slot, int raw) override {
        if (slot < 0 || slot > 5) {
            return false;
        }
        return start([this, page, slot, raw]() { return move_task(this, page, slot, raw); });
    }

    bool setToggle(int toggle, bool on) override {
        if (toggle != lexparams::kModeEnh && toggle != lexparams::kDecayOpt) {
            return false;
        }
        return start([this, toggle, on]() { return toggle_task(this, toggle, on); });
    }

private:
    static op::Task<void> select_task(Panel224Port *self, int index, unsigned identity) {
        op::Result<bool> result = co_await op::caught(self->op_.selectProgram(identity));
        if (result.ok && result.value) {
            self->know_.program = index;
            self->know_.variation = 1;
        }
        co_await settle(self);
    }

    static op::Task<void> variation_task(Panel224Port *self) {
        co_await op::caught(self->op_.loadVariation(1));
        co_await settle(self);
    }

    static op::Task<void> move_task(Panel224Port *self, int page, int slot, int raw) {
        op::Result<op::Echo> result = co_await op::caught(self->op_.moveSlider(page, unsigned(slot), unsigned(raw)));
        heard(self->know_, page, slot, result);
        co_await settle(self);
    }

    // (The toggles themselves are read from RAM: RamReader.)
    static op::Task<void> toggle_task(Panel224Port *self, int toggle, bool on) {
        co_await op::caught(self->op_.setToggle(toggle, on));
        co_await settle(self);
    }

    op::Panel224Operator op_;
};

}  // namespace lexplug
