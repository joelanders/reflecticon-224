// A picture of the plugin's editor on a booted machine, without a host or a
// window: the processor boots the set, renders a few seconds of noise (so the
// meters light), optionally loads a program through the operator, then the
// editor is drawn offscreen to PNG files.
//
//   editor_shot ROM_DIR OUT_DIR [--program N] [--seconds S]
//     OUT_DIR/editor-window.png   the editor at its default size (what a host shows)
//     OUT_DIR/editor-full.png     the whole scrolled content
#include "PluginProcessor.h"
#include "../../editor/editor_view.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void save(const juce::Image &image, const juce::File &file) {
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat png;
    if (out.openedOk() && png.writeImageToStream(image, out)) {
        std::printf("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    } else {
        std::printf("could not write %s\n", file.getFullPathName().toRawUTF8());
    }
}

lexui::EditorView *findView(juce::Component &editor) {
    for (int i = 0; i < editor.getNumChildComponents(); i++) {
        auto *view = dynamic_cast<lexui::EditorView *>(editor.getChildComponent(i));
        if (view != nullptr) {
            return view;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: editor_shot ROM_DIR OUT_DIR [--program N] [--seconds S]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    setenv("LEXICON224_ROMPATH", argv[1], 1);
    juce::File support = juce::File::createTempFile("lexplug-support");
    support.createDirectory();
    setenv("LEXICON224_SUPPORT_DIR", support.getFullPathName().toRawUTF8(), 1);
    juce::File outDir(juce::String::fromUTF8(argv[2]));
    outDir.createDirectory();
    int program = -1;
    double seconds = 3.0;
    for (int i = 3; i + 1 < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--program") {
            program = std::atoi(argv[++i]);
        } else if (arg == "--seconds") {
            seconds = std::atof(argv[++i]);
        }
    }

    auto processor = std::make_unique<PluginProcessor>();
    processor->setPlayConfigDetails(2, 2, 48000.0, 512);
    processor->prepareToPlay(48000.0, 512);
    if (!processor->wait_for_boot(300.0)) {
        std::printf("boot did not finish\n");
        return 1;
    }
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    juce::Random random(1);
    auto block = [&]() {
        for (int c = 0; c < 2; c++) {
            for (int i = 0; i < 512; i++) {
                buffer.setSample(c, i, (random.nextFloat() * 2.0f - 1.0f) * 0.3f);
            }
        }
        processor->processBlock(buffer, midi);
        processor->service_message_thread();
    };
    block();
    if (program >= 0) {
        processor->uiCommands().selectProgram(program);
    }
    int blocks = int(seconds * 48000.0 / 512.0);
    int lastBusy = -2;
    for (int b = 0; b < blocks; b++) {
        block();
        int busy = processor->scheduler().busyParam();
        if (busy != lastBusy) {
            std::printf("  %.2f s: busy on parameter %d", double(b) * 512.0 / 48000.0, busy);
            if (busy >= lexparams::kSlider1 && busy < lexparams::kSlider1 + lexparams::kSliders) {
                const lexparams::MachineState &m = processor->scheduler().machine();
                float host = processor->param(busy)->getValue();
                std::printf(" (host %.6f -> raw %d; machine position %d stored %d, range %d..%d)", double(host),
                            lexparams::toInt(busy, host, m.range()), int(m.sliders[busy - lexparams::kSlider1]),
                            int(m.stored[busy - lexparams::kSlider1]), m.rangeMin, m.rangeMax);
            }
            std::printf("\n");
            lastBusy = busy;
        }
    }
    lexui::UiSnapshot snapshot;
    processor->readUiSnapshot(snapshot);
    std::printf("status %d, program %d, variation %d, text '%s'; tasks dispatched %llu, rejected %llu, busy on %d\n",
                int(snapshot.status), snapshot.program, snapshot.variation, snapshot.statusText,
                (unsigned long long)processor->scheduler().dispatchedCount(),
                (unsigned long long)processor->scheduler().rejectedCount(), processor->scheduler().busyParam());

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    lexui::EditorView *view = findView(*editor);
    if (view == nullptr) {
        std::printf("no EditorView in the editor\n");
        return 1;
    }
    view->refresh();
    save(editor->createComponentSnapshot(editor->getLocalBounds()), outDir.getChildFile("editor-window.png"));
    juce::Component &content = view->content();
    content.setSize(content.getWidth(), view->contentHeight());
    view->refresh();
    save(content.createComponentSnapshot(content.getLocalBounds()), outDir.getChildFile("editor-full.png"));
    editor.reset();
    processor.reset();
    support.deleteRecursively();
    return 0;
}
