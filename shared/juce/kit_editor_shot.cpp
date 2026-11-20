// <target>_editor_shot OUT.png [param=value ...] [hardware] [press=K1,K2,...]: the editor on a booted
// machine, offscreen, then a saved-state round trip into a fresh instance (ROMs from $<rom_env> or
// the remembered paths). Not shipped. Values are plain (e.g. decay=20). `hardware` shows the
// hardware panel; `press=` presses panel keys (decimal or 0x ids) in turn, 150 ms each, 250 ms
// apart, with audio running, then lets 1.5 s pass and adopts what the panel set.
#include "kit_processor.hpp"
#include <juce_audio_utils/juce_audio_utils.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<kit::Processor> proc(dynamic_cast<kit::Processor*>(createPluginFilter()));
    proc->setPlayConfigDetails(2, 2, 48000, 512);
    proc->prepareToPlay(48000, 512);
    juce::StringArray presses;
    for (int i = 2; i < argc; i++) {
        juce::String a(argv[i]);
        if (a == "hardware") {
            proc->setHardwareMode(true);
            continue;
        }
        if (a.startsWith("press=")) {
            presses.addTokens(a.fromFirstOccurrenceOf("=", false, false), ",", "");
            continue;
        }
        if (auto* p = proc->params.getParameter(a.upToFirstOccurrenceOf("=", false, false)))
            p->setValueNotifyingHost(p->convertTo0to1(a.fromFirstOccurrenceOf("=", false, false).getFloatValue()));
    }
    proc->host().sync();
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    auto run = [&](double seconds) {
        for (int k = 0; k < int(seconds * 48000 / 512) + 1; k++) {
            for (int i = 0; i < 512; i++) buf.setSample(0, i, 0.1f * std::sin(float(i) * 0.05f)), buf.setSample(1, i, 0.f);
            proc->processBlock(buf, midi);
        }
    };
    run(0.43);
    for (auto& t : presses) {
        int id = t.trim().startsWithIgnoreCase("0x") ? t.trim().substring(2).getHexValue32() : t.trim().getIntValue();
        proc->host().panel_key(id, true);
        proc->host().refresh_panel();
        run(0.15);
        proc->host().panel_key(id, false);
        proc->host().refresh_panel();
        run(0.25);
    }
    if (!presses.isEmpty()) {
        run(1.5);
        proc->host().refresh_panel();
        proc->adoptPanelSettings();
        proc->host().sync();
        run(0.5);
    }
    proc->host().refresh_panel();
    proc->host().refresh_readouts();
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    ed->setVisible(true);
    if (const char* sc = std::getenv("KIT_SHOT_SCALE"))   // the window resized by the user
        ed->setSize(int(ed->getWidth() * std::atof(sc)), int(ed->getHeight() * std::atof(sc)));
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "editor.png"));
    out.deleteFile();
    {
        juce::FileOutputStream os(out);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    auto before = proc->host().readouts();
    std::printf("%s, latency %d, status: %s\n  panel [%s]\n", out.getFullPathName().toRawUTF8(), proc->getLatencySamples(),
                proc->status().toRawUTF8(), before.panel.c_str());
    if (proc->hasPanel()) {
        auto ps = proc->host().panel();
        std::printf("  hardware panel: valid %d, alpha [%s], LEDs lit:", int(ps.valid), ps.alpha.c_str());
        for (size_t i = 0; i < ps.leds.size(); i++)
            if (ps.leds[i]) std::printf(" %d", int(i));
        std::printf(", adopt serial %u\n  params:", ps.adopt_serial);
        for (auto& prm : proc->info().params)
            std::printf(" %s=%s", prm.id.c_str(), proc->params.getParameter(prm.id)->getCurrentValueAsText().toRawUTF8());
        std::printf("\n");
    }
    ed.reset();

    juce::MemoryBlock st;
    proc->getStateInformation(st);
    std::unique_ptr<kit::Processor> p2(dynamic_cast<kit::Processor*>(createPluginFilter()));
    p2->setStateInformation(st.getData(), int(st.getSize()));
    p2->setPlayConfigDetails(2, 2, 44100, 256);
    p2->prepareToPlay(44100, 256);
    auto after = p2->host().readouts();
    bool same = before.panel == after.panel && before.param == after.param;
    std::printf("state %d bytes; restored panel [%s]: %s\n", int(st.getSize()), after.panel.c_str(), same ? "same" : "DIFFERENT");
    p2.reset();
    proc.reset();
    return same ? 0 : 1;
}
