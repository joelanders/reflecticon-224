// The Rack module class without Rack running: construct it, point it at a
// ROM folder, and call process() as Rack's engine would. Linked against the
// SDK's libRack (make smoke ROMS=...); run from this folder (the catalogs are
// found under ./res/catalogs).
//
// A 440 Hz burst must make sound and leave a tail; then the module's own
// buttons: next program, next variation, next page, and a fader move, each
// checked against what the machine holds and what the UI's View shows.
#include "../src/Reflexion224.cpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

rack::plugin::Plugin *pluginInstance;

namespace {

const float rate = 48000.0f;
Reflexion224 *module;
rack::engine::Module::ProcessArgs args;
int64_t frame = 0;

void run(double seconds, float (*input)(int64_t) = nullptr) {
    int n = int(seconds * rate);
    for (int i = 0; i < n; i++, frame++) {
        module->inputs[Reflexion224::IN_L_INPUT].setVoltage(input ? input(frame) : 0.0f);
        args.frame = frame;
        module->process(args);
    }
}

// Press and release a button (one sample down is enough for its trigger).
void press(int param) {
    module->params[param].setValue(1.0f);
    run(0.001);
    module->params[param].setValue(0.0f);
}

// Run until the machine is idle again (or 10 s).
bool settle() {
    for (int i = 0; i < 1000; i++) {
        run(0.01);
        lexparams::Published p = module->core.published();
        if (p.ready && p.pendingMask == 0 && p.busyParam < 0 && p.machine.program >= 0) {
            run(0.02);   // let follow_machine see it
            return true;
        }
    }
    return false;
}

void show(const char *what) {
    Reflexion224::View v = module->view();
    auto texts = module->core.slider_texts();
    std::printf("  %-18s %-14s %s V%d  page %d/%d %-18s |", what, v.program ? v.program->name.c_str() : "?",
                v.program ? v.program->where.c_str() : "", v.pub.machine.variation, v.page_index + 1, v.page_count,
                v.page ? v.page->heading.c_str() : "");
    for (int s = 0; s < faders; s++) {
        int k = v.k(s);
        std::printf(" %s=%s(%g)", k ? v.program->slider(v.program->generic[size_t(k - 1)]).name.c_str() : "-",
                    k ? texts.text[k - 1] : "", module->params[Reflexion224::FADER_PARAM + s].getValue());
        if (s == 1) {
            std::printf(" ...");
            break;
        }
    }
    std::printf("\n");
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: module_smoke ROM_DIR\n");
        return 2;
    }
    pluginInstance = new rack::plugin::Plugin;
    pluginInstance->path = ".";
    module = new Reflexion224;
    module->core.set_sample_rate(int(rate));
    module->set_rom_path(argv[1]);
    for (int i = 0; i < 6000 && module->core.status() == lexvcv::Status::Booting; i++) {   // up to 60 s
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::printf("status: %s\n", module->core.status_text().c_str());
    module->inputs[Reflexion224::IN_L_INPUT].setChannels(1);
    module->outputs[Reflexion224::OUT_L_OUTPUT].setChannels(1);
    module->outputs[Reflexion224::OUT_R_OUTPUT].setChannels(1);
    args.sampleRate = rate;
    args.sampleTime = 1.0f / rate;
    int failures = 0;

    // Sound: a 0.2 s, 5 V, 440 Hz burst, then its tail.
    double peak[2] = {0, 0};
    for (int i = 0; i < int(rate) * 3; i++, frame++) {
        float v = i < int(rate / 5) ? 5.0f * std::sin(2.0f * float(M_PI) * 440.0f * float(i) / rate) : 0.0f;
        module->inputs[Reflexion224::IN_L_INPUT].setVoltage(v);
        args.frame = frame;
        module->process(args);
        float a = module->outputs[Reflexion224::OUT_L_OUTPUT].getVoltage();
        float c = module->outputs[Reflexion224::OUT_R_OUTPUT].getVoltage();
        peak[i < int(rate) ? 0 : 1] = std::max(peak[i < int(rate) ? 0 : 1], double(std::fabs(a)) + double(std::fabs(c)));
    }
    std::printf("output peak (V): first second %.3f, tail after 1 s %.3f\n", peak[0], peak[1]);
    failures += !(peak[0] > 0.01 && peak[1] > 0.0);
    // The burst's level detectors (bit k: 0.28 V * 2^k exceeded), as the meter shows them.
    std::printf("headroom hits since the burst: L 0x%02x R 0x%02x\n", module->core.take_headroom(0),
                module->core.take_headroom(1));

    // MIX fully dry: the input itself (gain 0 dB, no rate conversion at 48 kHz).
    module->params[Reflexion224::MIX_PARAM].setValue(0.0f);
    run(0.01, [](int64_t) { return 3.0f; });
    float dry = module->outputs[Reflexion224::OUT_L_OUTPUT].getVoltage();
    // +6.02 dB of level doubles it.
    module->params[Reflexion224::LEVEL_PARAM].setValue(20.0f * std::log10(2.0f));
    run(0.01, [](int64_t) { return 3.0f; });
    float doubled = module->outputs[Reflexion224::OUT_L_OUTPUT].getVoltage();
    std::printf("dry: 3 V in -> %.4f V out; at +6.02 dB -> %.4f V\n", dry, doubled);
    failures += std::fabs(dry - 3.0f) > 1e-4f || std::fabs(doubled - 6.0f) > 1e-3f;
    module->params[Reflexion224::MIX_PARAM].setValue(1.0f);
    module->params[Reflexion224::LEVEL_PARAM].setValue(0.0f);

    if (!module->core.catalog()) {
        std::printf("no catalog: the buttons have nothing to do\n");
    } else {
        failures += !settle();
        show("booted:");
        // Each named fader's tooltip carries the manual's help for its slider.
        failures += module->help_.empty();
        int described = 0;
        for (int s = 0; s < faders; s++) {
            auto *q = module->paramQuantities[Reflexion224::FADER_PARAM + s];
            std::string help = q->getDescription();
            if (module->view().k(s) > 0) {
                std::printf("  tooltip %-14s %.90s\n", q->getLabel().c_str(), help.c_str());
                described += !help.empty();
            }
        }
        failures += described == 0;
        int before = module->view().pub.machine.program;
        press(Reflexion224::PROGRAM_NEXT_PARAM);
        failures += !settle();
        show("next program:");
        failures += module->view().pub.machine.program != before + 1;

        int variation = module->view().pub.machine.variation;
        press(Reflexion224::VARIATION_NEXT_PARAM);
        failures += !settle();
        show("next variation:");
        const auto *program = module->view().program;
        bool more = program != nullptr && program->variations.size() > 1;
        failures += more && module->view().pub.machine.variation == variation;

        press(Reflexion224::PAGE_NEXT_PARAM);
        failures += !settle();
        show("next page:");
        failures += module->view().page_count > 1 && module->view().page_index != 1;

        // Move the first named slider on this page to the top of the range.
        Reflexion224::View v = module->view();
        int slot = 0;
        while (slot < faders && v.k(slot) == 0) {
            slot++;
        }
        if (slot < faders) {
            int k = v.k(slot);
            int top = v.pub.machine.rangeMax;
            module->params[Reflexion224::FADER_PARAM + slot].setValue(float(top));
            failures += !settle();
            show("fader to the top:");
            int held = module->view().pub.machine.sliders[k - 1];
            std::printf("  slider_%d holds %d (asked %d)\n", k, held, top);
            failures += held != top;
        }
        // A toggle the set offers, flipped as the menu does, then back.
        auto catalog = module->core.catalog();
        std::vector<lexcat::Toggle> offered = catalog->toggles();
        if (!offered.empty()) {
            std::string label = offered.front().label;
            std::replace(label.begin(), label.end(), '_', ' ');
            int t = int(std::find_if(std::begin(lexparams::kToggleLabels), std::end(lexparams::kToggleLabels),
                                     [&](const char *l) { return label == l; }) -
                        std::begin(lexparams::kToggleLabels));
            for (int pass = 0; pass < 2 && t < lexparams::kToggles; pass++) {
                bool was = (module->core.published().machine.toggles >> t) & 1;
                module->set_toggle(t, !was);
                failures += !settle();
                lexparams::Published p = module->core.published();
                bool now = (p.machine.toggles >> t) & 1;
                std::printf("  %s: %d -> %d (known %d)\n", label.c_str(), was, now, (p.machine.togglesKnown >> t) & 1);
                failures += now == was;
            }
        }

        // The display's dropdowns and the menu's toggles, filled into plain menus (no window needed).
        void (*fills[4])(rack::ui::Menu *, Reflexion224 *) = {append_programs, append_variations, append_pages,
                                                              append_toggles};
        const char *names[4] = {"programs", "variations", "pages", "toggles"};
        for (int f = 0; f < 4; f++) {
            auto *menu = new rack::ui::Menu;
            fills[f](menu, module);
            std::string items;
            for (rack::widget::Widget *w : menu->children) {
                if (auto *label = dynamic_cast<rack::ui::MenuLabel *>(w)) {
                    items += "[" + label->text + "] ";
                } else if (auto *item = dynamic_cast<rack::ui::MenuItem *>(w)) {
                    items += item->text + (item->rightText.empty() ? "" : " (" + item->rightText.substr(0, 24) + ")") + "; ";
                }
            }
            std::printf("  %s menu: %.300s\n", names[f], items.c_str());
            failures += menu->children.empty() && !(f == 3 && offered.empty());
            // Not deleted: ~Widget tells APP's event state, and there is no APP here.
        }
        // The patch's data round trip.
        json_t *data = module->dataToJson();
        char *text = json_dumps(data, JSON_COMPACT);
        std::printf("  saved: %.160s...\n", text);
        std::free(text);
        // Reopened as Rack does it (params first, then the module's data): a
        // second module boots the same set and comes back on the same
        // program, variation, page and sliders.
        Reflexion224 *first = module;
        int saved_page = first->view().page_index;
        lexparams::Published was = first->core.published();
        module = new Reflexion224;
        module->core.set_sample_rate(int(rate));
        for (int id : {Reflexion224::PROGRAM_PARAM, Reflexion224::VARIATION_PARAM}) {
            module->params[id].setValue(first->params[id].getValue());
        }
        module->dataFromJson(data);
        json_decref(data);
        for (int i = 0; i < 6000 && module->core.status() == lexvcv::Status::Booting; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        module->inputs[Reflexion224::IN_L_INPUT].setChannels(1);
        module->outputs[Reflexion224::OUT_L_OUTPUT].setChannels(1);
        module->outputs[Reflexion224::OUT_R_OUTPUT].setChannels(1);
        failures += !settle();
        run(0.5);
        failures += !settle();
        show("reopened:");
        lexparams::Published now = module->core.published();
        bool same = now.machine.program == was.machine.program && now.machine.variation == was.machine.variation &&
                    module->view().page_index == saved_page;
        for (int k = 1; k <= was.machine.sliderCount; k++) {
            same = same && now.machine.sliders[k - 1] == was.machine.sliders[k - 1];
        }
        // The faders show what the machine holds.
        for (int f = 0; f < faders; f++) {
            int k = module->view().k(f);
            same = same && (k == 0 || int(module->params[Reflexion224::FADER_PARAM + f].getValue()) == now.machine.sliders[k - 1]);
        }
        for (int k = 1; k <= was.machine.sliderCount; k++) {
            if (now.machine.sliders[k - 1] != was.machine.sliders[k - 1]) {
                std::printf("  slider_%d: saved %d, reopened %d\n", k, was.machine.sliders[k - 1], now.machine.sliders[k - 1]);
            }
        }
        for (int f = 0; f < faders; f++) {
            int k = module->view().k(f);
            if (k > 0 && int(module->params[Reflexion224::FADER_PARAM + f].getValue()) != now.machine.sliders[k - 1]) {
                std::printf("  fader %d shows %g, slider_%d holds %d\n", f + 1,
                            module->params[Reflexion224::FADER_PARAM + f].getValue(), k, now.machine.sliders[k - 1]);
            }
        }
        std::printf("  reopened: page %d (saved %d), program, variation and %d sliders %s\n",
                    module->view().page_index + 1, saved_page + 1, was.machine.sliderCount, same ? "as saved" : "DIFFER");
        failures += !same;
        delete module;
        module = first;
    }
    std::printf(failures == 0 ? "module_smoke: pass\n" : "module_smoke: FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
