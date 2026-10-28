// The firmware dropdown's request tracking (editor_view.cpp's applyRomSets).
//
// A click in the popup or an arrow key must send exactly one selectRomSet for
// the set the user picked, must survive the timer tick that still sees the old
// set running, and must not be sent twice (a second selectRomSet cancels the
// boot job that is in flight and starts it again).
//
// The rule this pins down: JUCE delivers ComboBox::onChange asynchronously
// (ComboBox::sendChange -> triggerAsyncUpdate), so a handler that reads
// getSelectedId() there names the box's id when the notification lands, which a
// timer tick may have put back on the running set. On Linux that is the usual
// order -- the X11 events and the message queue are dispatched from one poll
// batch -- and the plugin rebooted the running firmware instead of loading the
// chosen one.
//
//   editor_firmware_select      (no ROMs, no audio, no window)

#include "../../editor/editor_view.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok) {
        failures++;
    }
}

// The editor's calls, recorded.
struct FakeCommands final : lexui::UiCommands {
    std::vector<int> sets;
    int others = 0;

    void addRomLocation(const std::string &) override {}
    void selectRomSet(int index) override {
        sets.push_back(index);
    }
    void selectProgram(int) override {
        others++;
    }
    void selectVariation(int) override {
        others++;
    }
    void moveSlider(int, int) override {
        others++;
    }
    void setToggle(const std::string &, bool) override {
        others++;
    }
    void setLevelDb(float) override {
        others++;
    }
    void setDryWet(float) override {
        others++;
    }
    void setOutputPair(int, int) override {
        others++;
    }
    void setAnalog(bool) override {
        others++;
    }
    void gesture(lexui::UiGesture, int, bool) override {}
};

void nameSets(lexui::UiSnapshot &s, std::initializer_list<const char *> names) {
    s.romSetCount = int32_t(names.size());
    int i = 0;
    for (const char *name : names) {
        std::snprintf(s.romSets[i].name, sizeof s.romSets[i].name, "%s", name);
        s.romSets[i].model = 1;
        s.romSets[i].unsupported = false;
        i++;
    }
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    lexcat::CatalogLibrary catalogs;   // empty: the dropdown needs no catalog
    lexcat::ParamHelp help;
    FakeCommands commands;
    lexui::UiSnapshot snapshot = lexui::defaultSnapshot();
    nameSets(snapshot, {"224 v3.2", "224 v4.3", "224 v4.4"});
    snapshot.status = lexui::UiStatus::Ready;
    snapshot.selectedRomSet = 1;   // 224 v4.3 runs; ids are the index + 1
    snapshot.pendingRomSet = -1;

    lexui::EditorView view(catalogs, help, [&snapshot](lexui::UiSnapshot &s) { s = snapshot; }, commands);
    view.refresh();
    juce::ComboBox &box = view.romSetBox();

    std::printf("the dropdown shows the running set\n");
    check(box.getSelectedId() == 2, "id 2 (224 v4.3)");
    check(commands.sets.empty(), "no command from showing it");

    std::printf("a click in the popup on 224 v4.4 (id 3)\n");
    box.setSelectedId(3);   // exactly what the popup's result callback does
    std::printf("the tick that still sees 224 v4.3 running\n");
    view.refresh();
    check(commands.sets.size() == 1 && commands.sets[0] == 2, "one command, for index 2");
    check(box.getSelectedId() == 3, "the choice is not undone");

    std::printf("further ticks while the job boots\n");
    view.refresh();
    view.refresh();
    check(commands.sets.size() == 1, "still one command");

    std::printf("the processor reports the set it is preparing\n");
    snapshot.pendingRomSet = 2;
    view.refresh();
    check(commands.sets.size() == 1, "still one command");
    check(box.getSelectedId() == 3, "the box shows it");

    std::printf("its machine goes live\n");
    snapshot.pendingRomSet = -1;
    snapshot.selectedRomSet = 2;
    view.refresh();
    check(commands.sets.size() == 1, "still one command");
    check(box.getSelectedId() == 3, "the box shows it");

    std::printf("150 ms of the message loop: the editor's own ticks, and any stale notification\n");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    check(commands.sets.size() == 1, "still one command");
    check(box.getSelectedId() == 3, "still 224 v4.4");

    std::printf("a pick whose boot job ends without a machine\n");
    box.setSelectedId(1);   // 224 v3.2, index 0
    view.refresh();
    check(commands.sets.size() == 2 && commands.sets[1] == 0, "one command, for index 0");
    snapshot.pendingRomSet = 0;   // the job is booting it
    view.refresh();
    snapshot.pendingRomSet = -1;   // ... and stopped: 224 v4.4 still runs
    view.refresh();
    check(commands.sets.size() == 2, "no command for the failure");
    check(box.getSelectedId() == 3, "the box is back on the running set");

    check(commands.others == 0, "no other command");
    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
