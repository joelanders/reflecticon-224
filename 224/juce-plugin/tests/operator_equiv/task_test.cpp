// Unit checks of the coroutine machinery (../../source/operator/task.hpp,
// machine.hpp) that the ROM-driven tests do not reach: a failure deep in a
// chain skips the awaiting coroutines up to caught() (the JS try/catch) or
// to the root; values come back through co_await; frames all return to the
// pool. Uses an Engine with no ROM (the CPU runs through empty memory),
// only for the machine's clock.
//
//   task_test
#include "../../source/operator/machine.hpp"
#include <cstdio>
#include <cstring>

using namespace lexplug;
using namespace lexplug::op;

namespace {

int failures = 0;
void check(bool ok, const char *what) {
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

struct Log {
    int after_leaf = 0;     // must stay 0: code after a failing await never runs
    int after_middle = 0;
    int caught = 0;
    char message[Error::capacity] = {0};
    double woke = 0;
    int value = 0;
};

Task<int> leaf(Machine &m, bool fails, Log &log) {
    co_await m.sleep(0.01);
    if (fails) {
        co_await fail("leaf failed at %.2f", m.time());
    }
    log.woke = m.time();
    co_return 42;
}
Task<int> middle(Machine &m, bool fails, Log &log) {
    int v = co_await leaf(m, fails, log);
    log.after_leaf++;
    co_return v + 1;
}
Task<void> catching_root(Machine &m, Log &log) {
    Result<int> r = co_await caught(middle(m, true, log));
    if (!r.ok) {
        log.caught++;
        std::snprintf(log.message, sizeof log.message, "%s", r.error.text);
    }
    Result<int> good = co_await caught(middle(m, false, log));
    if (good.ok) {
        log.value = good.value;
    }
    log.after_middle++;
}
Task<void> failing_root(Machine &m, Log &log) {
    co_await middle(m, true, log);
    log.after_middle++;
}

}  // namespace

int main() {
    auto engine = std::make_unique<Engine>(0);
    auto machine = std::make_unique<Machine>(*engine);
    {
        Log log;
        RootState state = machine->run_task([&] { return catching_root(*machine, log); });
        check(!state.failed, "a caught failure does not fail the root");
        check(log.caught == 1, "caught() sees the failure");
        check(std::strncmp(log.message, "leaf failed at", 14) == 0, "the failure's message arrives");
        check(log.after_leaf == 1, "code after a failed await is skipped; after a good one it runs");
        check(log.value == 43, "values come back through co_await");
        check(log.after_middle == 1, "the root continues after caught()");
        check(machine->pool().in_use() == 0, "all frames returned after the caught root");
        std::printf("caught root: message \"%s\", value %d, woke at %.4f s\n", log.message, log.value, log.woke);
    }
    {
        Log log;
        RootState state = machine->run_task([&] { return failing_root(*machine, log); });
        check(state.failed, "an uncaught failure fails the root");
        check(std::strncmp(state.error.text, "leaf failed at", 14) == 0, "the root carries the message");
        check(log.after_leaf == 0 && log.after_middle == 0, "no code after the failing await runs");
        check(machine->pool().in_use() == 0, "all frames returned after the failed root");
        std::printf("failed root: \"%s\"\n", state.error.text);
    }
    check(machine->idle(), "nothing left scheduled or waiting");
    if (failures) {
        std::printf("FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
