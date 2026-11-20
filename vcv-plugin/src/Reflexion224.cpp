// The prototype module: stereo in with a level knob, a stereo out (two of
// the machine's outputs A-D, A and C by default, chosen in the menu) with a
// dry/wet knob, six sliders showing one page of the running program,
// program / variation / page buttons, a display with the input's headroom
// meter, and a status light. The toggles are in the context menu. The ROM folder comes from the context menu
// (saved with the patch) or LEXICON224_ROMPATH; the menu also lists the
// folder's sibling firmware sets, and the programs and variations by name.
//
// Programs, variations and sliders are requests to the core's scheduler
// (core.hpp), in the catalog's terms: a program is a catalog index, a slider
// is slider_k, the k-th named slider of the program in page order, and the
// six faders show the slots of one page (the module's page, independent of
// the firmware's: a move goes to whatever page it belongs to). What the
// machine holds comes back through Published: the program and variation
// params follow it whenever no request of theirs is pending, and the faders
// are set to the stored values whenever the program, the variation or the
// page changes (not after a move, so a fader being dragged is never pulled).
#include "plugin.hpp"
#include "core.hpp"
#include "catalog/help.hpp"
#include <osdialog.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace rack;

namespace {

constexpr int faders = 6;

// Panel geometry, millimetres (20 HP).
constexpr float panel_w = 101.6f;
constexpr float slider_x0 = 14.0f, slider_dx = 14.72f, slider_y = 61.3f;   // the slot: 50 mm tall
// The bottom row on the faders' six columns, as the value windows are.
constexpr float jack_y = 112.9f, jack_label_y = 105.2f, mix_ends_y = 119.6f;
const float in_x[2] = {slider_x0, slider_x0 + slider_dx};
constexpr float level_x = slider_x0 + 2 * slider_dx;
constexpr float mix_x = slider_x0 + 3 * slider_dx;
const float out_x[2] = {slider_x0 + 4 * slider_dx, slider_x0 + 5 * slider_dx};
// The value windows under the faders, and the headroom meter in the display.
constexpr float value_y = 88.15f, value_h = 8.4f, value_w = 13.2f;
constexpr float meter_x = 79.6f, meter_y[2] = {9.6f, 12.4f};
constexpr float meter_seg = 2.4f, meter_gap = 0.5f, meter_h = 2.0f;
const char *const output_names[4] = {"A", "B", "C", "D"};

// The folders next to `path` (and `path` itself) that hold a firmware set,
// sorted by name.
std::vector<std::string> sibling_sets(const std::string &path) {
    namespace fs = std::filesystem;
    std::vector<std::string> sets;
    std::error_code error;
    fs::path parent = lexvcv::utf8_path(path).parent_path();
    for (const auto &entry : fs::directory_iterator(parent, error)) {
        if (entry.is_directory(error) && lexplug::load_rom_directory(entry.path())) {
            sets.push_back(lexvcv::path_utf8(entry.path()));
        }
    }
    std::sort(sets.begin(), sets.end());
    return sets;
}

// A folder the user chose: the set itself, or else the first set inside it.
std::string set_in(const std::string &path) {
    namespace fs = std::filesystem;
    if (lexplug::load_rom_directory(lexvcv::utf8_path(path))) {
        return path;
    }
    std::vector<std::string> inside;
    std::error_code error;
    for (const auto &entry : fs::directory_iterator(lexvcv::utf8_path(path), error)) {
        if (entry.is_directory(error) && lexplug::load_rom_directory(entry.path())) {
            inside.push_back(lexvcv::path_utf8(entry.path()));
        }
    }
    if (inside.empty()) {
        return path;   // boots to "no ROM chips in ..."
    }
    std::sort(inside.begin(), inside.end());
    return inside.front();
}

bool pending(const lexparams::Published &p, int param) {
    return (p.pendingMask >> param) & 1;
}

}  // namespace

struct Reflexion224 : engine::Module {
    enum ParamId {
        ENUMS(FADER_PARAM, faders),
        PROGRAM_PARAM,
        VARIATION_PARAM,
        PROGRAM_PREV_PARAM,
        PROGRAM_NEXT_PARAM,
        VARIATION_PREV_PARAM,
        VARIATION_NEXT_PARAM,
        PAGE_PREV_PARAM,
        PAGE_NEXT_PARAM,
        // Appended (patches key params by index).
        MIX_PARAM,
        LEVEL_PARAM,
        OUT_LEFT_PARAM,    // the context menu's Outputs: 0-3 = A-D
        OUT_RIGHT_PARAM,
        PARAMS_LEN
    };
    enum InputId { IN_L_INPUT, IN_R_INPUT, INPUTS_LEN };
    enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };   // the status is on the display

    // What the UI shows: the newest catalog, the machine's state, and the
    // page in view. Any thread but the audio thread.
    struct View {
        std::shared_ptr<const lexcat::Catalog> catalog;
        lexparams::Published pub;
        const lexcat::Program *program = nullptr;
        const lexcat::Page *page = nullptr;
        int page_index = 0;
        int page_count = 0;
        // slider_k (1-based) on slot s of the page in view, 0 if none.
        int k(int slot) const {
            return program != nullptr && page != nullptr ? program->genericIndexOf(page->page, slot) : 0;
        }
    };

    lexvcv::Core core;
    std::string rom_path;
    // The sliders' help from the owner's manuals (res/param_help.json), as the
    // web demo and the JUCE plugin show it; empty if the file is missing.
    lexcat::ParamHelp help_;
    std::atomic<int> page_index{0};
    bool keep_page_ = false;   // set by dataFromJson, read once by requests()

    // Audio thread.
    lexparams::Published pub_;
    float seen_fader_[faders] = {};
    int seen_program_ = -1;     // -1: submit the param's value (a restored patch's) once
    int seen_variation_ = -1;
    int shown_program_ = -2, shown_variation_ = -2, shown_page_ = -1;
    bool shown_ready_ = false;
    dsp::SchmittTrigger buttons_[6];
    dsp::ClockDivider state_divider_;
    std::vector<int> restore_sliders_;   // set by dataFromJson, sent after the program
    int restore_toggles_ = 0, restore_toggles_known_ = 0;
    bool restore_pending_ = false;
    // The menu's toggle clicks (bit t: lexparams::Toggle), UI -> audio thread.
    std::atomic<int> toggles_on_{0}, toggles_off_{0};

    void set_toggle(int t, bool on) {
        (on ? toggles_on_ : toggles_off_).fetch_or(1 << t);
    }

    // An output jack's name says which machine output it carries.
    struct OutputInfo : PortInfo {
        std::string getName() override {
            int side = portId - OUT_L_OUTPUT;
            int dac = int(module->params[OUT_LEFT_PARAM + side].getValue());
            return string::f("%s (machine output %s)", side == 0 ? "Left" : "Right", output_names[std::clamp(dac, 0, 3)]);
        }
    };

    struct FaderQuantity : ParamQuantity {
        std::string getLabel() override {
            auto *m = dynamic_cast<Reflexion224 *>(module);
            if (m != nullptr) {
                View v = m->view();
                int k = v.k(paramId - FADER_PARAM);
                if (k > 0) {
                    return v.program->slider(v.program->generic[size_t(k - 1)]).name;
                }
            }
            return string::f("Slider %d (none on this page)", paramId - FADER_PARAM + 1);
        }
        std::string getDisplayValueString() override {
            auto *m = dynamic_cast<Reflexion224 *>(module);
            if (m != nullptr) {
                View v = m->view();
                int k = v.k(paramId - FADER_PARAM);
                char live[25];
                if (k > 0 && m->core.live_text(k, live)) {
                    return live;   // being dragged: the firmware's text now
                }
                auto texts = m->core.slider_texts();
                if (k > 0 && texts.text[k - 1][0] != 0) {
                    return texts.text[k - 1];
                }
            }
            return ParamQuantity::getDisplayValueString();
        }
        std::string getDescription() override {
            auto *m = dynamic_cast<Reflexion224 *>(module);
            if (m != nullptr) {
                View v = m->view();
                int k = v.k(paramId - FADER_PARAM);
                if (k > 0) {
                    const auto &slider = v.program->slider(v.program->generic[size_t(k - 1)]);
                    return m->help_.helpFor(slider.name, v.page->heading);
                }
            }
            return {};
        }
    };

    struct ProgramQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            auto *m = dynamic_cast<Reflexion224 *>(module);
            int i = int(getValue());
            if (m != nullptr) {
                auto catalog = m->core.catalog();
                if (catalog && size_t(i) < catalog->programs.size()) {
                    return catalog->programs[size_t(i)].label;
                }
            }
            return string::f("%d", i + 1);
        }
    };

    Reflexion224() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int i = 0; i < faders; i++) {
            configParam<FaderQuantity>(FADER_PARAM + i, 0.0f, 255.0f, 128.0f)->snapEnabled = true;
        }
        configParam<ProgramQuantity>(PROGRAM_PARAM, 0.0f, float(lexparams::kProgramSlots - 1), 0.0f, "Program")
            ->snapEnabled = true;
        configParam(VARIATION_PARAM, 1.0f, float(lexparams::kVariations), 1.0f, "Variation")->snapEnabled = true;
        configButton(PROGRAM_PREV_PARAM, "Previous program");
        configButton(PROGRAM_NEXT_PARAM, "Next program");
        configButton(VARIATION_PREV_PARAM, "Previous variation");
        configButton(VARIATION_NEXT_PARAM, "Next variation");
        configButton(PAGE_PREV_PARAM, "Previous page");
        configButton(PAGE_NEXT_PARAM, "Next page");
        configInput(IN_L_INPUT, "Left (also right when Right is unpatched)");
        configInput(IN_R_INPUT, "Right");
        configParam(MIX_PARAM, 0.0f, 1.0f, 1.0f, "Mix (dry / wet)", "% wet", 0.0f, 100.0f);
        configParam(LEVEL_PARAM, lexparams::kLevelMinDb, lexparams::kLevelMaxDb, 0.0f, "Input level", " dB");
        configSwitch(OUT_LEFT_PARAM, 0.0f, 3.0f, 0.0f, "Left output", {"A", "B", "C", "D"});
        configSwitch(OUT_RIGHT_PARAM, 0.0f, 3.0f, 2.0f, "Right output", {"A", "B", "C", "D"});
        configOutput<OutputInfo>(OUT_L_OUTPUT);
        configOutput<OutputInfo>(OUT_R_OUTPUT);
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);
        for (int i = 0; i < faders; i++) {
            seen_fader_[i] = params[FADER_PARAM + i].getValue();
        }
        state_divider_.setDivision(256);
        core.set_catalog_dir(asset::plugin(pluginInstance, "res/catalogs"));
        std::ifstream help(lexvcv::utf8_path(asset::plugin(pluginInstance, "res/param_help.json")), std::ios::binary);
        if (help) {
            std::stringstream text;
            text << help.rdbuf();
            try {
                help_ = lexcat::ParamHelp::parse(text.str());
            } catch (const std::exception &e) {
                WARN("res/param_help.json: %s", e.what());
            }
        }
    }

    void onAdd(const AddEvent &e) override {
        core.set_sample_rate(int(APP->engine->getSampleRate()));
        if (rom_path.empty()) {
            const char *env = std::getenv("LEXICON224_ROMPATH");
            if (env != nullptr && *env != 0) {
                set_rom_path(env);
            }
        }
    }

    void onSampleRateChange(const SampleRateChangeEvent &e) override {
        core.set_sample_rate(int(e.sampleRate));
    }

    void set_rom_path(const std::string &path) {
        rom_path = path;
        core.boot(path);
    }

    View view() const {
        View v;
        v.catalog = core.catalog();
        v.pub = core.published();
        if (v.catalog && v.pub.machine.program >= 0 && size_t(v.pub.machine.program) < v.catalog->programs.size()) {
            v.program = &v.catalog->programs[size_t(v.pub.machine.program)];
            v.page_count = int(v.program->pages.size());
            if (v.page_count > 0) {
                v.page_index = std::clamp(page_index.load(), 0, v.page_count - 1);
                v.page = &v.program->pages[size_t(v.page_index)];
            }
        }
        return v;
    }

    // Audio thread: the running program's catalog entry, as of pub_.
    const lexcat::Program *audio_program() const {
        const lexcat::Catalog *catalog = core.audio_catalog();
        int p = pub_.machine.program;
        if (catalog == nullptr || p < 0 || size_t(p) >= catalog->programs.size()) {
            return nullptr;
        }
        return &catalog->programs[size_t(p)];
    }

    void step_param(int id, int delta, int low, int high) {
        if (high < low) {
            return;
        }
        int v = std::clamp(int(std::lround(params[id].getValue())) + delta, low, high);
        params[id].setValue(float(v));
    }

    void buttons() {
        bool pressed[6];
        for (int i = 0; i < 6; i++) {
            pressed[i] = buttons_[i].process(params[PROGRAM_PREV_PARAM + i].getValue());
        }
        const lexcat::Catalog *catalog = core.audio_catalog();
        int programs = catalog == nullptr ? 0 : int(catalog->programs.size());
        if (pressed[0] || pressed[1]) {
            step_param(PROGRAM_PARAM, pressed[1] ? 1 : -1, 0, programs - 1);
        }
        const lexcat::Program *program = audio_program();
        if ((pressed[2] || pressed[3]) && program != nullptr && !program->variations.empty()) {
            const std::vector<int> &vs = program->variations;
            int at = int(std::find(vs.begin(), vs.end(), int(params[VARIATION_PARAM].getValue())) - vs.begin());
            at = std::clamp(at + (pressed[3] ? 1 : -1), 0, int(vs.size()) - 1);
            params[VARIATION_PARAM].setValue(float(vs[size_t(at)]));
        }
        if ((pressed[4] || pressed[5]) && program != nullptr && !program->pages.empty()) {
            int n = int(program->pages.size());
            page_index = (std::clamp(page_index.load(), 0, n - 1) + (pressed[5] ? 1 : n - 1)) % n;
        }
    }

    // The user's changes become requests.
    void requests() {
        int program = int(std::lround(params[PROGRAM_PARAM].getValue()));
        if (program != seen_program_) {
            seen_program_ = program;
            const lexcat::Catalog *catalog = core.audio_catalog();
            if (catalog == nullptr || size_t(program) < catalog->programs.size()) {
                core.request(lexparams::kProgram, program);
            }
            // A new program opens on its first page, except the program a
            // patch was saved with: its page comes back with it.
            if (!keep_page_) {
                page_index = 0;
            }
            keep_page_ = false;
        }
        int variation = int(std::lround(params[VARIATION_PARAM].getValue()));
        if (variation != seen_variation_) {
            seen_variation_ = variation;
            core.request(lexparams::kVariation, variation);
        }
        if (restore_pending_) {
            restore_pending_ = false;
            for (size_t k = 1; k <= restore_sliders_.size() && k <= size_t(lexparams::kSliders); k++) {
                core.request(lexparams::sliderParam(int(k)), restore_sliders_[k - 1]);
            }
            for (int t = 0; t < lexparams::kToggles; t++) {
                if ((restore_toggles_known_ >> t) & 1) {
                    core.request(lexparams::toggleParam(t), (restore_toggles_ >> t) & 1);
                }
            }
        }
        int on = toggles_on_.exchange(0), off = toggles_off_.exchange(0);
        for (int t = 0; t < lexparams::kToggles; t++) {
            if (((on | off) >> t) & 1) {
                core.request(lexparams::toggleParam(t), (on >> t) & 1);
            }
        }
        const lexcat::Program *shown = audio_program();
        int page = shown == nullptr || shown->pages.empty()
                       ? -1
                       : shown->pages[size_t(std::clamp(page_index.load(), 0, int(shown->pages.size()) - 1))].page;
        for (int s = 0; s < faders; s++) {
            float value = params[FADER_PARAM + s].getValue();
            if (value == seen_fader_[s]) {
                continue;
            }
            seen_fader_[s] = value;
            int k = shown == nullptr ? 0 : shown->genericIndexOf(page, s);
            if (k > 0) {
                core.request(lexparams::sliderParam(k), int(std::lround(value)));
            }
        }
    }

    // What the machine holds back into the params (see the header).
    void follow_machine() {
        pub_ = core.published();
        const lexparams::MachineState &m = pub_.machine;
        if (m.program >= 0 && !pending(pub_, lexparams::kProgram)) {
            params[PROGRAM_PARAM].setValue(float(m.program));
            seen_program_ = m.program;
        }
        if (m.variation > 0 && !pending(pub_, lexparams::kVariation)) {
            params[VARIATION_PARAM].setValue(float(m.variation));
            seen_variation_ = m.variation;
        }
        int page = page_index.load();
        bool ready = pub_.ready != 0;
        if (m.program == shown_program_ && m.variation == shown_variation_ && page == shown_page_ && ready == shown_ready_) {
            return;
        }
        shown_program_ = m.program;
        shown_variation_ = m.variation;
        shown_page_ = page;
        shown_ready_ = ready;
        const lexcat::Program *program = audio_program();
        if (program == nullptr || program->pages.empty()) {
            return;
        }
        const lexcat::Page &p = program->pages[size_t(std::clamp(page, 0, int(program->pages.size()) - 1))];
        for (int s = 0; s < faders; s++) {
            int k = program->genericIndexOf(p.page, s);
            if (k > 0 && pending(pub_, lexparams::sliderParam(k))) {
                shown_page_ = -1;   // look again once it has been applied
                continue;
            }
            // A slot with no slider on this page parks at the bottom.
            params[FADER_PARAM + s].setValue(k > 0 ? float(m.sliders[k - 1]) : 0.0f);
            seen_fader_[s] = params[FADER_PARAM + s].getValue();
        }
    }

    void process(const ProcessArgs &args) override {
        buttons();
        requests();
        if (state_divider_.process()) {
            follow_machine();
        }
        // Rack's 10 V peak to the machine's full scale (the web demo's +-1).
        float in_l = inputs[IN_L_INPUT].getVoltage() / 10.0f;
        float in_r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() / 10.0f : in_l;
        core.set_wet(params[MIX_PARAM].getValue());
        core.set_level_db(params[LEVEL_PARAM].getValue());
        core.set_outputs(int(params[OUT_LEFT_PARAM].getValue()), int(params[OUT_RIGHT_PARAM].getValue()));
        float out_l, out_r;
        core.process(in_l, in_r, out_l, out_r);
        outputs[OUT_L_OUTPUT].setVoltage(out_l * 10.0f);
        outputs[OUT_R_OUTPUT].setVoltage(out_r * 10.0f);
    }

    // The patch keeps the ROM folder, the page in view, every named slider's
    // position and the toggles (the program, variation, mix, level and
    // outputs are params).
    json_t *dataToJson() override {
        json_t *root = json_object();
        json_object_set_new(root, "romPath", json_string(rom_path.c_str()));
        json_object_set_new(root, "page", json_integer(page_index.load()));
        lexparams::Published p = core.published();
        if (p.ready && p.machine.program >= 0) {
            json_t *sliders = json_array();
            for (int k = 1; k <= p.machine.sliderCount; k++) {
                json_array_append_new(sliders, json_integer(p.machine.sliders[k - 1]));
            }
            json_object_set_new(root, "sliders", sliders);
            json_object_set_new(root, "toggles", json_integer(p.machine.toggles & p.machine.togglesKnown));
            json_object_set_new(root, "togglesKnown", json_integer(p.machine.togglesKnown));
        }
        return root;
    }

    void dataFromJson(json_t *root) override {
        json_t *page = json_object_get(root, "page");
        if (page != nullptr) {
            page_index = int(json_integer_value(page));
            keep_page_ = true;   // requests() re-sends the program below; not a new one
        }
        json_t *sliders = json_object_get(root, "sliders");
        if (sliders != nullptr) {
            restore_sliders_.clear();
            size_t i;
            json_t *value;
            json_array_foreach(sliders, i, value) {
                restore_sliders_.push_back(int(json_integer_value(value)));
            }
            restore_pending_ = true;
        }
        json_t *toggles = json_object_get(root, "toggles"), *known = json_object_get(root, "togglesKnown");
        if (toggles != nullptr && known != nullptr) {
            restore_toggles_ = int(json_integer_value(toggles));
            restore_toggles_known_ = int(json_integer_value(known));
            restore_pending_ = true;
        }
        seen_program_ = -1;
        seen_variation_ = -1;
        json_t *path = json_object_get(root, "romPath");
        if (path != nullptr && json_string_value(path)[0] != 0) {
            set_rom_path(json_string_value(path));
        }
    }
};

// The lists behind the display's dropdowns and the context menu.

// Every program, under its group's name (HALLS, PLATES, ...) where the
// catalog has groups.
void append_programs(ui::Menu *menu, Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (!v.catalog) {
        menu->addChild(createMenuLabel("No catalog for this firmware set"));
        return;
    }
    const std::string *group = nullptr;
    for (size_t i = 0; i < v.catalog->programs.size(); i++) {
        const lexcat::Program &p = v.catalog->programs[i];
        if (!p.group.empty() && (group == nullptr || *group != p.group)) {
            if (group != nullptr) {
                menu->addChild(new ui::MenuSeparator);
            }
            menu->addChild(createMenuLabel(p.group));
        }
        group = &p.group;
        int current = v.pub.machine.program;
        menu->addChild(createCheckMenuItem(
            p.label, "", [current, i] { return current == int(i); },
            [module, i] { module->paramQuantities[Reflexion224::PROGRAM_PARAM]->setValue(float(i)); }));
    }
}

void append_variations(ui::Menu *menu, Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (v.program == nullptr) {
        return;
    }
    int at = v.pub.machine.variation;
    for (int var : v.program->variations) {
        menu->addChild(createCheckMenuItem(
            string::f("V%d", var), "", [at, var] { return at == var; },
            [module, var] { module->paramQuantities[Reflexion224::VARIATION_PARAM]->setValue(float(var)); }));
    }
}

// Every page of the program, by its heading or its sliders' names.
void append_pages(ui::Menu *menu, Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (v.program == nullptr) {
        return;
    }
    for (int i = 0; i < v.page_count; i++) {
        const lexcat::Page &page = v.program->pages[size_t(i)];
        std::string names;
        for (const lexcat::Slider &s : page.sliders) {
            if (s.named()) {
                names += (names.empty() ? "" : ", ") + s.name;
            }
        }
        // In the title: a check item's right text is its check mark.
        std::string title = string::f("Page %d", i + 1);
        if (!page.heading.empty() && page.heading[0] == '[') {
            title += "  " + page.heading;
        }
        if (names.size() > 48) {
            names = names.substr(0, 45) + "...";
        }
        title += ":  " + names;
        int at = v.page_index;
        menu->addChild(createCheckMenuItem(title, "", [at, i] { return at == i; },
                                           [module, i] { module->page_index = i; }));
    }
}

// The toggles the set's remote offers (the 224XL: DYN DECAY, MODE ENH,
// DECAY OPT and the LARC's output mute; the 224: MODE ENH, DECAY OPT; the
// 224X panel: none), checked as the machine holds them.
void append_toggles(ui::Menu *menu, Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (!v.catalog || v.program == nullptr) {
        return;
    }
    std::vector<lexcat::Toggle> toggles = v.catalog->toggles();
    if (v.catalog->hasMute()) {
        toggles.push_back(lexcat::Catalog::muteToggle());
    }
    bool first = true;
    for (const lexcat::Toggle &toggle : toggles) {
        std::string label = toggle.label;
        std::replace(label.begin(), label.end(), '_', ' ');
        int t = -1;
        for (int i = 0; i < lexparams::kToggles; i++) {
            if (label == lexparams::kToggleLabels[i]) {
                t = i;
            }
        }
        if (t < 0) {
            continue;
        }
        if (first) {
            menu->addChild(new ui::MenuSeparator);
            first = false;
        }
        menu->addChild(createCheckMenuItem(
            toggle.title.empty() ? label : toggle.title, "",
            [module, t] { return (module->core.published().machine.toggles >> t) & 1; },
            [module, t] {
                bool on = (module->core.published().machine.toggles >> t) & 1;
                module->set_toggle(t, !on);
            }));
    }
}

// The printed labels, drawn at run time (Rack's SVG renderer skips <text>).
struct PanelLabels : widget::Widget {
    void draw(const DrawArgs &args) override {
        std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) {
            return;
        }
        nvgFontFaceId(args.vg, font->handle);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        auto label = [&](float x, float y, float size, NVGcolor color, const char *text) {
            nvgFontSize(args.vg, mm2px(size));
            nvgFillColor(args.vg, color);
            nvgText(args.vg, mm2px(x), mm2px(y), text, nullptr);
        };
        NVGcolor ink = nvgRGB(0x2b, 0x26, 0x20), light = nvgRGB(0xe8, 0xe2, 0xd4);
        nvgTextLetterSpacing(args.vg, mm2px(0.6f));
        label(panel_w / 2, 3.25f, 3.6f, ink, "REFLEXION 224");
        nvgTextLetterSpacing(args.vg, 0.0f);
        label(in_x[0], jack_label_y, 2.4f, ink, "IN L");
        label(in_x[1], jack_label_y, 2.4f, ink, "IN R");
        label(level_x, jack_label_y, 2.4f, ink, "LEVEL");
        label(mix_x, jack_label_y, 2.4f, ink, "MIX");
        label(mix_x - 4.4f, mix_ends_y, 1.8f, ink, "DRY");
        label(mix_x + 4.4f, mix_ends_y, 1.8f, ink, "WET");
        label(out_x[0], jack_label_y, 2.4f, light, "OUT L");
        label(out_x[1], jack_label_y, 2.4f, light, "OUT R");
    }
};

// The display's clickable fields (millimetres): the program name, the
// variation and the page, each a dropdown (DisplayChoice below) followed by a
// previous/next pair of arrows (DisplayArrow). Where each field and its
// arrows sit follows the text as drawn (DisplayLayout); these are the places
// before the first frame.
struct Field {
    float x, y, w, h;
};
const Field program_field = {7.0f, 7.0f, 50.0f, 8.0f};
// Centred on their row's text (y 11 and 17.5), as the arrows are.
const Field variation_field = {7.0f, 14.5f, 11.5f, 6.0f};
const Field page_field = {31.0f, 14.5f, 26.5f, 6.0f};
constexpr float arrow_w = 4.8f, arrow_gap = 0.4f;
// Every triangle on the display (a dropdown's mark, the arrows): one shape,
// tri_base across and tri_depth from base to tip, pointing down, left or right.
constexpr float tri_base = 2.2f, tri_depth = 1.5f;
// A field's text, then mark_lead, its mark, arrows_lead, and the two arrows.
constexpr float mark_lead = 1.2f, mark_w = tri_base, arrows_lead = 0.8f;
constexpr float arrows_w = 2 * arrow_w + arrow_gap;

// Where the display drew each field (program, variation, page) in the last
// frame, for the dropdowns' and the arrows' widgets to follow.
struct DisplayLayout {
    bool valid[3] = {};   // the field was drawn as a dropdown
    float choice_x[3] = {}, choice_w[3] = {}, arrows_x[3] = {};
};

// The display: the program, its variation and page (the bank and program
// number are in the program field's tooltip and menu),
// each fader's slider name above it and the firmware's value text below it
// (in its own window), and the input's headroom meter. Lit (layer 1), so it
// glows when Rack's room is dark.
struct ProgramDisplay : widget::Widget {
    Reflexion224 *module = nullptr;
    DisplayLayout layout;
    double lit_until_[2][5] = {};   // a detector's segment stays lit this long after a hit

    // Two rows (L, R) of five segments, one per level detector, 6 dB apart:
    // the top one is the ADC's clipping onset.
    void draw_meter(const DrawArgs &args) {
        double now = system::getTime();
        for (int c = 0; c < 2; c++) {
            uint8_t hits = module != nullptr ? module->core.take_headroom(c) : uint8_t(c == 0 ? 0x07 : 0x03);
            for (int k = 0; k < 5; k++) {
                if ((hits >> k) & 1) {
                    lit_until_[c][k] = now + (k == 4 ? 1.0 : 0.25);
                }
                bool lit = module == nullptr ? ((hits >> k) & 1) : now < lit_until_[c][k];
                NVGcolor on = k == 4 ? nvgRGB(0xff, 0x20, 0x10) : k == 3 ? nvgRGB(0xff, 0xb0, 0x20) : nvgRGB(0xff, 0x5a, 0x1f);
                nvgBeginPath(args.vg);
                nvgRect(args.vg, mm2px(meter_x + (meter_seg + meter_gap) * float(k)), mm2px(meter_y[c] - meter_h / 2),
                        mm2px(meter_seg), mm2px(meter_h));
                nvgFillColor(args.vg, lit ? on : nvgRGB(0x3a, 0x1c, 0x14));
                nvgFill(args.vg);
            }
        }
    }

    // A slider name in the strip over its fader: one line if it fits at the
    // full size, else two, broken where the lines come out most even (at a
    // space, or inside one of the firmware's compounds), shrunk only if a
    // line still does not fit.
    void draw_name(const DrawArgs &args, float x, const std::string &name) {
        constexpr float size = 3.3f, room = slider_dx - 1.2f, strip_y = 30.95f;
        std::vector<std::string> lines = name_lines(args, name, size, room);
        float widest = 0.0f, bounds[4];
        nvgFontSize(args.vg, mm2px(size));
        for (const std::string &line : lines) {
            widest = std::max(widest, nvgTextBounds(args.vg, 0, 0, line.c_str(), nullptr, bounds));
        }
        float scale = widest > mm2px(room) ? mm2px(room) / widest : 1.0f;
        nvgFontSize(args.vg, mm2px(size) * scale);
        float step = size * scale * 0.95f;
        float y = strip_y - step * float(lines.size() - 1) / 2;
        for (const std::string &line : lines) {
            nvgText(args.vg, mm2px(x), mm2px(y), line.c_str(), nullptr);
            y += step;
        }
    }

    static std::vector<std::string> name_lines(const DrawArgs &args, const std::string &name, float size, float room) {
        // The words, with the LARC's padding ("LF DECAY   L") collapsed, and
        // the compounds split where they join (a part ending in '-' keeps it).
        static const std::pair<const char *, const char *> compounds[] = {
            {"PREDELAY", "PRE"},     {"PRE-DELAY", "PRE-"},   {"CROSSOVER", "CROSS"},
            {"CROSSFEED", "CROSS"},  {"BANDWIDTH", "BAND"},   {"FEEDBACK", "FEED"},
        };
        struct Part {
            std::string text;
            bool space_before;
        };
        std::vector<Part> parts;
        std::istringstream words(name);
        std::string word;
        while (words >> word) {
            bool split = false;
            for (const auto &[whole, head] : compounds) {
                if (word == whole) {
                    parts.push_back({head, !parts.empty()});
                    parts.push_back({word.substr(std::strlen(head)), false});
                    split = true;
                }
            }
            if (!split) {
                parts.push_back({word, !parts.empty()});
            }
        }
        auto join = [&](size_t from, size_t to) {
            std::string s;
            for (size_t i = from; i < to; i++) {
                s += (i > from && parts[i].space_before ? " " : "") + parts[i].text;
            }
            return s;
        };
        float bounds[4];
        nvgFontSize(args.vg, mm2px(size));
        auto width = [&](const std::string &s) { return nvgTextBounds(args.vg, 0, 0, s.c_str(), nullptr, bounds); };
        std::string whole = join(0, parts.size());
        if (parts.size() < 2 || width(whole) <= mm2px(room)) {
            return {whole};
        }
        size_t best = 1;
        float best_width = 1e9f;
        for (size_t at = 1; at < parts.size(); at++) {
            float w = std::max(width(join(0, at)), width(join(at, parts.size())));
            if (w < best_width) {
                best_width = w;
                best = at;
            }
        }
        return {join(0, best), join(best, parts.size())};
    }

    void drawLayer(const DrawArgs &args, int layer) override {
        if (layer != 1) {
            return Widget::drawLayer(args, layer);
        }
        draw_meter(args);
        std::string line1, line2, variation, page, heading;
        bool choices = false;   // the fields are dropdowns (a program is running)
        std::string names[faders], values[faders];
        if (module == nullptr) {
            // The module browser's preview.
            line1 = "CONCERT HALL";
            variation = "V1";
            page = "PAGE 1/5";
            choices = true;
            const char *preview[faders] = {"LF DECAY", "MID DECAY", "CROSSOVER", "HF CUTOFF", "DEPTH", "PRE DELAY"};
            const char *preview_values[faders] = {"2.5 S", "2.0 S", "850 HZ", "6.5 KHZ", "MED", "24 MS"};
            for (int s = 0; s < faders; s++) {
                names[s] = preview[s];
                values[s] = preview_values[s];
            }
        } else {
            Reflexion224::View v = module->view();
            lexvcv::Status status = module->core.status();
            if (status == lexvcv::Status::NoRoms) {
                line1 = "NO ROMS";
                line2 = "RIGHT-CLICK: CHOOSE ROM FOLDER";
            } else if (status == lexvcv::Status::Failed) {
                line1 = "STOPPED";
                line2 = "SEE THE MENU";
            } else if (status == lexvcv::Status::Booting && v.program == nullptr) {
                line1 = "BOOTING...";
            } else if (!v.catalog) {
                line1 = "NO CATALOG";
                line2 = "POWER-ON PROGRAM ONLY";
            } else if (v.program == nullptr) {
                line1 = "READING...";
            } else {
                choices = true;
                line1 = v.program->name;
                variation = string::f("V%d", v.pub.machine.variation);
                if (v.page_count > 0) {
                    page = string::f("PAGE %d/%d", v.page_index + 1, v.page_count);
                }
                // A heading of its own ("[PREECHO LEVELS]", the 224's
                // "SHIFT"), not the LARC's row of slider abbreviations.
                if (v.page != nullptr && !v.page->heading.empty() &&
                    (v.page->heading[0] == '[' || v.catalog->remote == lexcat::Remote::Panel224)) {
                    heading = v.page->heading;
                }
                auto texts = module->core.slider_texts();
                for (int s = 0; s < faders; s++) {
                    int k = v.k(s);
                    if (k > 0) {
                        names[s] = v.program->slider(v.program->generic[size_t(k - 1)]).name;
                        char live[25];
                        values[s] = module->core.live_text(k, live) ? live : texts.text[k - 1];
                    }
                }
            }
        }
        std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) {
            return;
        }
        NVGcolor lit = nvgRGB(0xff, 0x5a, 0x1f);
        nvgFontFaceId(args.vg, font->handle);
        nvgFillColor(args.vg, lit);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        // The down-pointing triangle after a dropdown field's text, from x,
        // centred on y.
        auto mark = [&](float x, float y) {
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, mm2px(x), mm2px(y - tri_depth / 2));
            nvgLineTo(args.vg, mm2px(x + tri_base), mm2px(y - tri_depth / 2));
            nvgLineTo(args.vg, mm2px(x + tri_base / 2), mm2px(y + tri_depth / 2));
            nvgClosePath(args.vg);
            nvgFill(args.vg);
        };
        float bounds[4];
        auto mm = [](float px) { return px / mm2px(1.0f); };
        // A field's text at x, then (when it is a dropdown) its mark, and the
        // place for its arrows; returns where the field ends (its arrows' end).
        DisplayLayout drawn;
        auto field = [&](int i, float x, float y, const std::string &text) {
            float end = mm(nvgText(args.vg, mm2px(x), mm2px(y), text.c_str(), nullptr));
            if (!choices) {
                return end;
            }
            mark(end + mark_lead, y);
            float mark_end = end + mark_lead + mark_w;
            drawn.valid[i] = true;
            drawn.choice_x[i] = x - 1.0f;
            drawn.choice_w[i] = mark_end + 0.6f - drawn.choice_x[i];
            drawn.arrows_x[i] = mark_end + arrows_lead;
            return drawn.arrows_x[i] + arrows_w;
        };
        // The program, shrunk to fit with its mark and arrows before the meter.
        nvgFontSize(args.vg, mm2px(6.2f));
        float room1 = mm2px(meter_x - 3.7f - 8.0f - (mark_lead + mark_w + arrows_lead + arrows_w));
        float width1 = nvgTextBounds(args.vg, 0, 0, line1.c_str(), nullptr, bounds);
        if (width1 > room1) {
            nvgFontSize(args.vg, mm2px(6.2f) * room1 / width1);
        }
        field(0, 8.0f, 11.0f, line1);
        nvgFontSize(args.vg, mm2px(2.0f));
        nvgText(args.vg, mm2px(meter_x - 2.2f), mm2px(meter_y[0]), "L", nullptr);
        nvgText(args.vg, mm2px(meter_x - 2.2f), mm2px(meter_y[1]), "R", nullptr);
        nvgFontSize(args.vg, mm2px(4.2f));
        nvgText(args.vg, mm2px(8.0f), mm2px(17.5f), line2.c_str(), nullptr);
        // The variation, then the page a little after the variation's arrows,
        // then a page heading after the page's.
        float x = 8.0f;
        if (!variation.empty()) {
            x = field(1, x, 17.5f, variation) + 4.0f;
        }
        if (!page.empty()) {
            x = field(2, x, 17.5f, page) + 1.6f;
        }
        if (!heading.empty()) {
            float room = mm2px(96.5f - x);
            nvgFontSize(args.vg, mm2px(3.2f));
            float width = nvgTextBounds(args.vg, 0, 0, heading.c_str(), nullptr, bounds);
            if (width > room) {
                nvgFontSize(args.vg, mm2px(3.2f) * room / width);
            }
            nvgText(args.vg, mm2px(x), mm2px(17.5f), heading.c_str(), nullptr);
        }
        layout = drawn;
        // Names over the faders, on one line or two (name_lines), values under them.
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (int s = 0; s < faders; s++) {
            if (!names[s].empty()) {
                draw_name(args, slider_x0 + slider_dx * float(s), names[s]);
            }
        }
        // The values, lit in their windows (the panel's dark rects): the
        // number large, its unit (MSEC, SEC, %, METERS, KHZ, HZ: every
        // catalog's texts are a number and at most one unit) small under it.
        // Each shrunk to fit.
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        auto fitted = [&](const std::string &text, float size, float x, float y) {
            nvgFontSize(args.vg, mm2px(size));
            float room = mm2px(value_w - 1.2f);
            float width = nvgTextBounds(args.vg, 0, 0, text.c_str(), nullptr, bounds);
            if (width > room) {
                nvgFontSize(args.vg, mm2px(size) * room / width);
            }
            nvgText(args.vg, mm2px(x), mm2px(y), text.c_str(), nullptr);
        };
        for (int s = 0; s < faders; s++) {
            std::istringstream words(values[s]);
            std::string number, unit, word;
            words >> number;
            while (words >> word) {
                unit += (unit.empty() ? "" : " ") + word;
            }
            if (number.empty()) {
                continue;
            }
            float x = slider_x0 + slider_dx * float(s);
            if (unit.empty()) {
                fitted(number, 4.6f, x, value_y + value_h / 2);
            } else {
                fitted(number, 4.6f, x, value_y + 3.1f);
                fitted(unit, 2.3f, x, value_y + 6.7f);
            }
        }
    }
};

// A dropdown on the display, as Rack's own LedDisplayChoice (the Audio
// module's device field): a click opens a menu at the pointer. A faint box
// shows while the pointer is over it.
struct DisplayChoice : widget::OpaqueWidget {
    Reflexion224 *module = nullptr;
    const ProgramDisplay *display = nullptr;
    int index = 0;   // DisplayLayout's: 0 program, 1 variation, 2 page
    void (*fill)(ui::Menu *, Reflexion224 *) = nullptr;
    std::string (*tip)(Reflexion224 *) = nullptr;   // the tooltip's text, "" for none
    bool hovered = false;

    // A tooltip as a param's (app::ParamWidget's), under the field.
    struct Tip : ui::Tooltip {
        widget::Widget *owner = nullptr;
        void step() override {
            ui::Tooltip::step();
            box.pos = owner->getAbsoluteOffset(Vec(0, owner->box.size.y)).round();
            box = box.nudge(parent->box.zeroPos());
        }
    };
    Tip *tooltip = nullptr;
    void hide_tip() {
        if (tooltip != nullptr) {
            tooltip->requestDelete();
            tooltip = nullptr;
        }
    }
    ~DisplayChoice() override {
        hide_tip();
    }
    void step() override {
        if (display != nullptr && display->layout.valid[index]) {
            box.pos.x = mm2px(display->layout.choice_x[index]);
            box.size.x = mm2px(display->layout.choice_w[index]);
        }
        OpaqueWidget::step();
    }

    void onButton(const ButtonEvent &e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module != nullptr) {
            fill(createMenu(), module);
            e.consume(this);
        }
        OpaqueWidget::onButton(e);
    }
    void onEnter(const EnterEvent &e) override {
        hovered = true;
        if (settings::tooltips && tip != nullptr && module != nullptr && tooltip == nullptr) {
            std::string text = tip(module);
            if (!text.empty()) {
                tooltip = new Tip;
                tooltip->owner = this;
                tooltip->text = text;
                APP->scene->addChild(tooltip);
            }
        }
    }
    void onLeave(const LeaveEvent &e) override {
        hovered = false;
        hide_tip();
    }
    void draw(const DrawArgs &args) override {
        if (hovered && module != nullptr) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.8f));
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x5a, 0x1f, 0x26));
            nvgFill(args.vg);
        }
    }
};

// A previous/next arrow on the display, styled as the dropdowns: a lit
// triangle, and the same faint box while the pointer is over it. Still a
// momentary button param (the module's PROGRAM_PREV_PARAM...PAGE_NEXT_PARAM),
// so it keeps its tooltip, MIDI mapping and place in saved patches.
struct DisplayArrow : app::Switch {
    bool next = false;
    const ProgramDisplay *display = nullptr;
    int index = 0;
    void step() override {
        if (display != nullptr && display->layout.valid[index]) {
            box.pos.x = mm2px(display->layout.arrows_x[index] + (next ? arrow_w + arrow_gap : 0.0f));
        }
        app::Switch::step();
    }
    DisplayArrow() {
        momentary = true;
        box.size = mm2px(Vec(arrow_w, 6.0f));
    }
    // Shown once a program is running (or in the module browser's preview);
    // until then the display's second row carries the status text.
    bool shown() const {
        auto *m = dynamic_cast<Reflexion224 *>(module);
        return m == nullptr || m->view().program != nullptr;
    }
    void draw(const DrawArgs &args) override {
        if (module != nullptr && APP->event->hoveredWidget == this && shown()) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.8f));
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x5a, 0x1f, 0x26));
            nvgFill(args.vg);
        }
    }
    void drawLayer(const DrawArgs &args, int layer) override {
        if (layer != 1) {
            return app::Switch::drawLayer(args, layer);
        }
        if (!shown()) {
            return;
        }
        // The display's triangle (tri_base, tri_depth), turned to point left or right.
        float cx = box.size.x / 2, cy = box.size.y / 2, w = mm2px(tri_depth / 2), h = mm2px(tri_base / 2);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + (next ? w : -w), cy);
        nvgLineTo(args.vg, cx + (next ? -w : w), cy - h);
        nvgLineTo(args.vg, cx + (next ? -w : w), cy + h);
        nvgClosePath(args.vg);
        nvgFillColor(args.vg, nvgRGB(0xff, 0x5a, 0x1f));
        nvgFill(args.vg);
    }
};

std::string program_tip(Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (v.program == nullptr) {
        return "";
    }
    return v.program->name + " (" + v.program->where + ")\nClick for the list of programs";
}
std::string variation_tip(Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (v.program == nullptr) {
        return "";
    }
    return string::f("Variation %d of %d\nClick for the list", v.pub.machine.variation,
                     int(std::max<size_t>(v.program->variations.size(), 1)));
}
std::string page_tip(Reflexion224 *module) {
    Reflexion224::View v = module->view();
    if (v.page_count == 0) {
        return "";
    }
    return string::f("Page %d of %d: which sliders the faders move\nClick for the list", v.page_index + 1,
                     v.page_count);
}

// The fader: the LARC's black slot and wide, shallow cap (in black)
// (res/LarcFader.svg, res/LarcFaderCap.svg; the scale is on the panel).
struct LarcFader : app::SvgSlider {
    LarcFader() {
        setBackgroundSvg(Svg::load(asset::plugin(pluginInstance, "res/LarcFader.svg")));
        setHandleSvg(Svg::load(asset::plugin(pluginInstance, "res/LarcFaderCap.svg")));
        // Cap (10 x 4.6 mm) centres, bottom (minimum) then top, in the slot's
        // 10.4 x 50 mm: the cap's ends meet the slot's (0.6-49.4 mm).
        setHandlePosCentered(mm2px(Vec(5.2f, 47.1f)), mm2px(Vec(5.2f, 2.9f)));
    }
};

// LEVEL and MIX: one 8 mm knob in the fader caps' style (res/PlainKnob_bg.svg
// fixed, res/PlainKnob.svg's pointer turning over it), no drop shadow (the
// caps have none). Rack's RoundKnob, with its own SVGs.
struct PlainKnob : app::SvgKnob {
    widget::SvgWidget *bg;
    PlainKnob() {
        minAngle = -0.83f * float(M_PI);
        maxAngle = 0.83f * float(M_PI);
        bg = new widget::SvgWidget;
        fb->addChildBelow(bg, tw);
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/PlainKnob.svg")));
        bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/PlainKnob_bg.svg")));
        shadow->opacity = 0.0f;
    }
};

struct Reflexion224Widget : app::ModuleWidget {
    void add_choice(Reflexion224 *module, const ProgramDisplay *display, int index, const Field &f,
                    void (*fill)(ui::Menu *, Reflexion224 *), std::string (*tip)(Reflexion224 *), int prev_param) {
        auto *choice = createWidget<DisplayChoice>(mm2px(Vec(f.x, f.y)));
        choice->box.size = mm2px(Vec(f.w, f.h));
        choice->module = module;
        choice->display = display;
        choice->index = index;
        choice->fill = fill;
        choice->tip = tip;
        addChild(choice);
        for (int i = 0; i < 2; i++) {
            float x = f.x + f.w + arrows_lead + float(i) * (arrow_w + arrow_gap), y = f.y + (f.h - 6.0f) / 2;
            auto *arrow = createParam<DisplayArrow>(mm2px(Vec(x, y)), module, prev_param + i);
            arrow->next = i == 1;
            arrow->display = display;
            arrow->index = index;
            addParam(arrow);
        }
    }

    explicit Reflexion224Widget(Reflexion224 *module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Reflexion224.svg")));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        auto *labels = createWidget<PanelLabels>(Vec(0, 0));
        labels->box.size = box.size;
        addChild(labels);
        auto *display = createWidget<ProgramDisplay>(Vec(0, 0));
        display->box.size = box.size;
        display->module = module;
        addChild(display);
        add_choice(module, display, 0, program_field, append_programs, program_tip, Reflexion224::PROGRAM_PREV_PARAM);
        add_choice(module, display, 1, variation_field, append_variations, variation_tip,
                   Reflexion224::VARIATION_PREV_PARAM);
        add_choice(module, display, 2, page_field, append_pages, page_tip, Reflexion224::PAGE_PREV_PARAM);

        for (int i = 0; i < faders; i++) {
            addParam(createParamCentered<LarcFader>(mm2px(Vec(slider_x0 + slider_dx * float(i), slider_y)), module,
                                                    Reflexion224::FADER_PARAM + i));
        }
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(in_x[0], jack_y)), module, Reflexion224::IN_L_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(in_x[1], jack_y)), module, Reflexion224::IN_R_INPUT));
        addParam(createParamCentered<PlainKnob>(mm2px(Vec(level_x, jack_y)), module, Reflexion224::LEVEL_PARAM));
        addParam(createParamCentered<PlainKnob>(mm2px(Vec(mix_x, jack_y)), module, Reflexion224::MIX_PARAM));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(out_x[0], jack_y)), module, Reflexion224::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(out_x[1], jack_y)), module, Reflexion224::OUT_R_OUTPUT));
    }

    void appendContextMenu(ui::Menu *menu) override {
        auto *module = getModule<Reflexion224>();
        menu->addChild(new ui::MenuSeparator);
        menu->addChild(createMenuLabel(module->core.status_text()));

        // The display's dropdowns, here too.
        Reflexion224::View v = module->view();
        if (v.catalog && !v.catalog->programs.empty()) {
            menu->addChild(createSubmenuItem("Program", v.program != nullptr ? v.program->name : "",
                                             [module](ui::Menu *sub) { append_programs(sub, module); }));
        }
        if (v.program != nullptr && v.program->variations.size() > 1) {
            menu->addChild(createSubmenuItem("Variation", string::f("V%d", v.pub.machine.variation),
                                             [module](ui::Menu *sub) { append_variations(sub, module); }));
        }
        if (v.page_count > 1) {
            menu->addChild(createSubmenuItem("Page", string::f("%d/%d", v.page_index + 1, v.page_count),
                                             [module](ui::Menu *sub) { append_pages(sub, module); }));
        }
        append_toggles(menu, module);
        menu->addChild(createSubmenuItem("Outputs", "", [module](ui::Menu *sub) {
            for (int side = 0; side < 2; side++) {
                int id = Reflexion224::OUT_LEFT_PARAM + side;
                sub->addChild(createMenuLabel(side == 0 ? "Left jack" : "Right jack"));
                for (int dac = 0; dac < 4; dac++) {
                    sub->addChild(createCheckMenuItem(
                        string::f("Machine output %s", output_names[dac]), "",
                        [module, id, dac] { return int(module->params[id].getValue()) == dac; },
                        [module, id, dac] { module->paramQuantities[id]->setValue(float(dac)); }));
                }
            }
        }));

        if (!module->rom_path.empty()) {
            std::vector<std::string> sets = sibling_sets(module->rom_path);
            if (!sets.empty()) {
                menu->addChild(createSubmenuItem("Firmware set", lexvcv::path_utf8(lexvcv::utf8_path(module->rom_path).filename()),
                                                 [module, sets](ui::Menu *sub) {
                                                     for (const std::string &set : sets) {
                                                         sub->addChild(createCheckMenuItem(
                                                             lexvcv::path_utf8(lexvcv::utf8_path(set).filename()), "",
                                                             [module, set] { return module->rom_path == set; },
                                                             [module, set] { module->set_rom_path(set); }));
                                                     }
                                                 }));
            }
        }
        menu->addChild(createMenuItem("Choose ROM folder…", "", [module] {
            char *path = osdialog_file(OSDIALOG_OPEN_DIR, module->rom_path.empty() ? nullptr : module->rom_path.c_str(),
                                       nullptr, nullptr);
            if (path != nullptr) {
                module->set_rom_path(set_in(path));
                std::free(path);
            }
        }));
    }
};

Model *modelReflexion224 = createModel<Reflexion224, Reflexion224Widget>("Reflexion224");
