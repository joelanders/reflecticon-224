// The seam between the plugin and the operator:
// the one interface the operator implements for the plugin, and the stub
// used until it does.
//
// One OperatorPort per machine. It is made on the boot thread right after
// the machine's Session (before power-on: the operator's clock must count
// every frame from power-on, as the JS createMachine's does), pumped by the
// boot thread through the boot and a state restore, then handed to the audio
// thread with its Session, and destroyed off the audio thread.
//
// ---------------------------------------------------------------------
// WHAT THE OPERATOR MUST PROVIDE (fill makeOperatorPort in
// operator_port.cpp; nothing else in plugin/ needs to change):
//
//   render(left, right, out, frames)
//       Render `frames` 48 kHz frames of the machine: op::Machine::render,
//       which gives the operator's scheduled inputs (key taps, fader moves,
//       pokes, buttons, pots) at their frames and checks waiters / resumes
//       coroutines at its own segment ends, independently of how the host
//       splits blocks. Return false once the machine has stopped. Called on
//       the audio thread (live) and on the boot thread (boot, restore
//       replay): no allocation, no locks. The Session calls it between its
//       own test-hook controls (PluginProcessor::schedule_controls), which
//       are given at their frames before this is called for that frame.
//   busy()
//       An operator task is running (op::Machine: a root task not done).
//       Checked by the Scheduler at its 128-frame grid points only.
//   loadProgram(index, program)
//       Start app.js selectEntry(program): LARC selectProgram(bank, program)
//       (then recordSettled), panel loadProgram(identity), 224 loadProgram.
//       `index` is the program's index in the catalog (for describe()).
//   loadVariation(v)       op.loadVariation(v)
//   moveSlider(page, slot, raw)
//       op.moveSlider(page, slot, raw): page = the firmware's page number
//       (lexcat::Page::page), slot 0-based, raw in the remote's range.
//   setToggle(toggle, on)
//       toggle = lexparams::Toggle: DYN DECAY / MODE ENH / DECAY OPT via
//       op.setToggle(label, on); kMute via op.toggleMute() if the state
//       differs.
//     Each of the four STARTS a task (spawn a root coroutine) and returns
//     at once: true if started, false if it does not apply here (no such
//     program/variation/slider, toggle not offered by this remote). Called
//     only while !busy(), on the thread that renders (audio or boot), with
//     no allocation (the task frames come from op::Machine's pool).
//   startup()
//       Optional: start a task of its own right after the boot (the LARC
//       reads its toggles); the boot thread renders until !busy().
//   describe(knowledge)
//       What the operator knows that RAM does not show: the program and
//       variation it loaded, the toggles it read (LARC: PARAM taps), and
//       the display text a slider move echoed (larc/panel Echo).
//       Fields it does not know stay as they are (-1 / 0). Optional: the RAM reader (ram_reader.hpp)
//       fills the rest, so readState works without it.
//
// With the stub (NoOperator) every task request returns false: the
// Scheduler counts it rejected and the write-back puts the host parameter
// back to what the machine holds. Direct parameters (input level, dry/wet,
// output pair, analog boards) never go through the operator and work now.
// ---------------------------------------------------------------------
#pragma once
#include "../source/catalog/catalog.hpp"
#include "../source/engine.hpp"
#include "../source/params/layout.hpp"
#include <cstdint>
#include <memory>

namespace lexplug {

// What the operator knows beyond RAM (see describe()).
struct OperatorKnowledge {
    int32_t program = -1;           // catalog index of the program it loaded, -1 = not known
    int32_t variation = 0;          // 1..8, 0 = not known
    uint8_t toggles = 0;            // bit t = lexparams::Toggle t on
    uint8_t togglesKnown = 0;       // bit t = toggle t read
    // The firmware's echo of the last slider move (its display text), if it
    // answered: page (lexcat::Page::page), slot, text; echoCount counts moves
    // that answered.
    uint32_t echoCount = 0;
    int32_t echoPage = 0;
    int32_t echoSlot = 0;
    char echo[25] = {};
};

class OperatorPort {
public:
    virtual ~OperatorPort() = default;

    virtual bool render(const float *left, const float *right, float *const out[4], int frames) = 0;
    virtual bool busy() const = 0;
    virtual bool loadProgram(int index, const lexcat::Program &program) = 0;
    virtual bool loadVariation(int variation) = 0;
    virtual bool moveSlider(int page, int slot, int raw) = 0;
    virtual bool setToggle(int toggle, bool on) = 0;
    virtual void describe(OperatorKnowledge &) const {}
    // Start the operator's own task after the boot, if it has one (the LARC
    // reads its toggles, as app.js startEasyUI does). True if one started:
    // the boot thread then renders silence until !busy(). Not called when a
    // test timeline drives the machine (PluginProcessor::schedule_controls).
    virtual bool startup() {
        return false;
    }
};

// The stub: renders the engine directly (exactly what the machine does with
// no operator), and takes no tasks.
class NoOperator final : public OperatorPort {
public:
    explicit NoOperator(Engine &engine) : engine_(engine) {}

    bool render(const float *left, const float *right, float *const out[4], int frames) override {
        engine_.render(left, right, out, frames);
        return true;
    }
    bool busy() const override {
        return false;
    }
    bool loadProgram(int, const lexcat::Program &) override {
        return false;
    }
    bool loadVariation(int) override {
        return false;
    }
    bool moveSlider(int, int, int) override {
        return false;
    }
    bool setToggle(int, bool) override {
        return false;
    }

private:
    Engine &engine_;
};

// The operator for a machine: `engine` is the Session's, `catalog` the set's
// (null for an unrecognized set: then no operator can know its programs).
// Called on the boot thread before power-on. Today: NoOperator.
std::unique_ptr<OperatorPort> makeOperatorPort(Engine &engine, const lexcat::Catalog *catalog);

}  // namespace lexplug
